// SPDX-License-Identifier: Apache-2.0
//! Owning known-duration sparse writer using the portable sparse engine and scheduler.
use crate::{growing::array, HeapResult, HeapResults};
use libapta::{scheduler::*, session::*, sparse::*, waveform::*, *};

pub type OwningSparseSession = SparseSession<
    'static,
    Vec<SparseAccumulator>,
    Vec<FrameRange>,
    Vec<QueuedBlock>,
    Vec<NormalizedSample>,
    Vec<WaveformSpan>,
    Vec<WaveformColumn>,
    Vec<analysis::OnsetBin>,
    Vec<f32>,
    Vec<analysis::OnsetBin>,
    Vec<f32>,
    Vec<Beat>,
    Vec<band::BandSums>,
    Vec<detail_analysis::DetailTile>,
>;

#[derive(Clone, Copy, Debug)]
pub struct SparseLimits {
    pub maximum_columns: usize,
    pub queue_nodes: usize,
    pub range_capacity: usize,
    pub request_capacity: usize,
    pub maximum_working_bytes: usize,
    pub results: NativeLimits,
}
impl Default for SparseLimits {
    fn default() -> Self {
        Self {
            maximum_columns: 1_048_576,
            queue_nodes: 8,
            range_capacity: 4096,
            request_capacity: MAX_REQUESTS,
            maximum_working_bytes: 64 * 1024 * 1024,
            results: NativeLimits::default(),
        }
    }
}
/// One writer, fixed caller limits, independently retained heap generations.
/// Mutable storage is allocated before construction/attachment commits. After
/// a failed mirror, refresh before any further mutation. C intermediate stage
/// generations, allocator classes and callback ABI are not emulated.
pub struct OwnedSparseSession {
    session: OwningSparseSession,
    scheduler: Scheduler<'static, Vec<RequestSlot>>,
    limits: SparseLimits,
    working_bytes: usize,
    music_bytes: usize,
    bands_enabled: bool,
    detail_enabled: bool,
    results: HeapResults,
    dirty: bool,
    requested_features: Option<u64>,
    context: Option<crate::RuntimeContext>,
    // Keep the resource lease last, after all payload owners.
    _context_resource: Option<crate::context::Resource>,
}
fn bytes<T>(n: usize) -> Result<usize, Error> {
    n.checked_mul(core::mem::size_of::<T>())
        .ok_or(Error::LimitExceeded)
}
fn sum(parts: &[usize]) -> Result<usize, Error> {
    parts
        .iter()
        .try_fold(0usize, |n, p| n.checked_add(*p).ok_or(Error::LimitExceeded))
}
impl OwnedSparseSession {
    pub fn new(config: SessionConfig, limits: SparseLimits) -> Result<Self, Error> {
        Self::build(config, limits, None, None)
    }
    pub(crate) fn new_in_context(
        config: SessionConfig,
        limits: SparseLimits,
        context: crate::RuntimeContext,
        resource: crate::context::Resource,
    ) -> Result<Self, Error> {
        Self::build(config, limits, Some(context), Some(resource))
    }
    fn build(
        config: SessionConfig,
        limits: SparseLimits,
        context: Option<crate::RuntimeContext>,
        resource: Option<crate::context::Resource>,
    ) -> Result<Self, Error> {
        if config.total_frames == TOTAL_FRAMES_UNKNOWN
            || config.frames_per_column == 0
            || limits.queue_nodes == 0
            || limits.range_capacity == 0
            || limits.request_capacity > MAX_REQUESTS
        {
            return Err(Error::InvalidArgument);
        }
        let columns = usize::try_from(
            config
                .total_frames
                .div_ceil(u64::from(config.frames_per_column)),
        )
        .map_err(|_| Error::LimitExceeded)?;
        if columns > limits.maximum_columns {
            return Err(Error::LimitExceeded);
        }
        let pcm_count = limits
            .queue_nodes
            .checked_mul(NODE_FRAMES)
            .ok_or(Error::LimitExceeded)?;
        let minimum = sum(&[
            bytes::<SparseAccumulator>(columns)?,
            bytes::<FrameRange>(limits.range_capacity)?,
            bytes::<QueuedBlock>(limits.queue_nodes)?,
            bytes::<NormalizedSample>(pcm_count)?,
            bytes::<WaveformSpan>(columns)?,
            bytes::<WaveformColumn>(columns)?,
            bytes::<RequestSlot>(limits.request_capacity)?,
        ])?;
        if minimum > limits.maximum_working_bytes {
            return Err(Error::LimitExceeded);
        }
        let storage = SparseStorage {
            accumulators: array::<SparseAccumulator>(columns)?,
            ranges: array::<FrameRange>(limits.range_capacity)?,
            nodes: array::<QueuedBlock>(limits.queue_nodes)?,
            pcm: array::<NormalizedSample>(pcm_count)?,
            snapshot_spans: array::<WaveformSpan>(columns)?,
            snapshot_columns: array::<WaveformColumn>(columns)?,
        };
        let requests = array::<RequestSlot>(limits.request_capacity)?;
        let actual = sum(&[
            bytes::<SparseAccumulator>(storage.accumulators.capacity())?,
            bytes::<FrameRange>(storage.ranges.capacity())?,
            bytes::<QueuedBlock>(storage.nodes.capacity())?,
            bytes::<NormalizedSample>(storage.pcm.capacity())?,
            bytes::<WaveformSpan>(storage.snapshot_spans.capacity())?,
            bytes::<WaveformColumn>(storage.snapshot_columns.capacity())?,
            bytes::<RequestSlot>(requests.capacity())?,
        ])?;
        if actual > limits.maximum_working_bytes {
            return Err(Error::LimitExceeded);
        }
        let scheduler = Scheduler::with_storage(
            Some(config.total_frames),
            result::WAVEFORM_OVERVIEW,
            requests,
        )?;
        let mut session = SparseSession::with_storage(config, storage)?;
        let mut initial = HeapResult::copy_snapshot(&session.snapshot_graph(1)?, limits.results)?;
        if let Some(context) = &context {
            initial.attach_context(context)?;
        }
        Ok(Self {
            session,
            scheduler,
            limits,
            working_bytes: actual,
            music_bytes: 0,
            bands_enabled: false,
            detail_enabled: false,
            results: HeapResults::new(initial),
            dirty: false,
            requested_features: None,
            context,
            _context_resource: resource,
        })
    }
    pub fn session(&self) -> &OwningSparseSession {
        &self.session
    }
    pub fn results(&self) -> HeapResults {
        self.results.clone()
    }
    pub fn snapshot(&mut self) -> Result<libapta::session_snapshot::SessionSnapshot<'_>, Error> {
        self.ensure_clean()?;
        let snapshot = self
            .session
            .snapshot_graph(self.results.acquire()?.info().generation)?;
        match self.requested_features {
            Some(r) => snapshot.with_requested_features(r),
            None => Ok(snapshot),
        }
    }
    pub fn working_bytes(&self) -> usize {
        self.working_bytes
    }
    /// Configure explicit requested-capability projection before PCM. Attached
    /// stages may be a superset; the mask controls immutable output and mutations.
    pub fn set_requested_features(&mut self, requested: u64) -> Result<(), Error> {
        self.ensure_clean()?;
        if self.session.state() != SessionState::Created
            || self.music_bytes != 0
            || self.bands_enabled
            || self.detail_enabled
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
    fn feature_storage<T: Default + Clone>(&self, count: usize) -> Result<Vec<T>, Error> {
        self.ensure_clean()?;
        if self.session.state() != SessionState::Created
            || !self.session.accepted_ranges().is_empty()
        {
            return Err(Error::InvalidState);
        }
        let fits = |count| {
            bytes::<T>(count)
                .ok()
                .and_then(|n| self.working_bytes.checked_add(n))
                .is_some_and(|n| n <= self.limits.maximum_working_bytes)
        };
        if !fits(count) {
            return Err(Error::LimitExceeded);
        }
        let v = array(count)?;
        if !fits(v.capacity()) {
            return Err(Error::LimitExceeded);
        }
        Ok(v)
    }
    pub fn enable_three_band(&mut self) -> Result<(), Error> {
        if self
            .requested_features
            .is_some_and(|r| r & result::WAVEFORM_3BAND == 0)
        {
            return Err(Error::InvalidState);
        }
        if self.bands_enabled {
            return Err(Error::InvalidState);
        }
        let count =
            self.session
                .config()
                .total_frames
                .div_ceil(u64::from(self.session.config().frames_per_column)) as usize;
        let sums = self.feature_storage::<band::BandSums>(count)?;
        let bytes = bytes::<band::BandSums>(sums.capacity())?;
        self.session.enable_three_band(sums)?;
        self.working_bytes += bytes;
        self.bands_enabled = true;
        Ok(())
    }
    pub fn enable_detail(&mut self) -> Result<(), Error> {
        if self
            .requested_features
            .is_some_and(|r| r & result::WAVEFORM_DETAIL == 0)
        {
            return Err(Error::InvalidState);
        }
        if self.detail_enabled {
            return Err(Error::InvalidState);
        }
        let tiles =
            self.feature_storage::<detail_analysis::DetailTile>(detail_analysis::TILE_COUNT)?;
        let bytes = bytes::<detail_analysis::DetailTile>(tiles.capacity())?;
        self.session.enable_detail(tiles)?;
        self.session.configure_scheduler(
            &mut self.scheduler,
            self.requested_features.unwrap_or(result::ALL_FEATURES),
        );
        self.working_bytes += bytes;
        self.detail_enabled = true;
        Ok(())
    }
    pub fn enable_default_music(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        if self.music_bytes != 0 || self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        use libapta::{analysis, global_analysis};
        let minimum = self.working_bytes;
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
        self.working_bytes += actual;
        self.session.configure_scheduler(
            &mut self.scheduler,
            self.requested_features.unwrap_or(result::ALL_FEATURES),
        );
        Ok(())
    }

    /// Seed only validated overview evidence while Created. Attach features first.
    /// No new generation is published until subsequent input/process/EOF work.
    /// Native writers currently have no fingerprint, so required identity fails.
    pub fn seed_from_result(
        &mut self,
        checkpoint: &HeapResult,
        require_source_identity: bool,
    ) -> Result<(), Error> {
        self.ensure_clean()?;
        if self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        if require_source_identity {
            return Err(Error::Conflict);
        }
        let input = checkpoint.view();
        self.session.seed_overview(
            input.source,
            input.overview.ok_or(Error::Conflict)?,
            self.limits.results,
        )
    }
    pub fn request_region(&mut self, request: RegionRequest) -> Result<u32, Error> {
        self.ensure_clean()?;
        self.scheduler.request_region(request)
    }
    pub fn cancel_region_request(&mut self, id: u32) -> Result<(), Error> {
        self.ensure_clean()?;
        self.scheduler.cancel_region_request(id)
    }
    pub fn request_progress(&self, id: u32) -> Result<RequestProgress, Error> {
        self.scheduler.request_progress(id)
    }
    pub fn set_focus(&mut self, focus: Focus) -> Result<(), Error> {
        self.ensure_clean()?;
        self.scheduler.set_focus(focus)?;
        if self.music_bytes != 0 {
            self.session.set_tempo_focus(focus)?;
        }
        Ok(())
    }
    pub fn next_pcm_request(&mut self) -> Result<PcmDemand, Error> {
        self.ensure_clean()?;
        self.session.next_pcm_request(&mut self.scheduler)
    }
    pub(crate) fn next_overview_pcm_request(&mut self) -> Result<PcmDemand, Error> {
        self.ensure_clean()?;
        self.scheduler
            .next_overview_pcm_request(self.session.accepted_ranges())
    }
    pub fn push_at(&mut self, first: u64, pcm: PcmView<'_>) -> Result<usize, Error> {
        self.ensure_clean()?;
        let accepted = self
            .session
            .push_at_scheduled(first, pcm, Some(&mut self.scheduler))?;
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
            Some((us, clock)) => self.session.process_scheduled_with_clock(
                budget,
                us,
                clock,
                cancel,
                &mut self.scheduler,
            ),
            None => self
                .session
                .process_scheduled(budget, cancel, &mut self.scheduler),
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
        let mut result = crate::growing::snapshot_result(
            self.session.snapshot_graph(generation)?,
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
    pub fn set_result_limits(&mut self, limits: NativeLimits) {
        self.limits.results = limits;
    }
}
