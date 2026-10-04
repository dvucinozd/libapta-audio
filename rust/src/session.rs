// SPDX-License-Identifier: Apache-2.0
//! Allocation-free sequential mono/stereo waveform processing.
//!
//! All storage belongs to the caller. A step consumes at most 256 frames.
//! Zero budget fields mean unlimited, matching the C work-budget convention.
use core::sync::atomic::{AtomicBool, Ordering};

use crate::waveform::{NormalizedSample, PcmView, WaveformAccumulator};
use crate::{Error, WaveformColumn};

/// Unknown input length. The final length is resolved by [`Session::finish_input`].
/// This matches the C source-frame sentinel; it is never a valid actual length.
pub const TOTAL_FRAMES_UNKNOWN: u64 = u64::MAX;

#[derive(Clone, Copy, Debug)]
pub struct SessionConfig {
    pub sample_rate: u32,
    pub channel_count: u16,
    /// Declared length, or [`TOTAL_FRAMES_UNKNOWN`] for bounded streaming input.
    pub total_frames: u64,
    pub frames_per_column: u32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct WorkBudget {
    pub maximum_input_frames: u32,
    pub maximum_steps: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Progress {
    pub consumed_input_frames: u32,
    pub completed_steps: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SessionState {
    Created,
    Running,
    Draining,
    Complete,
    Cancelled,
    Failed,
}

/// Share this token with another thread; cancellation is checked between steps.
#[derive(Default)]
pub struct CancellationToken(AtomicBool);

impl CancellationToken {
    pub const fn new() -> Self {
        Self(AtomicBool::new(false))
    }
    pub fn cancel(&self) {
        self.0.store(true, Ordering::Release);
    }
    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::Acquire)
    }
}

pub struct Session<'a> {
    analysis: Option<crate::analysis::Analysis<'a>>,
    global: Option<crate::global_analysis::GlobalAnalysis<'a>>,
    key: Option<crate::key_analysis::KeyAnalysis>,
    quality_enabled: bool,
    detail: Option<crate::detail_analysis::DetailCache<'a>>,
    bands: Option<crate::band::OverviewBands<'a>>,
    config: SessionConfig,
    queue: &'a mut [NormalizedSample],
    output: &'a mut [WaveformColumn],
    head: usize,
    queued: usize,
    accepted: u64,
    input_capacity: u64,
    processed: u64,
    written: usize,
    accumulator: WaveformAccumulator,
    state: SessionState,
}

impl<'a> Session<'a> {
    pub fn new(
        config: SessionConfig,
        queue: &'a mut [NormalizedSample],
        output: &'a mut [WaveformColumn],
    ) -> Result<Self, Error> {
        if config.sample_rate == 0
            || !(1..=2).contains(&config.channel_count)
            || !(64..=65536).contains(&config.frames_per_column)
            || !config.frames_per_column.is_power_of_two()
            || queue.is_empty()
        {
            return Err(Error::InvalidArgument);
        }
        let columns = config.total_frames / u64::from(config.frames_per_column)
            + u64::from(config.total_frames % u64::from(config.frames_per_column) != 0);
        if config.total_frames != TOTAL_FRAMES_UNKNOWN && columns > output.len() as u64 {
            return Err(Error::BufferTooSmall);
        }
        let input_capacity = if config.total_frames == TOTAL_FRAMES_UNKNOWN {
            (output.len() as u64)
                .saturating_mul(u64::from(config.frames_per_column))
                .min(TOTAL_FRAMES_UNKNOWN - 1)
        } else {
            config.total_frames
        };
        Ok(Self {
            analysis: None,
            global: None,
            key: None,
            quality_enabled: false,
            detail: None,
            bands: None,
            config,
            queue,
            output,
            head: 0,
            queued: 0,
            accepted: 0,
            input_capacity,
            processed: 0,
            written: 0,
            accumulator: WaveformAccumulator::default(),
            state: SessionState::Created,
        })
    }

