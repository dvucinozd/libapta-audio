// SPDX-License-Identifier: Apache-2.0
//! Bounded sparse overview processing for known-length mono/stereo sources.
//! Accepted ranges include queued samples and remain reserved after processing.
//! All storage is caller-owned. No samples are retained from an input borrow.

use crate::session::{CancellationToken, Progress, SessionConfig, SessionState, WorkBudget};
use crate::waveform::{NormalizedSample, PcmView, WaveformAccumulator};
use crate::{Error, FeatureState, FrameRange, NativeOverview, WaveformColumn, WaveformSpan};

pub const NODE_FRAMES: usize = 4096;

#[derive(Clone, Copy, Debug, Default)]
pub struct SparseAccumulator {
    value: WaveformAccumulator,
    complete: bool,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct QueuedBlock {
    first_frame: u64,
    len: usize,
    processed: usize,
    serial: u64,
    next_serial: u64,
}

pub struct Workspace<'a> {
    pub accumulators: &'a mut [SparseAccumulator],
    pub ranges: &'a mut [FrameRange],
    pub nodes: &'a mut [QueuedBlock],
    /// Each node owns a fixed region of [`NODE_FRAMES`] normalized samples.
    pub pcm: &'a mut [NormalizedSample],
    /// Conservative capacity: one span for each logical column.
    pub snapshot_spans: &'a mut [WaveformSpan],
    pub snapshot_columns: &'a mut [WaveformColumn],
}

pub struct SparseSession<'a> {
    config: SessionConfig,
    workspace: Workspace<'a>,
    logical_columns: usize,
    range_count: usize,
    queued: usize,
    processed: u64,
    serial: u64,
    complete_columns: usize,
    eof: bool,
    state: SessionState,
}

impl<'a> SparseSession<'a> {
    pub fn new(config: SessionConfig, workspace: Workspace<'a>) -> Result<Self, Error> {
        if config.sample_rate == 0
            || config.sample_rate > 768000
            || !(1..=2).contains(&config.channel_count)
            || config.total_frames == u64::MAX
            || !(64..=65536).contains(&config.frames_per_column)
            || !config.frames_per_column.is_power_of_two()
        {
            return Err(Error::InvalidArgument);
        }
        let n = config
            .total_frames
            .div_ceil(u64::from(config.frames_per_column));
        if n > u64::from(u32::MAX) {
            return Err(Error::LimitExceeded);
        }
        let n = usize::try_from(n).map_err(|_| Error::LimitExceeded)?;
        let pcm_count = workspace
            .nodes
            .len()
            .checked_mul(NODE_FRAMES)
            .ok_or(Error::LimitExceeded)?;
        if workspace.accumulators.len() < n
            || workspace.snapshot_columns.len() < n
            || workspace.snapshot_spans.len() < n
            || workspace.pcm.len() < pcm_count
            || (n != 0 && (workspace.nodes.is_empty() || workspace.ranges.is_empty()))
        {
            return Err(Error::BufferTooSmall);
        }
        workspace.accumulators[..n].fill(SparseAccumulator::default());
        workspace.nodes.fill(QueuedBlock::default());
        Ok(Self {
            config,
            workspace,
            logical_columns: n,
            range_count: 0,
            queued: 0,
            processed: 0,
            serial: 0,
            complete_columns: 0,
            eof: false,
            state: SessionState::Created,
        })
    }

    pub fn config(&self) -> SessionConfig {
        self.config
    }
    pub fn state(&self) -> SessionState {
        self.state
    }
    pub fn processed_frames(&self) -> u64 {
        self.processed
    }
    pub fn queued_frames(&self) -> usize {
        self.queued
    }
    pub fn complete_columns(&self) -> usize {
        self.complete_columns
    }
    pub fn accepted_ranges(&self) -> &[FrameRange] {
        &self.workspace.ranges[..self.range_count]
    }
    pub fn coverage_complete(&self) -> bool {
        self.eof && self.complete_columns == self.logical_columns
    }

