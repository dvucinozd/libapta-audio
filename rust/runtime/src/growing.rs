// SPDX-License-Identifier: Apache-2.0
//! Owning, fallibly growing sequential waveform/detail/musical sessions. Processing is the
//! existing portable Session; allocation and concurrent retention live here.
use crate::{HeapResult, HeapResults};
use libapta::{
    session::{
        CancellationToken, Progress, Session, SessionConfig, SessionState, WorkBudget,
        TOTAL_FRAMES_UNKNOWN,
    },
    waveform::{NormalizedSample, PcmView},
    *,
};

#[derive(Clone, Copy, Debug)]
pub struct GrowingLimits {
    pub maximum_queue_frames: usize,
    pub maximum_columns: usize,
    /// All attached working Vec capacities, excluding controls and snapshots.
    pub maximum_working_bytes: usize,
    pub results: NativeLimits,
}
impl Default for GrowingLimits {
    fn default() -> Self {
        Self {
            maximum_queue_frames: 32768,
            maximum_columns: 1_048_576,
            maximum_working_bytes: 64 * 1024 * 1024,
            results: NativeLimits::default(),
        }
    }
}
pub(crate) fn array<T: Default + Clone>(count: usize) -> Result<Vec<T>, Error> {
    let mut v = Vec::new();
    v.try_reserve_exact(count)
        .map_err(|_| Error::LimitExceeded)?;
    v.resize(count, T::default());
    Ok(v)
}
fn working_bytes(queue: usize, columns: usize) -> Result<usize, Error> {
    queue
        .checked_mul(core::mem::size_of::<NormalizedSample>())
        .and_then(|n| {
            columns
                .checked_mul(core::mem::size_of::<WaveformColumn>())
                .and_then(|c| n.checked_add(c))
        })
        .ok_or(Error::LimitExceeded)
}

pub type OwningSession = Session<
    'static,
    Vec<NormalizedSample>,
    Vec<WaveformColumn>,
    Vec<libapta::analysis::OnsetBin>,
    Vec<f32>,
    Vec<libapta::analysis::OnsetBin>,
    Vec<f32>,
    Vec<Beat>,
    Vec<libapta::band::BandSums>,
    Vec<libapta::detail_analysis::DetailTile>,
>;