    /// Enable native broadband onset, tempo and local grid before accepting PCM.
    pub fn enable_calibrated_quality(&mut self) -> Result<(), Error> {
        if self.state != SessionState::Created
            || self.accepted != 0
            || self.analysis.is_none()
            || self.quality_enabled
        {
            return Err(Error::InvalidState);
        }
        self.quality_enabled = true;
        Ok(())
    }
    pub fn bpm_quality(&self) -> Option<crate::QualityRecord> {
        if !self.quality_enabled {
            return None;
        }
        let total = self.total_frames()?;
        if total == 0 {
            return None;
        }
        let t = self.tempo()?.selected;
        Some(crate::QualityRecord {
            feature: crate::result::BPM,
            calibration_model_id: 1867860160,
            evidence_coverage_permille: crate::analysis::coverage_permille(self.accepted, total),
            confidence: crate::analysis::calibrated_bpm_confidence(t.confidence),
            state: t.state,
            flags: 0,
        })
    }
    pub fn set_tempo_focus(&mut self, focus: crate::Focus) -> Result<(), Error> {
        self.analysis
            .as_mut()
            .ok_or(Error::InvalidState)?
            .set_focus(focus)
    }
    pub fn lock_grid_range(&mut self, range: crate::FrameRange) -> Result<(), Error> {
        self.analysis
            .as_mut()
            .ok_or(Error::InvalidState)?
            .lock_range(range)
    }
    pub(crate) fn lock_checkpoint(&self) -> Option<(Option<crate::LocalGrid>, bool, u64)> {
        self.analysis.as_ref().map(|a| a.lock_checkpoint())
    }
    pub(crate) fn restore_lock(&mut self, checkpoint: (Option<crate::LocalGrid>, bool, u64)) {
        if let Some(a) = &mut self.analysis {
            a.restore_lock(checkpoint);
        }
    }
    pub fn enable_meter(&mut self) -> Result<(), Error> {
        if self.state != SessionState::Created || self.accepted != 0 {
            return Err(Error::InvalidState);
        }
        self.analysis
            .as_mut()
            .ok_or(Error::InvalidState)?
            .enable_meter()
    }
    pub fn meter(&self) -> Option<crate::Meter<'_>> {
        self.analysis.as_ref().and_then(|a| a.meter())
    }
    pub(crate) fn meter_serial(&self) -> u64 {
        self.analysis.as_ref().map_or(0, |a| a.meter_serial())
    }
    pub fn enable_key(&mut self) -> Result<(), Error> {
        if self.state != SessionState::Created || self.accepted != 0 || self.key.is_some() {
            return Err(Error::InvalidState);
        }
        self.key = Some(crate::key_analysis::KeyAnalysis::new(
            self.config.sample_rate,
        )?);
        Ok(())
    }
    pub fn key(&self) -> Option<crate::Key<'_>> {
        self.key.as_ref().and_then(|k| k.key()).map(|mut k| {
            if self.state == SessionState::Complete {
                k.state = crate::FeatureState::Final;
            }
            k
        })
    }
    pub(crate) fn key_serial(&self) -> u64 {
        self.key.as_ref().map_or(0, |k| k.mutation_serial())
    }
    pub fn enable_global_grid(
        &mut self,
        dynamic: bool,
        bins: &'a mut [crate::analysis::OnsetBin],
        flux: &'a mut [f32],
        beats: &'a mut [crate::Beat],
    ) -> Result<(), Error> {
        if self.state != SessionState::Created || self.accepted != 0 || self.global.is_some() {
            return Err(Error::InvalidState);
        }
        self.global = Some(crate::global_analysis::GlobalAnalysis::new(
            self.config.sample_rate,
            self.total_frames(),
            dynamic,
            bins,
            flux,
            beats,
        )?);
        Ok(())
    }
    pub fn global_grid(&self) -> Option<crate::GlobalGrid<'_>> {
        self.global.as_ref().and_then(|g| g.grid()).map(|mut g| {
            if self.state == SessionState::Complete {
                g.state = crate::FeatureState::Final;
            }
            g
        })
    }
    /// Accept a pending global proposal into the locked local grid.
    pub fn apply_grid_revision(&mut self, id: u32) -> Result<(), Error> {
        if id == 0 {
            return Err(Error::InvalidArgument);
        }
        let local = self.local_grid().ok_or(Error::InvalidState)?;
        let global = self.global.as_mut().ok_or(Error::InvalidState)?;
        let segment = global.revision_segment(id, local.segment)?;
        let dynamic = global.grid().is_some_and(|g| g.flags & 2 != 0);
        self.analysis
            .as_mut()
            .ok_or(Error::InvalidState)?
            .apply_revision(segment, dynamic)?;
        global.apply_revision()
    }
    pub fn grid_revision(&self) -> Option<crate::GridRevision> {
        self.global.as_ref().and_then(|g| g.revision())
    }
    pub(crate) fn global_serial(&self) -> u64 {
        self.global.as_ref().map_or(0, |g| g.mutation_serial())
    }
    pub fn enable_tempo(
        &mut self,
        bins: &'a mut [crate::analysis::OnsetBin],
        flux: &'a mut [f32],
    ) -> Result<(), Error> {
        if self.state != SessionState::Created || self.accepted != 0 || self.analysis.is_some() {
            return Err(Error::InvalidState);
        }
        self.analysis = Some(crate::analysis::Analysis::new(
            self.config.sample_rate,
            bins,
            flux,
        )?);
        Ok(())
    }
    pub fn tempo(&self) -> Option<crate::TempoView<'_>> {
        self.analysis.as_ref().and_then(|a| a.tempo()).map(|mut t| {
            if self.state == SessionState::Complete {
                t.selected.state = crate::FeatureState::Final;
            }
            t
        })
    }
    pub fn local_grid(&self) -> Option<crate::LocalGrid> {
        self.analysis
            .as_ref()
            .and_then(|a| a.local_grid())
            .map(|mut g| {
                if self.state == SessionState::Complete {
                    g.state = crate::FeatureState::Final;
                    g.segment.state = crate::FeatureState::Final;
                }
                g
            })
    }
    pub(crate) fn analysis_serial(&self) -> u64 {
        self.analysis.as_ref().map_or(0, |a| a.mutation_serial())
    }
    /// Enable three-band overview while Created, before input or seeding.
    /// Storage must cover every possible logical overview column.
    pub fn enable_three_band(
        &mut self,
        sums: &'a mut [crate::band::BandSums],
    ) -> Result<(), Error> {
        if self.state != SessionState::Created || self.accepted != 0 || self.bands.is_some() {
            return Err(Error::InvalidState);
        }
        if sums.len() < self.output.len() {
            return Err(Error::BufferTooSmall);
        }
        let filter = crate::band::BandFilter::new(self.config.sample_rate)?;
        sums[..self.output.len()].fill(crate::band::BandSums::default());
        self.bands = Some(crate::band::OverviewBands { sums, filter });
        Ok(())
    }

    /// Attach an eager detail cache before input. Storage remains caller-owned.
    pub fn enable_detail(
        &mut self,
        tiles: &'a mut [crate::detail_analysis::DetailTile],
    ) -> Result<(), Error> {
        if self.state != SessionState::Created || self.accepted != 0 || self.detail.is_some() {
            return Err(Error::InvalidState);
        }
        if self
            .input_capacity
            .div_ceil(crate::detail_analysis::FRAMES_PER_COLUMN)
            > u64::from(u32::MAX)
        {
            return Err(Error::LimitExceeded);
        }
        self.detail = Some(crate::detail_analysis::DetailCache::new(tiles)?);
        Ok(())
    }

    /// Copy currently complete detail runs into independent caller storage.
    pub fn copy_detail_into<'b>(
        &self,
        tiles: &'b mut [crate::NativeTile],
        columns: &'b mut [WaveformColumn],
    ) -> Result<Option<crate::NativeDetail<'b>>, Error> {
        match &self.detail {
            None => Ok(None),
            Some(detail) => {
                let output = detail.snapshot_into(tiles, columns)?;
                Ok((!output.tiles.is_empty()).then_some(output))
            }
        }
    }

    pub(crate) fn detail_mutation_serial(&self) -> u64 {
        self.detail.as_ref().map_or(0, |d| d.mutation_serial())
    }

    pub fn config(&self) -> SessionConfig {
        self.config
    }
    /// Resolved input length; unknown until EOF for a streaming session.
    pub fn total_frames(&self) -> Option<u64> {
        (self.config.total_frames != TOTAL_FRAMES_UNKNOWN).then_some(self.config.total_frames)
    }
    /// Maximum accepted frames with the supplied output storage (fixed at creation).
    pub fn input_capacity_frames(&self) -> u64 {
        self.input_capacity
    }
    pub fn state(&self) -> SessionState {
        self.state
    }
    pub fn accepted_frames(&self) -> u64 {
        self.accepted
    }
    pub fn processed_frames(&self) -> u64 {
        self.processed
    }
    pub fn queued_frames(&self) -> usize {
        self.queued
    }

    /// Copies the accepted prefix into the bounded queue. A full queue accepts zero.
    /// An invalid sample in that prefix leaves the entire session unchanged.
    pub fn push_interleaved(&mut self, samples: &[f32]) -> Result<usize, Error> {
        self.push_pcm(PcmView::F32Interleaved(samples))
    }

    /// Accepts a bounded prefix in any supported PCM representation.
    /// The input is borrowed only for this call; accepted frames are copied.
    /// Unknown-duration input is limited by output columns times frames per column.
    /// A push may accept a prefix at that limit; retrying a nonempty suffix returns
    /// `BufferTooSmall`. Processing cannot free output capacity. By contrast, a
    /// temporarily full input queue returns zero and processing frees queue space.
    /// Empty, geometrically valid input remains accepted at the capacity limit.
    pub fn push_pcm(&mut self, samples: PcmView<'_>) -> Result<usize, Error> {
        if !matches!(self.state, SessionState::Created | SessionState::Running) {
            return Err(Error::InvalidState);
        }
        let channels = self.config.channel_count;
        let count = self.preflight_push(samples)?;
        for frame in 0..count {
            let sample = samples.sample_frame(frame, channels)?;
            if let Some(detail) = &mut self.detail {
                detail.push_normalized(self.accepted + frame as u64, sample, |_| false)?;
            }
            if let Some(analysis) = &mut self.analysis {
                analysis.push(self.accepted + frame as u64, sample.value);
            }
            if let Some(global) = &mut self.global {
                global.push(self.accepted + frame as u64, sample.value);
            }
            let index = (self.head + self.queued) % self.queue.len();
            self.queue[index] = sample;
            self.queued += 1;
        }
        self.accepted += count as u64;
        if let Some(detail) = &mut self.detail {
            detail.refresh_completed(None)?;
        }
        if count != 0 {
            self.state = SessionState::Running;
        }
        Ok(count)
    }

    // Shared preflight lets publication reject invalid accepted PCM before a
    // first-push state generation is visible. No caller or working data changes.
    pub(crate) fn preflight_push(&self, samples: PcmView<'_>) -> Result<usize, Error> {
        let channels = self.config.channel_count;
        let available = self.input_capacity - self.accepted;
        let supplied = samples.frame_count(channels)?;
        if self.config.total_frames == TOTAL_FRAMES_UNKNOWN && available == 0 && supplied != 0 {
            return Err(Error::BufferTooSmall);
        }
        let count = supplied
            .min(self.queue.len() - self.queued)
            .min(usize::try_from(available).unwrap_or(usize::MAX));
        if let Some(analysis) = &self.analysis {
            analysis.preflight(self.accepted, count)?;
        }
        if let Some(global) = &self.global {
            global.preflight(self.accepted, count)?;
        }
        for index in 0..count {
            samples.sample_frame(index, channels)?;
        }
        Ok(count)
    }

    /// Signals EOF; a known source length must have been supplied exactly.
    /// For unknown input, resolves the length to accepted frames before draining.
    /// The final partial column is published when processing drains the queue.
    pub fn finish_input(&mut self) -> Result<(), Error> {
        if !matches!(self.state, SessionState::Created | SessionState::Running)
            || (self.config.total_frames != TOTAL_FRAMES_UNKNOWN
                && self.accepted != self.config.total_frames)
        {
            return Err(Error::InvalidState);
        }
        if let Some(detail) = &mut self.detail {
            detail.refresh_completed(Some(self.accepted))?;
        }
        self.config.total_frames = self.accepted;
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
            &mut crate::deadline::Deadline::disabled(),
        )
    }

    /// Cooperatively stop after a processing step reaches the injected clock's
    /// deadline. Times are nanoseconds; an initial zero disables timing. This
    /// does not bound callback or publication duration.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancellation: &CancellationToken,
    ) -> Result<Progress, Error> {
        let mut deadline = crate::deadline::Deadline::new(soft_us, clock);
        self.process_inner(budget, cancellation, true, &mut deadline)
    }

    pub(crate) fn set_publication_total_frames(&mut self, total: u64) {
        self.config.total_frames = total;
    }

    pub(crate) fn set_publication_state(&mut self, state: SessionState) {
        self.state = state;
        if let Some(a) = &mut self.analysis {
            a.set_completed(state == SessionState::Complete);
        }
        if let Some(g) = &mut self.global {
            g.set_completed(state == SessionState::Complete);
        }
    }

    pub(crate) fn ready_to_complete(&self) -> bool {
        self.state == SessionState::Draining
            && self.queued == 0
            && self.analysis.as_ref().map_or(true, |a| {
                !a.pending(Some(self.accepted)) && !a.meter_pending()
            })
            && self
                .global
                .as_ref()
                .map_or(true, |g| !g.pending(Some(self.accepted)))
            && self.key.as_ref().map_or(true, |k| !k.pending(true))
    }

    pub(crate) fn process_deferred_deadline(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<Progress, Error> {
        self.process_inner(budget, cancellation, false, deadline)
    }

    pub(crate) fn process_analysis(
        &mut self,
        steps: u32,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<u32, Error> {
        let available = if deadline.expired() {
            0
        } else if self.global.is_some() && steps != u32::MAX && steps > 1 {
            steps / 2
        } else {
            steps
        };
        let proposal = self.global.as_ref().and_then(|g| g.proposal());
        if let Some(analysis) = &mut self.analysis {
            analysis.set_global_proposal(proposal);
            let previous = analysis.tempo().map(|t| t.selected);
            let mut done = analysis.refresh(
                available,
                (self.state == SessionState::Draining).then_some(self.accepted),
                deadline,
            )?;
            if analysis.ensemble_clock_check() && done < steps && !deadline.expired() {
                done += analysis.ensemble(steps - done, previous)?;
            }
            Ok(done)
        } else {
            Ok(0)
        }
    }
    pub(crate) fn has_analysis(&self) -> bool {
        self.analysis.is_some() || self.global.is_some() || self.key.is_some()
    }
    pub(crate) fn process_global_analysis(
        &mut self,
        steps: u32,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<u32, Error> {
        let available = if deadline.expired() { 0 } else { steps };
        let fallback = self.tempo().map(|t| t.selected);
        let locked = self
            .local_grid()
            .filter(|g| g.flags & 256 != 0)
            .map(|g| g.segment);
        self.global.as_mut().map_or(Ok(0), |g| {
            g.refresh(
                available,
                (self.state == SessionState::Draining).then_some(self.accepted),
                fallback,
                locked,
                deadline,
            )
        })
    }
    pub(crate) fn process_key_analysis(
        &mut self,
        steps: u32,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<u32, Error> {
        let available = if deadline.expired() { 0 } else { steps };
        self.key.as_mut().map_or(Ok(0), |k| {
            k.refresh(available, self.state == SessionState::Draining)
        })
    }
    pub(crate) fn process_meter_analysis(
        &mut self,
        steps: u32,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<u32, Error> {
        let available = if deadline.expired() { 0 } else { steps };
        self.analysis
            .as_mut()
            .map_or(Ok(0), |a| a.refresh_meter(available))
    }
    fn process_inner(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        complete: bool,
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
        if self.state == SessionState::Draining {
            if let Some(detail) = &mut self.detail {
                detail.refresh_completed(Some(self.accepted))?;
            }
        }
        let mut progress = Progress::default();
        let frame_limit = if budget.maximum_input_frames == 0 {
            u32::MAX
        } else {
            budget.maximum_input_frames
        };
        let step_limit = if budget.maximum_steps == 0 {
            u32::MAX
        } else {
            budget.maximum_steps
        };
        while self.queued != 0
            && progress.consumed_input_frames < frame_limit
            && progress.completed_steps < step_limit
        {
            if cancellation.is_cancelled() {
                self.state = SessionState::Cancelled;
                return Err(Error::Cancelled);
            }
            let count = self
                .queued
                .min(256)
                .min((frame_limit - progress.consumed_input_frames) as usize);
            for _ in 0..count {
                let sample = self.queue[self.head];
                if let Some(key) = &mut self.key {
                    key.push(self.processed, sample.value);
                }
                self.accumulator
                    .push_normalized(sample.value, sample.clipped)?;
                if let Some(bands) = &mut self.bands {
                    bands.sums[self.written].add(bands.filter.split(sample.value)?)?;
                }
                self.head = (self.head + 1) % self.queue.len();
                self.queued -= 1;
                self.processed += 1;
                if self.processed % u64::from(self.config.frames_per_column) == 0 {
                    self.publish_column();
                }
            }
            progress.consumed_input_frames += count as u32;
            progress.completed_steps += 1;
            if deadline.expired() {
                break;
            }
        }
        if complete {
            progress.completed_steps +=
                self.process_analysis(step_limit - progress.completed_steps, deadline)?;
            progress.completed_steps +=
                self.process_global_analysis(step_limit - progress.completed_steps, deadline)?;
            progress.completed_steps +=
                self.process_meter_analysis(step_limit - progress.completed_steps, deadline)?;
            progress.completed_steps +=
                self.process_key_analysis(step_limit - progress.completed_steps, deadline)?;
        } else if !self.has_analysis() {
            deadline.analysis_boundaries();
        }
        if self.state == SessionState::Draining && self.queued == 0 {
            if self.accumulator.sample_count() != 0 {
                self.publish_column();
            }
            if complete
                && self.analysis.as_ref().map_or(true, |a| {
                    !a.pending(Some(self.accepted)) && !a.meter_pending()
                })
                && self
                    .global
                    .as_ref()
                    .map_or(true, |g| !g.pending(Some(self.accepted)))
                && self.key.as_ref().map_or(true, |k| !k.pending(true))
            {
                self.set_publication_state(SessionState::Complete);
            }
        }
        Ok(progress)
    }

    fn publish_column(&mut self) {
        let mut column = self.accumulator.column();
        if let Some(bands) = &self.bands {
            column =
                bands.sums[self.written].apply_complete(column, self.accumulator.sample_count());
        }
        self.output[self.written] = column;
        self.written += 1;
        self.accumulator.clear();
    }

    /// Completed columns. Rust prevents mutating the session while this view is used.
    pub fn columns(&self) -> &[WaveformColumn] {
        &self.output[..self.written]
    }

    /// Copies a snapshot into independent caller storage; it can outlive this session.
    pub fn copy_snapshot_into(&self, destination: &mut [WaveformColumn]) -> Result<usize, Error> {
        if destination.len() < self.written {
            return Err(Error::BufferTooSmall);
        }
        destination[..self.written].copy_from_slice(self.columns());
        Ok(self.written)
    }
}
