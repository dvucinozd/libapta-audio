// SPDX-License-Identifier: Apache-2.0
//! Complete borrowed container results. Parsing performs payload and cross-feature
//! checks before returning a view; storage stays immutable and caller-owned.
use crate::{
    container::{self, Container, ParseOptions, WaveformResultView},
    dj, grid, tempo, *,
};

pub const WAVEFORM_OVERVIEW: u64 = 1;
pub const WAVEFORM_DETAIL: u64 = 1 << 1;
pub const WAVEFORM_3BAND: u64 = 1 << 2;
pub const BPM: u64 = 1 << 3;
pub const LOCAL_BEATGRID: u64 = 1 << 4;
pub const GLOBAL_BEATGRID: u64 = 1 << 5;
pub const DYNAMIC_TEMPO: u64 = 1 << 6;
pub const CONFIDENCE: u64 = 1 << 7;
pub const GRID_LOCKING: u64 = 1 << 8;
pub const MUSICAL_KEY: u64 = 1 << 9;
pub const METER_DOWNBEAT: u64 = 1 << 10;
pub const CALIBRATED_QUALITY: u64 = 1 << 11;
pub const ALL_FEATURES: u64 = (1 << 12) - 1;

#[derive(Clone, Copy, Debug)]
pub struct Limits {
    pub container: ParseOptions,
    pub maximum_grid_segments: usize,
    pub maximum_beats: usize,
    pub maximum_key_candidates: usize,
    pub maximum_meter_segments: usize,
    pub maximum_quality_records: usize,
}
impl Default for Limits {
    fn default() -> Self {
        Self {
            container: ParseOptions::default(),
            maximum_grid_segments: 8,
            maximum_beats: 3072,
            maximum_key_candidates: 24,
            maximum_meter_segments: 65536,
            maximum_quality_records: 11,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct ResultView<'a> {
    pub source: SourceInfo,
    pub waveform: WaveformResultView<'a>,
    pub tempo: Option<tempo::TempoPayload<'a>>,
    pub local_grid: Option<LocalGrid>,
    pub global_grid: Option<grid::GridPayload<'a>>,
    pub revision: Option<GridRevision>,
    pub key: Option<dj::KeyView<'a>>,
    pub meter: Option<dj::MeterView<'a>>,
    pub quality: Option<dj::QualityView<'a>>,
    pub available_features: u64,
}
fn payload<'a>(c: &Container<'a>, id: &[u8; 4]) -> Option<&'a [u8]> {
    (0..c.section_count())
        .filter_map(|i| c.section(i))
        .find(|s| &s.fourcc == id)
        .map(|s| s.payload)
}

/// Selectively retained result. Dependencies used during validation are hidden
/// from accessors when they were not requested.
#[derive(Clone, Copy, Debug)]
pub struct SelectedResultView<'a> {
    pub source: SourceInfo,
    pub waveform: container::WaveformFields<'a>,
    pub tempo: Option<tempo::TempoPayload<'a>>,
    pub local_grid: Option<LocalGrid>,
    pub global_grid: Option<grid::GridPayload<'a>>,
    pub revision: Option<GridRevision>,
    pub key: Option<dj::KeyView<'a>>,
    pub meter: Option<dj::MeterView<'a>>,
    pub quality: Option<SelectedQuality<'a>>,
    pub available_features: u64,
}
/// Quality records filtered to features actually requested and materialized.
#[derive(Clone, Copy, Debug)]
pub struct SelectedQuality<'a> {
    view: dj::QualityView<'a>,
    mask: u64,
}
impl SelectedQuality<'_> {
    pub fn record_count(&self) -> usize {
        (0..self.view.record_count())
            .filter(|i| self.view.record(*i).unwrap().feature & self.mask != 0)
            .count()
    }
    pub fn record(&self, index: usize) -> Option<QualityRecord> {
        (0..self.view.record_count())
            .filter_map(|i| self.view.record(i))
            .filter(|q| q.feature & self.mask != 0)
            .nth(index)
    }
    pub fn copy_into<'b>(
        &self,
        out: &'b mut [QualityRecord],
    ) -> Result<&'b [QualityRecord], Error> {
        let count = self.record_count();
        if out.len() < count {
            return Err(Error::BufferTooSmall);
        }
        for (i, value) in out[..count].iter_mut().enumerate() {
            *value = self.record(i).unwrap();
        }
        Ok(&out[..count])
    }
}

