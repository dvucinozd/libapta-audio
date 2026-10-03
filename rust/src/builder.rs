// SPDX-License-Identifier: Apache-2.0
//! Validated external construction into independent caller-owned container bytes.
//!
//! This is the serializable subset of the C result builder: an overview is
//! required, grids have one coverage range, and no provenance/session-state or
//! allocator handles are represented. Unknown grid confidence and sub-frame-only
//! periods accepted by the C in-memory builder cannot be encoded by its reader
//! and are rejected here. TEMP requires at least one candidate on wire; grid
//! and tempo lifecycle states must be provisional, stable, or final. It does not claim full C builder API parity.
use crate::{
    grid,
    result::{self, Limits, ResultInput, ResultView},
    *,
};

fn range(r: FrameRange, total: Option<u64>) -> Result<(), Error> {
    if r.first_frame >= r.end_frame || total.is_some_and(|n| r.end_frame > n) {
        Err(Error::InvalidArgument)
    } else {
        Ok(())
    }
}
fn modifiers(g: &GlobalGrid<'_>, local: bool) -> Result<(), Error> {
    let elements = g.segments.iter().fold(0, |flags, s| flags | s.flags)
        | g.beats.iter().fold(0, |flags, b| flags | b.flags);
    if (g.flags & 0x102) != (elements & 0x102) {
        return Err(Error::InvalidArgument);
    }
    if (local && elements & 2 != 0) || (!local && elements & 256 != 0) {
        return Err(Error::Unsupported);
    }
    Ok(())
}
/// Exact rational form of the C builder's <= 1 millibpm tolerance. This avoids
/// target-dependent long-double precision at the acceptance boundary.
fn matches_tempo(period_q32: u128, ordinal_delta: u64, sample_rate: u32, tempo: u32) -> bool {
    let numerator = (sample_rate as u128) * 60000 * (ordinal_delta as u128);
    let Some(numerator) = numerator.checked_mul(1u128 << 32) else {
        return false;
    };
    let target = period_q32 * (tempo as u128);
    numerator.abs_diff(target) <= period_q32
}
fn tempo_grid(g: &GlobalGrid<'_>, sample_rate: u32, selected: u32) -> Result<(), Error> {
    if g.representation == GridRepresentation::Explicit {
        for pair in g.beats.windows(2) {
            let a = pair[0];
            let b = pair[1];
            let first = ((a.position.whole_frame as u128) << 32) | a.position.fraction_q32 as u128;
            let end = ((b.position.whole_frame as u128) << 32) | b.position.fraction_q32 as u128;
            if !matches_tempo(
                end - first,
                (b.ordinal as u64).wrapping_sub(a.ordinal as u64),
                sample_rate,
                selected,
            ) {
                return Err(Error::InvalidArgument);
            }
        }
        return Ok(());
    }
    let mut found = false;
    for s in g.segments {
        let period = ((s.frames_per_beat.whole_frames as u128) << 32)
            | s.frames_per_beat.fraction_q32 as u128;
        if !matches_tempo(period, 1, sample_rate, s.nominal_tempo_millibpm) {
            return Err(Error::InvalidArgument);
        }
        if s.nominal_tempo_millibpm == selected {
            found = true
        } else if g.flags & 2 == 0 {
            return Err(Error::InvalidArgument);
        }
    }
    if found {
        Ok(())
    } else {
        Err(Error::InvalidArgument)
    }
}
fn column(c: &WaveformColumn) -> Result<(), Error> {
    if c.flags & !0x1f != 0 {
        return Err(Error::Unsupported);
    }
    if c.minimum > c.maximum
        || c.flags & 1 == 0
        || (c.flags & 8 == 0 && (c.low != 0 || c.mid != 0 || c.high != 0))
    {
        return Err(Error::InvalidArgument);
    }
    Ok(())
}
fn local_as_global(g: &LocalGrid) -> GlobalGrid<'_> {
    GlobalGrid {
        state: g.state,
        confidence: g.confidence,
        flags: g.flags,
        representation: GridRepresentation::Segments,
        requested_range: g.requested_range,
        evidence_range: g.evidence_range,
        applicability_range: g.applicability_range,
        coverage_range: g.coverage,
        segments: core::slice::from_ref(&g.segment),
        beats: &[],
    }
}

