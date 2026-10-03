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
        let available = self.input_capacity - self.accepted;
        let supplied = samples.frame_count(channels)?;
        if self.config.total_frames == TOTAL_FRAMES_UNKNOWN && available == 0 && supplied != 0 {
            return Err(Error::BufferTooSmall);
        }
        let count = supplied
            .min(self.queue.len() - self.queued)
            .min(usize::try_from(available).unwrap_or(usize::MAX));
        for index in 0..count {
            samples.sample_frame(index, channels)?;
        }
        for frame in 0..count {
            let sample = samples.sample_frame(frame, channels)?;
            let index = (self.head + self.queued) % self.queue.len();
            self.queue[index] = sample;
            self.queued += 1;
        }
        self.accepted += count as u64;
        if count != 0 {
            self.state = SessionState::Running;
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

    pub(crate) fn set_publication_state(&mut self, state: SessionState) {
        self.state = state;
    }

    pub(crate) fn ready_to_complete(&self) -> bool {
        self.state == SessionState::Draining && self.queued == 0
    }

    pub(crate) fn process_deferred_completion(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.process_deferred_deadline(
            budget,
            cancellation,
            &mut crate::deadline::Deadline::disabled(),
        )
    }

    pub(crate) fn process_deferred_deadline(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<Progress, Error> {
        self.process_inner(budget, cancellation, false, deadline)
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
                self.accumulator
                    .push_normalized(sample.value, sample.clipped)?;
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
        if self.state == SessionState::Draining && self.queued == 0 {
            if self.accumulator.sample_count() != 0 {
                self.publish_column();
            }
            if complete {
                self.state = SessionState::Complete;
            }
        }
        Ok(progress)
    }

    fn publish_column(&mut self) {
        self.output[self.written] = self.accumulator.column();
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
