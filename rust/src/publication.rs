// SPDX-License-Identifier: Apache-2.0
//! Bounded native publication with two caller-owned typed result slots.
//!
//! This safe pool has one thread of control (`RefCell`, not `Sync`). Independent
//! owned results can be shared between threads; concurrent C acquire/release
//! requires the later synchronized ABI boundary. The pool lives independently
//! of its session, so retained leases survive session destruction.
use crate::{
    owned_result::{self, OwnedResult, Storage},
    session::{
        self, CancellationToken, Progress, Session, SessionConfig, SessionState, WorkBudget,
    },
    waveform::{NormalizedSample, PcmView},
    *,
};
use core::cell::{Cell, Ref, RefCell};
use core::ops::Deref;

/// Caller storage needed for a complete sequential overview. There are two
/// result slots in addition to the session's working columns and PCM queue.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WorkspacePlan {
    pub working_columns: usize,
    pub columns_per_slot: usize,
    pub spans_per_slot: usize,
    /// Native retained graph size, using the same accounting as `OwnedResult`.
    /// This excludes excess caller capacity and the session/queue/pool controls.
    pub retained_bytes_per_slot: usize,
}

pub fn plan(config: SessionConfig, limits: NativeLimits) -> Result<WorkspacePlan, Error> {
    if config.sample_rate == 0
        || config.sample_rate > 768000
        || !(1..=2).contains(&config.channel_count)
        || !(64..=65536).contains(&config.frames_per_column)
        || !config.frames_per_column.is_power_of_two()
        || config.total_frames == session::TOTAL_FRAMES_UNKNOWN
    {
        return Err(Error::InvalidArgument);
    }
    let n = config.total_frames / u64::from(config.frames_per_column)
        + u64::from(config.total_frames % u64::from(config.frames_per_column) != 0);
    let n = usize::try_from(n).map_err(|_| Error::LimitExceeded)?;
    let spans = usize::from(n != 0);
    if n > u32::MAX as usize
        || n > limits.maximum_waveform_columns
        || spans > limits.maximum_overview_spans
    {
        return Err(Error::LimitExceeded);
    }
    let bytes = n
        .checked_mul(core::mem::size_of::<WaveformColumn>())
        .and_then(|v| v.checked_add(spans * core::mem::size_of::<WaveformSpan>()))
        .and_then(|v| v.checked_add(core::mem::size_of::<OwnedResult<'_>>()))
        .ok_or(Error::LimitExceeded)?;
    if bytes > limits.maximum_storage_bytes {
        return Err(Error::LimitExceeded);
    }
    Ok(WorkspacePlan {
        working_columns: n,
        columns_per_slot: n,
        spans_per_slot: spans,
        retained_bytes_per_slot: bytes,
    })
}

/// Storage for one session lifetime. A pool cannot be attached to a second
/// session, including after the original session has been dropped.
pub struct ResultPool<'a> {
    slots: [RefCell<OwnedResult<'a>>; 2],
    current: Cell<usize>,
    generation: Cell<u64>,
    attached: Cell<bool>,
    source: SourceInfo,
    limits: NativeLimits,
    column_capacity: [usize; 2],
    span_capacity: [usize; 2],
}

pub struct ResultLease<'p, 'a> {
    result: Ref<'p, OwnedResult<'a>>,
}
impl<'p, 'a> Clone for ResultLease<'p, 'a> {
    fn clone(&self) -> Self {
        Self {
            result: Ref::clone(&self.result),
        }
    }
}
impl<'a> Deref for ResultLease<'_, 'a> {
    type Target = OwnedResult<'a>;
    fn deref(&self) -> &Self::Target {
        &self.result
    }
}
fn empty<'a>(source: SourceInfo) -> NativeResultInput<'a> {
    NativeResultInput {
        source,
        info: NativeResultInfo {
            session_state: ResultSessionState::Created,
            ..NativeResultInfo::default()
        },
        provenance: Provenance {
            origin: ProvenanceOrigin::Unspecified,
            source_name: "",
            source_version: "",
        },
        overview: None,
        detail: None,
        metadata: None,
        tempo: None,
        local_grid: None,
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    }
}
impl<'a> ResultPool<'a> {
    pub fn new(
        source: SourceInfo,
        storage: [Storage<'a>; 2],
        limits: NativeLimits,
    ) -> Result<Self, Error> {
        let column_capacity = [
            storage[0].overview_columns.len(),
            storage[1].overview_columns.len(),
        ];
        let span_capacity = [
            storage[0].overview_spans.len(),
            storage[1].overview_spans.len(),
        ];
        let [a, b] = storage;
        let input = empty(source);
        let a = owned_result::copy_session(&input, a, limits, 0)?;
        let b = owned_result::copy_session(&input, b, limits, 0)?;
        Ok(Self {
            slots: [RefCell::new(a), RefCell::new(b)],
            current: Cell::new(0),
            generation: Cell::new(1),
            attached: Cell::new(false),
            source,
            limits,
            column_capacity,
            span_capacity,
        })
    }
    pub fn acquire(&self) -> Result<ResultLease<'_, 'a>, Error> {
        let result = self.slots[self.current.get()]
            .try_borrow()
            .map_err(|_| Error::InvalidState)?;
        Ok(ResultLease { result })
    }
    pub fn generation(&self) -> u64 {
        self.generation.get()
    }
    pub fn source(&self) -> SourceInfo {
        self.source
    }
    fn publish(&self, input: &NativeResultInput<'_>, changed: u64) -> Result<u64, Error> {
        if input.source != self.source {
            return Err(Error::InvalidArgument);
        }
        let next = self
            .generation
            .get()
            .checked_add(1)
            .ok_or(Error::LimitExceeded)?;
        let index = 1 - self.current.get();
        let mut slot = self.slots[index]
            .try_borrow_mut()
            .map_err(|_| Error::ResultSlotsExhausted)?;
        let mut input = *input;
        input.info.generation = next;
        slot.replace_session(&input, self.limits, changed)?;
        drop(slot);
        self.current.set(index);
        self.generation.set(next);
        Ok(next)
    }
}

/// Sequential waveform session integrated with immutable generation publication.
/// A slot-exhaustion error may follow consumed PCM. A later `process` can consume
/// further queued PCM and publishes the accumulated snapshot once a slot is free.
/// Query `session().processed_frames()` when observing an error after work.
pub struct PublishedSession<'p, 'work, 'storage> {
    session: Session<'work>,
    pool: &'p ResultPool<'storage>,
    pending: bool,
    changed: u64,
    previous_columns: usize,
    ended: bool,
}
impl<'p, 'work, 'storage> PublishedSession<'p, 'work, 'storage> {
    pub fn new(
        config: SessionConfig,
        queue: &'work mut [NormalizedSample],
        columns: &'work mut [WaveformColumn],
        pool: &'p ResultPool<'storage>,
    ) -> Result<Self, Error> {
        if config.total_frames == session::TOTAL_FRAMES_UNKNOWN
            || pool.source.sample_rate != config.sample_rate
            || pool.source.channel_count != config.channel_count
            || pool.source.total_frames != Some(config.total_frames)
        {
            return Err(Error::InvalidArgument);
        }
        let plan = plan(config, pool.limits)?;
        let session = Session::new(config, queue, columns)?;
        if pool
            .column_capacity
            .iter()
            .any(|cap| *cap < plan.columns_per_slot)
            || pool
                .span_capacity
                .iter()
                .any(|cap| *cap < plan.spans_per_slot)
        {
            return Err(Error::BufferTooSmall);
        }
        if pool.attached.replace(true) {
            return Err(Error::InvalidState);
        }
        Ok(Self {
            session,
            pool,
            pending: false,
            changed: 0,
            previous_columns: 0,
            ended: false,
        })
    }
    pub fn session(&self) -> &Session<'work> {
        &self.session
    }
    pub fn publication_pending(&self) -> bool {
        self.pending
    }
    pub fn push_pcm(&mut self, pcm: PcmView<'_>) -> Result<usize, Error> {
        if self.session.state() == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        pcm.frame_count(self.session.config().channel_count)?;
        if self.session.state() == SessionState::Created {
            self.transition(SessionState::Running)?;
        }
        self.session.push_pcm(pcm)
    }
    pub fn finish_input(&mut self) -> Result<(), Error> {
        if self.ended {
            return Ok(());
        }
        if !matches!(
            self.session.state(),
            SessionState::Created | SessionState::Running
        ) || self.session.accepted_frames() != self.session.config().total_frames
        {
            return Err(Error::InvalidState);
        }
        if self.session.state() == SessionState::Created {
            self.transition(SessionState::Running)?;
        }
        // Snapshot construction needs EOF before publishing the draining state.
        self.ended = true;
        if let Err(error) = self.transition(SessionState::Draining) {
            self.ended = false;
            return Err(error);
        }
        Ok(())
    }
    fn transition(&mut self, state: SessionState) -> Result<(), Error> {
        if self.session.state() == state {
            return Ok(());
        }
        let old = self.session.state();
        let changed = self.changed;
        self.session.set_publication_state(state);
        self.changed = 0;
        if let Err(error) = self.publish() {
            self.session.set_publication_state(old);
            self.changed = changed;
            return Err(error);
        }
        Ok(())
    }
    fn publish(&mut self) -> Result<(), Error> {
        let columns = self.session.columns();
        let config = self.session.config();
        let state = match self.session.state() {
            SessionState::Created => ResultSessionState::Created,
            SessionState::Running => ResultSessionState::AcceptingInput,
            SessionState::Draining => ResultSessionState::Draining,
            SessionState::Complete => ResultSessionState::Completed,
            SessionState::Cancelled => ResultSessionState::Cancelled,
            SessionState::Failed => ResultSessionState::Failed,
        };
        let end = (columns.len() as u64)
            .checked_mul(u64::from(config.frames_per_column))
            .ok_or(Error::LimitExceeded)?
            .min(config.total_frames);
        let spans = [WaveformSpan {
            first_frame: 0,
            end_frame: end,
            first_column_index: 0,
            column_count: u32::try_from(columns.len()).map_err(|_| Error::LimitExceeded)?,
            data_column_offset: 0,
        }];
        let mut input = empty(self.pool.source);
        input.info.session_state = state;
        let hide_pending = self.pending && self.changed == 0;
        input.overview = if columns.is_empty() || hide_pending {
            None
        } else {
            Some(NativeOverview {
                frames_per_column: config.frames_per_column,
                origin_frame: 0,
                state: if state == ResultSessionState::Completed {
                    FeatureState::Final
                } else if self.ended && end == config.total_frames {
                    FeatureState::Stable
                } else {
                    FeatureState::Partial
                },
                confidence: 255,
                spans: &spans,
                columns,
            })
        };
        self.pool.publish(&input, self.changed)?;
        self.pending = false;
        self.changed = 0;
        self.previous_columns = if hide_pending { 0 } else { columns.len() };
        Ok(())
    }
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.process_deadline(budget, cancel, None)
    }
    /// Apply a cooperative deadline using a caller-provided nanosecond clock.
    /// Clock checks occur after chunks; publication itself is outside the bound.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        let mut deadline = crate::deadline::Deadline::new(soft_us, clock);
        self.process_deadline(budget, cancel, Some(&mut deadline))
    }
    fn process_deadline(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
        deadline: Option<&mut crate::deadline::Deadline<'_>>,
    ) -> Result<Progress, Error> {
        if self.session.state() == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        if self.session.state() == SessionState::Failed {
            return Err(if cancel.is_cancelled() {
                Error::InvalidState
            } else {
                Error::Internal
            });
        }
        if cancel.is_cancelled() {
            if self.session.state() == SessionState::Complete {
                return Err(Error::InvalidState);
            }
            self.transition(SessionState::Cancelled)?;
            return Err(Error::Cancelled);
        }
        let old_state = self.session.state();
        let progress = match deadline {
            Some(deadline) => self.session.process_deferred_deadline(budget, cancel, deadline),
            None => self.session.process_deferred_completion(budget, cancel),
        };
        if progress == Err(Error::Cancelled) && old_state != SessionState::Cancelled {
            self.session.set_publication_state(old_state);
            self.transition(SessionState::Cancelled)?;
            return Err(Error::Cancelled);
        }
        if self.pending || self.session.columns().len() != self.previous_columns {
            self.pending = true;
            self.changed = crate::result::WAVEFORM_OVERVIEW;
            self.publish()?;
        }
        if self.session.ready_to_complete() {
            self.transition(SessionState::Complete)?;
        }
        progress
    }
}