/// Validate native input, cross-feature relationships and all limits before
/// producing bytes. Returns the required encoded size without allocation.
pub fn validate(input: &ResultInput<'_>, limits: Limits) -> Result<usize, Error> {
    let size = result::serialized_size(input)?;
    if size > limits.container.maximum_file_bytes
        || input.overview.spans.len() > limits.container.maximum_overview_spans
        || input.overview.columns.len() > limits.container.maximum_waveform_columns
    {
        return Err(Error::LimitExceeded);
    }
    let section_count = 1
        + usize::from(!input.tiles.is_empty())
        + usize::from(input.metadata.is_some())
        + usize::from(input.tempo.is_some())
        + usize::from(input.local_grid.is_some())
        + 2 * usize::from(input.global_grid.is_some())
        + usize::from(input.key.is_some())
        + usize::from(input.meter.is_some())
        + usize::from(!input.quality.is_empty());
    if section_count > limits.container.maximum_section_count {
        return Err(Error::LimitExceeded);
    }
    let source = input.source;
    if source.sample_rate == 0
        || source.sample_rate > 768000
        || source.channel_count == 0
        || source.channel_count > 8
        || source.channel_layout > 2
        || (source.channel_layout != 0 && source.channel_count != source.channel_layout)
        || source.total_frames == Some(u64::MAX)
        || source.fingerprint_kind > 2
        || (source.fingerprint_kind == 0 && source.fingerprint.iter().any(|v| *v != 0))
    {
        return Err(Error::InvalidArgument);
    }
    let total = input.source.total_frames;
    if input.overview.state == FeatureState::Final && total.is_none() {
        return Err(Error::InvalidArgument);
    }
    let overview = input.overview;
    if overview.level_id != 0 || overview.frames_per_column == 0 || overview.spans.is_empty() {
        return Err(Error::InvalidArgument);
    }
    if overview.logical_column_count == 0 {
        return Err(Error::InvalidArgument);
    }
    if overview.logical_column_count as usize > limits.container.maximum_waveform_columns {
        return Err(Error::LimitExceeded);
    }
    if let Some(total) = total {
        let relative = total
            .checked_sub(overview.origin_frame)
            .ok_or(Error::InvalidArgument)?;
        let fpc = u64::from(overview.frames_per_column);
        if relative / fpc + u64::from(relative % fpc != 0)
            != u64::from(overview.logical_column_count)
        {
            return Err(Error::InvalidArgument);
        }
    }
    let mut aggregate_columns = 0usize;
    let mut previous_end = None;
    let mut previous_column = 0;
    for (span_index, span) in overview.spans.iter().enumerate() {
        range(
            FrameRange {
                first_frame: span.first_frame,
                end_frame: span.end_frame,
            },
            total,
        )?;
        let end_column = span
            .first_column_index
            .checked_add(span.column_count)
            .ok_or(Error::LimitExceeded)?;
        let first = overview
            .origin_frame
            .checked_add(u64::from(span.first_column_index) * u64::from(overview.frames_per_column))
            .ok_or(Error::LimitExceeded)?;
        let end = overview
            .origin_frame
            .checked_add(u64::from(end_column) * u64::from(overview.frames_per_column))
            .ok_or(Error::LimitExceeded)?;
        if span.column_count == 0
            || end_column > overview.logical_column_count
            || span.first_frame != first
            || span.end_frame != total.map_or(end, |n| n.min(end))
            || previous_end.is_some_and(|n| span.first_frame < n)
            || span.first_column_index < previous_column
            || (overview.state == FeatureState::Final
                && (span.first_column_index != previous_column
                    || span.first_frame != previous_end.unwrap_or(overview.origin_frame)))
        {
            return Err(Error::InvalidArgument);
        }
        let column_end = (span.data_column_offset as usize)
            .checked_add(span.column_count as usize)
            .ok_or(Error::LimitExceeded)?;
        aggregate_columns = aggregate_columns
            .checked_add(span.column_count as usize)
            .ok_or(Error::LimitExceeded)?;
        if aggregate_columns > limits.container.maximum_waveform_columns {
            return Err(Error::LimitExceeded);
        }
        if overview.spans[..span_index].iter().any(|previous| {
            u64::from(span.data_column_offset)
                < u64::from(previous.data_column_offset) + u64::from(previous.column_count)
                && u64::from(previous.data_column_offset) < column_end as u64
        }) {
            return Err(Error::InvalidArgument);
        }
        if column_end > overview.columns.len() {
            return Err(Error::InvalidArgument);
        }
        previous_end = Some(span.end_frame);
        previous_column = end_column;
    }
    if aggregate_columns != overview.columns.len() {
        return Err(Error::InvalidArgument);
    }
    if overview.state == FeatureState::Final && previous_end != total {
        return Err(Error::InvalidArgument);
    }
    if input.tiles.len() > limits.container.maximum_detail_tiles {
        return Err(Error::LimitExceeded);
    }
    for c in input.overview.columns {
        column(c)?
    }
    for tile in input.tiles {
        aggregate_columns = aggregate_columns
            .checked_add(tile.columns.len())
            .ok_or(Error::LimitExceeded)?;
        if aggregate_columns > limits.container.maximum_waveform_columns {
            return Err(Error::LimitExceeded);
        }
        range(
            FrameRange {
                first_frame: tile.first_frame,
                end_frame: tile.end_frame,
            },
            total,
        )?;
        for c in tile.columns {
            column(c)?
        }
    }
    let selected = input.tempo.map(|t| t.selected.tempo_millibpm);
    if let Some(t) = input.tempo {
        range(t.selected.evidence_range, total)?;
        range(t.selected.applicability_range, total)?;
        if t.selected.flags & !0xff != 0 || t.candidates.iter().any(|c| c.flags & !0xff != 0) {
            return Err(Error::Unsupported);
        }
        if !t
            .candidates
            .iter()
            .any(|c| c.tempo_millibpm == t.selected.tempo_millibpm)
        {
            return Err(Error::InvalidArgument);
        }
        for (i, c) in t.candidates.iter().enumerate() {
            if (i > 0 && c.score >= t.candidates[i - 1].score)
                || t.candidates[..i]
                    .iter()
                    .any(|p| p.tempo_millibpm == c.tempo_millibpm)
            {
                return Err(Error::InvalidArgument);
            }
        }
    }
    if let Some(g) = input.local_grid {
        let g = local_as_global(&g);
        grid::validate_external(&g, total, true)?;
        modifiers(&g, true)?;
        tempo_grid(
            &g,
            input.source.sample_rate,
            selected.ok_or(Error::InvalidArgument)?,
        )?;
    }
    if let Some(g) = input.global_grid {
        if g.segments.len() > limits.maximum_grid_segments || g.beats.len() > limits.maximum_beats {
            return Err(Error::LimitExceeded);
        }
        grid::validate_external(&g, total, true)?;
        modifiers(&g, false)?;
        tempo_grid(
            &g,
            input.source.sample_rate,
            selected.ok_or(Error::InvalidArgument)?,
        )?;
        grid::validate_external_revision(
            &input.revision.ok_or(Error::InvalidArgument)?,
            &g,
            total,
            true,
        )?;
    }
    if input
        .key
        .is_some_and(|k| k.candidates.len() > limits.maximum_key_candidates)
        || input
            .meter
            .is_some_and(|m| m.segments.len() > limits.maximum_meter_segments)
        || input.quality.len() > limits.maximum_quality_records
    {
        return Err(Error::LimitExceeded);
    }
    if input.meter.is_some_and(|m| {
        m.segments
            .first()
            .is_some_and(|s| s.downbeat_ordinal == i64::MIN)
    }) {
        return Err(Error::InvalidArgument);
    }
    validate_relations(input)?;
    Ok(size)
}