/// Reads framing and every CRC through bounded scratch, retaining only requested
/// sections and cross-feature dependencies in caller storage. No source borrow
/// survives the call; the returned view borrows descriptors and the payload arena.
pub fn read_from_stream<'a>(
    input: &mut impl crate::stream::Input,
    options: crate::stream::StreamOptions,
    scratch: &mut [u8],
    descriptors: &'a mut [crate::stream::StoredSection],
    arena: &'a mut [u8],
) -> Result<SelectedResultView<'a>, Error> {
    let sections = crate::stream::read_transport(input, options, scratch, descriptors, arena)?;
    let requested = sections.requested_features;
    let c = Container::from_stream(sections);
    let mut r = parse_sections(c, options.limits, Some(requested))?;
    crate::builder::validate_selected(&r, options.limits)?;
    // Validation dependencies do not grant access to unrequested features.
    r.available_features &= requested;
    if requested & BPM == 0 {
        r.tempo = None;
    }
    if requested & LOCAL_BEATGRID == 0 {
        r.local_grid = None;
    }
    if requested & GLOBAL_BEATGRID == 0 {
        r.global_grid = None;
        r.revision = None;
    }
    Ok(r)
}

pub fn parse(bytes: &[u8], limits: Limits) -> Result<ResultView<'_>, Error> {
    let c = Container::parse(bytes, limits.container)?;
    let r = parse_sections(c, limits, None)?;
    Ok(ResultView {
        source: r.source,
        waveform: r.waveform.require_overview()?,
        tempo: r.tempo,
        local_grid: r.local_grid,
        global_grid: r.global_grid,
        revision: r.revision,
        key: r.key,
        meter: r.meter,
        quality: r.quality.map(|q| q.view),
        available_features: r.available_features,
    })
}
pub(crate) fn parse_sections(
    c: Container<'_>,
    limits: Limits,
    selection: Option<u64>,
) -> Result<SelectedResultView<'_>, Error> {
    let partial = c.flags & 1 != 0;
    let strict = limits.container.strict;
    let waveform = c.parse_selected_waveform(limits.container, true)?;
    let mut features = if waveform.overview.is_some() {
        WAVEFORM_OVERVIEW
    } else {
        0
    };
    if waveform.tile_count() != 0 {
        features |= WAVEFORM_DETAIL;
    }
    if let Some(overview) = waveform.overview {
        for i in 0..overview.span_count() {
            for j in 0..overview.span(i).unwrap().column_count as usize {
                if overview.column(i, j).unwrap().flags & 8 != 0 {
                    features |= WAVEFORM_3BAND;
                }
            }
        }
    }
    let tempo = payload(&c, b"TEMP")
        .map(|b| tempo::TempoPayload::parse(b, partial, strict))
        .transpose()?;
    if tempo.is_some() {
        features |= BPM;
        if selection.map_or(true, |s| {
            s & (BPM | LOCAL_BEATGRID | GLOBAL_BEATGRID | DYNAMIC_TEMPO | GRID_LOCKING) != 0
        }) {
            features |= CONFIDENCE;
        }
    }
    let local_grid = payload(&c, b"LGRD")
        .map(|b| {
            tempo::parse_local_grid(
                b,
                partial,
                strict,
                tempo
                    .as_ref()
                    .ok_or(Error::Corrupt)?
                    .selected()
                    .tempo_millibpm,
            )
        })
        .transpose()?;
    if let Some(g) = local_grid {
        features |= LOCAL_BEATGRID;
        if g.flags & 256 != 0 {
            features |= GRID_LOCKING;
        }
    }
    let global_grid = payload(&c, b"GGRD")
        .map(|b| {
            grid::GridPayload::parse(
                b,
                partial,
                grid::GridOptions {
                    strict,
                    maximum_segments: limits.maximum_grid_segments,
                    maximum_beats: limits.maximum_beats,
                },
            )
        })
        .transpose()?;
    let revision = payload(&c, b"REVN")
        .map(|b| {
            grid::parse_revision(
                b,
                global_grid.as_ref().ok_or(Error::Corrupt)?,
                partial,
                strict,
            )
        })
        .transpose()?;
    if let Some(g) = global_grid {
        features |= GLOBAL_BEATGRID;
        if g.flags() & 2 != 0 {
            features |= DYNAMIC_TEMPO;
        }
        if g.confidence() != 255
            && selection.map_or(true, |s| {
                s & (GLOBAL_BEATGRID | DYNAMIC_TEMPO | GRID_LOCKING) != 0
            })
        {
            features |= CONFIDENCE;
        }
    }
    let key = payload(&c, b"MKEY")
        .map(|b| {
            dj::parse_key(
                b,
                c.source.total_frames,
                partial,
                limits.maximum_key_candidates,
            )
        })
        .transpose()?;
    if let Some(k) = key {
        features |= MUSICAL_KEY;
        if selection.is_none() && k.confidence() != 255 {
            features |= CONFIDENCE;
        }
    }
    let meter = payload(&c, b"MTRD")
        .map(|b| {
            dj::parse_meter(
                b,
                c.source.total_frames,
                partial,
                limits.maximum_meter_segments,
            )
        })
        .transpose()?;
    if let Some(m) = meter {
        let mut beat_index = 0usize;
        let mut segment_index = 0usize;
        m.validate_grid(|frame, ordinal| {
            (local_grid.is_none() && global_grid.is_none())
                || local_grid.is_some_and(|g| segment_matches(&g.segment, frame, ordinal))
                || global_grid.is_some_and(|g| {
                    while let Some(b) = g.beat(beat_index) {
                        if b.position.whole_frame < frame
                            || (b.position.whole_frame == frame && b.ordinal < ordinal)
                        {
                            beat_index += 1;
                            continue;
                        }
                        if b.position.whole_frame == frame && b.ordinal == ordinal {
                            return true;
                        }
                        break;
                    }
                    while let Some(s) = g.segment(segment_index) {
                        if s.applicability_range.end_frame <= frame {
                            segment_index += 1;
                            continue;
                        }
                        return segment_matches(&s, frame, ordinal);
                    }
                    false
                })
        })?;
        features |= METER_DOWNBEAT;
        if selection.is_none() && m.confidence() != 255 {
            features |= CONFIDENCE;
        }
    }
    let quality = payload(&c, b"CONF")
        .map(|b| {
            dj::parse_quality(
                b,
                partial,
                if selection.is_some() {
                    ALL_FEATURES
                } else {
                    features
                },
                limits.maximum_quality_records,
            )
        })
        .transpose()?;
    let quality = quality
        .map(|view| SelectedQuality {
            view,
            mask: selection.map_or(ALL_FEATURES, |s| s & features),
        })
        .filter(|q| q.record_count() != 0);
    if quality.is_some() {
        features |= CALIBRATED_QUALITY;
    }
    Ok(SelectedResultView {
        source: c.source,
        waveform,
        tempo,
        local_grid,
        global_grid,
        revision,
        key,
        meter,
        quality,
        available_features: features,
    })
}
fn segment_matches(s: &GridSegment, frame: u64, ordinal: i64) -> bool {
    frame >= s.applicability_range.first_frame
        && frame < s.applicability_range.end_frame
        && grid::segment_position_at_ordinal(s, ordinal).is_some_and(|p| p.whole_frame == frame)
}

