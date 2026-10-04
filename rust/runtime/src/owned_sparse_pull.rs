// SPDX-License-Identifier: Apache-2.0
//! Scheduled random-access pull input with bounded owned PCM and result storage.
//! Each process call performs at most one source read. Acquired blocks are always
//! released before processing; no callback borrow survives the call.
use crate::{HeapResults, OwnedSparseSession};
use libapta::pull::{PullProgress, PullRead, PullSource};
use libapta::session::{CancellationToken, SessionState, WorkBudget};
use libapta::{Error, Focus, PcmDemand, RegionRequest, RequestProgress};

pub struct OwnedScheduledPullSession<S> {
    session: OwnedSparseSession,
    source: S,
    failure: Option<Error>,
}

impl<S: PullSource> OwnedScheduledPullSession<S> {
    /// Attach once, while Created. This adapter currently requires a known
    /// source length, inherited from the sparse session. A reported length must
    /// match it; `None` leaves the configured length unchanged.
    pub fn new(session: OwnedSparseSession, mut source: S) -> Result<Self, Error> {
        if session.session().state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        if let Some(total) = source.total_frames() {
            if total == u64::MAX || total != session.session().config().total_frames {
                return Err(Error::Conflict);
            }
        }
        Ok(Self {
            session,
            source,
            failure: None,
        })
    }

    pub fn session(&self) -> &OwnedSparseSession {
        &self.session
    }
    pub fn results(&self) -> HeapResults {
        self.session.results()
    }
    pub fn set_tempo_focus(&mut self, focus: libapta::Focus) -> Result<(), Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.session.set_tempo_focus(focus)
    }
    pub fn lock_grid_range(&mut self, range: libapta::FrameRange) -> Result<(), Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.session.lock_grid_range(range)
    }
    pub fn apply_grid_revision(&mut self, id: u32) -> Result<(), Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.session.apply_grid_revision(id)
    }
    pub fn refresh(&mut self) -> Result<bool, Error> {
        self.session.refresh()
    }
    pub fn set_result_limits(&mut self, limits: libapta::NativeLimits) {
        self.session.set_result_limits(limits);
    }
    pub fn failure(&self) -> Option<Error> {
        self.failure
    }
    pub fn request_region(&mut self, request: RegionRequest) -> Result<u32, Error> {
        self.session.request_region(request)
    }
    pub fn cancel_region_request(&mut self, id: u32) -> Result<(), Error> {
        self.session.cancel_region_request(id)
    }
    pub fn request_progress(&self, id: u32) -> Result<RequestProgress, Error> {
        self.session.request_progress(id)
    }
    pub fn set_focus(&mut self, focus: Focus) -> Result<(), Error> {
        self.session.set_focus(focus)
    }
    pub fn next_pcm_request(&mut self) -> Result<PcmDemand, Error> {
        self.session.next_pcm_request()
    }
    /// Inspect source-owned diagnostics without changing its callback state.
    pub fn source(&self) -> &S {
        &self.source
    }
    pub fn into_source(self) -> S {
        self.source
    }

    /// Drain already-owned samples before calling the source again. Selection
    /// ages requests before reading, even if the source returns WouldBlock.
    /// A selected range with no missing PCM ends this known-length session;
    /// unrelated holes remain visible as partial overview coverage. Automatic
    /// reads use C's internal overview demand, including with detail enabled;
    /// explicit `next_pcm_request` exposes C's public detail replay demand.
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
    ) -> Result<PullProgress, Error> {
        self.process_clock(budget, cancellation, None)
    }
    /// Source reads and releases precede deadline initialization, matching C.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancellation: &CancellationToken,
    ) -> Result<PullProgress, Error> {
        self.process_clock(budget, cancellation, Some((soft_us, clock)))
    }
    fn process_clock(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<PullProgress, Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        self.session.refresh()?;
        if cancellation.is_cancelled() {
            return self.process_owned(budget, cancellation, false, clock);
        }
        match self.session.session().state() {
            SessionState::Failed => return Err(Error::Source),
            SessionState::Cancelled => return Err(Error::Cancelled),
            SessionState::Draining | SessionState::Complete => {
                return self.process_owned(budget, cancellation, false, clock)
            }
            _ => {}
        }
        if self.session.session().queued_frames() != 0 {
            return self.process_owned(budget, cancellation, false, clock);
        }
        let demand = match self.session.next_overview_pcm_request() {
            Ok(demand) => demand,
            Err(Error::NotAvailable) => {
                self.session.finish_input()?;
                return self.process_owned(budget, cancellation, false, clock);
            }
            Err(error) => return Err(error),
        };
        let mut maximum = u32::try_from(demand.range.end_frame - demand.range.first_frame)
            .map_err(|_| Error::Source)?;
        if budget.maximum_input_frames != 0 {
            maximum = maximum.min(budget.maximum_input_frames);
        }
        let channels = self.session.session().config().channel_count;
        // Split field borrows: the block borrows only source, leaving session
        // available for validation/acceptance and exactly-once drop on errors.
        let accepted = {
            let response = self.source.read_frames(demand.range.first_frame, maximum);
            if cancellation.is_cancelled() {
                drop(response);
                return self.process_owned(budget, cancellation, false, clock);
            }
            match response {
                Err(_) => Err(Error::Source),
                Ok(PullRead::WouldBlock) => Ok(true),
                Ok(PullRead::EndOfInput) => Err(Error::Source),
                Ok(PullRead::Data(block)) => {
                    let valid = block.pcm().frame_count(channels).is_ok_and(|frames| {
                        frames != 0
                            && frames <= maximum as usize
                            && block.first_frame() == demand.range.first_frame
                            && block
                                .first_frame()
                                .checked_add(frames as u64)
                                .is_some_and(|end| end <= demand.range.end_frame)
                    });
                    let status = if valid {
                        self.session
                            .push_at(block.first_frame(), block.pcm())
                            .map(|_| false)
                    } else {
                        Err(Error::Source)
                    };
                    drop(block);
                    status
                }
            }
        };
        match accepted {
            Ok(would_block) => self.process_owned(budget, cancellation, would_block, clock),
            Err(error @ (Error::ResultSlotsExhausted | Error::LimitExceeded)) => Err(error),
            Err(_) => {
                self.failure = Some(Error::Source);
                Err(Error::Source)
            }
        }
    }

    fn process_owned(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        would_block: bool,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<PullProgress, Error> {
        let processing = match clock {
            Some((soft_us, clock)) => {
                self.session
                    .process_with_clock(budget, soft_us, clock, cancellation)
            }
            None => self.session.process(budget, cancellation),
        }?;
        Ok(PullProgress {
            processing,
            would_block,
        })
    }
}