    /// Copies at most 4096 frames, stopping before the first existing range.
    /// Starting inside an existing range is a conflict. A full queue or range
    /// table accepts zero without mutation, including when ranges could merge.
    pub fn push_at(&mut self, first_frame: u64, samples: PcmView<'_>) -> Result<usize, Error> {
        if self.state == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        if !matches!(self.state, SessionState::Created | SessionState::Running) {
            return Err(Error::InvalidState);
        }
        let supplied = samples.frame_count(self.config.channel_count)?;
        let end = first_frame
            .checked_add(supplied as u64)
            .ok_or(Error::InvalidArgument)?;
        if supplied == 0 || first_frame == u64::MAX {
            return Err(Error::InvalidArgument);
        }
        if end > self.config.total_frames {
            return Err(Error::Conflict);
        }
        let mut count = supplied.min(NODE_FRAMES);
        for range in self.accepted_ranges() {
            if range.end_frame <= first_frame {
                continue;
            }
            if range.first_frame <= first_frame {
                return Err(Error::Conflict);
            }
            count =
                count.min(usize::try_from(range.first_frame - first_frame).unwrap_or(usize::MAX));
            break;
        }
        if self.range_count == self.workspace.ranges.len() {
            return Ok(0);
        }
        let Some(slot) = self.workspace.nodes.iter().position(|node| node.len == 0) else {
            return Ok(0);
        };
        let serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        for index in 0..count {
            samples.sample_frame(index, self.config.channel_count)?;
        }
        for index in 0..count {
            self.workspace.pcm[slot * NODE_FRAMES + index] =
                samples.sample_frame(index, self.config.channel_count)?;
        }
        self.insert_range(FrameRange {
            first_frame,
            end_frame: first_frame + count as u64,
        });
        self.workspace.nodes[slot] = QueuedBlock {
            first_frame,
            len: count,
            processed: 0,
            serial,
            next_serial: 0,
        };
        self.serial = serial;
        self.queued += count;
        self.state = SessionState::Running;
        Ok(count)
    }

    fn insert_range(&mut self, mut range: FrameRange) {
        let mut first = 0;
        while first < self.range_count && self.workspace.ranges[first].end_frame < range.first_frame
        {
            first += 1;
        }
        let mut end = first;
        while end < self.range_count && self.workspace.ranges[end].first_frame <= range.end_frame {
            range.first_frame = range
                .first_frame
                .min(self.workspace.ranges[end].first_frame);
            range.end_frame = range.end_frame.max(self.workspace.ranges[end].end_frame);
            end += 1;
        }
        self.workspace
            .ranges
            .copy_within(end..self.range_count, first + 1);
        self.range_count = self.range_count - (end - first) + 1;
        self.workspace.ranges[first] = range;
    }

    /// Install a previously validated owned overview. Preflight all retained
    /// columns and worst-case range capacity before changing working storage.
    pub(crate) fn install_seed(&mut self, overview: NativeOverview<'_>) -> Result<(), Error> {
        if self.state != SessionState::Created {
            return Err(Error::InvalidState);
        }
        if self
            .range_count
            .checked_add(overview.spans.len())
            .ok_or(Error::LimitExceeded)?
            > self.workspace.ranges.len()
        {
            return Err(Error::BufferTooSmall);
        }
        for span in overview.spans {
            for offset in 0..span.column_count as usize {
                let index = (span.first_column_index as usize)
                    .checked_add(offset)
                    .ok_or(Error::LimitExceeded)?;
                if index >= self.logical_columns {
                    return Err(Error::Conflict);
                }
                let first = index as u64 * u64::from(self.config.frames_per_column);
                let count = (self.config.total_frames - first)
                    .min(u64::from(self.config.frames_per_column))
                    as u32;
                let source = overview
                    .columns
                    .get(span.data_column_offset as usize + offset)
                    .ok_or(Error::InvalidArgument)?;
                WaveformAccumulator::from_seed_column(*source, count)?;
            }
        }
        for span in overview.spans {
            for offset in 0..span.column_count as usize {
                let index = span.first_column_index as usize + offset;
                let first = index as u64 * u64::from(self.config.frames_per_column);
                let count = (self.config.total_frames - first)
                    .min(u64::from(self.config.frames_per_column))
                    as u32;
                let source = overview.columns[span.data_column_offset as usize + offset];
                let value = WaveformAccumulator::from_seed_column(source, count)?;
                if !self.workspace.accumulators[index].complete {
                    self.complete_columns += 1;
                }
                self.workspace.accumulators[index] = SparseAccumulator {
                    value,
                    complete: true,
                };
            }
            self.insert_range(FrameRange {
                first_frame: span.first_frame,
                end_frame: span.end_frame,
            });
        }
        Ok(())
    }