/// One owning writer with independently retained heap generations. Supports
/// waveform, three-band overview, eager detail and all default musical stages. This does not emulate C's bounded-slot generations.
/// After snapshot failure, retry `refresh` before further mutations. PCM already
/// processed remains committed and old acquired generations remain unchanged.
pub struct GrowingSession {
    session: OwningSession,
    limits: GrowingLimits,
    queue_capacity: usize,
    column_capacity: usize,
    allocated_queue: usize,
    allocated_columns: usize,
    results: HeapResults,
    dirty: bool,
    requested_features: Option<u64>,
    music_bytes: usize,
    band_capacity: Option<usize>,
    band_bytes: usize,
    detail_bytes: usize,
    music_enabled: bool,
    key_enabled: bool,
    context: Option<crate::RuntimeContext>,
    _context_resource: Option<crate::context::Resource>,
}
impl GrowingSession {
    pub fn new(config: SessionConfig, limits: GrowingLimits) -> Result<Self, Error> {
        Self::build(config, limits, Default::default(), None, None)
    }
    /// Fix host identity before the initial immutable generation is published.
    pub fn new_with_identity(
        config: SessionConfig,
        limits: GrowingLimits,
        identity: libapta::session::SourceIdentity,
    ) -> Result<Self, Error> {
        Self::build(config, limits, identity, None, None)
    }
    fn build(
        config: SessionConfig,
        limits: GrowingLimits,
        identity: libapta::session::SourceIdentity,
        context: Option<crate::RuntimeContext>,
        context_resource: Option<crate::context::Resource>,
    ) -> Result<Self, Error> {
        if limits.maximum_queue_frames == 0 || config.frames_per_column == 0 {
            return Err(Error::InvalidArgument);
        }
        let columns = if config.total_frames == TOTAL_FRAMES_UNKNOWN {
            0
        } else {
            usize::try_from(
                config
                    .total_frames
                    .div_ceil(u64::from(config.frames_per_column)),
            )
            .map_err(|_| Error::LimitExceeded)?
        };
        let queue = limits.maximum_queue_frames.min(256);
        if columns > limits.maximum_columns
            || working_bytes(queue, columns)? > limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let q = array(queue)?;
        let o = array(columns)?;
        if working_bytes(q.capacity(), o.capacity())? > limits.maximum_working_bytes {
            return Err(Error::LimitExceeded);
        }
        let allocated_queue = q.capacity();
        let allocated_columns = o.capacity();
        let mut session = Session::with_storage(config, q, o)?;
        session.set_source_identity(identity)?;
        let mut initial = Self::snapshot(&session, 1, limits.results)?;
        if let Some(context) = &context {
            initial.attach_context(context)?;
        }
        Ok(Self {
            session,
            limits,
            queue_capacity: queue,
            column_capacity: columns,
            allocated_queue,
            allocated_columns,
            results: HeapResults::new(initial),
            dirty: false,
            requested_features: None,
            music_bytes: 0,
            band_capacity: None,
            band_bytes: 0,
            detail_bytes: 0,
            music_enabled: false,
            key_enabled: false,
            context,
            _context_resource: context_resource,
        })
    }
    pub(crate) fn new_in_context(
        config: SessionConfig,
        limits: GrowingLimits,
        identity: libapta::session::SourceIdentity,
        context: crate::RuntimeContext,
        resource: crate::context::Resource,
    ) -> Result<Self, Error> {
        Self::build(config, limits, identity, Some(context), Some(resource))
    }
    pub fn session(&self) -> &OwningSession {
        &self.session
    }
    pub(crate) fn maximum_queue_frames(&self) -> usize {
        self.limits.maximum_queue_frames
    }
    pub fn results(&self) -> HeapResults {
        self.results.clone()
    }
    /// Attach owning three-band accumulators before input. Unknown-duration
    /// storage grows together with overview columns, preserving filter history.
    pub fn enable_three_band(&mut self) -> Result<(), Error> {
        if self
            .requested_features
            .is_some_and(|r| r & result::WAVEFORM_3BAND == 0)
        {
            return Err(Error::InvalidState);
        }
        self.ensure_clean()?;
        if self.band_capacity.is_some() || self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        let bytes = self
            .column_capacity
            .checked_mul(core::mem::size_of::<libapta::band::BandSums>())
            .ok_or(Error::LimitExceeded)?;
        let base = working_bytes(self.allocated_queue, self.allocated_columns)?
            .checked_add(self.music_bytes)
            .and_then(|n| n.checked_add(self.detail_bytes))
            .ok_or(Error::LimitExceeded)?;
        if base.checked_add(bytes).ok_or(Error::LimitExceeded)? > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let sums = array::<libapta::band::BandSums>(self.column_capacity)?;
        let actual = sums
            .capacity()
            .checked_mul(core::mem::size_of::<libapta::band::BandSums>())
            .ok_or(Error::LimitExceeded)?;
        if base.checked_add(actual).ok_or(Error::LimitExceeded)? > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        self.session.enable_three_band(sums)?;
        self.band_capacity = Some(self.column_capacity);
        self.band_bytes = actual;
        Ok(())
    }
    /// Own the reference four-tile eager detail cache. Evicted tiles remain
    /// available through independently retained heap generations.
    pub fn enable_detail(&mut self) -> Result<(), Error> {
        if self
            .requested_features
            .is_some_and(|r| r & result::WAVEFORM_DETAIL == 0)
        {
            return Err(Error::InvalidState);
        }
        self.ensure_clean()?;
        if self.detail_bytes != 0 || self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        use libapta::detail_analysis::{DetailTile, TILE_COUNT};
        let base = working_bytes(self.allocated_queue, self.allocated_columns)?
            .checked_add(self.music_bytes)
            .and_then(|n| n.checked_add(self.band_bytes))
            .ok_or(Error::LimitExceeded)?;
        let fits = |count: usize| {
            count
                .checked_mul(core::mem::size_of::<DetailTile>())
                .and_then(|n| base.checked_add(n))
                .is_some_and(|n| n <= self.limits.maximum_working_bytes)
        };
        if !fits(TILE_COUNT) {
            return Err(Error::LimitExceeded);
        }
        let tiles = array::<DetailTile>(TILE_COUNT)?;
        if !fits(tiles.capacity()) {
            return Err(Error::LimitExceeded);
        }
        let bytes = tiles.capacity() * core::mem::size_of::<DetailTile>();
        self.session.enable_detail(tiles)?;
        self.detail_bytes = bytes;
        Ok(())
    }
    pub fn enable_key(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        self.session
            .enable_key_with_math(libapta::key_analysis::KeyMath {
                cos: f32::cos,
                log: f32::ln,
                sqrt: f32::sqrt,
            })?;
        self.key_enabled = true;
        Ok(())
    }
    /// Own all default musical workspaces before PCM. Every allocation and
    /// aggregate byte limit is preflighted before any analysis stage is attached.
    pub fn enable_default_music(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        if self.music_enabled || self.key_enabled || self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        use libapta::{analysis, global_analysis};
        let minimum = working_bytes(self.allocated_queue, self.allocated_columns)?
            .checked_add(self.band_bytes)
            .and_then(|n| n.checked_add(self.detail_bytes))
            .ok_or(Error::LimitExceeded)?;
        let bytes = (analysis::BIN_CAPACITY + global_analysis::BIN_CAPACITY)
            .checked_mul(core::mem::size_of::<analysis::OnsetBin>() + core::mem::size_of::<f32>())
            .and_then(|n| n.checked_add(global_analysis::MAX_BEATS * core::mem::size_of::<Beat>()))
            .ok_or(Error::LimitExceeded)?;
        if minimum.checked_add(bytes).ok_or(Error::LimitExceeded)?
            > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let bins = array::<analysis::OnsetBin>(analysis::BIN_CAPACITY)?;
        let flux = array::<f32>(analysis::BIN_CAPACITY)?;
        let global_bins = array::<analysis::OnsetBin>(global_analysis::BIN_CAPACITY)?;
        let global_flux = array::<f32>(global_analysis::BIN_CAPACITY)?;
        let beats = array::<Beat>(global_analysis::MAX_BEATS)?;
        let actual = (bins.capacity() + global_bins.capacity())
            .checked_mul(core::mem::size_of::<analysis::OnsetBin>())
            .and_then(|n| {
                (flux.capacity() + global_flux.capacity())
                    .checked_mul(core::mem::size_of::<f32>())
                    .and_then(|f| n.checked_add(f))
            })
            .and_then(|n| {
                beats
                    .capacity()
                    .checked_mul(core::mem::size_of::<Beat>())
                    .and_then(|b| n.checked_add(b))
            })
            .ok_or(Error::LimitExceeded)?;
        if minimum.checked_add(actual).ok_or(Error::LimitExceeded)?
            > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        self.session.enable_tempo(bins, flux)?;
        self.session.enable_global_grid(
            self.requested_features
                .map_or(true, |r| r & result::DYNAMIC_TEMPO != 0),
            global_bins,
            global_flux,
            beats,
        )?;
        self.session.enable_meter()?;
        self.session
            .enable_key_with_math(libapta::key_analysis::KeyMath {
                cos: f32::cos,
                log: f32::ln,
                sqrt: f32::sqrt,
            })?;
        self.session.enable_calibrated_quality()?;
        self.music_bytes = actual;
        self.music_enabled = true;
        self.key_enabled = true;
        Ok(())
    }
    /// Configure explicit requested-capability projection before PCM. Attached
    /// stages may be a superset; the mask controls immutable output and mutations.
    pub fn set_requested_features(&mut self, requested: u64) -> Result<(), Error> {
        self.ensure_clean()?;
        if self.session.state() != SessionState::Created
            || self.music_enabled
            || self.key_enabled
            || self.band_capacity.is_some()
            || self.detail_bytes != 0
        {
            return Err(Error::InvalidState);
        }
        libapta::publication::validate_requested_features(requested)?;
        if requested & result::WAVEFORM_OVERVIEW == 0 {
            return Err(Error::Unsupported);
        }
        self.requested_features = Some(requested);
        Ok(())
    }
    pub fn set_tempo_focus(&mut self, focus: Focus) -> Result<(), Error> {
        self.ensure_clean()?;
        self.session.set_tempo_focus(focus)
    }
    /// Working lock and current graph both remain unchanged if publication fails.
    pub fn lock_grid_range(&mut self, range: FrameRange) -> Result<(), Error> {
        self.ensure_clean()?;
        if range.first_frame >= range.end_frame {
            return Err(Error::InvalidArgument);
        }
        let required = result::LOCAL_BEATGRID | result::GRID_LOCKING;
        if self
            .requested_features
            .is_some_and(|r| r & required != required)
        {
            return Err(Error::Unsupported);
        }
        let generation = self
            .results
            .acquire()?
            .info()
            .generation
            .checked_add(1)
            .ok_or(Error::LimitExceeded)?;
        let requested = self.requested_features;
        let limits = self.limits.results;
        let context = &self.context;
        let results = &self.results;
        self.session
            .publish_grid_lock(range, generation, |snapshot| {
                let mut result = crate::growing::snapshot_result(snapshot, requested, limits)?;
                if let Some(context) = context {
                    result.attach_context(context)?;
                }
                results.publish(result)
            })?;
        Ok(())
    }
    /// Acceptance precedes publication. A failed mirror keeps Applied state and
    /// blocks further mutations until refresh succeeds; do not reapply the ID.
    pub fn apply_grid_revision(&mut self, id: u32) -> Result<(), Error> {
        self.ensure_clean()?;
        self.session.apply_grid_revision(id)?;
        self.dirty = true;
        self.refresh()?;
        Ok(())
    }
    fn ensure_clean(&self) -> Result<(), Error> {
        if self.dirty {
            Err(Error::InvalidState)
        } else {
            Ok(())
        }
    }
    /// Reserve queue/output together before committing either replacement.
    /// Growth keeps queued FIFO samples, a partial column and key history intact.
    fn reserve(&mut self, queue: usize, columns: usize) -> Result<(), Error> {
        let queue = queue.max(self.queue_capacity);
        let columns = columns.max(self.column_capacity);
        if self.detail_bytes != 0
            && (columns as u64)
                .checked_mul(u64::from(self.session.config().frames_per_column))
                .map_or(true, |n| {
                    n.div_ceil(libapta::detail_analysis::FRAMES_PER_COLUMN) > u64::from(u32::MAX)
                })
        {
            return Err(Error::LimitExceeded);
        }
        let band_bytes = if self.band_capacity.is_some() {
            columns
                .checked_mul(core::mem::size_of::<libapta::band::BandSums>())
                .ok_or(Error::LimitExceeded)?
        } else {
            0
        };
        if queue > self.limits.maximum_queue_frames
            || columns > self.limits.maximum_columns
            || working_bytes(queue, columns)?
                .checked_add(self.music_bytes)
                .and_then(|n| n.checked_add(self.detail_bytes))
                .and_then(|n| n.checked_add(band_bytes))
                .ok_or(Error::LimitExceeded)?
                > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let q = if queue > self.queue_capacity {
            Some(array::<NormalizedSample>(queue)?)
        } else {
            None
        };
        let o = if columns > self.column_capacity {
            Some(array::<WaveformColumn>(columns)?)
        } else {
            None
        };
        let bands = if self.band_capacity.is_some_and(|n| columns > n) {
            Some(array::<libapta::band::BandSums>(columns)?)
        } else {
            None
        };
        let actual_bands = match &bands {
            Some(b) => b
                .capacity()
                .checked_mul(core::mem::size_of::<libapta::band::BandSums>())
                .ok_or(Error::LimitExceeded)?,
            None => self.band_bytes,
        };
        let actual_q = q.as_ref().map_or(self.allocated_queue, Vec::capacity);
        let actual_o = o.as_ref().map_or(self.allocated_columns, Vec::capacity);
        if working_bytes(actual_q, actual_o)?
            .checked_add(self.music_bytes)
            .and_then(|n| n.checked_add(self.detail_bytes))
            .and_then(|n| n.checked_add(actual_bands))
            .ok_or(Error::LimitExceeded)?
            > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        if let Some(bands) = bands {
            self.session.replace_band_storage(bands)?;
            self.band_capacity = Some(columns);
            self.band_bytes = actual_bands;
        }
        self.allocated_queue = actual_q;
        self.allocated_columns = actual_o;
        if let Some(o) = o {
            self.session.replace_output(o)?;
            self.column_capacity = columns;
        }
        if let Some(q) = q {
            self.session.replace_queue(q)?;
            self.queue_capacity = queue;
        }
        Ok(())
    }
    pub fn push_pcm(&mut self, pcm: PcmView<'_>) -> Result<usize, Error> {
        self.ensure_clean()?;
        if !matches!(
            self.session.state(),
            SessionState::Created | SessionState::Running
        ) {
            return Err(Error::InvalidState);
        }
        let config = self.session.config();
        let supplied = pcm.frame_count(config.channel_count)?;
        let n = supplied
            .min(self.limits.maximum_queue_frames - self.session.queued_frames())
            .min(
                usize::try_from(
                    self.session
                        .total_frames()
                        .map_or(u64::MAX, |t| t - self.session.accepted_frames()),
                )
                .unwrap_or(usize::MAX),
            );
        for i in 0..n {
            pcm.sample_frame(i, config.channel_count)?;
        }
        let end = self
            .session
            .accepted_frames()
            .checked_add(n as u64)
            .ok_or(Error::LimitExceeded)?;
        let columns = usize::try_from(end.div_ceil(u64::from(config.frames_per_column)))
            .map_err(|_| Error::LimitExceeded)?;
        // Geometric output growth amortizes copying, with a hard caller ceiling.
        let columns = if columns > self.column_capacity {
            columns
                .max(self.column_capacity.saturating_mul(2))
                .min(self.limits.maximum_columns)
        } else {
            self.column_capacity
        };
        if end.div_ceil(u64::from(config.frames_per_column)) > columns as u64 {
            return Err(Error::LimitExceeded);
        }
        self.reserve(self.session.queued_frames() + n, columns)?;
        let accepted = self.session.push_pcm(pcm)?;
        if accepted != 0 {
            self.dirty = true;
            self.refresh()?;
        }
        Ok(accepted)
    }
    pub fn finish_input(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        self.session.finish_input()?;
        self.dirty = true;
        self.refresh()?;
        Ok(())
    }
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.process_clock(budget, cancel, None)
    }
    /// Cooperative processing clock; allocation/publication time is outside the
    /// core deadline. A failed mirror retries without repeating clocked work.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.process_clock(budget, cancel, Some((soft_us, clock)))
    }
    fn process_clock(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<Progress, Error> {
        self.ensure_clean()?;
        let before = (
            self.session.state(),
            self.session.processed_frames(),
            self.session.publication_serials(),
        );
        let work = match clock {
            Some((soft_us, clock)) => self
                .session
                .process_with_clock(budget, soft_us, clock, cancel),
            None => self.session.process(budget, cancel),
        };
        if before
            != (
                self.session.state(),
                self.session.processed_frames(),
                self.session.publication_serials(),
            )
        {
            self.dirty = true;
            self.refresh()?;
        }
        work
    }
    /// Retry only an unmirrored publication; no PCM or analysis step is repeated.
    pub fn refresh(&mut self) -> Result<bool, Error> {
        if !self.dirty {
            return Ok(false);
        }
        let generation = self
            .results
            .acquire()?
            .info()
            .generation
            .checked_add(1)
            .ok_or(Error::LimitExceeded)?;
        let mut result = snapshot_result(
            self.session.snapshot(generation)?,
            self.requested_features,
            self.limits.results,
        )?;
        if let Some(context) = &self.context {
            result.attach_context(context)?;
        }
        self.results.publish(result)?;
        self.dirty = false;
        Ok(true)
    }
    /// Adjust result limits to retry a rejected snapshot. Working limits stay fixed.
    pub fn set_result_limits(&mut self, limits: NativeLimits) {
        self.limits.results = limits;
    }
    fn snapshot(
        session: &OwningSession,
        generation: u64,
        limits: NativeLimits,
    ) -> Result<HeapResult, Error> {
        HeapResult::copy_snapshot(&session.snapshot(generation)?, limits)
    }
}

pub(crate) fn snapshot_result(
    snapshot: libapta::session_snapshot::SessionSnapshot<'_>,
    requested: Option<u64>,
    limits: NativeLimits,
) -> Result<HeapResult, Error> {
    let snapshot = match requested {
        Some(r) => snapshot.with_requested_features(r)?,
        None => snapshot,
    };
    HeapResult::copy_snapshot(&snapshot, limits)
}
