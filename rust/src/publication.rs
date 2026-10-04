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

/// Plan a sequential session with a caller-selected output ceiling when its
/// source duration is unknown. Known-duration sessions use their exact length.
pub fn plan_with_capacity(
    mut config: SessionConfig,
    limits: NativeLimits,
    maximum_columns: usize,
) -> Result<WorkspacePlan, Error> {
    if config.total_frames == session::TOTAL_FRAMES_UNKNOWN {
        config.total_frames = u64::try_from(maximum_columns)
            .map_err(|_| Error::LimitExceeded)?
            .checked_mul(u64::from(config.frames_per_column))
            .filter(|n| *n != session::TOTAL_FRAMES_UNKNOWN)
            .ok_or(Error::LimitExceeded)?;
    }
    plan(config, limits)
}

/// Complete typed-array planning for the attached default analysis profile.
/// Counts are hard maxima; retained bytes exclude unused caller capacity.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FeatureWorkspacePlan {
    pub overview: WorkspacePlan,
    pub result_slot: owned_result::Requirements,
    pub band_sums: usize,
    pub detail_cache_tiles: usize,
    pub detail_output_columns: usize,
    pub tempo_bins: usize,
    pub global_bins: usize,
    pub global_beats: usize,
    /// Bytes in caller-owned optional working arrays (not queue/control structs).
    pub optional_working_bytes: usize,
    /// Maximum retained graph bytes across both immutable slots.
    pub result_pool_bytes: usize,
}
fn feature_requirements(
    base: WorkspacePlan,
    features: u64,
    limits: NativeLimits,
) -> Result<owned_result::Requirements, Error> {
    use crate::result::*;
    let mut r = owned_result::Requirements {
        overview_spans: base.spans_per_slot,
        overview_columns: base.columns_per_slot,
        ..owned_result::Requirements::default()
    };
    if features & WAVEFORM_DETAIL != 0 {
        r.detail_tiles = 4;
        r.detail_columns = 256;
    }
    if features & BPM != 0 {
        r.tempo_candidates = 3;
        r.local_grid = owned_result::GridRequirements {
            coverage_ranges: 1,
            segments: 1,
            beats: 0,
        };
    }
    if features & GLOBAL_BEATGRID != 0 {
        r.global_grid = owned_result::GridRequirements {
            coverage_ranges: 1,
            segments: crate::global_analysis::MAX_SEGMENTS,
            beats: crate::global_analysis::MAX_BEATS,
        };
    }
    if features & MUSICAL_KEY != 0 {
        r.key_candidates = 3;
    }
    if features & METER_DOWNBEAT != 0 {
        r.meter_segments = 1;
    }
    if features & CALIBRATED_QUALITY != 0 {
        r.quality = 1;
    }
    if r.overview_columns
        .checked_add(r.detail_columns)
        .ok_or(Error::LimitExceeded)?
        > limits.maximum_waveform_columns
        || r.overview_spans > limits.maximum_overview_spans
        || r.detail_tiles > limits.maximum_detail_tiles
        || r.tempo_candidates > limits.maximum_tempo_candidates
        || r.key_candidates > limits.maximum_key_candidates
        || r.meter_segments > limits.maximum_meter_segments
        || r.quality > limits.maximum_quality_records
        || [r.local_grid, r.global_grid].iter().any(|g| {
            g.coverage_ranges > limits.maximum_grid_coverage_ranges
                || g.segments > limits.maximum_grid_segments
                || g.beats > limits.maximum_grid_beats
        })
    {
        return Err(Error::LimitExceeded);
    }
    r.retained_bytes = owned_result::retained_size(&r)?;
    if r.retained_bytes > limits.maximum_storage_bytes {
        return Err(Error::LimitExceeded);
    }
    Ok(r)
}
/// Plan known-duration sequential storage before initializing caller arrays.
/// Native S4 reserves local grid storage even for a BPM-only compatibility mask.
/// Sparse publication uses its own span/column base with the same feature preflight.
pub fn plan_features(
    config: SessionConfig,
    features: u64,
    limits: NativeLimits,
) -> Result<FeatureWorkspacePlan, Error> {
    use crate::result::*;
    validate_requested_features(features)?;
    let overview = plan(config, limits)?;
    let result_slot = feature_requirements(overview, features, limits)?;
    let mut p = FeatureWorkspacePlan {
        overview,
        result_slot,
        band_sums: if features & WAVEFORM_3BAND != 0 {
            overview.working_columns
        } else {
            0
        },
        detail_cache_tiles: if features & WAVEFORM_DETAIL != 0 {
            4
        } else {
            0
        },
        detail_output_columns: if features & WAVEFORM_DETAIL != 0 {
            256
        } else {
            0
        },
        tempo_bins: if features & BPM != 0 {
            crate::analysis::BIN_CAPACITY
        } else {
            0
        },
        global_bins: if features & GLOBAL_BEATGRID != 0 {
            crate::global_analysis::BIN_CAPACITY
        } else {
            0
        },
        global_beats: if features & GLOBAL_BEATGRID != 0 {
            crate::global_analysis::MAX_BEATS
        } else {
            0
        },
        optional_working_bytes: 0,
        result_pool_bytes: result_slot
            .retained_bytes
            .checked_mul(2)
            .ok_or(Error::LimitExceeded)?,
    };
    let entries = [
        (p.band_sums, core::mem::size_of::<crate::band::BandSums>()),
        (
            p.detail_cache_tiles,
            core::mem::size_of::<crate::detail_analysis::DetailTile>(),
        ),
        (
            p.detail_cache_tiles,
            core::mem::size_of::<crate::NativeTile>(),
        ),
        (
            p.detail_output_columns,
            core::mem::size_of::<WaveformColumn>(),
        ),
        (
            p.tempo_bins,
            core::mem::size_of::<crate::analysis::OnsetBin>() + core::mem::size_of::<f32>(),
        ),
        (
            p.global_bins,
            core::mem::size_of::<crate::analysis::OnsetBin>() + core::mem::size_of::<f32>(),
        ),
        (p.global_beats, core::mem::size_of::<Beat>()),
    ];
    for (count, size) in entries {
        p.optional_working_bytes = p
            .optional_working_bytes
            .checked_add(count.checked_mul(size).ok_or(Error::LimitExceeded)?)
            .ok_or(Error::LimitExceeded)?;
    }
    Ok(p)
}

