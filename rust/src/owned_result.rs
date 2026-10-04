// SPDX-License-Identifier: Apache-2.0
//! Immutable native generations in caller-owned typed storage. No encoding,
//! allocation, input references, or self-referential pointers are retained.
use crate::*;
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct GridRequirements {
    pub coverage_ranges: usize,
    pub segments: usize,
    pub beats: usize,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Requirements {
    pub overview_spans: usize,
    pub overview_columns: usize,
    pub detail_tiles: usize,
    pub detail_columns: usize,
    pub tempo_candidates: usize,
    pub local_grid: GridRequirements,
    pub global_grid: GridRequirements,
    pub key_candidates: usize,
    pub meter_segments: usize,
    pub quality: usize,
    pub text_bytes: usize,
    /// Actual retained header and copied arrays; excess caller capacity is excluded.
    pub retained_bytes: usize,
}
#[derive(Debug, Default)]
pub struct GridStorage<'a> {
    pub coverage_ranges: &'a mut [FrameRange],
    pub segments: &'a mut [GridSegment],
    pub beats: &'a mut [Beat],
}
#[derive(Debug, Default)]
pub struct Storage<'a> {
    pub overview_spans: &'a mut [WaveformSpan],
    pub overview_columns: &'a mut [WaveformColumn],
    pub detail_tiles: &'a mut [NativeTile],
    pub detail_columns: &'a mut [WaveformColumn],
    pub tempo_candidates: &'a mut [TempoCandidate],
    pub local_grid: GridStorage<'a>,
    pub global_grid: GridStorage<'a>,
    pub key_candidates: &'a mut [KeyCandidate],
    pub meter_segments: &'a mut [MeterSegment],
    pub quality: &'a mut [QualityRecord],
    pub text_bytes: &'a mut [u8],
}
#[derive(Clone, Copy, Debug)]
struct TextRange {
    offset: usize,
    len: usize,
}
#[derive(Clone, Copy, Debug)]
struct OverviewHeader {
    frames_per_column: u32,
    origin_frame: u64,
    state: FeatureState,
    confidence: u8,
}
#[derive(Clone, Copy, Debug)]
struct GridHeader {
    state: FeatureState,
    confidence: u8,
    flags: u32,
    representation: GridRepresentation,
    requested_range: FrameRange,
    evidence_range: FrameRange,
    applicability_range: FrameRange,
}
#[derive(Clone, Copy, Debug)]
struct KeyHeader {
    state: FeatureState,
    confidence: u8,
    tonic: u8,
    mode: u8,
    tuning_offset_cents: i16,
    first_frame: u64,
    end_frame: u64,
}
#[derive(Clone, Copy, Debug)]
struct MeterHeader {
    state: FeatureState,
    confidence: u8,
    numerator: u16,
    denominator: u16,
    downbeat_frame: u64,
    downbeat_ordinal: i64,
}
#[derive(Debug)]
pub struct OwnedResult<'a> {
    storage: Storage<'a>,
    requirements: Requirements,
    validation: NativeValidation,
    source: SourceInfo,
    info: NativeResultInfo,
    provenance_origin: ProvenanceOrigin,
    text: [Option<TextRange>; 8],
    metadata_present: bool,
    creation_unix_time: Option<u64>,
    source_id_is_bytes: bool,
    overview: Option<OverviewHeader>,
    detail_present: bool,
    tempo: Option<TempoValue>,
    local_grid: Option<GridHeader>,
    global_grid: Option<GridHeader>,
    revision: Option<GridRevision>,
    key: Option<KeyHeader>,
    meter: Option<MeterHeader>,
}
fn text_fields<'a>(input: &NativeResultInput<'a>) -> [Option<&'a [u8]>; 8] {
    let m = input.metadata.unwrap_or_default();
    [
        m.producer_name.map(str::as_bytes),
        m.producer_version_string.map(str::as_bytes),
        m.backend_name.map(str::as_bytes),
        m.backend_version.map(str::as_bytes),
        m.application_source_id.map(|s| match s {
            SourceId::Text(s) => s.as_bytes(),
            SourceId::Bytes(b) => b,
        }),
        m.comments.map(str::as_bytes),
        Some(input.provenance.source_name.as_bytes()),
        Some(input.provenance.source_version.as_bytes()),
    ]
}
fn grid_requirements(g: Option<NativeGrid<'_>>) -> GridRequirements {
    g.map_or(GridRequirements::default(), |g| GridRequirements {
        coverage_ranges: g.coverage_ranges.len(),
        segments: g.segments.len(),
        beats: g.beats.len(),
    })
}
fn add<T>(bytes: &mut usize, count: usize) -> Result<(), Error> {
    *bytes = bytes
        .checked_add(
            count
                .checked_mul(core::mem::size_of::<T>())
                .ok_or(Error::LimitExceeded)?,
        )
        .ok_or(Error::LimitExceeded)?;
    Ok(())
}
pub(crate) fn counts(
    input: &NativeResultInput<'_>,
    limits: NativeLimits,
) -> Result<Requirements, Error> {
    let mut r = Requirements {
        overview_spans: input.overview.map_or(0, |v| v.spans.len()),
        overview_columns: 0,
        detail_tiles: input.detail.map_or(0, |v| v.tiles.len()),
        detail_columns: 0,
        tempo_candidates: input.tempo.map_or(0, |v| v.candidates.len()),
        local_grid: grid_requirements(input.local_grid),
        global_grid: grid_requirements(input.global_grid),
        key_candidates: input.key.map_or(0, |v| v.candidates.len()),
        meter_segments: input.meter.map_or(0, |v| v.segments.len()),
        quality: input.quality.len(),
        ..Requirements::default()
    };
    if let Some(o) = input.overview {
        for span in o.spans {
            r.overview_columns = r
                .overview_columns
                .checked_add(span.column_count as usize)
                .ok_or(Error::LimitExceeded)?;
        }
    }
    if r.overview_columns > u32::MAX as usize {
        return Err(Error::LimitExceeded);
    }
    if let Some(d) = input.detail {
        for tile in d.tiles {
            r.detail_columns = r
                .detail_columns
                .checked_add(tile.column_count)
                .ok_or(Error::LimitExceeded)?;
        }
    }
    for field in text_fields(input).iter().flatten() {
        r.text_bytes = r
            .text_bytes
            .checked_add(field.len())
            .ok_or(Error::LimitExceeded)?;
    }
    r.retained_bytes = retained_size(&r)?;
    if r.retained_bytes > limits.maximum_storage_bytes {
        return Err(Error::LimitExceeded);
    }
    Ok(r)
}
/// Checked native graph accounting for typed storage planning. This does not
/// validate payload semantics or promise C ABI/workspace layout sizes.
pub fn retained_size(r: &Requirements) -> Result<usize, Error> {
    let mut bytes = core::mem::size_of::<OwnedResult<'_>>();
    let b = &mut bytes;
    add::<WaveformSpan>(b, r.overview_spans)?;
    add::<WaveformColumn>(b, r.overview_columns)?;
    add::<NativeTile>(b, r.detail_tiles)?;
    add::<WaveformColumn>(b, r.detail_columns)?;
    add::<TempoCandidate>(b, r.tempo_candidates)?;
    for g in [r.local_grid, r.global_grid] {
        add::<FrameRange>(b, g.coverage_ranges)?;
        add::<GridSegment>(b, g.segments)?;
        add::<Beat>(b, g.beats)?;
    }
    add::<KeyCandidate>(b, r.key_candidates)?;
    add::<MeterSegment>(b, r.meter_segments)?;
    add::<QualityRecord>(b, r.quality)?;
    add::<u8>(b, r.text_bytes)?;
    Ok(bytes)
}