/// On success the result borrows only `output`; all input storage can be reused.
/// No allocation occurs. Failed serialization may modify output, but never
/// publishes a result view or changes the input.
pub fn finalize<'a>(
    input: &ResultInput<'_>,
    output: &'a mut [u8],
    limits: Limits,
) -> Result<ResultView<'a>, Error> {
    let size = validate(input, limits)?;
    if output.len() < size {
        return Err(Error::BufferTooSmall);
    }
    let written = result::write(input, output, limits).map_err(|error| {
        if error == Error::Corrupt {
            Error::InvalidArgument
        } else {
            error
        }
    })?;
    result::parse(&output[..written], limits)
}

/// Additional checks applied by the C streaming reader after materialization.
/// Unlike full native finalization, the C streaming path does not compare grid
/// periods against selected tempo. Input must already have passed payload parse.
pub fn validate_selected(
    input: &result::SelectedResultView<'_>,
    limits: Limits,
) -> Result<(), Error> {
    validate_selected_inner(input, limits).map_err(|error| match error {
        Error::InvalidArgument => Error::Corrupt,
        other => other,
    })
}
fn validate_selected_inner(
    input: &result::SelectedResultView<'_>,
    limits: Limits,
) -> Result<(), Error> {
    let total = input.source.total_frames;
    if let Some(w) = input.waveform.overview {
        if w.level_id != 0 {
            return Err(Error::InvalidArgument);
        }
        if w.span_count() > limits.container.maximum_overview_spans
            || w.column_count() > limits.container.maximum_waveform_columns
        {
            return Err(Error::LimitExceeded);
        }
    }
    if let Some(t) = input.tempo {
        let selected = t.selected();
        range(selected.evidence_range, total)?;
        range(selected.applicability_range, total)?;
        if selected.flags & !255 != 0 {
            return Err(Error::Unsupported);
        }
        let mut found = false;
        for i in 0..t.candidate_count() {
            let c = t.candidate(i).unwrap();
            if c.flags & !255 != 0 {
                return Err(Error::Unsupported);
            }
            if c.tempo_millibpm == selected.tempo_millibpm {
                found = true
            }
            if i > 0 && c.score >= t.candidate(i - 1).unwrap().score {
                return Err(Error::InvalidArgument);
            }
            if (0..i).any(|j| t.candidate(j).unwrap().tempo_millibpm == c.tempo_millibpm) {
                return Err(Error::InvalidArgument);
            }
        }
        if t.candidate_count() != 0 && !found {
            return Err(Error::InvalidArgument);
        }
    }
    if let Some(g) = input.local_grid {
        let g = local_as_global(&g);
        grid::validate_external(&g, total, true)?;
        modifiers(&g, true)?;
    }
    if let Some(g) = input.global_grid {
        if g.segment_count() > limits.maximum_grid_segments || g.beat_count() > limits.maximum_beats
        {
            return Err(Error::LimitExceeded);
        }
        for r in [
            g.requested_range(),
            g.evidence_range(),
            g.applicability_range(),
            g.coverage_range(),
        ] {
            range(r, total)?
        }
        let a = g.applicability_range();
        let coverage = g.coverage_range();
        if coverage.first_frame < a.first_frame || coverage.end_frame > a.end_frame {
            return Err(Error::InvalidArgument);
        }
        if g.flags() & !0x1ff != 0 {
            return Err(Error::Unsupported);
        }
        let mut elements = 0;
        for i in 0..g.segment_count() {
            let s = g.segment(i).unwrap();
            if s.flags & !0x1ff != 0 {
                return Err(Error::Unsupported);
            }
            elements |= s.flags;
            if s.segment_id == 0 || (0..i).any(|j| g.segment(j).unwrap().segment_id == s.segment_id)
            {
                return Err(Error::InvalidArgument);
            }
        }
        // Segment cursor keeps hybrid matching linear in beat + segment count.
        let mut segment_index = 0;
        for i in 0..g.beat_count() {
            let b = g.beat(i).unwrap();
            if b.flags & !0x1ff != 0 {
                return Err(Error::Unsupported);
            }
            elements |= b.flags;
            if i > 0 && g.beat(i - 1).unwrap().position == b.position {
                return Err(Error::InvalidArgument);
            }
            if g.representation() == GridRepresentation::Hybrid {
                while segment_index < g.segment_count()
                    && g.segment(segment_index)
                        .unwrap()
                        .applicability_range
                        .end_frame
                        <= b.position.whole_frame
                {
                    segment_index += 1
                }
                let s = g.segment(segment_index).ok_or(Error::InvalidArgument)?;
                if b.position.whole_frame < s.applicability_range.first_frame
                    || b.ordinal < s.anchor_ordinal
                {
                    return Err(Error::InvalidArgument);
                }
                if grid::segment_position_at_ordinal(&s, b.ordinal).ok_or(Error::LimitExceeded)?
                    != b.position
                {
                    return Err(Error::InvalidArgument);
                }
            }
        }
        if g.flags() & 0x102 != elements & 0x102 {
            return Err(Error::InvalidArgument);
        }
        if elements & 256 != 0 {
            return Err(Error::Unsupported);
        }
        let r = input.revision.ok_or(Error::InvalidArgument)?;
        if r.revision_id == r.previous_revision_id
            || r.affected_range.first_frame < a.first_frame
            || r.affected_range.end_frame > a.end_frame
            || r.flags & 2 != g.flags() & 2
        {
            return Err(Error::InvalidArgument);
        }
    }
    // C builder initializes previous_downbeat to INT64_MIN and rejects equality.
    if input
        .meter
        .is_some_and(|m| m.segment(0).is_some_and(|s| s.downbeat_ordinal == i64::MIN))
    {
        return Err(Error::InvalidArgument);
    }
    Ok(())
}