/// Validate the unchanged C feature dependencies for the supported session surface.
pub fn validate_requested_features(features: u64) -> Result<(), Error> {
    use crate::result::*;
    if features & !ALL_FEATURES != 0 {
        return Err(Error::Unsupported);
    }
    if features & !WAVEFORM_OVERVIEW != 0 && features & WAVEFORM_OVERVIEW == 0
        || features & (LOCAL_BEATGRID | GLOBAL_BEATGRID | CALIBRATED_QUALITY) != 0
            && features & BPM == 0
        || features & DYNAMIC_TEMPO != 0 && features & GLOBAL_BEATGRID == 0
        || features & (GRID_LOCKING | METER_DOWNBEAT) != 0 && features & LOCAL_BEATGRID == 0
    {
        return Err(Error::InvalidArgument);
    }
    Ok(())
}

pub(crate) fn project_session_payload(
    input: &mut NativeResultInput<'_>,
    requested: u64,
    initially_unknown: bool,
) {
    if requested & crate::result::WAVEFORM_DETAIL == 0 {
        input.detail = None;
    }
    // Derived confidence/locking/dynamic bits in C are capabilities,
    // independently of the corresponding payload flags.
    if requested & crate::result::BPM == 0 {
        input.tempo = None;
    }
    // Unknown-duration compatibility follows C's nonbounded S4
    // snapshot, which retains its derived local grid for BPM-only.
    if requested & crate::result::LOCAL_BEATGRID == 0
        && (!initially_unknown || requested & crate::result::BPM == 0)
    {
        input.local_grid = None;
    }
    if requested & crate::result::GLOBAL_BEATGRID == 0 {
        input.global_grid = None;
        input.revision = None;
    }
    if requested & crate::result::MUSICAL_KEY == 0 {
        input.key = None;
    }
    if requested & crate::result::METER_DOWNBEAT == 0 {
        input.meter = None;
    }
    if requested & crate::result::CALIBRATED_QUALITY == 0
        || !matches!(
            input.info.session_state,
            ResultSessionState::Draining | ResultSessionState::Completed
        )
    {
        input.quality = &[];
    }
}
pub(crate) fn session_capabilities(mut available: u64, requested: u64) -> u64 {
    use crate::result::*;
    available &= !(CONFIDENCE | GRID_LOCKING | DYNAMIC_TEMPO | WAVEFORM_3BAND);
    if available & (WAVEFORM_OVERVIEW | BPM | LOCAL_BEATGRID | GLOBAL_BEATGRID) != 0 {
        available |= requested & CONFIDENCE;
    }
    if available & LOCAL_BEATGRID != 0 {
        available |= requested & GRID_LOCKING;
    }
    if available & GLOBAL_BEATGRID != 0 {
        available |= requested & DYNAMIC_TEMPO;
    }
    available
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
    detail_capacity: [(usize, usize); 2],
    music_capacity: [owned_result::Requirements; 2],
    requested_features: Option<u64>,
    attached_features: Cell<u64>,
    base_plan: Cell<Option<WorkspacePlan>>,
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
        let detail_capacity = storage
            .each_ref()
            .map(|s| (s.detail_tiles.len(), s.detail_columns.len()));
        let music_capacity = storage.each_ref().map(|s| owned_result::Requirements {
            tempo_candidates: s.tempo_candidates.len(),
            local_grid: owned_result::GridRequirements {
                coverage_ranges: s.local_grid.coverage_ranges.len(),
                segments: s.local_grid.segments.len(),
                beats: s.local_grid.beats.len(),
            },
            global_grid: owned_result::GridRequirements {
                coverage_ranges: s.global_grid.coverage_ranges.len(),
                segments: s.global_grid.segments.len(),
                beats: s.global_grid.beats.len(),
            },
            key_candidates: s.key_candidates.len(),
            meter_segments: s.meter_segments.len(),
            quality: s.quality.len(),
            ..owned_result::Requirements::default()
        });
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
            detail_capacity,
            music_capacity,
            requested_features: None,
            attached_features: Cell::new(crate::result::WAVEFORM_OVERVIEW),
            base_plan: Cell::new(None),
        })
    }
    /// Use C's requested-capability publication semantics. The ordinary `new`
    /// constructor preserves native content-derived availability. Analysis still
    /// needs explicit caller-storage attachment before input.
    pub fn new_with_requested_features(
        source: SourceInfo,
        storage: [Storage<'a>; 2],
        limits: NativeLimits,
        requested_features: u64,
    ) -> Result<Self, Error> {
        validate_requested_features(requested_features)?;
        if requested_features & crate::result::WAVEFORM_OVERVIEW == 0 {
            return Err(Error::Unsupported);
        }
        let mut pool = Self::new(source, storage, limits)?;
        pool.requested_features = Some(requested_features);
        Ok(pool)
    }
    pub fn requested_features(&self) -> Option<u64> {
        self.requested_features
    }
    fn preflight_lock(&self, range: crate::FrameRange) -> Result<(), Error> {
        if range.first_frame >= range.end_frame {
            return Err(Error::InvalidArgument);
        }
        let required = crate::result::LOCAL_BEATGRID | crate::result::GRID_LOCKING;
        if self
            .requested_features
            .is_some_and(|r| r & required != required)
        {
            return Err(Error::Unsupported);
        }
        Ok(())
    }
    fn preflight_feature(&self, feature: u64) -> Result<(), Error> {
        if self
            .requested_features
            .is_some_and(|requested| requested & feature == 0)
        {
            return Err(Error::InvalidState);
        }
        let base = self.base_plan.get().ok_or(Error::InvalidState)?;
        feature_requirements(base, self.attached_features.get() | feature, self.limits)?;
        Ok(())
    }
    fn note_feature(&self, feature: u64) {
        self.attached_features
            .set(self.attached_features.get() | feature);
    }
    fn ensure_music_capacity(&self, feature: u64) -> Result<(), Error> {
        self.preflight_feature(feature)?;
        use crate::result::*;
        let limits = self.limits;
        let limit_ok = match feature {
            BPM => {
                limits.maximum_tempo_candidates >= 3
                    && limits.maximum_grid_coverage_ranges >= 1
                    && limits.maximum_grid_segments >= 1
            }
            GLOBAL_BEATGRID => {
                limits.maximum_grid_coverage_ranges >= 1
                    && limits.maximum_grid_segments >= crate::global_analysis::MAX_SEGMENTS
                    && limits.maximum_grid_beats >= crate::global_analysis::MAX_BEATS
            }
            MUSICAL_KEY => limits.maximum_key_candidates >= 3,
            METER_DOWNBEAT => limits.maximum_meter_segments >= 1,
            CALIBRATED_QUALITY => limits.maximum_quality_records >= 1,
            _ => return Err(Error::InvalidArgument),
        };
        if !limit_ok {
            return Err(Error::LimitExceeded);
        }
        for capacity in self.music_capacity {
            let fits = match feature {
                BPM => {
                    capacity.tempo_candidates >= 3
                        && capacity.local_grid.coverage_ranges >= 1
                        && capacity.local_grid.segments >= 1
                }
                GLOBAL_BEATGRID => {
                    capacity.global_grid.coverage_ranges >= 1
                        && capacity.global_grid.segments >= crate::global_analysis::MAX_SEGMENTS
                        && capacity.global_grid.beats >= crate::global_analysis::MAX_BEATS
                }
                MUSICAL_KEY => capacity.key_candidates >= 3,
                METER_DOWNBEAT => capacity.meter_segments >= 1,
                CALIBRATED_QUALITY => capacity.quality >= 1,
                _ => false,
            };
            if !fits {
                return Err(Error::BufferTooSmall);
            }
        }
        Ok(())
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
        let mut identity = input.source;
        if self.source.total_frames.is_none() {
            identity.total_frames = None;
        }
        if identity != self.source {
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
        let changed = if let Some(requested) = self.requested_features {
            project_session_payload(&mut input, requested, self.source.total_frames.is_none());
            let mut changed = changed;
            if changed & crate::result::BPM != 0 {
                if input.tempo.is_none() {
                    changed &= !crate::result::BPM;
                }
                // C reports the S4 stage mutation (BPM | LOCAL_BEATGRID)
                // even when the requested mask projects out the grid payload.
                if input.tempo.is_some() {
                    changed |= requested & crate::result::CONFIDENCE;
                }
                if input.local_grid.is_some() {
                    changed |= requested & crate::result::GRID_LOCKING;
                }
            }
            if changed & crate::result::GLOBAL_BEATGRID != 0 {
                changed |= requested & (crate::result::CONFIDENCE | crate::result::DYNAMIC_TEMPO);
                if input
                    .revision
                    .is_some_and(|r| r.state == crate::RevisionState::Pending)
                {
                    changed |= requested & crate::result::GRID_LOCKING;
                }
            }
            changed
        } else {
            changed
        };
        slot.replace_session(&input, self.limits, changed)?;
        if let Some(requested) = self.requested_features {
            slot.apply_session_capabilities(requested);
        }
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
    detail_output: Option<(&'work mut [crate::NativeTile], &'work mut [WaveformColumn])>,
    previous_detail_serial: u64,
    previous_analysis_serial: u64,
    previous_global_serial: u64,
    previous_key_serial: u64,
    previous_meter_serial: u64,
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
        if pool.source.sample_rate != config.sample_rate
            || pool.source.channel_count != config.channel_count
            || pool.source.total_frames
                != (config.total_frames != session::TOTAL_FRAMES_UNKNOWN)
                    .then_some(config.total_frames)
        {
            return Err(Error::InvalidArgument);
        }
        let plan = plan_with_capacity(config, pool.limits, columns.len())?;
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
        pool.base_plan.set(Some(plan));
        Ok(Self {
            detail_output: None,
            previous_detail_serial: 0,
            previous_analysis_serial: 0,
            previous_global_serial: 0,
            previous_key_serial: 0,
            previous_meter_serial: 0,
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
    /// Attach optional caller-owned three-band overview storage before input.
    pub fn enable_three_band(
        &mut self,
        sums: &'work mut [crate::band::BandSums],
    ) -> Result<(), Error> {
        self.pool.preflight_feature(crate::result::WAVEFORM_3BAND)?;
        self.session.enable_three_band(sums)?;
        self.pool.note_feature(crate::result::WAVEFORM_3BAND);
        Ok(())
    }
    /// Attach caller-owned eager detail and immutable publication storage.
    pub fn enable_detail(
        &mut self,
        cache: &'work mut [crate::detail_analysis::DetailTile],
        tiles: &'work mut [crate::NativeTile],
        columns: &'work mut [WaveformColumn],
    ) -> Result<(), Error> {
        self.pool
            .preflight_feature(crate::result::WAVEFORM_DETAIL)?;
        let plan = self.pool.base_plan.get().ok_or(Error::InvalidState)?;
        let required_columns = plan
            .columns_per_slot
            .checked_add(256)
            .ok_or(Error::LimitExceeded)?;
        let required_bytes = plan
            .retained_bytes_per_slot
            .checked_add(4 * core::mem::size_of::<crate::NativeTile>())
            .and_then(|b| b.checked_add(256 * core::mem::size_of::<WaveformColumn>()))
            .ok_or(Error::LimitExceeded)?;
        if self.pool.limits.maximum_detail_tiles < 4
            || self.pool.limits.maximum_waveform_columns < required_columns
            || self.pool.limits.maximum_storage_bytes < required_bytes
        {
            return Err(Error::LimitExceeded);
        }
        if tiles.len() < 4
            || columns.len() < 256
            || self
                .pool
                .detail_capacity
                .iter()
                .any(|(t, c)| *t < 4 || *c < 256)
        {
            return Err(Error::BufferTooSmall);
        }
        self.session.enable_detail(cache)?;
        self.pool.note_feature(crate::result::WAVEFORM_DETAIL);
        self.detail_output = Some((tiles, columns));
        Ok(())
    }
    /// Borrow the current immutable generation without borrowing working PCM.
    pub fn acquire_result(&self) -> Result<ResultLease<'_, 'storage>, Error> {
        self.pool.acquire()
    }
    pub fn publication_pending(&self) -> bool {
        self.pending
    }
    pub fn push_pcm(&mut self, pcm: PcmView<'_>) -> Result<usize, Error> {
        if self.session.state() == SessionState::Cancelled {
            return Err(Error::Cancelled);
        }
        self.session.preflight_push(pcm)?;
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
        ) || (self.session.config().total_frames != session::TOTAL_FRAMES_UNKNOWN
            && self.session.accepted_frames() != self.session.config().total_frames)
        {
            return Err(Error::InvalidState);
        }
        if self.session.state() == SessionState::Created {
            self.transition(SessionState::Running)?;
        }
        // Snapshot construction needs EOF before publishing the draining state.
        self.ended = true;
        let original_total = self.session.config().total_frames;
        self.session
            .set_publication_total_frames(self.session.accepted_frames());
        if let Err(error) = self.transition(SessionState::Draining) {
            self.session.set_publication_total_frames(original_total);
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
        input.source.total_frames =
            (config.total_frames != session::TOTAL_FRAMES_UNKNOWN).then_some(config.total_frames);
        input.info.session_state = state;
        if let Some((tiles, columns)) = &mut self.detail_output {
            input.detail = self.session.copy_detail_into(tiles, columns)?;
        }
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
        input.tempo = self.session.tempo();
        let local = self.session.local_grid();
        let coverage = local.map(|g| [g.coverage]);
        let segments = local.map(|g| [g.segment]);
        input.local_grid = local.map(|g| crate::NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: crate::GridRepresentation::Segments,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: coverage.as_ref().unwrap(),
            segments: segments.as_ref().unwrap(),
            beats: &[],
        });
        let global = self.session.global_grid();
        let global_coverage = global.map(|g| [g.coverage_range]);
        input.global_grid = global.map(|g| crate::NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: g.representation,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: global_coverage.as_ref().unwrap(),
            segments: g.segments,
            beats: g.beats,
        });
        input.revision = self.session.grid_revision();
        input.key = self.session.key();
        let quality = self.session.bpm_quality().map(|q| [q]);
        input.quality = quality.as_ref().map_or(&[], |q| q.as_slice());
        input.meter = self.session.meter();
        let published = self.changed;
        self.pool.publish(&input, published)?;
        self.pending = false;
        self.changed = 0;
        self.previous_columns = if hide_pending { 0 } else { columns.len() };
        // Copying a complete graph does not acknowledge unrelated pending
        // stage mutations. C marks each stage only after its own publication.
        // Detail is part of the waveform snapshot, including state-only EOF.
        self.previous_detail_serial = self.session.detail_mutation_serial();
        if published & (crate::result::BPM | crate::result::LOCAL_BEATGRID) != 0 {
            self.previous_analysis_serial = self.session.analysis_serial();
        }
        if published & crate::result::GLOBAL_BEATGRID != 0 {
            self.previous_global_serial = self.session.global_serial();
        }
        if published & crate::result::MUSICAL_KEY != 0 {
            self.previous_key_serial = self.session.key_serial();
        }
        if published & crate::result::METER_DOWNBEAT != 0 {
            self.previous_meter_serial = self.session.meter_serial();
        }
        Ok(())
    }
    pub fn set_tempo_focus(&mut self, focus: crate::Focus) -> Result<(), Error> {
        self.session.set_tempo_focus(focus)
    }
    /// C-compatible acceptance ordering: a failed publication retains the accepted revision.
    pub fn apply_grid_revision(&mut self, id: u32) -> Result<(), Error> {
        self.session.apply_grid_revision(id)?;
        let previous = (self.pending, self.changed);
        self.changed = crate::result::LOCAL_BEATGRID
            | crate::result::GLOBAL_BEATGRID
            | crate::result::GRID_LOCKING;
        if self.session.global_grid().is_some_and(|g| g.flags & 2 != 0) {
            self.changed |= crate::result::DYNAMIC_TEMPO;
        }
        self.pending = true;
        if let Err(error) = self.publish() {
            // Applied working state remains. Retry through the separate S4/S6
            // handlers so failure ordering and stage acknowledgements survive.
            (self.pending, self.changed) = previous;
            return Err(error);
        }
        Ok(())
    }
    pub fn lock_grid_range(&mut self, range: crate::FrameRange) -> Result<(), Error> {
        self.pool.preflight_lock(range)?;
        let checkpoint = self.session.lock_checkpoint().ok_or(Error::InvalidState)?;
        self.session.lock_grid_range(range)?;
        if self.session.analysis_serial() == checkpoint.2 {
            return Ok(());
        }
        let changed = self.changed;
        self.changed = crate::result::LOCAL_BEATGRID | crate::result::GRID_LOCKING;
        if let Err(e) = self.publish() {
            self.session.restore_lock(checkpoint);
            self.changed = changed;
            return Err(e);
        }
        Ok(())
    }
    pub fn enable_calibrated_quality(&mut self) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::CALIBRATED_QUALITY)?;
        self.session.enable_calibrated_quality()?;
        self.pool.note_feature(crate::result::CALIBRATED_QUALITY);
        Ok(())
    }
    pub fn enable_meter(&mut self) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::METER_DOWNBEAT)?;
        self.session.enable_meter()?;
        self.pool.note_feature(crate::result::METER_DOWNBEAT);
        Ok(())
    }
    pub fn enable_key(&mut self) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::MUSICAL_KEY)?;
        self.session.enable_key()?;
        self.pool.note_feature(crate::result::MUSICAL_KEY);
        Ok(())
    }
    pub fn enable_global_grid(
        &mut self,
        dynamic: bool,
        bins: &'work mut [crate::analysis::OnsetBin],
        flux: &'work mut [f32],
        beats: &'work mut [crate::Beat],
    ) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::GLOBAL_BEATGRID)?;
        self.session
            .enable_global_grid(dynamic, bins, flux, beats)?;
        self.pool.note_feature(crate::result::GLOBAL_BEATGRID);
        Ok(())
    }
    pub fn enable_tempo(
        &mut self,
        bins: &'work mut [crate::analysis::OnsetBin],
        flux: &'work mut [f32],
    ) -> Result<(), Error> {
        self.pool.ensure_music_capacity(crate::result::BPM)?;
        self.session.enable_tempo(bins, flux)?;
        self.pool.note_feature(crate::result::BPM);
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
        let mut disabled = crate::deadline::Deadline::disabled();
        let deadline = deadline.unwrap_or(&mut disabled);
        let mut progress = self
            .session
            .process_deferred_deadline(budget, cancel, deadline);
        if progress == Err(Error::Cancelled) && old_state != SessionState::Cancelled {
            self.session.set_publication_state(old_state);
            self.transition(SessionState::Cancelled)?;
            return Err(Error::Cancelled);
        }
        let overview_changed = self.session.columns().len() != self.previous_columns;
        let detail_changed = self.session.detail_mutation_serial() != self.previous_detail_serial;
        if self.pending || overview_changed || detail_changed {
            self.pending = true;
            if overview_changed {
                self.changed = crate::result::WAVEFORM_OVERVIEW;
            }
            if detail_changed && !overview_changed {
                self.changed |= crate::result::WAVEFORM_DETAIL;
            }
            self.publish()?;
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_analysis(available, deadline)?;
            }
            if self.session.analysis_serial() != self.previous_analysis_serial {
                self.pending = true;
                self.changed = crate::result::BPM | crate::result::LOCAL_BEATGRID;
                self.publish()?;
            }
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_global_analysis(available, deadline)?;
            }
            if self.session.global_serial() != self.previous_global_serial {
                self.pending = true;
                self.changed = crate::result::GLOBAL_BEATGRID;
                self.publish()?;
            }
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_meter_analysis(available, deadline)?;
            }
            if self.session.meter_serial() != self.previous_meter_serial {
                self.pending = true;
                self.changed = crate::result::METER_DOWNBEAT;
                self.publish()?;
            }
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_key_analysis(available, deadline)?;
            }
            if self.session.key_serial() != self.previous_key_serial {
                self.pending = true;
                self.changed = crate::result::MUSICAL_KEY;
                self.publish()?;
            }
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
    detail_output: Option<(&'work mut [crate::NativeTile], &'work mut [WaveformColumn])>,
    previous_detail_serial: u64,
    previous_analysis_serial: u64,
    previous_global_serial: u64,
    previous_key_serial: u64,
    previous_meter_serial: u64,
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
        pool.base_plan.set(Some(plan));
        Ok(Self {
            session,
            scheduler,
            detail_output: None,
            previous_detail_serial: 0,
            previous_analysis_serial: 0,
            previous_global_serial: 0,
            previous_key_serial: 0,
            previous_meter_serial: 0,
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
    /// Attach optional band/detail storage before seeding. Only overview peaks,
    /// RMS, clipping and accepted coverage are restored: band sums/filter history
    /// and detail cache stay fresh even if the checkpoint publishes those outputs.
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
    fn enable_scheduled_music(&mut self, features: u64) {
        self.scheduler.enable_music(
            features
                & self
                    .pool
                    .requested_features
                    .unwrap_or(crate::result::ALL_FEATURES),
        );
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
        self.scheduler.set_focus(focus)?;
        if self.pool.attached_features.get() & crate::result::BPM != 0 {
            self.session.set_tempo_focus(focus)?;
        }
        Ok(())
    }
    pub fn next_pcm_request(&mut self) -> Result<PcmDemand, Error> {
        if let Some(detail) = self.session.detail_cache() {
            match self.scheduler.next_detail_request(detail) {
                Ok(demand) => return Ok(demand),
                Err(Error::NotAvailable) => {}
                Err(error) => return Err(error),
            }
        }
        self.scheduler
            .next_pcm_request(self.session.accepted_ranges())
    }
    // C's effective pull wrapper uses overview demand, unlike its public
    // next_pcm_request which prioritizes detail replay. Preserve that distinction.
    pub(crate) fn next_overview_pcm_request(&mut self) -> Result<PcmDemand, Error> {
        self.scheduler
            .next_overview_pcm_request(self.session.accepted_ranges())
    }
    /// Attach eager detail cache and publication scratch while Created.
    /// Enables detail requests, focus protection and aligned replay demand.
    /// All four cache tiles and retained snapshots remain caller-owned.
    pub fn enable_detail(
        &mut self,
        cache: &'work mut [crate::detail_analysis::DetailTile],
        tiles: &'work mut [crate::NativeTile],
        columns: &'work mut [WaveformColumn],
    ) -> Result<(), Error> {
        self.pool
            .preflight_feature(crate::result::WAVEFORM_DETAIL)?;
        let plan = plan_sparse(self.session.config(), self.pool.limits)?;
        let columns_required = plan
            .columns_per_slot
            .checked_add(256)
            .ok_or(Error::LimitExceeded)?;
        let bytes_required = plan
            .retained_bytes_per_slot
            .checked_add(4 * core::mem::size_of::<crate::NativeTile>())
            .and_then(|bytes| bytes.checked_add(256 * core::mem::size_of::<WaveformColumn>()))
            .ok_or(Error::LimitExceeded)?;
        if self.pool.limits.maximum_detail_tiles < 4
            || self.pool.limits.maximum_waveform_columns < columns_required
            || self.pool.limits.maximum_storage_bytes < bytes_required
        {
            return Err(Error::LimitExceeded);
        }
        if tiles.len() < 4
            || columns.len() < 256
            || self
                .pool
                .detail_capacity
                .iter()
                .any(|(t, c)| *t < 4 || *c < 256)
        {
            return Err(Error::BufferTooSmall);
        }
        self.session.enable_detail(cache)?;
        self.pool.note_feature(crate::result::WAVEFORM_DETAIL);
        self.scheduler.enable_detail();
        self.detail_output = Some((tiles, columns));
        Ok(())
    }
    /// Attach optional caller-owned three-band overview storage before input.
    pub fn enable_three_band(
        &mut self,
        sums: &'work mut [crate::band::BandSums],
    ) -> Result<(), Error> {
        self.pool.preflight_feature(crate::result::WAVEFORM_3BAND)?;
        self.session.enable_three_band(sums)?;
        self.pool.note_feature(crate::result::WAVEFORM_3BAND);
        Ok(())
    }
    /// Borrow the current immutable generation without borrowing working PCM.
    pub fn acquire_result(&self) -> Result<ResultLease<'_, 'storage>, Error> {
        self.pool.acquire()
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
        // Reject invalid floating PCM before the first state publication. The
        // sparse core also preflights its accepted prefix before working writes.
        if matches!(pcm, PcmView::F32Interleaved(_) | PcmView::F32Planar(_)) {
            for index in 0..n {
                pcm.sample_frame(index, self.session.config().channel_count)?;
            }
        }
        if self.session.state() == SessionState::Created {
            self.transition(SessionState::Running)?;
        }
        self.session
            .push_at_scheduled(first, pcm, Some(&mut self.scheduler))
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
        let _ = self.session.snapshot();
        let local = self.session.local_grid();
        let global = self.session.global_grid();
        let mut input = empty(self.pool.source);
        input.info.session_state = state;
        if let Some((tiles, columns)) = &mut self.detail_output {
            input.detail = self.session.copy_detail_into(tiles, columns)?;
        }
        // C marks completed accumulators pending after slot exhaustion. A
        // state-only transition before the next process observes no overview.
        let hide_pending = self.pending && self.changed == 0;
        input.overview = if hide_pending {
            None
        } else {
            self.session.snapshot_view()
        };
        input.tempo = self.session.tempo();
        let coverage = local.map(|g| [g.coverage]);
        let segments = local.map(|g| [g.segment]);
        input.local_grid = local.map(|g| crate::NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: crate::GridRepresentation::Segments,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: coverage.as_ref().unwrap(),
            segments: segments.as_ref().unwrap(),
            beats: &[],
        });
        let global_coverage = global.map(|g| [g.coverage_range]);
        input.global_grid = global.map(|g| crate::NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: g.representation,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: global_coverage.as_ref().unwrap(),
            segments: g.segments,
            beats: g.beats,
        });
        input.revision = self.session.grid_revision();
        input.key = self.session.key();
        let quality = self.session.bpm_quality().map(|q| [q]);
        input.quality = quality.as_ref().map_or(&[], |q| q.as_slice());
        input.meter = self.session.meter();
        let published = self.changed;
        self.pool.publish(&input, published)?;
        self.pending = false;
        self.changed = 0;
        self.previous_columns = if hide_pending { 0 } else { n };
        // Copying a complete graph does not acknowledge unrelated pending
        // stage mutations. C marks each stage only after its own publication.
        // Detail is part of the waveform snapshot, including state-only EOF.
        self.previous_detail_serial = self.session.detail_mutation_serial();
        if published & (crate::result::BPM | crate::result::LOCAL_BEATGRID) != 0 {
            self.previous_analysis_serial = self.session.analysis_serial();
        }
        if published & crate::result::GLOBAL_BEATGRID != 0 {
            self.previous_global_serial = self.session.global_serial();
        }
        if published & crate::result::MUSICAL_KEY != 0 {
            self.previous_key_serial = self.session.key_serial();
        }
        if published & crate::result::METER_DOWNBEAT != 0 {
            self.previous_meter_serial = self.session.meter_serial();
        }
        Ok(())
    }
    pub fn set_tempo_focus(&mut self, focus: crate::Focus) -> Result<(), Error> {
        self.set_focus(focus)
    }
    /// C-compatible acceptance ordering: a failed publication retains the accepted revision.
    pub fn apply_grid_revision(&mut self, id: u32) -> Result<(), Error> {
        self.session.apply_grid_revision(id)?;
        let previous = (self.pending, self.changed);
        self.changed = crate::result::LOCAL_BEATGRID
            | crate::result::GLOBAL_BEATGRID
            | crate::result::GRID_LOCKING;
        if self.session.global_grid().is_some_and(|g| g.flags & 2 != 0) {
            self.changed |= crate::result::DYNAMIC_TEMPO;
        }
        self.pending = true;
        if let Err(error) = self.publish() {
            // Applied working state remains. Retry through the separate S4/S6
            // handlers so failure ordering and stage acknowledgements survive.
            (self.pending, self.changed) = previous;
            return Err(error);
        }
        Ok(())
    }
    pub fn lock_grid_range(&mut self, range: crate::FrameRange) -> Result<(), Error> {
        self.pool.preflight_lock(range)?;
        let checkpoint = self.session.lock_checkpoint().ok_or(Error::InvalidState)?;
        self.session.lock_grid_range(range)?;
        if self.session.analysis_serial() == checkpoint.2 {
            return Ok(());
        }
        let changed = self.changed;
        self.changed = crate::result::LOCAL_BEATGRID | crate::result::GRID_LOCKING;
        if let Err(e) = self.publish() {
            self.session.restore_lock(checkpoint);
            self.changed = changed;
            return Err(e);
        }
        Ok(())
    }
    pub fn enable_calibrated_quality(&mut self) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::CALIBRATED_QUALITY)?;
        self.session.enable_calibrated_quality()?;
        self.pool.note_feature(crate::result::CALIBRATED_QUALITY);
        self.scheduler
            .enable_music(crate::result::CALIBRATED_QUALITY);
        Ok(())
    }
    pub fn enable_meter(&mut self) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::METER_DOWNBEAT)?;
        self.session.enable_meter()?;
        self.pool.note_feature(crate::result::METER_DOWNBEAT);
        self.enable_scheduled_music(crate::result::METER_DOWNBEAT);
        Ok(())
    }
    pub fn enable_key(&mut self) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::MUSICAL_KEY)?;
        self.session.enable_key()?;
        self.pool.note_feature(crate::result::MUSICAL_KEY);
        self.enable_scheduled_music(crate::result::MUSICAL_KEY);
        Ok(())
    }
    pub fn enable_global_grid(
        &mut self,
        dynamic: bool,
        bins: &'work mut [crate::analysis::OnsetBin],
        flux: &'work mut [f32],
        beats: &'work mut [crate::Beat],
    ) -> Result<(), Error> {
        self.pool
            .ensure_music_capacity(crate::result::GLOBAL_BEATGRID)?;
        self.session
            .enable_global_grid(dynamic, bins, flux, beats)?;
        self.pool.note_feature(crate::result::GLOBAL_BEATGRID);
        self.enable_scheduled_music(
            crate::result::GLOBAL_BEATGRID
                | if dynamic {
                    crate::result::DYNAMIC_TEMPO
                } else {
                    0
                },
        );
        Ok(())
    }
    pub fn enable_tempo(
        &mut self,
        bins: &'work mut [crate::analysis::OnsetBin],
        flux: &'work mut [f32],
    ) -> Result<(), Error> {
        self.pool.ensure_music_capacity(crate::result::BPM)?;
        self.session.enable_tempo(bins, flux)?;
        self.pool.note_feature(crate::result::BPM);
        self.enable_scheduled_music(
            crate::result::BPM | crate::result::LOCAL_BEATGRID | crate::result::GRID_LOCKING,
        );
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
        let mut disabled = crate::deadline::Deadline::disabled();
        let deadline = deadline.unwrap_or(&mut disabled);
        let work =
            self.session
                .process_scheduled_deadline(budget, cancel, &self.scheduler, deadline);
        let choice = work.as_ref().map_or(0, |(_, choice)| *choice);
        let mut progress = work.map(|(progress, _)| progress);
        if progress == Err(Error::Cancelled) && old != SessionState::Cancelled {
            self.session.set_publication_state(old);
            self.transition(SessionState::Cancelled)?;
            return Err(Error::Cancelled);
        }
        let fpc = self.session.config().frames_per_column;
        let snapshot = self.session.snapshot();
        self.scheduler
            .refresh_waveform(snapshot.map_or(&[], |v| v.spans), fpc)?;
        let detail_changed = self.session.detail_mutation_serial() != self.previous_detail_serial;
        let overview_changed = self.session.complete_columns() != self.previous_columns;
        let overview_publication = overview_changed
            || (self.pending && self.changed & crate::result::WAVEFORM_OVERVIEW != 0);
        if overview_publication {
            self.pending = true;
            self.changed = crate::result::WAVEFORM_OVERVIEW;
            // Failed overview publication returns before request aging and
            // before actual detail coverage refresh, matching the C wrapper.
            self.publish()?;
        }
        if progress
            .as_ref()
            .is_ok_and(|p| p.consumed_input_frames != 0)
        {
            self.scheduler.note_choice(choice);
        }
        if let Some(detail) = self.session.detail_cache() {
            self.scheduler.refresh_detail(detail);
        }
        if !overview_publication && (self.pending || detail_changed) {
            self.pending = true;
            if detail_changed {
                self.changed |= crate::result::WAVEFORM_DETAIL;
            }
            // Detail-only publication happens after actual request refresh.
            self.publish()?;
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_analysis(available, deadline)?;
            }
            if self.session.analysis_serial() != self.previous_analysis_serial {
                self.pending = true;
                self.changed = crate::result::BPM | crate::result::LOCAL_BEATGRID;
                self.publish()?;
            }
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_global_analysis(available, deadline)?;
            }
            if self.session.global_serial() != self.previous_global_serial {
                self.pending = true;
                self.changed = crate::result::GLOBAL_BEATGRID;
                self.publish()?;
            }
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_meter_analysis(available, deadline)?;
            }
            if self.session.meter_serial() != self.previous_meter_serial {
                self.pending = true;
                self.changed = crate::result::METER_DOWNBEAT;
                self.publish()?;
            }
        }
        if self.session.has_analysis() {
            if let Ok(p) = &mut progress {
                let available = if budget.maximum_steps == 0 {
                    u32::MAX
                } else {
                    budget.maximum_steps.saturating_sub(p.completed_steps)
                };
                p.completed_steps += self.session.process_key_analysis(available, deadline)?;
            }
            if self.session.key_serial() != self.previous_key_serial {
                self.pending = true;
                self.changed = crate::result::MUSICAL_KEY;
                self.publish()?;
            }
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
