// SPDX-License-Identifier: Apache-2.0
//! In-memory external-result validation matching the C builder's meaningful
//! fields, without imposing container limits. Storage accounting belongs to
//! `owned_result::requirements`; this module checks semantic and count limits.
//! C unknown duration (`UINT64_MAX`) maps to `None`; `Some(u64::MAX)` is invalid.
//! Referenced column counts obey C limits. Aliased and unused source storage are
//! allowed; owned copying duplicates referenced columns and normalizes offsets.
use crate::{result as feature, *};

fn check(valid: bool) -> Result<(), Error> {
    if valid {
        Ok(())
    } else {
        Err(Error::InvalidArgument)
    }
}
fn confidence(c: u8) -> bool {
    c <= 100 || c == 255
}
fn count(n: usize, limit: usize) -> Result<(), Error> {
    if n > limit || n > u32::MAX as usize {
        Err(Error::LimitExceeded)
    } else {
        Ok(())
    }
}
fn range(r: FrameRange, total: Option<u64>) -> Result<(), Error> {
    check(r.first_frame < r.end_frame && !total.is_some_and(|t| r.end_frame > t))
}
fn inside(r: FrameRange, o: FrameRange) -> bool {
    r.first_frame >= o.first_frame && r.end_frame <= o.end_frame
}
fn position(p: FractionalFrame) -> u128 {
    ((p.whole_frame as u128) << 32) | p.fraction_q32 as u128
}
fn column(c: WaveformColumn) -> Result<(), Error> {
    if c.flags & !31 != 0 {
        return Err(Error::Unsupported);
    }
    check(
        c.minimum <= c.maximum
            && c.flags & 1 != 0
            && (c.flags & 8 != 0 || (c.low == 0 && c.mid == 0 && c.high == 0)),
    )
}
fn columns(input: &[WaveformColumn], offset: usize, n: usize) -> Result<&[WaveformColumn], Error> {
    let end = offset.checked_add(n).ok_or(Error::LimitExceeded)?;
    let data = input.get(offset..end).ok_or(Error::InvalidArgument)?;
    for c in data {
        column(*c)?
    }
    Ok(data)
}
fn known_flags(flags: u32, allowed: u32) -> Result<(), Error> {
    if flags & !allowed != 0 {
        Err(Error::Unsupported)
    } else {
        Ok(())
    }
}
fn bpm(value: u32) -> bool {
    (40000..=300000).contains(&value)
}
fn waveform(input: &NativeResultInput<'_>, limits: NativeLimits) -> Result<u64, Error> {
    let mut features = 0;
    if let Some(w) = input.overview {
        check(
            w.frames_per_column != 0
                && confidence(w.confidence)
                && !w.spans.is_empty()
                && (w.state != FeatureState::Final || input.source.total_frames.is_some()),
        )?;
        count(w.spans.len(), limits.maximum_overview_spans)?;
        let mut previous = None;
        let mut previous_column = 0u32;
        let mut total = 0usize;
        for s in w.spans {
            range(
                FrameRange {
                    first_frame: s.first_frame,
                    end_frame: s.end_frame,
                },
                input.source.total_frames,
            )?;
            let end_column = s
                .first_column_index
                .checked_add(s.column_count)
                .ok_or(Error::InvalidArgument)?;
            check(
                s.column_count != 0
                    && previous.map_or(true, |end| end <= s.first_frame)
                    && s.first_column_index >= previous_column,
            )?;
            if w.state == FeatureState::Final {
                check(
                    s.first_frame == previous.unwrap_or(w.origin_frame)
                        && s.first_column_index == previous_column,
                )?
            }
            let first = w
                .origin_frame
                .checked_add(u64::from(s.first_column_index) * u64::from(w.frames_per_column))
                .ok_or(Error::LimitExceeded)?;
            let end = w
                .origin_frame
                .checked_add(u64::from(end_column) * u64::from(w.frames_per_column))
                .ok_or(Error::LimitExceeded)?;
            check(
                s.first_frame == first
                    && (s.end_frame == end
                        || (input.source.total_frames == Some(s.end_frame) && s.end_frame < end)),
            )?;
            total = total
                .checked_add(s.column_count as usize)
                .ok_or(Error::LimitExceeded)?;
            count(total, limits.maximum_waveform_columns)?;
            let data = columns(
                w.columns,
                s.data_column_offset as usize,
                s.column_count as usize,
            )?;
            if data.iter().any(|c| c.flags & 8 != 0) {
                features |= feature::WAVEFORM_3BAND
            }
            previous = Some(s.end_frame);
            previous_column = end_column;
        }
        if w.state == FeatureState::Final {
            let n = input
                .source
                .total_frames
                .unwrap()
                .checked_sub(w.origin_frame)
                .ok_or(Error::InvalidArgument)?;
            let f = u64::from(w.frames_per_column);
            check(n / f + u64::from(n % f != 0) == u64::from(previous_column))?;
        }
        features |= feature::WAVEFORM_OVERVIEW;
        if w.confidence != 255 {
            features |= feature::CONFIDENCE
        }
    }
    if let Some(d) = input.detail {
        check(!d.tiles.is_empty())?;
        count(d.tiles.len(), limits.maximum_detail_tiles)?;
        let mut previous = None;
        let mut total = 0usize;
        for (i, t) in d.tiles.iter().enumerate() {
            range(
                FrameRange {
                    first_frame: t.first_frame,
                    end_frame: t.end_frame,
                },
                input.source.total_frames,
            )?;
            count(t.column_count, u32::MAX as usize)?;
            let end_column = t
                .first_column_index
                .checked_add(t.column_count as u32)
                .ok_or(Error::InvalidArgument)?;
            check(
                t.level_id == 1
                    && t.column_count != 0
                    && confidence(t.confidence)
                    && previous.map_or(true, |end| end <= t.first_frame)
                    && (i == 0 || d.tiles[i - 1].tile_index < t.tile_index),
            )?;
            let tile_start = u64::from(t.tile_index) * 64;
            check(
                u64::from(t.first_column_index) >= tile_start
                    && u64::from(end_column) <= tile_start + 64,
            )?;
            let expected_end = u64::from(end_column) * 256;
            check(
                t.first_frame == u64::from(t.first_column_index) * 256
                    && (t.end_frame == expected_end
                        || (input.source.total_frames == Some(t.end_frame)
                            && t.end_frame < expected_end))
                    && (t.state != FeatureState::Final || input.source.total_frames.is_some()),
            )?;
            total = total
                .checked_add(t.column_count)
                .ok_or(Error::LimitExceeded)?;
            count(total, limits.maximum_waveform_columns)?;
            columns(d.columns, t.data_column_offset, t.column_count)?;
            previous = Some(t.end_frame);
        }
        features |= feature::WAVEFORM_DETAIL;
        if d.tiles[0].confidence != 255 {
            features |= feature::CONFIDENCE
        }
    }
    Ok(features)
}
fn tempo(
    t: TempoView<'_>,
    total: Option<u64>,
    limits: NativeLimits,
    session: bool,
) -> Result<(), Error> {
    range(t.selected.evidence_range, total)?;
    range(t.selected.applicability_range, total)?;
    check(bpm(t.selected.tempo_millibpm) && confidence(t.selected.confidence))?;
    known_flags(t.selected.flags, 255)?;
    count(t.candidates.len(), limits.maximum_tempo_candidates)?;
    let mut found = t.candidates.is_empty();
    for (i, c) in t.candidates.iter().enumerate() {
        check(
            bpm(c.tempo_millibpm)
                && c.confidence <= 100
                && c.relation_to_selected <= 8
                && (i == 0
                    || if session {
                        c.score <= t.candidates[i - 1].score
                    } else {
                        c.score < t.candidates[i - 1].score
                    }),
        )?;
        known_flags(c.flags, 255)?;
        check(
            !t.candidates[..i]
                .iter()
                .any(|p| p.tempo_millibpm == c.tempo_millibpm),
        )?;
        found |= c.tempo_millibpm == t.selected.tempo_millibpm;
    }
    check(session || found)
}
fn native_grid(
    g: NativeGrid<'_>,
    total: Option<u64>,
    limits: NativeLimits,
    local: bool,
    session: bool,
) -> Result<(), Error> {
    for r in [g.requested_range, g.evidence_range, g.applicability_range] {
        range(r, total)?
    }
    check(confidence(g.confidence) && !g.coverage_ranges.is_empty())?;
    known_flags(g.flags, 511)?;
    count(g.coverage_ranges.len(), limits.maximum_grid_coverage_ranges)?;
    count(g.segments.len(), limits.maximum_grid_segments)?;
    count(g.beats.len(), limits.maximum_grid_beats)?;
    check(match g.representation {
        GridRepresentation::Segments => !g.segments.is_empty() && g.beats.is_empty(),
        GridRepresentation::Explicit => g.segments.is_empty() && !g.beats.is_empty(),
        GridRepresentation::Hybrid => !g.segments.is_empty() && !g.beats.is_empty(),
    })?;
    let mut previous = None;
    for r in g.coverage_ranges {
        range(*r, total)?;
        check(
            inside(*r, g.applicability_range) && previous.map_or(true, |end| end <= r.first_frame),
        )?;
        previous = Some(r.end_frame)
    }
    let mut elements = 0;
    for (i, s) in g.segments.iter().enumerate() {
        range(s.applicability_range, total)?;
        check(
            inside(s.applicability_range, g.applicability_range)
                && (i == 0
                    || g.segments[i - 1].applicability_range.end_frame
                        <= s.applicability_range.first_frame)
                && (s.frames_per_beat.whole_frames != 0 || s.frames_per_beat.fraction_q32 != 0)
                && bpm(s.nominal_tempo_millibpm)
                && confidence(s.confidence)
                && s.segment_id != 0,
        )?;
        known_flags(s.flags, 511)?;
        check(!g.segments[..i].iter().any(|p| p.segment_id == s.segment_id))?;
        elements |= s.flags;
    }
    let mut si = 0;
    for (i, b) in g.beats.iter().enumerate() {
        check(
            b.position.whole_frame >= g.applicability_range.first_frame
                && b.position.whole_frame < g.applicability_range.end_frame
                && confidence(b.confidence)
                && (i == 0
                    || (position(g.beats[i - 1].position) < position(b.position)
                        && g.beats[i - 1].ordinal < b.ordinal)),
        )?;
        known_flags(b.flags, 511)?;
        elements |= b.flags;
        if g.representation == GridRepresentation::Hybrid {
            while si < g.segments.len()
                && g.segments[si].applicability_range.end_frame <= b.position.whole_frame
            {
                si += 1
            }
            let s = g.segments.get(si).ok_or(Error::InvalidArgument)?;
            check(
                b.position.whole_frame >= s.applicability_range.first_frame
                    && b.ordinal >= s.anchor_ordinal,
            )?;
            check(
                crate::grid::segment_position_at_ordinal(s, b.ordinal)
                    .ok_or(Error::LimitExceeded)?
                    == b.position,
            )?;
        }
    }
    check(g.flags & 0x102 == elements & 0x102)?;
    if (local && !session && elements & 2 != 0) || (!local && elements & 256 != 0) {
        return Err(Error::Unsupported);
    }
    Ok(())
}
fn revision(r: GridRevision, g: NativeGrid<'_>) -> Result<(), Error> {
    check(
        r.revision_id != 0 && r.revision_id != r.previous_revision_id && confidence(r.confidence),
    )?;
    known_flags(r.flags, 7)?;
    check(
        r.affected_range.first_frame < r.affected_range.end_frame
            && inside(r.affected_range, g.applicability_range)
            && r.proposed_representation == g.representation
            && r.proposed_segment_count as usize == g.segments.len()
            && r.proposed_beat_count as usize == g.beats.len()
            && r.flags & 2 == g.flags & 2
            && g.segments.iter().all(|s| s.revision == r.revision_id)
            && g.beats.iter().all(|b| b.revision == r.revision_id),
    )
}
fn matches_tempo(period: u128, delta: u64, sr: u32, tempo: u32) -> bool {
    let n = (sr as u128) * 60000 * (delta as u128);
    let Some(n) = n.checked_mul(1 << 32) else {
        return false;
    };
    n.abs_diff(period * (tempo as u128)) <= period
}
fn grid_tempo(g: NativeGrid<'_>, sr: u32, tempo: u32, require_selected: bool) -> Result<(), Error> {
    if g.representation == GridRepresentation::Explicit {
        for p in g.beats.windows(2) {
            check(matches_tempo(
                position(p[1].position) - position(p[0].position),
                (p[1].ordinal as u64).wrapping_sub(p[0].ordinal as u64),
                sr,
                tempo,
            ))?
        }
        return Ok(());
    }
    let mut found = false;
    for s in g.segments {
        check(matches_tempo(
            ((s.frames_per_beat.whole_frames as u128) << 32)
                | s.frames_per_beat.fraction_q32 as u128,
            1,
            sr,
            s.nominal_tempo_millibpm,
        ))?;
        if s.nominal_tempo_millibpm == tempo {
            found = true
        } else if require_selected {
            check(g.flags & 2 != 0)?
        }
    }
    check(!require_selected || found)
}
fn key_value(tonic: u8, mode: u8, tuning: i16) -> bool {
    tonic <= 11 && (mode == 1 || mode == 2) && (-100..=100).contains(&tuning)
}
fn key(k: Key<'_>, total: Option<u64>, limits: NativeLimits) -> Result<(), Error> {
    range(
        FrameRange {
            first_frame: k.first_frame,
            end_frame: k.end_frame,
        },
        total,
    )?;
    check(key_value(k.tonic, k.mode, k.tuning_offset_cents) && confidence(k.confidence))?;
    count(k.candidates.len(), limits.maximum_key_candidates)?;
    let mut found = k.candidates.is_empty();
    for (i, c) in k.candidates.iter().enumerate() {
        check(
            key_value(c.tonic, c.mode, c.tuning_offset_cents)
                && confidence(c.confidence)
                && (i == 0 || c.score < k.candidates[i - 1].score),
        )?;
        let identity = (c.tonic, c.mode, c.tuning_offset_cents);
        check(
            !k.candidates[..i]
                .iter()
                .any(|p| (p.tonic, p.mode, p.tuning_offset_cents) == identity),
        )?;
        found |= identity == (k.tonic, k.mode, k.tuning_offset_cents);
    }
    check(found)
}
fn meter_value(n: u16, d: u16) -> bool {
    (1..=32).contains(&n) && (1..=32).contains(&d) && d.is_power_of_two()
}
fn meter(m: Meter<'_>, total: Option<u64>, limits: NativeLimits) -> Result<(), Error> {
    check(
        meter_value(m.numerator, m.denominator)
            && confidence(m.confidence)
            && !m.segments.is_empty(),
    )?;
    count(m.segments.len(), limits.maximum_meter_segments)?;
    let mut previous_ordinal = i64::MIN;
    for (i, s) in m.segments.iter().enumerate() {
        range(
            FrameRange {
                first_frame: s.first_frame,
                end_frame: s.end_frame,
            },
            total,
        )?;
        check(
            meter_value(s.numerator, s.denominator)
                && confidence(s.confidence)
                && s.downbeat_frame >= s.first_frame
                && s.downbeat_frame < s.end_frame
                && s.segment_id != 0
                && s.state as u8 >= m.state as u8
                && s.downbeat_ordinal > previous_ordinal
                && (i == 0
                    || (m.segments[i - 1].end_frame <= s.first_frame
                        && m.segments[i - 1].segment_id < s.segment_id)),
        )?;
        previous_ordinal = s.downbeat_ordinal;
    }
    let s = m.segments[0];
    check(
        (
            m.downbeat_frame,
            m.downbeat_ordinal,
            m.numerator,
            m.denominator,
        ) == (
            s.downbeat_frame,
            s.downbeat_ordinal,
            s.numerator,
            s.denominator,
        ),
    )
}
struct Cursor {
    beat: usize,
    segment: usize,
}
impl Cursor {
    fn matches(&mut self, g: NativeGrid<'_>, frame: u64, ordinal: i64) -> bool {
        while self.beat < g.beats.len()
            && (g.beats[self.beat].position.whole_frame < frame
                || (g.beats[self.beat].position.whole_frame == frame
                    && g.beats[self.beat].ordinal < ordinal))
        {
            self.beat += 1
        }
        if self.beat < g.beats.len()
            && g.beats[self.beat].position.whole_frame == frame
            && g.beats[self.beat].ordinal == ordinal
        {
            return true;
        }
        while self.segment < g.segments.len()
            && g.segments[self.segment].applicability_range.end_frame <= frame
        {
            self.segment += 1
        }
        g.segments.get(self.segment).is_some_and(|s| {
            frame >= s.applicability_range.first_frame
                && frame < s.applicability_range.end_frame
                && crate::grid::segment_position_at_ordinal(s, ordinal)
                    .is_some_and(|p| p.whole_frame == frame)
        })
    }
}
/// Validate external imports. Empty initial native-session results use a separate
/// trusted construction path; they are deliberately not accepted as imports.
pub fn validate(
    input: &NativeResultInput<'_>,
    limits: NativeLimits,
) -> Result<NativeValidation, Error> {
    validate_inner(input, limits, None)
}