/// Validate semantics, counts and the exact size of the retained native graph.
pub fn requirements(
    input: &NativeResultInput<'_>,
    limits: NativeLimits,
) -> Result<Requirements, Error> {
    crate::native_validation::validate(input, limits)?;
    counts(input, limits)
}
fn grid_fits(s: &GridStorage<'_>, r: GridRequirements) -> bool {
    s.coverage_ranges.len() >= r.coverage_ranges
        && s.segments.len() >= r.segments
        && s.beats.len() >= r.beats
}
fn copy_grid(input: Option<NativeGrid<'_>>, storage: &mut GridStorage<'_>) -> Option<GridHeader> {
    input.map(|g| {
        storage.coverage_ranges[..g.coverage_ranges.len()].copy_from_slice(g.coverage_ranges);
        storage.segments[..g.segments.len()].copy_from_slice(g.segments);
        storage.beats[..g.beats.len()].copy_from_slice(g.beats);
        GridHeader {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: g.representation,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
        }
    })
}
/// All validation and capacity checks precede writes. Success retains only
/// caller storage; input may immediately be destroyed or reused. Referenced
/// waveform columns are packed in span/tile order, duplicating aliases and
/// omitting unused backing columns, as the C result builder does.
pub fn copy<'a>(
    input: &NativeResultInput<'_>,
    storage: Storage<'a>,
    limits: NativeLimits,
) -> Result<OwnedResult<'a>, Error> {
    let validation = crate::native_validation::validate(input, limits)?;
    let r = counts(input, limits)?;
    capacity(&storage, r)?;
    Ok(copy_validated(input, storage, r, validation))
}
pub(crate) fn copy_session<'a>(
    input: &NativeResultInput<'_>,
    storage: Storage<'a>,
    limits: NativeLimits,
    changed_features: u64,
) -> Result<OwnedResult<'a>, Error> {
    let validation = crate::native_validation::validate_session(input, limits, changed_features)?;
    let r = counts(input, limits)?;
    capacity(&storage, r)?;
    Ok(copy_validated(input, storage, r, validation))
}
fn capacity(storage: &Storage<'_>, r: Requirements) -> Result<(), Error> {
    if storage.overview_spans.len() < r.overview_spans
        || storage.overview_columns.len() < r.overview_columns
        || storage.detail_tiles.len() < r.detail_tiles
        || storage.detail_columns.len() < r.detail_columns
        || storage.tempo_candidates.len() < r.tempo_candidates
        || !grid_fits(&storage.local_grid, r.local_grid)
        || !grid_fits(&storage.global_grid, r.global_grid)
        || storage.key_candidates.len() < r.key_candidates
        || storage.meter_segments.len() < r.meter_segments
        || storage.quality.len() < r.quality
        || storage.text_bytes.len() < r.text_bytes
    {
        return Err(Error::BufferTooSmall);
    }
    Ok(())
}
// Every index/count used here was checked before any storage mutation.
fn copy_validated<'a>(
    input: &NativeResultInput<'_>,
    mut storage: Storage<'a>,
    r: Requirements,
    validation: NativeValidation,
) -> OwnedResult<'a> {
    let overview = input.overview.map(|o| {
        let mut offset = 0;
        for (dst, src) in storage.overview_spans[..r.overview_spans]
            .iter_mut()
            .zip(o.spans)
        {
            *dst = *src;
            dst.data_column_offset = offset as u32;
            let start = src.data_column_offset as usize;
            let len = src.column_count as usize;
            storage.overview_columns[offset..offset + len]
                .copy_from_slice(&o.columns[start..start + len]);
            offset += len;
        }
        OverviewHeader {
            frames_per_column: o.frames_per_column,
            origin_frame: o.origin_frame,
            state: o.state,
            confidence: o.confidence,
        }
    });
    if let Some(d) = input.detail {
        let mut offset = 0;
        for (dst, src) in storage.detail_tiles[..r.detail_tiles]
            .iter_mut()
            .zip(d.tiles)
        {
            *dst = *src;
            dst.data_column_offset = offset;
            let start = src.data_column_offset;
            let len = src.column_count;
            storage.detail_columns[offset..offset + len]
                .copy_from_slice(&d.columns[start..start + len]);
            offset += len;
        }
    }
    let tempo = input.tempo.map(|t| {
        storage.tempo_candidates[..r.tempo_candidates].copy_from_slice(t.candidates);
        t.selected
    });
    let local_grid = copy_grid(input.local_grid, &mut storage.local_grid);
    let global_grid = copy_grid(input.global_grid, &mut storage.global_grid);
    let key = input.key.map(|k| {
        storage.key_candidates[..r.key_candidates].copy_from_slice(k.candidates);
        KeyHeader {
            state: k.state,
            confidence: k.confidence,
            tonic: k.tonic,
            mode: k.mode,
            tuning_offset_cents: k.tuning_offset_cents,
            first_frame: k.first_frame,
            end_frame: k.end_frame,
        }
    });
    let meter = input.meter.map(|m| {
        storage.meter_segments[..r.meter_segments].copy_from_slice(m.segments);
        MeterHeader {
            state: m.state,
            confidence: m.confidence,
            numerator: m.numerator,
            denominator: m.denominator,
            downbeat_frame: m.downbeat_frame,
            downbeat_ordinal: m.downbeat_ordinal,
        }
    });
    storage.quality[..r.quality].copy_from_slice(input.quality);
    let mut text = [None; 8];
    let mut end = 0;
    for (i, field) in text_fields(input).into_iter().enumerate() {
        if let Some(bytes) = field {
            storage.text_bytes[end..end + bytes.len()].copy_from_slice(bytes);
            text[i] = Some(TextRange {
                offset: end,
                len: bytes.len(),
            });
            end += bytes.len();
        }
    }
    OwnedResult {
        storage,
        requirements: r,
        validation,
        source: input.source,
        info: input.info,
        provenance_origin: input.provenance.origin,
        text,
        metadata_present: input.metadata.is_some(),
        creation_unix_time: input.metadata.and_then(|m| m.creation_unix_time),
        source_id_is_bytes: matches!(
            input.metadata.and_then(|m| m.application_source_id),
            Some(SourceId::Bytes(_))
        ),
        overview,
        detail_present: input.detail.is_some(),
        tempo,
        local_grid,
        global_grid,
        revision: input.revision,
        key,
        meter,
    }
}
fn grid_view<'a>(h: GridHeader, s: &'a GridStorage<'_>, r: GridRequirements) -> NativeGrid<'a> {
    NativeGrid {
        state: h.state,
        confidence: h.confidence,
        flags: h.flags,
        representation: h.representation,
        requested_range: h.requested_range,
        evidence_range: h.evidence_range,
        applicability_range: h.applicability_range,
        coverage_ranges: &s.coverage_ranges[..r.coverage_ranges],
        segments: &s.segments[..r.segments],
        beats: &s.beats[..r.beats],
    }
}
impl<'a> OwnedResult<'a> {
    /// Replace this generation only after semantic, capacity and byte-limit
    /// validation succeeds. Any returned error preserves all current bytes and
    /// views. Live borrowed views prevent mutable replacement at compile time.
    ///
    /// ```compile_fail
    /// use libapta::{owned_result::OwnedResult, NativeResultInput, NativeLimits};
    /// fn cannot_replace_live_view(owner: &mut OwnedResult<'_>, input: &NativeResultInput<'_>) {
    ///     let retained = owner.view();
    ///     owner.replace(input, NativeLimits::default()).unwrap();
    ///     assert!(retained.tempo.is_some());
    /// }
    /// ```
    pub fn replace(
        &mut self,
        input: &NativeResultInput<'_>,
        limits: NativeLimits,
    ) -> Result<(), Error> {
        let validation = crate::native_validation::validate(input, limits)?;
        let r = counts(input, limits)?;
        capacity(&self.storage, r)?;
        let storage = core::mem::take(&mut self.storage);
        *self = copy_validated(input, storage, r, validation);
        Ok(())
    }
    pub(crate) fn replace_session(
        &mut self,
        input: &NativeResultInput<'_>,
        limits: NativeLimits,
        changed_features: u64,
    ) -> Result<(), Error> {
        let validation =
            crate::native_validation::validate_session(input, limits, changed_features)?;
        let r = counts(input, limits)?;
        capacity(&self.storage, r)?;
        let storage = core::mem::take(&mut self.storage);
        *self = copy_validated(input, storage, r, validation);
        Ok(())
    }

