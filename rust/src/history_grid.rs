// SPDX-License-Identifier: Apache-2.0
//! Experimental conversion of append-only history into independent provisional
//! segments and exact supported ranges, with explicit mechanical meter binding.
//! No meter estimator, transport activation or wire publication.
use crate::{
    global_history::{HistoryView, WindowOutcome, WindowRecord, WINDOW_FRAMES},
    Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment,
};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Requirements {
    pub segments: usize,
    pub supported_ranges: usize,
    pub represented_beats: u64,
    pub unsupported_windows: usize,
}

/// Native provisional graph. Ordinals enumerate represented beats only: they
/// cannot establish musical continuity across an unsupported range. Revisions
/// belong to the caller's history/source namespace, not Session GGRD/REVN.
#[derive(Clone, Copy, Debug)]
pub struct HistoryGrid<'a> {
    pub sample_rate: u32,
    pub revision_id: u32,
    pub observed_frames: u64,
    pub inspected_frames: u64,
    pub finished: bool,
    pub capture_capacity_exhausted: bool,
    pub unsupported_windows: usize,
    pub represented_beats: u64,
    pub segments: &'a [GridSegment],
    pub supported_ranges: &'a [FrameRange],
}

pub(crate) fn prepare(
    sample_rate: u32,
    revision: u32,
    index: usize,
    ordinal: u64,
    record: &WindowRecord,
) -> Result<Option<GridSegment>, Error> {
    let WindowOutcome::Accepted(estimate) = record.outcome else {
        return Ok(None);
    };
    if !(40000..=300000).contains(&estimate.tempo_millibpm)
        || estimate.confidence > 100
        || estimate.anchor_frame < record.range.first_frame
        || estimate.anchor_frame >= record.range.end_frame
    {
        return Err(Error::InvalidArgument);
    }
    let numerator = u64::from(sample_rate) * 60000;
    let tempo = u64::from(estimate.tempo_millibpm);
    let period = (u128::from(numerator) << 32) / u128::from(tempo);
    if period < (1u128 << 32) {
        return Err(Error::InvalidArgument);
    }
    let available = u128::from(record.range.end_frame - estimate.anchor_frame) << 32;
    let count = u32::try_from((available - 1) / period + 1).map_err(|_| Error::LimitExceeded)?;
    let segment = GridSegment {
        applicability_range: record.range,
        anchor_position: FractionalFrame {
            whole_frame: estimate.anchor_frame,
            fraction_q32: 0,
        },
        anchor_ordinal: i64::try_from(ordinal).map_err(|_| Error::LimitExceeded)?,
        frames_per_beat: FramePeriod {
            whole_frames: (period >> 32) as u64,
            fraction_q32: period as u32,
        },
        beat_count: count,
        nominal_tempo_millibpm: estimate.tempo_millibpm,
        confidence: estimate.confidence,
        state: FeatureState::Provisional,
        flags: 1, // Provisional phase: capture acceptance is not accuracy acceptance.
        segment_id: u32::try_from(index)
            .ok()
            .and_then(|i| i.checked_add(1))
            .ok_or(Error::LimitExceeded)?,
        revision,
    };
    let next = ordinal
        .checked_add(u64::from(count))
        .ok_or(Error::LimitExceeded)?;
    i64::try_from(next).map_err(|_| Error::LimitExceeded)?;
    // Check the same bounded arithmetic used by native consumers, including
    // the last fractional carry, before exposing any output.
    segment.beat_at(count - 1)?;
    Ok(Some(segment))
}

/// Validate the complete history before sizing or touching output. A public
/// HistoryView may be constructed externally; never trust its record geometry.
pub fn requirements(history: HistoryView<'_>, revision_id: u32) -> Result<Requirements, Error> {
    if revision_id == 0 || history.sample_rate == 0 || history.sample_rate > 768000 {
        return Err(Error::InvalidArgument);
    }
    if history.discontinuous_input {
        return Err(Error::InvalidState);
    }
    if history.inspected_frames > history.observed_frames
        || (history.finished && history.inspected_frames != history.observed_frames)
        || (history.capacity_exhausted && history.inspected_frames == history.observed_frames)
    {
        return Err(Error::InvalidArgument);
    }
    let mut next = 0;
    let mut required = Requirements::default();
    for (index, record) in history.records.iter().enumerate() {
        if record.range.first_frame != next
            || record.range.end_frame <= next
            || record.range.end_frame > history.inspected_frames
        {
            return Err(Error::InvalidArgument);
        }
        let length = record.range.end_frame - next;
        if length > WINDOW_FRAMES
            || (length != WINDOW_FRAMES
                && (!history.finished || index + 1 != history.records.len()))
        {
            return Err(Error::InvalidArgument);
        }
        if let Some(segment) = prepare(
            history.sample_rate,
            revision_id,
            index,
            required.represented_beats,
            record,
        )? {
            required.segments += 1;
            required.supported_ranges += 1;
            required.represented_beats += u64::from(segment.beat_count);
        } else {
            required.unsupported_windows += 1;
        }
        next = record.range.end_frame;
    }
    if next != history.inspected_frames {
        return Err(Error::InvalidArgument);
    }
    Ok(required)
}