/// Trusted native snapshot validation. C publication uses unspecified provenance;
/// explicitly native provenance is also permitted. Empty generations and empty
/// provenance strings are valid. Changed bits may name features removed here.
pub(crate) fn validate_session(
    input: &NativeResultInput<'_>,
    limits: NativeLimits,
    changed_features: u64,
) -> Result<NativeValidation, Error> {
    if changed_features & !feature::ALL_FEATURES != 0 {
        return Err(Error::InvalidArgument);
    }
    validate_inner(input, limits, Some(changed_features))
}

fn validate_inner(
    input: &NativeResultInput<'_>,
    limits: NativeLimits,
    changed_features: Option<u64>,
) -> Result<NativeValidation, Error> {
    let session = changed_features.is_some();
    let source = input.source;
    check(
        source.total_frames != Some(u64::MAX)
            && source.sample_rate != 0
            && source.sample_rate <= 768000
            && (1..=8).contains(&source.channel_count)
            && source.channel_layout <= 2
            && (source.channel_layout == 0 || source.channel_layout == source.channel_count)
            && source.fingerprint_kind <= 2
            && (source.fingerprint_kind != 0 || source.fingerprint.iter().all(|b| *b == 0)),
    )?;
    check(
        input.info.generation != 0
            && input.info.container_version <= 1
            && (session || input.info.session_state != ResultSessionState::Created),
    )?;
    match (session, input.provenance.origin) {
        (false, ProvenanceOrigin::Unspecified) => return Err(Error::InvalidArgument),
        (false, ProvenanceOrigin::NativeAnalysis) | (true, ProvenanceOrigin::ExternalImport) => {
            return Err(Error::Unsupported)
        }
        _ => (),
    }
    check(
        (session || !input.provenance.source_name.is_empty())
            && input.provenance.source_name.len() <= 255
            && input.provenance.source_version.len() <= 127,
    )?;
    if let Some(metadata) = input.metadata {
        crate::meta::serialized_size(&metadata)?;
    }
    let mut features = waveform(input, limits)?;
    if let Some(t) = input.tempo {
        tempo(t, source.total_frames, limits, session)?;
        features |= feature::BPM | feature::CONFIDENCE
    }
    for (g, local) in [(input.local_grid, true), (input.global_grid, false)] {
        if let Some(g) = g {
            native_grid(g, source.total_frames, limits, local, session)?;
            grid_tempo(
                g,
                source.sample_rate,
                input
                    .tempo
                    .ok_or(Error::InvalidArgument)?
                    .selected
                    .tempo_millibpm,
                !session || local,
            )?;
            features |= if local {
                feature::LOCAL_BEATGRID
            } else {
                feature::GLOBAL_BEATGRID
            };
            if g.confidence != 255 {
                features |= feature::CONFIDENCE
            }
            if local && g.flags & 256 != 0 {
                features |= feature::GRID_LOCKING
            }
            if !local && g.flags & 2 != 0 {
                features |= feature::DYNAMIC_TEMPO
            }
        }
    }
    if let Some(g) = input.global_grid {
        revision(input.revision.ok_or(Error::InvalidArgument)?, g)?
    } else {
        check(input.revision.is_none())?
    }
    if let Some(k) = input.key {
        key(k, source.total_frames, limits)?;
        features |= feature::MUSICAL_KEY;
        if k.confidence != 255 {
            features |= feature::CONFIDENCE
        }
    }
    if let Some(m) = input.meter {
        meter(m, source.total_frames, limits)?;
        features |= feature::METER_DOWNBEAT;
        if m.confidence != 255 {
            features |= feature::CONFIDENCE
        }
        let mut local = Cursor {
            beat: 0,
            segment: 0,
        };
        let mut global = Cursor {
            beat: 0,
            segment: 0,
        };
        for s in m.segments {
            check(
                session
                    || (input.local_grid.is_none() && input.global_grid.is_none())
                    || input
                        .local_grid
                        .is_some_and(|g| local.matches(g, s.downbeat_frame, s.downbeat_ordinal))
                    || input
                        .global_grid
                        .is_some_and(|g| global.matches(g, s.downbeat_frame, s.downbeat_ordinal)),
            )?;
        }
    }
    count(input.quality.len(), limits.maximum_quality_records)?;
    for (i, q) in input.quality.iter().enumerate() {
        check(
            q.feature.is_power_of_two()
                && q.feature < 1 << 11
                && features & q.feature != 0
                && confidence(q.confidence)
                && (q.evidence_coverage_permille <= 1000 || q.evidence_coverage_permille == 65535)
                && !input.quality[..i].iter().any(|p| p.feature == q.feature),
        )?;
        known_flags(q.flags, 15)?;
    }
    if !input.quality.is_empty() {
        features |= feature::CALIBRATED_QUALITY
    }
    check(session || features != 0)?;
    if input.info.session_state == ResultSessionState::Completed {
        check(
            input.overview.map_or(true, |w| {
                w.state == FeatureState::Final || (session && w.state == FeatureState::Partial)
            }) && input.detail.map_or(true, |d| {
                d.tiles.iter().all(|t| {
                    t.state == FeatureState::Final || (session && t.state == FeatureState::Partial)
                })
            }) && input
                .tempo
                .map_or(true, |t| t.selected.state == FeatureState::Final)
                && input
                    .local_grid
                    .map_or(true, |g| g.state == FeatureState::Final)
                && input
                    .global_grid
                    .map_or(true, |g| g.state == FeatureState::Final)
                && input
                    .revision
                    .map_or(true, |r| session || r.state == RevisionState::Applied)
                && input.key.map_or(true, |k| k.state == FeatureState::Final)
                && input.meter.map_or(true, |m| {
                    m.state == FeatureState::Final
                        && m.segments.iter().all(|s| s.state == FeatureState::Final)
                })
                && input.quality.iter().all(|q| q.state == FeatureState::Final),
        )?;
    }
    Ok(NativeValidation {
        available_features: features,
        changed_features: changed_features.unwrap_or(features),
    })
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
                total_frames: Some(0),
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
    fn trusted_completed_detail_retains_partial_cache_tiles() {
        let mut input = empty();
        input.source.total_frames = Some(513);
        input.info.session_state = ResultSessionState::Completed;
        let columns = [WaveformColumn {
            flags: 1,
            ..WaveformColumn::default()
        }; 2];
        let tiles = [NativeTile {
            level_id: 1,
            tile_index: 0,
            first_frame: 256,
            end_frame: 513,
            first_column_index: 1,
            state: FeatureState::Partial,
            confidence: 255,
            data_column_offset: 0,
            column_count: 2,
        }];
        input.detail = Some(NativeDetail {
            tiles: &tiles,
            columns: &columns,
        });
        assert!(
            validate_session(&input, NativeLimits::default(), feature::WAVEFORM_DETAIL).is_ok()
        );
        input.provenance = Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "fixture",
            source_version: "1",
        };
        assert_eq!(
            validate(&input, NativeLimits::default()),
            Err(Error::InvalidArgument)
        );
        let stable_tiles = [NativeTile {
            state: FeatureState::Stable,
            ..tiles[0]
        }];
        input.detail = Some(NativeDetail {
            tiles: &stable_tiles,
            columns: &columns,
        });
        input.provenance.origin = ProvenanceOrigin::Unspecified;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0),
            Err(Error::InvalidArgument)
        );
    }
    #[test]
    fn trusted_empty_generation_and_external_boundary() {
        let mut input = empty();
        for state in [
            ResultSessionState::Created,
            ResultSessionState::AcceptingInput,
            ResultSessionState::Draining,
            ResultSessionState::Completed,
            ResultSessionState::Cancelled,
            ResultSessionState::Failed,
        ] {
            input.info.session_state = state;
            assert_eq!(
                validate_session(&input, NativeLimits::default(), 0),
                Ok(NativeValidation {
                    available_features: 0,
                    changed_features: 0
                })
            );
            assert!(validate(&input, NativeLimits::default()).is_err());
        }
        input.provenance.origin = ProvenanceOrigin::ExternalImport;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0),
            Err(Error::Unsupported)
        );
    }
    #[test]
    fn trusted_changed_mask_preserves_removals() {
        let mut input = empty();
        let v = validate_session(&input, NativeLimits::default(), feature::BPM).unwrap();
        assert_eq!(v.available_features, 0);
        assert_eq!(v.changed_features, feature::BPM);
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 1u64 << 63),
            Err(Error::InvalidArgument)
        );
        input.provenance.origin = ProvenanceOrigin::NativeAnalysis;
        assert!(validate_session(&input, NativeLimits::default(), 0).is_ok());
        input.provenance.origin = ProvenanceOrigin::Unspecified;
        input.info.session_state = ResultSessionState::Completed;
        assert_eq!(
            validate(&input, NativeLimits::default()),
            Err(Error::InvalidArgument)
        );
    }
    #[test]
    fn trusted_path_preserves_source_and_generation_validation() {
        let mut input = empty();
        input.info.generation = 0;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0),
            Err(Error::InvalidArgument)
        );
        input.info.generation = 1;
        input.source.sample_rate = 0;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0),
            Err(Error::InvalidArgument)
        );
    }
    #[test]
    fn trusted_completed_sparse_overview_preserves_holes() {
        let mut input = empty();
        input.source.total_frames = Some(128);
        input.info.session_state = ResultSessionState::Completed;
        let spans = [WaveformSpan {
            first_frame: 64,
            end_frame: 128,
            first_column_index: 1,
            column_count: 1,
            data_column_offset: 0,
        }];
        let columns = [WaveformColumn {
            flags: 1,
            ..WaveformColumn::default()
        }];
        input.overview = Some(NativeOverview {
            frames_per_column: 64,
            origin_frame: 0,
            state: FeatureState::Partial,
            confidence: 255,
            spans: &spans,
            columns: &columns,
        });
        assert!(validate_session(&input, NativeLimits::default(), 0).is_ok());
        input.provenance.origin = ProvenanceOrigin::ExternalImport;
        input.provenance.source_name = "sparse-test";
        assert_eq!(
            validate(&input, NativeLimits::default()),
            Err(Error::InvalidArgument)
        );
    }

    #[test]
    fn trusted_completion_still_requires_final_features() {
        let mut input = empty();
        input.source.total_frames = Some(1000);
        input.tempo = Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Partial,
                confidence: 255,
                flags: 0,
                tempo_millibpm: 120000,
                candidate_set_id: 0,
                evidence_range: FrameRange {
                    first_frame: 0,
                    end_frame: 1000,
                },
                applicability_range: FrameRange {
                    first_frame: 0,
                    end_frame: 1000,
                },
            },
            candidates: &[],
        });
        input.info.session_state = ResultSessionState::Completed;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0),
            Err(Error::InvalidArgument)
        );
        input.info.session_state = ResultSessionState::AcceptingInput;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0)
                .unwrap()
                .available_features,
            feature::BPM | feature::CONFIDENCE
        );
        input
            .tempo
            .as_mut()
            .unwrap()
            .selected
            .evidence_range
            .end_frame = 1001;
        assert_eq!(
            validate_session(&input, NativeLimits::default(), 0),
            Err(Error::InvalidArgument)
        );
    }
}