    // Only publication's explicit C capability path may override derived bits.
    // Payload validation and external builder rules remain content based.
    pub(crate) fn apply_session_capabilities(&mut self, requested: u64) {
        use crate::result::*;
        let mut available = self.validation.available_features
            & !(CONFIDENCE | GRID_LOCKING | DYNAMIC_TEMPO | WAVEFORM_3BAND);
        if available & (WAVEFORM_OVERVIEW | BPM | LOCAL_BEATGRID | GLOBAL_BEATGRID) != 0 {
            available |= requested & CONFIDENCE;
        }
        if available & LOCAL_BEATGRID != 0 {
            available |= requested & GRID_LOCKING;
        }
        if available & GLOBAL_BEATGRID != 0 {
            available |= requested & DYNAMIC_TEMPO;
        }
        self.validation.available_features = available;
    }

    /// Deep-copy this already validated immutable graph into independent caller
    /// storage. Session exceptions and capability masks remain attached to the
    /// copied generation; arbitrary external inputs still require validation.
    /// A short destination fails before changing any destination element.
    pub fn copy_to<'b>(&self, storage: Storage<'b>) -> Result<OwnedResult<'b>, Error> {
        capacity(&storage, self.requirements)?;
        Ok(copy_validated(
            &self.view(),
            storage,
            self.requirements,
            self.validation,
        ))
    }

    pub fn requirements(&self) -> Requirements {
        self.requirements
    }
    pub fn source(&self) -> SourceInfo {
        self.source
    }
    pub fn info(&self) -> NativeResultInfo {
        self.info
    }
    pub fn available_features(&self) -> u64 {
        self.validation.available_features
    }
    pub fn changed_features(&self) -> u64 {
        self.validation.changed_features
    }
    fn bytes(&self, index: usize) -> Option<&[u8]> {
        self.text[index].map(|r| &self.storage.text_bytes[r.offset..r.offset + r.len])
    }
    fn text(&self, index: usize) -> Option<&str> {
        self.bytes(index)
            .map(|b| core::str::from_utf8(b).expect("copied validated UTF-8"))
    }
    pub fn provenance(&self) -> Provenance<'_> {
        Provenance {
            origin: self.provenance_origin,
            source_name: self.text(6).unwrap(),
            source_version: self.text(7).unwrap(),
        }
    }
    pub fn metadata(&self) -> Option<Metadata<'_>> {
        self.metadata_present.then(|| Metadata {
            producer_name: self.text(0),
            producer_version_string: self.text(1),
            backend_name: self.text(2),
            backend_version: self.text(3),
            creation_unix_time: self.creation_unix_time,
            application_source_id: if self.source_id_is_bytes {
                self.bytes(4).map(SourceId::Bytes)
            } else {
                self.text(4).map(SourceId::Text)
            },
            comments: self.text(5),
        })
    }
    pub fn overview(&self) -> Option<NativeOverview<'_>> {
        self.overview.map(|h| NativeOverview {
            frames_per_column: h.frames_per_column,
            origin_frame: h.origin_frame,
            state: h.state,
            confidence: h.confidence,
            spans: &self.storage.overview_spans[..self.requirements.overview_spans],
            columns: &self.storage.overview_columns[..self.requirements.overview_columns],
        })
    }
    pub fn detail(&self) -> Option<NativeDetail<'_>> {
        self.detail_present.then(|| NativeDetail {
            tiles: &self.storage.detail_tiles[..self.requirements.detail_tiles],
            columns: &self.storage.detail_columns[..self.requirements.detail_columns],
        })
    }
    pub fn tempo(&self) -> Option<TempoView<'_>> {
        self.tempo.map(|selected| TempoView {
            selected,
            candidates: &self.storage.tempo_candidates[..self.requirements.tempo_candidates],
        })
    }
    pub fn local_grid(&self) -> Option<NativeGrid<'_>> {
        self.local_grid
            .map(|h| grid_view(h, &self.storage.local_grid, self.requirements.local_grid))
    }
    pub fn global_grid(&self) -> Option<NativeGrid<'_>> {
        self.global_grid
            .map(|h| grid_view(h, &self.storage.global_grid, self.requirements.global_grid))
    }
    pub fn revision(&self) -> Option<GridRevision> {
        self.revision
    }
    pub fn key(&self) -> Option<Key<'_>> {
        self.key.map(|h| Key {
            state: h.state,
            confidence: h.confidence,
            tonic: h.tonic,
            mode: h.mode,
            tuning_offset_cents: h.tuning_offset_cents,
            first_frame: h.first_frame,
            end_frame: h.end_frame,
            candidates: &self.storage.key_candidates[..self.requirements.key_candidates],
        })
    }
    pub fn meter(&self) -> Option<Meter<'_>> {
        self.meter.map(|h| Meter {
            state: h.state,
            confidence: h.confidence,
            numerator: h.numerator,
            denominator: h.denominator,
            downbeat_frame: h.downbeat_frame,
            downbeat_ordinal: h.downbeat_ordinal,
            segments: &self.storage.meter_segments[..self.requirements.meter_segments],
        })
    }
    pub fn quality(&self) -> &[QualityRecord] {
        &self.storage.quality[..self.requirements.quality]
    }
    pub fn view(&self) -> NativeResultInput<'_> {
        NativeResultInput {
            source: self.source,
            info: self.info,
            provenance: self.provenance(),
            overview: self.overview(),
            detail: self.detail(),
            metadata: self.metadata(),
            tempo: self.tempo(),
            local_grid: self.local_grid(),
            global_grid: self.global_grid(),
            revision: self.revision,
            key: self.key(),
            meter: self.meter(),
            quality: self.quality(),
        }
    }
    /// Consume the immutable generation to recover every caller buffer, including
    /// its unused capacity. Outstanding views prevent this through Rust borrowing.
    pub fn into_storage(self) -> Storage<'a> {
        self.storage
    }
}