    pub fn finish_input(&mut self) -> Result<(), Error> {
        if self.eof {
            return Ok(());
        }
        if !matches!(self.state, SessionState::Created | SessionState::Running) {
            return Err(Error::InvalidState);
        }
        self.eof = true;
        self.state = SessionState::Draining;
        Ok(())
    }

    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.process_inner(
            budget,
            cancellation,
            true,
            None,
            &mut crate::deadline::Deadline::disabled(),
        )
    }

    /// Check an injected nanosecond clock after each processing chunk. An
    /// initial zero disables the soft deadline; at least one queued chunk runs.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancellation: &CancellationToken,
    ) -> Result<Progress, Error> {
        let mut deadline = crate::deadline::Deadline::new(soft_us, clock);
        self.process_inner(budget, cancellation, true, None, &mut deadline)
    }
    pub(crate) fn set_publication_state(&mut self, state: SessionState) {
        self.state = state;
    }
    pub(crate) fn set_publication_eof(&mut self, eof: bool) {
        self.eof = eof;
    }
    pub(crate) fn ready_to_complete(&self) -> bool {
        self.state == SessionState::Draining && self.queued == 0
    }
    /// Return the initially selected request. The publication layer updates
    /// request states and ages requests only after successful overview output.
    pub(crate) fn process_scheduled_deferred(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        scheduler: &crate::scheduler::Scheduler<'_>,
    ) -> Result<(Progress, u32), Error> {
        self.process_scheduled_deadline(
            budget,
            cancellation,
            scheduler,
            &mut crate::deadline::Deadline::disabled(),
        )
    }

    pub(crate) fn process_scheduled_deadline(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        scheduler: &crate::scheduler::Scheduler<'_>,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<(Progress, u32), Error> {
        let selected = if !cancellation.is_cancelled()
            && !matches!(self.state, SessionState::Cancelled | SessionState::Complete)
        {
            self.sort_queue(scheduler)
        } else {
            0
        };
        self.process_inner(budget, cancellation, false, Some(scheduler), deadline)
            .map(|progress| (progress, selected))
    }

    fn node_range(node: &QueuedBlock) -> FrameRange {
        FrameRange {
            first_frame: node.first_frame + node.processed as u64,
            end_frame: node.first_frame + node.len as u64,
        }
    }

    // Compute ranks before changing ordering keys. PCM stays in its fixed slot;
    // equal scores preserve the ordering left by the previous process call.
    fn sort_queue(&mut self, scheduler: &crate::scheduler::Scheduler<'_>) -> u32 {
        let mut selected = 0;
        let mut active = 0;
        for index in 0..self.workspace.nodes.len() {
            let node = self.workspace.nodes[index];
            if node.len == 0 {
                continue;
            }
            active += 1;
            let score = scheduler.score_range(Self::node_range(&node));
            let mut rank = 1;
            for other in self.workspace.nodes.iter().filter(|n| n.len != 0) {
                let other_score = scheduler.score_range(Self::node_range(other));
                if other_score.better_than(&score)
                    || (other_score == score && other.serial < node.serial)
                {
                    rank += 1;
                }
            }
            self.workspace.nodes[index].next_serial = rank;
            if rank == 1 {
                selected = score.request_id;
            }
        }
        for node in self.workspace.nodes.iter_mut().filter(|n| n.len != 0) {
            node.serial = node.next_serial;
        }
        self.serial = active;
        selected
    }

    fn refresh_complete(&mut self) {
        for (index, column) in self.workspace.accumulators[..self.logical_columns]
            .iter_mut()
            .enumerate()
        {
            let first = index as u64 * u64::from(self.config.frames_per_column);
            let expected = if self.eof {
                (self.config.total_frames - first).min(u64::from(self.config.frames_per_column))
                    as u32
            } else {
                self.config.frames_per_column
            };
            if !column.complete && column.value.sample_count() == expected {
                column.complete = true;
                self.complete_columns += 1;
            }
        }
    }

    fn process_inner(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        complete: bool,
        scheduler: Option<&crate::scheduler::Scheduler<'_>>,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<Progress, Error> {
        if self.state == SessionState::Failed {
            return Err(Error::Internal);
        }
        if self.state == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        if self.state == SessionState::Complete {
            return Ok(Progress::default());
        }
        if cancellation.is_cancelled() {
            self.state = SessionState::Cancelled;
            return Err(Error::Cancelled);
        }
        self.refresh_complete();
        let frames = if budget.maximum_input_frames == 0 {
            u32::MAX
        } else {
            budget.maximum_input_frames
        };
        let steps = if budget.maximum_steps == 0 {
            u32::MAX
        } else {
            budget.maximum_steps
        };
        let mut progress = Progress::default();
        while self.queued != 0
            && progress.consumed_input_frames < frames
            && progress.completed_steps < steps
        {
            if cancellation.is_cancelled() {
                self.state = SessionState::Cancelled;
                return Err(Error::Cancelled);
            }
            let slot = self
                .workspace
                .nodes
                .iter()
                .enumerate()
                .filter(|(_, node)| node.len != 0)
                .min_by_key(|(_, node)| {
                    let priority = scheduler.map_or(0, |s| {
                        s.score_range(Self::node_range(node)).effective_priority
                    });
                    (core::cmp::Reverse(priority), node.serial)
                })
                .map(|(index, _)| index)
                .ok_or(Error::InvalidState)?;
            let node = &mut self.workspace.nodes[slot];
            let count = (node.len - node.processed)
                .min(256)
                .min((frames - progress.consumed_input_frames) as usize);
            for _ in 0..count {
                let sample = self.workspace.pcm[slot * NODE_FRAMES + node.processed];
                let frame = node.first_frame + node.processed as u64;
                self.workspace.accumulators
                    [(frame / u64::from(self.config.frames_per_column)) as usize]
                    .value
                    .push_normalized(sample.value, sample.clipped)?;
                node.processed += 1;
            }
            if node.processed == node.len {
                *node = QueuedBlock::default();
            }
            self.queued -= count;
            self.processed += count as u64;
            progress.consumed_input_frames += count as u32;
            progress.completed_steps += 1;
            self.refresh_complete();
            if deadline.expired() {
                break;
            }
        }
        if complete && self.ready_to_complete() {
            self.state = SessionState::Complete;
        }
        Ok(progress)
    }

    /// Packs complete columns into sorted contiguous spans. Incomplete columns
    /// never appear. The borrowed view prevents processing until released.
    pub fn snapshot(&mut self) -> Option<NativeOverview<'_>> {
        if self.complete_columns == 0 {
            return None;
        }
        let mut spans = 0usize;
        let mut columns = 0usize;
        let fpc = u64::from(self.config.frames_per_column);
        for (index, column) in self.workspace.accumulators[..self.logical_columns]
            .iter()
            .enumerate()
        {
            if !column.complete {
                continue;
            }
            self.workspace.snapshot_columns[columns] = column.value.column();
            let first = index as u64 * fpc;
            let end = (first + fpc).min(self.config.total_frames);
            if spans != 0 && self.workspace.snapshot_spans[spans - 1].end_frame == first {
                self.workspace.snapshot_spans[spans - 1].end_frame = end;
                self.workspace.snapshot_spans[spans - 1].column_count += 1;
            } else {
                self.workspace.snapshot_spans[spans] = WaveformSpan {
                    first_frame: first,
                    end_frame: end,
                    first_column_index: index as u32,
                    column_count: 1,
                    data_column_offset: columns as u32,
                };
                spans += 1;
            }
            columns += 1;
        }
        let state = if self.coverage_complete() {
            if self.state == SessionState::Complete {
                FeatureState::Final
            } else {
                FeatureState::Stable
            }
        } else {
            FeatureState::Partial
        };
        Some(NativeOverview {
            frames_per_column: self.config.frames_per_column,
            origin_frame: 0,
            state,
            confidence: 255,
            spans: &self.workspace.snapshot_spans[..spans],
            columns: &self.workspace.snapshot_columns[..columns],
        })
    }
}

