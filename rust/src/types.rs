// SPDX-License-Identifier: Apache-2.0
//! Shared native types. These are not C ABI layouts.

/// Explicit failures; no malformed-input path should panic or allocate.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum Error {
    InvalidArgument,
    InvalidState,
    LimitExceeded,
    BufferTooSmall,
    Corrupt,
    Unsupported,
    NotAvailable,
    Cancelled,
    /// A stream callback stalled or violated its byte-count contract.
    Source,
    ResultSlotsExhausted,
    Conflict,
    Internal,
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(match self {
            Self::InvalidArgument => "invalid argument",
            Self::InvalidState => "invalid state",
            Self::LimitExceeded => "resource limit exceeded",
            Self::BufferTooSmall => "output buffer too small",
            Self::Corrupt => "corrupt container",
            Self::Unsupported => "unsupported feature or version",
            Self::NotAvailable => "result not available",
            Self::Cancelled => "processing cancelled",
            Self::Source => "source or stream error",
            Self::ResultSlotsExhausted => "retained results occupy all publication slots",
            Self::Conflict => "input conflicts with previously accepted data",
            Self::Internal => "session has failed",
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum FeatureState {
    Partial = 1,
    Provisional = 2,
    Stable = 3,
    Final = 4,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SourceInfo {
    pub sample_rate: u32,
    pub channel_count: u16,
    pub channel_layout: u16,
    pub total_frames: Option<u64>,
    pub fingerprint_kind: u32,
    pub fingerprint: [u8; 32],
}

/// Quantized wire-compatible values; Rust memory layout is not a wire format.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct WaveformColumn {
    pub minimum: i16,
    pub maximum: i16,
    pub rms: u16,
    pub low: u8,
    pub mid: u8,
    pub high: u8,
    pub flags: u8,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct WaveformSpan {
    pub first_frame: u64,
    pub end_frame: u64,
    pub first_column_index: u32,
    pub column_count: u32,
    pub data_column_offset: u32,
}

/// Read-only view into storage owned by the caller. Copy into separate storage
/// to retain a generation while a session continues processing.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WaveformOverview<'a> {
    pub level_id: u32,
    pub frames_per_column: u32,
    pub origin_frame: u64,
    pub logical_column_count: u32,
    pub state: FeatureState,
    pub spans: &'a [WaveformSpan],
    pub columns: &'a [WaveformColumn],
}

impl core::error::Error for Error {}

/// Borrowed application identity; text and opaque bytes remain distinct on wire.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SourceId<'a> {
    Text(&'a str),
    Bytes(&'a [u8]),
}

/// Recognized META fields. `None` differs from a present empty string.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Metadata<'a> {
    pub producer_name: Option<&'a str>,
    pub producer_version_string: Option<&'a str>,
    pub backend_name: Option<&'a str>,
    pub backend_version: Option<&'a str>,
    pub creation_unix_time: Option<u64>,
    pub application_source_id: Option<SourceId<'a>>,
    pub comments: Option<&'a str>,
}

/// Caller-owned detail data. Columns are copied explicitly when retaining a result.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WaveformTile<'a> {
    pub level_id: u32,
    pub tile_index: u32,
    pub first_frame: u64,
    pub end_frame: u64,
    pub first_column_index: u32,
    pub state: FeatureState,
    pub confidence: u8,
    pub columns: &'a [WaveformColumn],
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FrameRange {
    pub first_frame: u64,
    pub end_frame: u64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FractionalFrame {
    pub whole_frame: u64,
    pub fraction_q32: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FramePeriod {
    pub whole_frames: u64,
    pub fraction_q32: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TempoValue {
    pub state: FeatureState,
    pub confidence: u8,
    pub flags: u32,
    pub tempo_millibpm: u32,
    pub candidate_set_id: u32,
    pub evidence_range: FrameRange,
    pub applicability_range: FrameRange,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TempoCandidate {
    pub tempo_millibpm: u32,
    pub score: u16,
    pub confidence: u8,
    pub relation_to_selected: u8,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TempoView<'a> {
    pub selected: TempoValue,
    pub candidates: &'a [TempoCandidate],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GridSegment {
    pub applicability_range: FrameRange,
    pub anchor_position: FractionalFrame,
    pub anchor_ordinal: i64,
    pub frames_per_beat: FramePeriod,
    pub beat_count: u32,
    pub nominal_tempo_millibpm: u32,
    pub confidence: u8,
    pub state: FeatureState,
    pub flags: u32,
    pub segment_id: u32,
    pub revision: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LocalGrid {
    pub requested_range: FrameRange,
    pub evidence_range: FrameRange,
    pub applicability_range: FrameRange,
    pub coverage: FrameRange,
    pub segment: GridSegment,
    pub state: FeatureState,
    pub confidence: u8,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum GridRepresentation {
    Segments = 1,
    Explicit = 2,
    Hybrid = 3,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Beat {
    pub position: FractionalFrame,
    pub ordinal: i64,
    pub revision: u32,
    pub flags: u32,
    pub confidence: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GlobalGrid<'a> {
    pub state: FeatureState,
    pub confidence: u8,
    pub flags: u32,
    pub representation: GridRepresentation,
    pub requested_range: FrameRange,
    pub evidence_range: FrameRange,
    pub applicability_range: FrameRange,
    pub coverage_range: FrameRange,
    pub segments: &'a [GridSegment],
    pub beats: &'a [Beat],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum RevisionState {
    Pending = 1,
    Applied = 2,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GridRevision {
    pub state: RevisionState,
    pub confidence: u8,
    pub flags: u32,
    pub revision_id: u32,
    pub previous_revision_id: u32,
    pub proposed_representation: GridRepresentation,
    pub proposed_segment_count: u32,
    pub proposed_beat_count: u32,
    pub affected_range: FrameRange,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KeyCandidate {
    pub tonic: u8,
    pub mode: u8,
    pub tuning_offset_cents: i16,
    pub score: u16,
    pub confidence: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key<'a> {
    pub state: FeatureState,
    pub confidence: u8,
    pub tonic: u8,
    pub mode: u8,
    pub tuning_offset_cents: i16,
    pub first_frame: u64,
    pub end_frame: u64,
    pub candidates: &'a [KeyCandidate],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MeterSegment {
    pub first_frame: u64,
    pub end_frame: u64,
    pub downbeat_frame: u64,
    pub downbeat_ordinal: i64,
    pub numerator: u16,
    pub denominator: u16,
    pub state: FeatureState,
    pub confidence: u8,
    pub segment_id: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Meter<'a> {
    pub state: FeatureState,
    pub confidence: u8,
    pub numerator: u16,
    pub denominator: u16,
    pub downbeat_frame: u64,
    pub downbeat_ordinal: i64,
    pub segments: &'a [MeterSegment],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct QualityRecord {
    pub feature: u64,
    pub calibration_model_id: u32,
    pub evidence_coverage_permille: u16,
    pub confidence: u8,
    pub state: FeatureState,
    pub flags: u32,
}

/// Session state retained in a native result, independent of live session access.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum ResultSessionState {
    Created = 0,
    AcceptingInput = 1,
    Draining = 2,
    Completed = 3,
    Cancelled = 4,
    Failed = 5,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeResultInfo {
    pub generation: u64,
    pub container_version: u32,
    pub session_state: ResultSessionState,
    pub lineage_id_high: u64,
    pub lineage_id_low: u64,
}
impl Default for NativeResultInfo {
    fn default() -> Self {
        Self {
            generation: 1,
            container_version: 0,
            session_state: ResultSessionState::Completed,
            lineage_id_high: 0,
            lineage_id_low: 0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum ProvenanceOrigin {
    Unspecified = 0,
    ExternalImport = 1,
    NativeAnalysis = 2,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Provenance<'a> {
    pub origin: ProvenanceOrigin,
    pub source_name: &'a str,
    pub source_version: &'a str,
}

/// Native overview geometry need not supply the wire logical-column count.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeOverview<'a> {
    pub frames_per_column: u32,
    pub origin_frame: u64,
    pub state: FeatureState,
    pub confidence: u8,
    pub spans: &'a [WaveformSpan],
    pub columns: &'a [WaveformColumn],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeTile {
    pub level_id: u32,
    pub tile_index: u32,
    pub first_frame: u64,
    pub end_frame: u64,
    pub first_column_index: u32,
    pub state: FeatureState,
    pub confidence: u8,
    pub data_column_offset: usize,
    pub column_count: usize,
}

/// Typed offsets make copied detail storage independent of its input addresses.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeDetail<'a> {
    pub tiles: &'a [NativeTile],
    pub columns: &'a [WaveformColumn],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeGrid<'a> {
    pub state: FeatureState,
    pub confidence: u8,
    pub flags: u32,
    pub representation: GridRepresentation,
    pub requested_range: FrameRange,
    pub evidence_range: FrameRange,
    pub applicability_range: FrameRange,
    pub coverage_ranges: &'a [FrameRange],
    pub segments: &'a [GridSegment],
    pub beats: &'a [Beat],
}

/// Borrowed external result input. Semantic validation precedes all owned copies.
#[derive(Clone, Copy, Debug)]
pub struct NativeResultInput<'a> {
    pub source: SourceInfo,
    pub info: NativeResultInfo,
    pub provenance: Provenance<'a>,
    pub overview: Option<NativeOverview<'a>>,
    pub detail: Option<NativeDetail<'a>>,
    pub metadata: Option<Metadata<'a>>,
    pub tempo: Option<TempoView<'a>>,
    pub local_grid: Option<NativeGrid<'a>>,
    pub global_grid: Option<NativeGrid<'a>>,
    pub revision: Option<GridRevision>,
    pub key: Option<Key<'a>>,
    pub meter: Option<Meter<'a>>,
    pub quality: &'a [QualityRecord],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeValidation {
    pub available_features: u64,
    pub changed_features: u64,
}

/// Explicit native limits: zero is a zero cap, unlike C initializer defaults.
/// Storage bytes count the native owned graph, not the C ABI allocation layout.
#[derive(Clone, Copy, Debug)]
pub struct NativeLimits {
    pub maximum_overview_spans: usize,
    pub maximum_waveform_columns: usize,
    pub maximum_detail_tiles: usize,
    pub maximum_tempo_candidates: usize,
    pub maximum_grid_coverage_ranges: usize,
    pub maximum_grid_segments: usize,
    pub maximum_grid_beats: usize,
    pub maximum_key_candidates: usize,
    pub maximum_meter_segments: usize,
    pub maximum_quality_records: usize,
    pub maximum_storage_bytes: usize,
}
impl Default for NativeLimits {
    fn default() -> Self {
        Self {
            maximum_overview_spans: 65536,
            maximum_waveform_columns: 16777216,
            maximum_detail_tiles: 65536,
            maximum_tempo_candidates: 32,
            maximum_grid_coverage_ranges: 65536,
            maximum_grid_segments: 65536,
            maximum_grid_beats: 1048576,
            maximum_key_candidates: 24,
            maximum_meter_segments: 65536,
            maximum_quality_records: 11,
            maximum_storage_bytes: 268435456,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Focus {
    pub playhead_frame: u64,
    pub lookbehind_frames: u64,
    pub lookahead_frames: u64,
    pub feature_mask: u64,
    pub priority: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RegionRequest {
    pub range: FrameRange,
    pub feature_mask: u64,
    pub soft_deadline_monotonic_ns: u64,
    pub request_id: u32,
    pub priority: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum RequestState {
    Queued = 0,
    WaitingForPcm = 1,
    Runnable = 2,
    PartiallySatisfied = 3,
    Satisfied = 4,
    Cancelled = 5,
    Failed = 6,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RequestProgress {
    pub request_id: u32,
    pub state: RequestState,
    pub requested_range: FrameRange,
    pub requested_features: u64,
    pub satisfied_features: u64,
    pub progress_permille: u16,
    pub diagnostic_code: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PcmDemand {
    pub range: FrameRange,
    pub feature_mask: u64,
    pub priority: u8,
    pub request_token: u32,
}