fn segment_matches(s: &GridSegment, frame: u64, ordinal: i64) -> bool {
    frame >= s.applicability_range.first_frame
        && frame < s.applicability_range.end_frame
        && grid::segment_position_at_ordinal(s, ordinal).is_some_and(|p| p.whole_frame == frame)
}
fn validate_relations(input: &ResultInput<'_>) -> Result<(), Error> {
    let mut features = result::WAVEFORM_OVERVIEW;
    if !input.tiles.is_empty() {
        features |= result::WAVEFORM_DETAIL;
    }
    if input.overview.spans.iter().any(|s| {
        input.overview.columns
            [s.data_column_offset as usize..s.data_column_offset as usize + s.column_count as usize]
            .iter()
            .any(|c| c.flags & 8 != 0)
    }) {
        features |= result::WAVEFORM_3BAND;
    }
    if input.tempo.is_some() {
        features |= result::BPM | result::CONFIDENCE;
    }
    if let Some(g) = input.local_grid {
        features |= result::LOCAL_BEATGRID;
        if g.flags & 256 != 0 {
            features |= result::GRID_LOCKING;
        }
    }
    if let Some(g) = input.global_grid {
        features |= result::GLOBAL_BEATGRID;
        if g.flags & 2 != 0 {
            features |= result::DYNAMIC_TEMPO;
        }
        if g.confidence != 255 {
            features |= result::CONFIDENCE;
        }
    }
    if let Some(k) = input.key {
        features |= result::MUSICAL_KEY;
        if k.confidence != 255 {
            features |= result::CONFIDENCE;
        }
    }
    if let Some(m) = input.meter {
        features |= result::METER_DOWNBEAT;
        if m.confidence != 255 {
            features |= result::CONFIDENCE;
        }
        let mut beat_index = 0;
        let mut segment_index = 0;
        for s in m.segments {
            if input.local_grid.is_none() && input.global_grid.is_none() {
                continue;
            }
            if input
                .local_grid
                .is_some_and(|g| segment_matches(&g.segment, s.downbeat_frame, s.downbeat_ordinal))
            {
                continue;
            }
            let Some(g) = input.global_grid else {
                return Err(Error::InvalidArgument);
            };
            while beat_index < g.beats.len()
                && (g.beats[beat_index].position.whole_frame < s.downbeat_frame
                    || (g.beats[beat_index].position.whole_frame == s.downbeat_frame
                        && g.beats[beat_index].ordinal < s.downbeat_ordinal))
            {
                beat_index += 1
            }
            if beat_index < g.beats.len()
                && g.beats[beat_index].position.whole_frame == s.downbeat_frame
                && g.beats[beat_index].ordinal == s.downbeat_ordinal
            {
                continue;
            }
            while segment_index < g.segments.len()
                && g.segments[segment_index].applicability_range.end_frame <= s.downbeat_frame
            {
                segment_index += 1
            }
            if segment_index >= g.segments.len()
                || !segment_matches(
                    &g.segments[segment_index],
                    s.downbeat_frame,
                    s.downbeat_ordinal,
                )
            {
                return Err(Error::InvalidArgument);
            }
        }
    }
    if input.quality.is_empty() {
        Ok(())
    } else {
        crate::dj::validate_quality(input.quality, true, features)
    }
}