/// Copy into independent caller storage. Failure leaves both output slices
/// unchanged, including capacity or malformed-data failures. Extra capacity is
/// untouched. Repeating conversion with the same revision preserves the exact
/// earlier segment prefix when the history is appended. A changed interpretation
/// requires the caller to choose a new revision and preserve its old graph.
pub fn convert_into<'a>(
    history: HistoryView<'_>,
    revision_id: u32,
    segments: &'a mut [GridSegment],
    ranges: &'a mut [FrameRange],
) -> Result<HistoryGrid<'a>, Error> {
    let required = requirements(history, revision_id)?;
    if segments.len() < required.segments || ranges.len() < required.supported_ranges {
        return Err(Error::BufferTooSmall);
    }
    let mut count = 0;
    let mut ordinal = 0;
    for (index, record) in history.records.iter().enumerate() {
        // Deterministic second pass over the same immutable input. All failure
        // conditions and ordinal arithmetic were checked before the first write.
        if let Some(segment) = prepare(history.sample_rate, revision_id, index, ordinal, record)? {
            ordinal += u64::from(segment.beat_count);
            segments[count] = segment;
            ranges[count] = record.range;
            count += 1;
        }
    }
    Ok(HistoryGrid {
        sample_rate: history.sample_rate,
        revision_id,
        observed_frames: history.observed_frames,
        inspected_frames: history.inspected_frames,
        finished: history.finished,
        capture_capacity_exhausted: history.capacity_exhausted,
        unsupported_windows: required.unsupported_windows,
        represented_beats: required.represented_beats,
        segments: &segments[..count],
        supported_ranges: &ranges[..count],
    })
}

/// Mechanical association only: a matching coordinate does not prove that the
/// supplied beat is musically a downbeat. State is capped at provisional;
/// an incomplete input label remains partial.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BoundMeterSegment {
    pub meter: crate::MeterSegment,
    pub grid_segment_id: u32,
    pub grid_revision_id: u32,
    pub downbeat_position: FractionalFrame,
}

#[derive(Clone, Copy, Debug)]
pub struct BoundMeter<'a> {
    pub sample_rate: u32,
    pub grid_revision_id: u32,
    pub segments: &'a [BoundMeterSegment],
}

pub(crate) fn validate_converted_grid(grid: HistoryGrid<'_>) -> Result<(), Error> {
    if grid.sample_rate == 0
        || grid.sample_rate > 768000
        || grid.revision_id == 0
        || grid.segments.len() != grid.supported_ranges.len()
        || grid.inspected_frames > grid.observed_frames
        || (grid.finished && grid.inspected_frames != grid.observed_frames)
        || (grid.capture_capacity_exhausted && grid.inspected_frames == grid.observed_frames)
        || (!grid.finished && grid.inspected_frames % WINDOW_FRAMES != 0)
    {
        return Err(Error::InvalidArgument);
    }
    let mut end = 0;
    let mut ordinal = 0;
    for (segment, range) in grid.segments.iter().zip(grid.supported_ranges) {
        if *range != segment.applicability_range
            || range.first_frame < end
            || range.first_frame % WINDOW_FRAMES != 0
            || range.end_frame <= range.first_frame
            || range.end_frame > grid.inspected_frames
            || (range.end_frame - range.first_frame != WINDOW_FRAMES
                && (!grid.finished || range.end_frame != grid.inspected_frames))
            || range.end_frame - range.first_frame > WINDOW_FRAMES
        {
            return Err(Error::InvalidArgument);
        }
        let index =
            usize::try_from(range.first_frame / WINDOW_FRAMES).map_err(|_| Error::LimitExceeded)?;
        let record = WindowRecord {
            range: *range,
            outcome: WindowOutcome::Accepted(crate::global_history::WindowEstimate {
                anchor_frame: segment.anchor_position.whole_frame,
                tempo_millibpm: segment.nominal_tempo_millibpm,
                confidence: segment.confidence,
            }),
        };
        let expected = prepare(grid.sample_rate, grid.revision_id, index, ordinal, &record)?;
        if expected.as_ref() != Some(segment) {
            return Err(Error::InvalidArgument);
        }
        ordinal += u64::from(segment.beat_count);
        end = range.end_frame;
    }
    let windows = grid.inspected_frames.div_ceil(WINDOW_FRAMES);
    if ordinal != grid.represented_beats
        || windows.checked_sub(grid.segments.len() as u64) != Some(grid.unsupported_windows as u64)
    {
        return Err(Error::InvalidArgument);
    }
    Ok(())
}