#[cfg(test)]
mod session_tests {
    use super::*;
    fn empty() -> NativeResultInput<'static> {
        NativeResultInput {
            source: SourceInfo {
                sample_rate: 48000,
                channel_count: 1,
                channel_layout: 1,
                total_frames: None,
                fingerprint_kind: 0,
                fingerprint: [0; 32],
            },
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
    #[test]
    fn trusted_empty_native_sessions_and_removed_feature_masks() {
        let mut input = empty();
        let mut owned =
            copy_session(&input, Storage::default(), NativeLimits::default(), 0).unwrap();
        assert_eq!(owned.available_features(), 0);
        assert_eq!(owned.info().generation, 1);
        assert_eq!(owned.provenance().origin, ProvenanceOrigin::Unspecified);
        input.info.generation = 2;
        input.info.session_state = ResultSessionState::Completed;
        owned
            .replace_session(&input, NativeLimits::default(), crate::result::BPM)
            .unwrap();
        assert_eq!(owned.available_features(), 0);
        assert_eq!(owned.changed_features(), crate::result::BPM);
        assert_eq!(owned.info().generation, 2);
        assert_eq!(
            owned.replace_session(&input, NativeLimits::default(), 1 << 63),
            Err(Error::InvalidArgument)
        );
        assert_eq!(owned.changed_features(), crate::result::BPM);
        input.provenance.origin = ProvenanceOrigin::ExternalImport;
        assert!(owned
            .replace_session(&input, NativeLimits::default(), 0)
            .is_err());
        assert_eq!(owned.info().generation, 2);
        assert!(copy(&empty(), Storage::default(), NativeLimits::default()).is_err());
    }
    #[test]
    fn trusted_capacity_failure_preserves_empty_generation() {
        let input = empty();
        let mut owned =
            copy_session(&input, Storage::default(), NativeLimits::default(), 0).unwrap();
        let candidate = KeyCandidate {
            tonic: 0,
            mode: 1,
            tuning_offset_cents: 0,
            score: 10,
            confidence: 255,
        };
        let mut input = empty();
        input.info.generation = 2;
        input.info.session_state = ResultSessionState::AcceptingInput;
        input.key = Some(Key {
            state: FeatureState::Provisional,
            confidence: 255,
            tonic: 0,
            mode: 1,
            tuning_offset_cents: 0,
            first_frame: 0,
            end_frame: 64,
            candidates: core::slice::from_ref(&candidate),
        });
        assert_eq!(
            owned.replace_session(&input, NativeLimits::default(), crate::result::MUSICAL_KEY),
            Err(Error::BufferTooSmall)
        );
        assert_eq!(owned.info().generation, 1);
        assert_eq!(owned.available_features(), 0);
        assert!(owned.key().is_none());
    }
}