/// Conservative retained-slot plan for arbitrary sparse complete-column coverage.
/// Working sparse buffers are described by `sparse::Workspace`.
pub fn plan_sparse(config: SessionConfig, limits: NativeLimits) -> Result<WorkspacePlan, Error> {
    let mut p = plan(config, limits)?;
    let spans = p.columns_per_slot / 2 + p.columns_per_slot % 2;
    if spans > limits.maximum_overview_spans {
        return Err(Error::LimitExceeded);
    }
    p.retained_bytes_per_slot = p
        .retained_bytes_per_slot
        .checked_add(
            (spans - p.spans_per_slot)
                .checked_mul(core::mem::size_of::<WaveformSpan>())
                .ok_or(Error::LimitExceeded)?,
        )
        .ok_or(Error::LimitExceeded)?;
    if p.retained_bytes_per_slot > limits.maximum_storage_bytes {
        return Err(Error::LimitExceeded);
    }
    p.spans_per_slot = spans;
    Ok(p)
}

/// Sparse overview input integrated with the same two immutable result slots.
/// Accepted ranges remain reserved after processing; result spans contain only
/// complete columns. EOF may complete a session whose overview still has holes.
pub struct PublishedSparseSession<'p, 'work, 'storage> {
    session: crate::sparse::SparseSession<'work>,
    scheduler: crate::scheduler::Scheduler<'work>,
    pool: &'p ResultPool<'storage>,
    pending: bool,
    changed: u64,
    previous_columns: usize,
    ended: bool,
}
impl<'p, 'work, 'storage> PublishedSparseSession<'p, 'work, 'storage> {
    pub fn new(
        config: SessionConfig,
        workspace: crate::sparse::Workspace<'work>,
        pool: &'p ResultPool<'storage>,
    ) -> Result<Self, Error> {
        Self::new_scheduled(config, workspace, pool, &mut [])
    }
    pub fn new_scheduled(
        config: SessionConfig,
        workspace: crate::sparse::Workspace<'work>,
        pool: &'p ResultPool<'storage>,
        requests: &'work mut [crate::scheduler::RequestSlot],
    ) -> Result<Self, Error> {
        if pool.source.sample_rate != config.sample_rate
            || pool.source.channel_count != config.channel_count
            || pool.source.total_frames != Some(config.total_frames)
        {
            return Err(Error::InvalidArgument);
        }
        let plan = plan_sparse(config, pool.limits)?;
        if pool
            .column_capacity
            .iter()
            .any(|cap| *cap < plan.columns_per_slot)
            || pool
                .span_capacity
                .iter()
                .any(|cap| *cap < plan.spans_per_slot)
        {
            return Err(Error::BufferTooSmall);
        }
        if pool.attached.get() {
            return Err(Error::InvalidState);
        }
        let scheduler = crate::scheduler::Scheduler::new(
            Some(config.total_frames),
            crate::result::WAVEFORM_OVERVIEW,
            requests,
        )?;
        let session = crate::sparse::SparseSession::new(config, workspace)?;
        pool.attached.set(true);
        Ok(Self {
            session,
            scheduler,
            pool,
            pending: false,
            changed: 0,
            previous_columns: 0,
            ended: false,
        })
    }
    pub fn session(&self) -> &crate::sparse::SparseSession<'work> {
        &self.session
    }
    /// Resume from validated owned overview data while still Created. Seeding
    /// does not publish, copy lineage, or retain a borrow of the checkpoint.
    /// Range storage is conservatively preflighted for all incoming spans.
    pub fn seed_from_result(
        &mut self,
        checkpoint: &OwnedResult<'_>,
        require_source_identity: bool,
    ) -> Result<(), Error> {
        if self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        let input = checkpoint.view();
        let overview = input.overview.ok_or(Error::Conflict)?;
        let source = self.pool.source;
        if input.source.sample_rate != source.sample_rate
            || input.source.channel_count != source.channel_count
            || (input.source.channel_layout != 0
                && source.channel_layout != 0
                && input.source.channel_layout != source.channel_layout)
            || (input.source.total_frames.is_some()
                && input.source.total_frames != source.total_frames)
            || overview.frames_per_column != self.session.config().frames_per_column
            || overview
                .spans
                .iter()
                .any(|s| s.end_frame > self.session.config().total_frames)
            || (require_source_identity
                && (source.fingerprint_kind == 0 || input.source.fingerprint_kind == 0))
            || (source.fingerprint_kind != 0
                && input.source.fingerprint_kind != 0
                && (source.fingerprint_kind != input.source.fingerprint_kind
                    || source.fingerprint != input.source.fingerprint))
        {
            return Err(Error::Conflict);
        }
        self.session.install_seed(overview)?;
        self.previous_columns = self.session.complete_columns();
        Ok(())
    }
    pub fn request_region(&mut self, request: RegionRequest) -> Result<u32, Error> {
        self.scheduler.request_region(request)
    }
    pub fn cancel_region_request(&mut self, id: u32) -> Result<(), Error> {
        self.scheduler.cancel_region_request(id)
    }
    pub fn request_progress(&self, id: u32) -> Result<RequestProgress, Error> {
        self.scheduler.request_progress(id)
    }
    pub fn set_focus(&mut self, focus: Focus) -> Result<(), Error> {
        self.scheduler.set_focus(focus)
    }
    pub fn next_pcm_request(&mut self) -> Result<PcmDemand, Error> {
        self.scheduler
            .next_pcm_request(self.session.accepted_ranges())
    }
    pub fn publication_pending(&self) -> bool {
        self.pending
    }
    pub fn push_at(&mut self, first: u64, pcm: PcmView<'_>) -> Result<usize, Error> {
        if self.session.state() == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        if !matches!(
            self.session.state(),
            SessionState::Created | SessionState::Running
        ) {
            return Err(Error::InvalidState);
        }
        let n = pcm.frame_count(self.session.config().channel_count)?;
        if n == 0 || first == u64::MAX {
            return Err(Error::InvalidArgument);
        }
        let end = first.checked_add(n as u64).ok_or(Error::InvalidArgument)?;
        if end > self.session.config().total_frames {
            return Err(Error::Conflict);
        }
        if self.session.state() == SessionState::Created {
            self.transition(SessionState::Running)?;
        }
        self.session.push_at(first, pcm)
    }
    pub fn finish_input(&mut self) -> Result<(), Error> {
        if self.ended {
            return Ok(());
        }
        if !matches!(
            self.session.state(),
            SessionState::Created | SessionState::Running
        ) {
            return Err(Error::InvalidState);
        }
        if self.session.state() == SessionState::Created {
            self.transition(SessionState::Running)?;
        }
        self.ended = true;
        self.session.set_publication_eof(true);
        if let Err(error) = self.transition(SessionState::Draining) {
            self.ended = false;
            self.session.set_publication_eof(false);
            return Err(error);
        }
        Ok(())
    }
    /// Publish a source failure transactionally. If no slot is free, the prior
    /// state remains usable so the pull adapter can retry the source operation.
    pub(crate) fn fail_source(&mut self) -> Result<(), Error> {
        if matches!(
            self.session.state(),
            SessionState::Complete | SessionState::Cancelled
        ) {
            return Err(Error::InvalidState);
        }
        self.transition(SessionState::Failed)
    }
    fn transition(&mut self, state: SessionState) -> Result<(), Error> {
        if self.session.state() == state {
            return Ok(());
        }
        let old = self.session.state();
        let changed = self.changed;
        self.session.set_publication_state(state);
        self.changed = 0;
        if let Err(error) = self.publish() {
            self.session.set_publication_state(old);
            self.changed = changed;
            return Err(error);
        }
        Ok(())
    }
    fn publish(&mut self) -> Result<(), Error> {
        let state = match self.session.state() {
            SessionState::Created => ResultSessionState::Created,
            SessionState::Running => ResultSessionState::AcceptingInput,
            SessionState::Draining => ResultSessionState::Draining,
            SessionState::Complete => ResultSessionState::Completed,
            SessionState::Cancelled => ResultSessionState::Cancelled,
            SessionState::Failed => ResultSessionState::Failed,
        };
        let n = self.session.complete_columns();
        let mut input = empty(self.pool.source);
        input.info.session_state = state;
        // C marks completed accumulators pending after slot exhaustion. A
        // state-only transition before the next process observes no overview.
        let hide_pending = self.pending && self.changed == 0;
        input.overview = if hide_pending {
            None
        } else {
            self.session.snapshot()
        };
        self.pool.publish(&input, self.changed)?;
        self.pending = false;
        self.changed = 0;
        self.previous_columns = if hide_pending { 0 } else { n };
        Ok(())
    }
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.process_deadline(budget, cancel, None)
    }
    /// Apply a cooperative deadline using a caller-provided nanosecond clock.
    /// Clock checks occur after chunks; publication itself is outside the bound.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        let mut deadline = crate::deadline::Deadline::new(soft_us, clock);
        self.process_deadline(budget, cancel, Some(&mut deadline))
    }
    fn process_deadline(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
        deadline: Option<&mut crate::deadline::Deadline<'_>>,
    ) -> Result<Progress, Error> {
        if self.session.state() == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        if self.session.state() == SessionState::Failed {
            return Err(if cancel.is_cancelled() {
                Error::InvalidState
            } else {
                Error::Internal
            });
        }
        if cancel.is_cancelled() {
            if self.session.state() == SessionState::Complete {
                return Err(Error::InvalidState);
            }
            self.transition(SessionState::Cancelled)?;
            return Err(Error::Cancelled);
        }
        let old = self.session.state();
        let work = match deadline {
            Some(deadline) => self.session.process_scheduled_deadline(budget, cancel, &self.scheduler, deadline),
            None => self.session.process_scheduled_deferred(budget, cancel, &self.scheduler),
        };
        let choice = work.as_ref().map_or(0, |(_, choice)| *choice);
        let progress = work.map(|(progress, _)| progress);
        if progress == Err(Error::Cancelled) && old != SessionState::Cancelled {
            self.session.set_publication_state(old);
            self.transition(SessionState::Cancelled)?;
            return Err(Error::Cancelled);
        }
        let fpc = self.session.config().frames_per_column;
        let snapshot = self.session.snapshot();
        self.scheduler
            .refresh_waveform(snapshot.map_or(&[], |v| v.spans), fpc)?;
        if self.pending || self.session.complete_columns() != self.previous_columns {
            self.pending = true;
            self.changed = crate::result::WAVEFORM_OVERVIEW;
            self.publish()?;
        }
        if progress
            .as_ref()
            .is_ok_and(|p| p.consumed_input_frames != 0)
        {
            self.scheduler.note_choice(choice);
        }
        if self.session.ready_to_complete() {
            self.transition(SessionState::Complete)?;
        }
        progress
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generation_overflow_preserves_current_result() {
        let source = SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: Some(0),
            channel_layout: 0,
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        };
        let pool = ResultPool::new(
            source,
            [Storage::default(), Storage::default()],
            NativeLimits::default(),
        )
        .unwrap();
        pool.generation.set(u64::MAX);
        assert_eq!(pool.publish(&empty(source), 0), Err(Error::LimitExceeded));
        assert_eq!(pool.generation(), u64::MAX);
        assert_eq!(pool.acquire().unwrap().view().info.generation, 1);
        assert_eq!(pool.current.get(), 0);
    }
}