fn bind_one(
    grid: HistoryGrid<'_>,
    meter: crate::MeterSegment,
    cursor: &mut usize,
) -> Result<BoundMeterSegment, Error> {
    if meter.first_frame >= meter.end_frame
        || meter.downbeat_frame < meter.first_frame
        || meter.downbeat_frame >= meter.end_frame
        || !(1..=32).contains(&meter.numerator)
        || !(1..=32).contains(&meter.denominator)
        || !meter.denominator.is_power_of_two()
        || (meter.confidence > 100 && meter.confidence != 255)
        || !matches!(
            meter.state,
            FeatureState::Partial
                | FeatureState::Provisional
                | FeatureState::Stable
                | FeatureState::Final
        )
        || meter.segment_id == 0
    {
        return Err(Error::InvalidArgument);
    }
    while *cursor < grid.segments.len()
        && grid.segments[*cursor].applicability_range.end_frame <= meter.first_frame
    {
        *cursor += 1;
    }
    let segment = grid.segments.get(*cursor).ok_or(Error::Conflict)?;
    // Each captured window is an independent phase hypothesis. Even adjacent
    // windows do not authorize a meter range spanning both hypotheses.
    if meter.first_frame < segment.applicability_range.first_frame
        || meter.end_frame > segment.applicability_range.end_frame
    {
        return Err(Error::Conflict);
    }
    let index = meter
        .downbeat_ordinal
        .checked_sub(segment.anchor_ordinal)
        .and_then(|n| u32::try_from(n).ok())
        .ok_or(Error::Conflict)?;
    let beat = segment.beat_at(index)?.ok_or(Error::Conflict)?;
    // MTRD uses the whole frame component, not rounded Q32 milliseconds/frames.
    if beat.position.whole_frame != meter.downbeat_frame {
        return Err(Error::Conflict);
    }
    Ok(BoundMeterSegment {
        meter: crate::MeterSegment {
            state: if meter.state == FeatureState::Partial {
                FeatureState::Partial
            } else {
                FeatureState::Provisional
            },
            ..meter
        },
        grid_segment_id: segment.segment_id,
        grid_revision_id: grid.revision_id,
        downbeat_position: beat.position,
    })
}

/// Bind labels already expressed in the selected grid's ordinal namespace.
/// Local-grid ordinals are never silently reinterpreted. All input is validated
/// before output is touched; errors leave every output element unchanged.
/// No output borrows the input grid or labels, only the caller's output buffer.
pub fn bind_meter_into<'a>(
    grid: HistoryGrid<'_>,
    expected_revision: u32,
    labels: &[crate::MeterSegment],
    output: &'a mut [BoundMeterSegment],
) -> Result<BoundMeter<'a>, Error> {
    if expected_revision == 0 {
        return Err(Error::InvalidArgument);
    }
    if expected_revision != grid.revision_id {
        return Err(Error::Conflict);
    }
    validate_converted_grid(grid)?;
    let mut cursor = 0;
    let mut previous: Option<crate::MeterSegment> = None;
    for &label in labels {
        if previous.is_some_and(|p| {
            p.end_frame > label.first_frame
                || p.segment_id >= label.segment_id
                || p.downbeat_ordinal >= label.downbeat_ordinal
        }) {
            return Err(Error::InvalidArgument);
        }
        bind_one(grid, label, &mut cursor)?;
        previous = Some(label);
    }
    if output.len() < labels.len() {
        return Err(Error::BufferTooSmall);
    }
    cursor = 0;
    for (&label, destination) in labels.iter().zip(output.iter_mut()) {
        // Same immutable grid/labels and deterministic cursor as the preflight.
        *destination = bind_one(grid, label, &mut cursor)?;
    }
    Ok(BoundMeter {
        sample_rate: grid.sample_rate,
        grid_revision_id: grid.revision_id,
        segments: &output[..labels.len()],
    })
}