/// Native values borrowed for wire serialization, preserving the wire reader's
/// acceptance rules. External builder validation is a separate operation.
/// A successful write
/// produces independent, validated bytes in caller storage.
#[derive(Clone, Copy, Debug)]
pub struct ResultInput<'a> {
    pub source: SourceInfo,
    pub overview: WaveformOverview<'a>,
    pub tiles: &'a [WaveformTile<'a>],
    pub metadata: Option<Metadata<'a>>,
    pub tempo: Option<TempoView<'a>>,
    pub local_grid: Option<LocalGrid>,
    pub global_grid: Option<GlobalGrid<'a>>,
    pub revision: Option<GridRevision>,
    pub key: Option<Key<'a>>,
    pub meter: Option<Meter<'a>>,
    pub quality: &'a [QualityRecord],
}
fn align(n: usize) -> Result<usize, Error> {
    n.checked_add(7).map(|n| n & !7).ok_or(Error::LimitExceeded)
}
fn sizes(r: &ResultInput<'_>) -> Result<[usize; 7], Error> {
    if (r.local_grid.is_some() || r.global_grid.is_some()) && r.tempo.is_none()
        || r.global_grid.is_some() != r.revision.is_some()
    {
        return Err(Error::InvalidArgument);
    }
    Ok([
        r.tempo
            .as_ref()
            .map(tempo::tempo_payload_size)
            .transpose()?
            .unwrap_or(0),
        r.local_grid
            .as_ref()
            .map(|g| tempo::local_grid_payload_size(g, r.tempo.unwrap().selected.tempo_millibpm))
            .transpose()?
            .unwrap_or(0),
        r.global_grid
            .as_ref()
            .map(|g| grid::validate(g, true))
            .transpose()?
            .unwrap_or(0),
        if r.revision.is_some() { 80 } else { 0 },
        if let Some(k) = r.key {
            dj::validate_key(k, r.source.total_frames, true)?;
            40 + 16 * k.candidates.len()
        } else {
            0
        },
        if let Some(m) = r.meter {
            dj::validate_meter(m, r.source.total_frames, true)?;
            48 + 56 * m.segments.len()
        } else {
            0
        },
        if r.quality.is_empty() {
            0
        } else {
            dj::validate_quality(r.quality, true, ALL_FEATURES)?;
            16 + 32 * r.quality.len()
        },
    ])
}
pub fn serialized_size(r: &ResultInput<'_>) -> Result<usize, Error> {
    let lengths = sizes(r)?;
    let mut size =
        container::waveform_result_size(&r.source, &r.overview, r.tiles, r.metadata.as_ref())?
            .checked_add(lengths.iter().filter(|n| **n != 0).count() * 40)
            .ok_or(Error::LimitExceeded)?;
    for len in lengths {
        if len != 0 {
            size = align(size)?.checked_add(len).ok_or(Error::LimitExceeded)?;
        }
    }
    Ok(size)
}
fn put32(b: &mut [u8], p: usize, v: u32) {
    b[p..p + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(b: &mut [u8], p: usize, v: u64) {
    b[p..p + 8].copy_from_slice(&v.to_le_bytes());
}
fn get32(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
fn get64(b: &[u8], p: usize) -> u64 {
    u64::from_le_bytes(b[p..p + 8].try_into().unwrap())
}

/// Output is valid only on success. Limits apply to the whole result, including
/// dependencies and aggregate waveform storage; no allocation occurs.
pub fn write(r: &ResultInput<'_>, output: &mut [u8], limits: Limits) -> Result<usize, Error> {
    let lengths = sizes(r)?;
    let size = serialized_size(r)?;
    if size > limits.container.maximum_file_bytes {
        return Err(Error::LimitExceeded);
    }
    let b = output.get_mut(..size).ok_or(Error::BufferTooSmall)?;
    let base =
        container::write_waveform_result(&r.source, &r.overview, r.tiles, r.metadata.as_ref(), b)?;
    let count = get32(b, 20) as usize;
    let extra = lengths.iter().filter(|n| **n != 0).count();
    let first = 96 + count * 40;
    b.copy_within(first..base, first + extra * 40);
    b[first..first + extra * 40].fill(0);
    b[base + extra * 40..].fill(0);
    for i in 0..count {
        let p = 96 + i * 40 + 8;
        let v = get64(b, p);
        put64(b, p, v + extra as u64 * 40);
    }
    put32(b, 20, (count + extra) as u32);
    put64(b, 32, size as u64);
    let partial = r
        .tempo
        .is_some_and(|t| t.selected.state != FeatureState::Final)
        || r.local_grid.is_some_and(|g| {
            g.state != FeatureState::Final || g.segment.state != FeatureState::Final
        })
        || r.global_grid.is_some_and(|g| {
            g.state != FeatureState::Final
                || g.segments.iter().any(|s| s.state != FeatureState::Final)
        })
        || r.revision
            .is_some_and(|v| v.state == RevisionState::Pending)
        || r.key.is_some_and(|k| k.state != FeatureState::Final)
        || r.meter.is_some_and(|m| {
            m.state != FeatureState::Final
                || m.segments.iter().any(|s| s.state != FeatureState::Final)
        })
        || r.quality.iter().any(|q| q.state != FeatureState::Final);
    if partial {
        let f = get32(b, 16);
        put32(b, 16, f | 1);
    }
    let partial = get32(b, 16) & 1 != 0;
    let mut end = base + extra * 40;
    let mut entry = first;
    let ids = [
        b"TEMP", b"LGRD", b"GGRD", b"REVN", b"MKEY", b"MTRD", b"CONF",
    ];
    for (i, len) in lengths.into_iter().enumerate() {
        if len == 0 {
            continue;
        }
        let start = align(end)?;
        let out = &mut b[start..start + len];
        match i {
            0 => {
                tempo::write_tempo_payload(&r.tempo.unwrap(), out)?;
            }
            1 => {
                tempo::write_local_grid(
                    &r.local_grid.unwrap(),
                    r.tempo.unwrap().selected.tempo_millibpm,
                    out,
                )?;
            }
            2 => {
                grid::write_payload(&r.global_grid.unwrap(), partial, out)?;
            }
            3 => {
                grid::write_revision(&r.revision.unwrap(), &r.global_grid.unwrap(), partial, out)?;
            }
            4 => {
                dj::write_key(r.key.unwrap(), r.source.total_frames, partial, out)?;
            }
            5 => {
                dj::write_meter(r.meter.unwrap(), r.source.total_frames, partial, out)?;
            }
            6 => {
                dj::write_quality(r.quality, partial, ALL_FEATURES, out)?;
            }
            _ => unreachable!(),
        }
        let crc = container::crc32c(out);
        b[entry..entry + 4].copy_from_slice(ids[i]);
        b[entry + 4] = 1;
        put64(b, entry + 8, start as u64);
        put64(b, entry + 16, len as u64);
        put64(b, entry + 24, len as u64);
        put32(b, entry + 32, crc);
        entry += 40;
        end = start + len;
    }
    let crc = container::crc32c(&b[..92]);
    put32(b, 92, crc);
    parse(b, limits)?;
    Ok(size)
}

/// Borrow the serializable portion of a validated native result. Use an owned
/// result's compacted view when input waveform columns alias or contain unused
/// backing data. The caller supplies detail-view descriptors; no allocation occurs.
///
/// A result without overview returns `NotAvailable`. Valid native representations
/// outside the current wire subset return `Unsupported`, rather than silently
/// dropping analysis. Generation, lineage, provenance and overview confidence
/// are native fields without corresponding container-v1 fields.
pub fn from_native<'a>(
    input: &NativeResultInput<'a>,
    tile_views: &'a mut [WaveformTile<'a>],
    limits: NativeLimits,
) -> Result<ResultInput<'a>, Error> {
    crate::native_validation::validate(input, limits)?;
    from_native_validated(input, tile_views, false)
}
/// Convert a trusted immutable session result. C session snapshots permit S4 and
/// S6 to disagree while external imports require selected-tempo coherence.
pub fn from_session_result<'a>(
    owned: &'a crate::owned_result::OwnedResult<'_>,
    tile_views: &'a mut [WaveformTile<'a>],
    limits: NativeLimits,
) -> Result<ResultInput<'a>, Error> {
    let input = owned.view();
    crate::native_validation::validate_session(&input, limits, owned.changed_features())?;
    from_native_validated(&input, tile_views, true)
}
/// Convert a graph borrowed directly from actual native session processing.
/// The opaque snapshot preserves the same trusted rules as owned session results.
pub fn from_session_snapshot<'a>(
    snapshot: &'a crate::session_snapshot::SessionSnapshot<'_>,
    tile_views: &'a mut [WaveformTile<'a>],
    limits: NativeLimits,
) -> Result<ResultInput<'a>, Error> {
    let input = snapshot.view();
    crate::native_validation::validate_session(&input, limits, 0)?;
    from_native_validated(&input, tile_views, true)
}
fn from_native_validated<'a>(
    input: &NativeResultInput<'a>,
    tile_views: &'a mut [WaveformTile<'a>],
    session: bool,
) -> Result<ResultInput<'a>, Error> {
    let overview = input.overview.ok_or(Error::NotAvailable)?;
    let logical = if let Some(total) = input.source.total_frames {
        let frames = total
            .checked_sub(overview.origin_frame)
            .ok_or(Error::Unsupported)?;
        let width = u64::from(overview.frames_per_column);
        u32::try_from(frames / width + u64::from(frames % width != 0))
            .map_err(|_| Error::LimitExceeded)?
    } else {
        overview
            .spans
            .iter()
            .map(|s| {
                s.first_column_index
                    .checked_add(s.column_count)
                    .ok_or(Error::LimitExceeded)
            })
            .try_fold(0u32, |a, b| Ok::<_, Error>(a.max(b?)))?
    };
    let overview = WaveformOverview {
        level_id: 0,
        frames_per_column: overview.frames_per_column,
        origin_frame: overview.origin_frame,
        logical_column_count: logical,
        state: overview.state,
        spans: overview.spans,
        columns: overview.columns,
    };
    let count = input.detail.map_or(0, |d| d.tiles.len());
    if tile_views.len() < count {
        return Err(Error::BufferTooSmall);
    }
    let local_grid = input
        .local_grid
        .map(|g| {
            if g.representation != GridRepresentation::Segments
                || g.segments.len() != 1
                || g.coverage_ranges.len() != 1
                || !g.beats.is_empty()
            {
                return Err(Error::Unsupported);
            }
            Ok(LocalGrid {
                requested_range: g.requested_range,
                evidence_range: g.evidence_range,
                applicability_range: g.applicability_range,
                coverage: g.coverage_ranges[0],
                segment: g.segments[0],
                state: g.state,
                confidence: g.confidence,
                flags: g.flags,
            })
        })
        .transpose()?;
    let global_grid = input
        .global_grid
        .map(|g| {
            if g.coverage_ranges.len() != 1 {
                return Err(Error::Unsupported);
            }
            Ok(GlobalGrid {
                requested_range: g.requested_range,
                evidence_range: g.evidence_range,
                applicability_range: g.applicability_range,
                coverage_range: g.coverage_ranges[0],
                segments: g.segments,
                beats: g.beats,
                state: g.state,
                confidence: g.confidence,
                flags: g.flags,
                representation: g.representation,
            })
        })
        .transpose()?;
    if let Some(detail) = input.detail {
        for (out, tile) in tile_views.iter_mut().zip(detail.tiles) {
            let end = tile
                .data_column_offset
                .checked_add(tile.column_count)
                .ok_or(Error::LimitExceeded)?;
            *out = WaveformTile {
                level_id: tile.level_id,
                tile_index: tile.tile_index,
                first_frame: tile.first_frame,
                end_frame: tile.end_frame,
                first_column_index: tile.first_column_index,
                state: tile.state,
                confidence: tile.confidence,
                columns: &detail.columns[tile.data_column_offset..end],
            };
        }
    }
    let wire = ResultInput {
        source: input.source,
        overview,
        tiles: &tile_views[..count],
        metadata: input.metadata,
        tempo: input.tempo,
        local_grid,
        global_grid,
        revision: input.revision,
        key: input.key,
        meter: input.meter,
        quality: input.quality,
    };
    let wire_limits = Limits {
        container: ParseOptions {
            maximum_file_bytes: usize::MAX,
            maximum_section_count: 10,
            maximum_overview_spans: wire.overview.spans.len(),
            maximum_waveform_columns: usize::MAX,
            maximum_detail_tiles: wire.tiles.len(),
            strict: true,
        },
        ..Limits::default()
    };
    if session {
        sizes(&wire)?;
        container::waveform_result_size(
            &wire.source,
            &wire.overview,
            wire.tiles,
            wire.metadata.as_ref(),
        )?;
    } else {
        crate::builder::validate(&wire, wire_limits).map_err(|error| match error {
            Error::LimitExceeded => Error::LimitExceeded,
            _ => Error::Unsupported,
        })?;
    }
    Ok(wire)
}
