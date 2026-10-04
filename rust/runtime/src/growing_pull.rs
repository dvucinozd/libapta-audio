// SPDX-License-Identifier: Apache-2.0
//! Owning sequential source adapter. Blocks are released before processing;
//! accepted input and failed mirrors are never replayed by the adapter.
use crate::{GrowingSession, HeapResults};
use libapta::{
    pull::{PullProgress, PullRead, PullSource, PullState},
    session::{CancellationToken, SessionState, WorkBudget},
    Error,
};

pub struct GrowingPullSession<S> {
    writer: GrowingSession,
    source: S,
    failure: Option<Error>,
}
impl<S: PullSource> GrowingPullSession<S> {
    /// Attach a configured owning session before any input. Its configured
    /// duration is authoritative, as for the portable sequential pull adapter.
    pub fn new(writer: GrowingSession, source: S) -> Result<Self, Error> {
        if writer.session().state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        Ok(Self {
            writer,
            source,
            failure: None,
        })
    }
    pub fn session(&self) -> &GrowingSession {
        &self.writer
    }
    pub fn results(&self) -> HeapResults {
        self.writer.results()
    }
    pub fn failure(&self) -> Option<Error> {
        self.failure
    }
    pub fn state(&self) -> PullState {
        match self.failure {
            Some(Error::Cancelled) => PullState::Cancelled,
            Some(_) => PullState::Failed,
            None => match self.writer.session().state() {
                SessionState::Created => PullState::Created,
                SessionState::Running => PullState::Running,
                SessionState::Draining => PullState::Draining,
                SessionState::Complete => PullState::Complete,
                SessionState::Cancelled => PullState::Cancelled,
                SessionState::Failed => PullState::Failed,
            },
        }
    }
    pub fn set_tempo_focus(&mut self, focus: libapta::Focus) -> Result<(), Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.writer.set_tempo_focus(focus)
    }
    pub fn lock_grid_range(&mut self, range: libapta::FrameRange) -> Result<(), Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.writer.lock_grid_range(range)
    }
    pub fn apply_grid_revision(&mut self, id: u32) -> Result<(), Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.writer.apply_grid_revision(id)
    }
    pub fn refresh(&mut self) -> Result<bool, Error> {
        self.writer.refresh()
    }
    pub fn set_result_limits(&mut self, limits: libapta::NativeLimits) {
        self.writer.set_result_limits(limits);
    }
    pub fn into_inner(self) -> S {
        self.source
    }
    /// At most one read and one 256-frame processing step per call. Allocation
    /// and mirror errors are retryable; source errors and malformed blocks are
    /// terminal. A retry first refreshes the mirror, then drains accepted PCM.
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
    ) -> Result<PullProgress, Error> {
        self.process_clock(budget, cancel, None)
    }
    /// Start the soft deadline after source read/release, matching sequential
    /// core pull. Source and heap allocation latency are outside this budget.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancel: &CancellationToken,
    ) -> Result<PullProgress, Error> {
        self.process_clock(budget, cancel, Some((soft_us, clock)))
    }
    fn process_clock(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<PullProgress, Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.writer.refresh()?;
        let maximum =
            self.writer
                .maximum_queue_frames()
                .min(256)
                .min(if budget.maximum_input_frames == 0 {
                    usize::MAX
                } else {
                    budget.maximum_input_frames as usize
                }) as u32;
        let step = WorkBudget {
            maximum_input_frames: maximum,
            maximum_steps: 1,
        };
        if cancel.is_cancelled()
            || self.writer.session().queued_frames() != 0
            || matches!(
                self.writer.session().state(),
                SessionState::Draining
                    | SessionState::Complete
                    | SessionState::Cancelled
                    | SessionState::Failed
            )
        {
            return self.process_accepted(step, cancel, false, clock);
        }
        let first = self.writer.session().accepted_frames();
        let total = self.writer.session().total_frames();
        if total == Some(first) {
            self.writer.finish_input()?;
            return self.process_accepted(step, cancel, false, clock);
        }
        let maximum = total.map_or(maximum, |end| (end - first).min(u64::from(maximum)) as u32);
        let mut would_block = false;
        {
            let response = self.source.read_frames(first, maximum);
            if cancel.is_cancelled() {
                drop(response);
                return self.process_accepted(step, cancel, false, clock);
            }
            match response {
                Err(error) => {
                    self.failure = Some(error);
                    return Err(error);
                }
                Ok(PullRead::WouldBlock) => {
                    would_block = true;
                }
                Ok(PullRead::EndOfInput) => {
                    if total.is_some() {
                        self.failure = Some(Error::InvalidArgument);
                        return Err(Error::InvalidArgument);
                    }
                    self.writer.finish_input()?;
                }
                Ok(PullRead::Data(block)) => {
                    let count = block
                        .pcm()
                        .frame_count(self.writer.session().config().channel_count);
                    if block.first_frame() != first
                        || !matches!(count, Ok(n) if n != 0 && n <= maximum as usize)
                    {
                        self.failure = Some(Error::InvalidArgument);
                        return Err(Error::InvalidArgument);
                    }
                    let accepted = self.writer.push_pcm(block.pcm());
                    drop(block);
                    match accepted {
                        Ok(n) if Some(n) == count.ok() => {}
                        Ok(_) => {
                            self.failure = Some(Error::InvalidState);
                            return Err(Error::InvalidState);
                        }
                        Err(error) => {
                            if error != Error::LimitExceeded {
                                self.failure = Some(error);
                            }
                            return Err(error);
                        }
                    }
                    if total == Some(self.writer.session().accepted_frames()) {
                        self.writer.finish_input()?;
                    }
                }
            }
        }
        self.process_accepted(step, cancel, would_block, clock)
    }
    fn process_accepted(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
        would_block: bool,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<PullProgress, Error> {
        let result = match clock {
            Some((soft_us, clock)) => self
                .writer
                .process_with_clock(budget, soft_us, clock, cancel),
            None => self.writer.process(budget, cancel),
        };
        match result {
            Ok(processing) => Ok(PullProgress {
                processing,
                would_block,
            }),
            Err(error) => {
                if error != Error::LimitExceeded {
                    self.failure = Some(error);
                }
                Err(error)
            }
        }
    }
}
