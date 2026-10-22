// SPDX-License-Identifier: Apache-2.0
//! Independent provisional history copies. No wire export or state promotion.
use crate::{
    global_history::{HistoryView, WindowOutcome},
    history_grid::{self, BoundMeterSegment, HistoryGrid},
    history_meter::{MeterHistoryView, MeterOutcome, MeterRecord, MAX_METER_BEATS},
    Error, FeatureState, FrameRange, GridSegment, MeterSegment, ResultSessionState, SourceInfo,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PublicationKey {
    /// Caller-authenticated identity of the PCM source; no hashing is performed.
    pub source: SourceInfo,
    pub lineage: [u64; 2],
    pub generation: u64,
    pub grid_revision: u32,
    /// Actual producer lifecycle, supplied by the source owner.
    pub state: ResultSessionState,
}

pub struct PublicationStorage<'a> {
    pub segments: &'a mut [GridSegment],
    pub ranges: &'a mut [FrameRange],
    pub meters: &'a mut [BoundMeterSegment],
    pub meter_records: &'a mut [MeterRecord],
}

#[derive(Clone, Copy, Debug)]
pub struct Publication<'a> {
    key: PublicationKey,
    grid: HistoryGrid<'a>,
    meters: &'a [BoundMeterSegment],
    records: &'a [MeterRecord],
    meter_inspected_frames: u64,
    meter_finished: bool,
    meter_capacity_exhausted: bool,
}

impl<'a> Publication<'a> {
    pub fn key(&self) -> PublicationKey {
        self.key
    }
    pub fn grid(&self) -> HistoryGrid<'a> {
        self.grid
    }
    pub fn meters(&self) -> &'a [BoundMeterSegment] {
        self.meters
    }
    pub fn meter_records(&self) -> &'a [MeterRecord] {
        self.records
    }
    pub fn meter_inspected_frames(&self) -> u64 {
        self.meter_inspected_frames
    }
    pub fn meter_finished(&self) -> bool {
        self.meter_finished
    }
    pub fn meter_capacity_exhausted(&self) -> bool {
        self.meter_capacity_exhausted
    }

    /// Replace a diagnostic view only within the same source/lineage. Both
    /// backing banks remain pinned by their borrows; retained copies stay valid.
    /// This is not product playback adoption or permission to promote state.
    pub fn upgrade(&mut self, next: Self) -> Result<(), Error> {
        if matches!(
            self.key.state,
            ResultSessionState::Cancelled | ResultSessionState::Failed
        ) || (self.key.state == ResultSessionState::Completed
            && next.key.state != ResultSessionState::Completed)
        {
            return Err(Error::InvalidState);
        }
        if self.key.source != next.key.source || self.key.lineage != next.key.lineage {
            return Err(Error::Conflict);
        }
        if next.key.generation <= self.key.generation
            || next.key.grid_revision < self.key.grid_revision
            || next.grid.observed_frames < self.grid.observed_frames
            || next.grid.inspected_frames < self.grid.inspected_frames
            || next.meter_inspected_frames < self.meter_inspected_frames
            || (self.grid.finished && !next.grid.finished)
            || (self.meter_finished && !next.meter_finished)
        {
            return Err(Error::Conflict);
        }
        if next.key.grid_revision == self.key.grid_revision
            && (!next.grid.segments.starts_with(self.grid.segments)
                || !next
                    .grid
                    .supported_ranges
                    .starts_with(self.grid.supported_ranges)
                || !next.meters.starts_with(self.meters)
                || !next.records.starts_with(self.records))
        {
            return Err(Error::Conflict);
        }
        *self = next;
        Ok(())
    }
}