#[cfg(test)]
mod scheduling_tests {
    use super::*;
    use crate::scheduler::{RequestSlot, Scheduler};
    use crate::{result::WAVEFORM_OVERVIEW, RegionRequest};
    fn run(f: impl FnOnce(&mut SparseSession<'_>, &mut Scheduler<'_>)) {
        let mut accumulators = [SparseAccumulator::default(); 16];
        let mut ranges = [FrameRange {
            first_frame: 0,
            end_frame: 0,
        }; 4];
        let mut nodes = [QueuedBlock::default(); 3];
        let mut pcm = [NormalizedSample::default(); 3 * NODE_FRAMES];
        let mut spans = [WaveformSpan::default(); 16];
        let mut columns = [WaveformColumn::default(); 16];
        let mut slots = [RequestSlot::default(); 4];
        let mut scheduler = Scheduler::new(Some(1024), WAVEFORM_OVERVIEW, &mut slots).unwrap();
        let mut session = SparseSession::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 1024,
                frames_per_column: 64,
            },
            Workspace {
                accumulators: &mut accumulators,
                ranges: &mut ranges,
                nodes: &mut nodes,
                pcm: &mut pcm,
                snapshot_spans: &mut spans,
                snapshot_columns: &mut columns,
            },
        )
        .unwrap();
        f(&mut session, &mut scheduler);
    }
    fn request(s: &mut Scheduler<'_>, first: u64, end: u64, priority: u8, deadline: u64) -> u32 {
        s.request_region(RegionRequest {
            range: FrameRange {
                first_frame: first,
                end_frame: end,
            },
            feature_mask: WAVEFORM_OVERVIEW,
            soft_deadline_monotonic_ns: deadline,
            request_id: 0,
            priority,
        })
        .unwrap()
    }
    #[test]
    fn deadline_sort_persists_after_requests_cancel() {
        run(|s, scheduler| {
            s.push_at(0, PcmView::S16Interleaved(&[1; 512])).unwrap();
            s.push_at(512, PcmView::S16Interleaved(&[2; 512])).unwrap();
            let first = request(scheduler, 0, 512, 100, 200);
            let second = request(scheduler, 512, 1024, 100, 100);
            let (progress, selected) = s
                .process_scheduled_deferred(
                    WorkBudget {
                        maximum_input_frames: 64,
                        maximum_steps: 1,
                    },
                    &CancellationToken::new(),
                    scheduler,
                )
                .unwrap();
            assert_eq!(progress.consumed_input_frames, 64);
            assert_eq!(selected, second);
            assert_eq!(s.snapshot().unwrap().spans[0].first_frame, 512);
            scheduler.cancel_region_request(first).unwrap();
            scheduler.cancel_region_request(second).unwrap();
            s.process_scheduled_deferred(
                WorkBudget {
                    maximum_input_frames: 64,
                    maximum_steps: 1,
                },
                &CancellationToken::new(),
                scheduler,
            )
            .unwrap();
            let view = s.snapshot().unwrap();
            assert_eq!(view.spans.len(), 1);
            assert_eq!(view.spans[0].first_frame, 512);
            assert_eq!(view.spans[0].end_frame, 640);
        });
    }
    #[test]
    fn priority_uses_unprocessed_remainder_after_each_chunk() {
        run(|s, scheduler| {
            s.push_at(0, PcmView::S16Interleaved(&[1; 512])).unwrap();
            s.push_at(512, PcmView::S16Interleaved(&[2; 512])).unwrap();
            let selected = request(scheduler, 0, 1, 200, 0);
            request(scheduler, 512, 1024, 100, 0);
            let (_, actual) = s
                .process_scheduled_deferred(
                    WorkBudget {
                        maximum_input_frames: 512,
                        maximum_steps: 2,
                    },
                    &CancellationToken::new(),
                    scheduler,
                )
                .unwrap();
            assert_eq!(actual, selected);
            let view = s.snapshot().unwrap();
            assert_eq!(view.spans.len(), 2);
            assert_eq!(
                (view.spans[0].first_frame, view.spans[0].end_frame),
                (0, 256)
            );
            assert_eq!(
                (view.spans[1].first_frame, view.spans[1].end_frame),
                (512, 768)
            );
        });
    }
}