fn check_pair(history: HistoryView<'_>, meter: MeterHistoryView<'_>) -> Result<usize, Error> {
    if meter.discontinuous_input {
        return Err(Error::InvalidState);
    }
    if meter.sample_rate != history.sample_rate
        || meter.observed_frames != history.observed_frames
        || meter.inspected_frames > history.inspected_frames
        || meter.records.len() > history.records.len()
        || (meter.finished
            && (!history.finished || meter.inspected_frames != meter.observed_frames))
        || (meter.capacity_exhausted && meter.inspected_frames == meter.observed_frames)
    {
        return Err(Error::InvalidArgument);
    }
    let mut end = 0;
    let mut count = 0;
    for (record, window) in meter.records.iter().zip(history.records) {
        if record.window != *window {
            return Err(Error::Conflict);
        }
        let segment = history_grid::prepare(history.sample_rate, 1, 0, 0, window)?;
        let valid = match (record.outcome, segment) {
            (MeterOutcome::Estimated(estimate), Some(segment)) => {
                count += 1;
                (8..=MAX_METER_BEATS as u32).contains(&segment.beat_count)
                    && u32::from(estimate.observed_beats) == segment.beat_count
                    && matches!(estimate.numerator, 3 | 4)
                    && estimate.downbeat_index < u32::from(estimate.numerator)
                    && estimate.confidence < 50
            }
            (MeterOutcome::GridUnsupported, None) => true,
            (MeterOutcome::InsufficientBeats, Some(segment)) => segment.beat_count < 8,
            (MeterOutcome::BeatLimit, Some(segment)) => {
                segment.beat_count as usize > MAX_METER_BEATS
            }
            (MeterOutcome::Ambiguous | MeterOutcome::Unavailable, Some(_)) => true,
            _ => false,
        };
        if !valid {
            return Err(Error::InvalidArgument);
        }
        end = window.range.end_frame;
    }
    if end != meter.inspected_frames {
        return Err(Error::InvalidArgument);
    }
    Ok(count)
}

/// Validate and copy a complete experimental graph into independent bounded
/// storage. Every error leaves all four destination arrays untouched. Private
/// source identity is supplied/verified by the caller, not inferred from audio.
pub fn publish_into<'a>(
    key: PublicationKey,
    history: HistoryView<'_>,
    meter: MeterHistoryView<'_>,
    storage: PublicationStorage<'a>,
) -> Result<Publication<'a>, Error> {
    let source = key.source;
    if source.sample_rate != history.sample_rate
        || !(1..=8).contains(&source.channel_count)
        || source.channel_layout > 2
        || (source.channel_layout != 0 && source.channel_layout != source.channel_count)
        || !(1..=2).contains(&source.fingerprint_kind)
        || source.total_frames == Some(u64::MAX)
        || source
            .total_frames
            .is_some_and(|total| total < history.observed_frames)
        || (history.finished && source.total_frames != Some(history.observed_frames))
        || key.generation == 0
        || key.lineage == [0; 2]
        || key.state == ResultSessionState::Created
        || (key.state == ResultSessionState::Completed
            && ((!history.finished && !history.capacity_exhausted)
                || (!meter.finished && !meter.capacity_exhausted && !history.capacity_exhausted)))
    {
        return Err(Error::InvalidArgument);
    }
    let required = history_grid::requirements(history, key.grid_revision)?;
    let meter_count = check_pair(history, meter)?;
    if storage.segments.len() < required.segments
        || storage.ranges.len() < required.supported_ranges
        || storage.meters.len() < meter_count
        || storage.meter_records.len() < meter.records.len()
    {
        return Err(Error::BufferTooSmall);
    }
    // All second-pass failures have been preflighted on these immutable inputs.
    let grid =
        history_grid::convert_into(history, key.grid_revision, storage.segments, storage.ranges)?;
    let mut output = 0;
    let mut segment_index = 0;
    for (index, window) in history.records.iter().enumerate() {
        if !matches!(window.outcome, WindowOutcome::Accepted(_)) {
            continue;
        }
        let segment = grid.segments[segment_index];
        segment_index += 1;
        let Some(record) = meter.records.get(index) else {
            continue;
        };
        if let MeterOutcome::Estimated(estimate) = record.outcome {
            let beat = segment
                .beat_at(estimate.downbeat_index)?
                .ok_or(Error::Internal)?;
            storage.meters[output] = BoundMeterSegment {
                meter: MeterSegment {
                    first_frame: window.range.first_frame,
                    end_frame: window.range.end_frame,
                    downbeat_frame: beat.position.whole_frame,
                    downbeat_ordinal: beat.ordinal,
                    numerator: estimate.numerator,
                    denominator: 4,
                    state: FeatureState::Provisional,
                    confidence: estimate.confidence,
                    segment_id: segment.segment_id,
                },
                grid_segment_id: segment.segment_id,
                grid_revision_id: key.grid_revision,
                downbeat_position: beat.position,
            };
            output += 1;
        }
    }
    storage.meter_records[..meter.records.len()].copy_from_slice(meter.records);
    Ok(Publication {
        key,
        grid,
        meters: &storage.meters[..output],
        records: &storage.meter_records[..meter.records.len()],
        meter_inspected_frames: meter.inspected_frames,
        meter_finished: meter.finished,
        meter_capacity_exhausted: meter.capacity_exhausted,
    })
}
