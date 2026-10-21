// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-s6-history")]
use libapta::{
    global_history::{HistoryView, WindowEstimate, WindowOutcome, WindowRecord, WINDOW_FRAMES},
    history_grid::{bind_meter_into, convert_into, BoundMeterSegment, HistoryGrid},
    Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment, MeterSegment,
};

fn empty_segment() -> GridSegment {
    GridSegment {
        applicability_range: FrameRange::default(),
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod::default(),
        beat_count: 0,
        nominal_tempo_millibpm: 0,
        confidence: 0,
        state: FeatureState::Provisional,
        flags: 0,
        segment_id: 0,
        revision: 0,
    }
}
fn with_grid(gapped: bool, test: impl FnOnce(HistoryGrid<'_>)) {
    let records: Vec<_> = (0..3)
        .map(|i| WindowRecord {
            range: FrameRange {
                first_frame: i * WINDOW_FRAMES,
                end_frame: (i + 1) * WINDOW_FRAMES,
            },
            outcome: if gapped && i == 1 {
                WindowOutcome::Rejected
            } else {
                WindowOutcome::Accepted(WindowEstimate {
                    anchor_frame: i * WINDOW_FRAMES + 2048,
                    tempo_millibpm: 127999,
                    confidence: 80,
                })
            },
        })
        .collect();
    let view = HistoryView {
        sample_rate: 48000,
        records: &records,
        observed_frames: 3 * WINDOW_FRAMES,
        inspected_frames: 3 * WINDOW_FRAMES,
        capacity_exhausted: false,
        discontinuous_input: false,
        finished: true,
    };
    let mut segments = [empty_segment(); 3];
    let mut ranges = [FrameRange::default(); 3];
    test(convert_into(view, 9, &mut segments, &mut ranges).unwrap());
}
// Explicit synthetic label fixture, not output from the meter estimator.
fn label(segment: GridSegment, index: u32, id: u32) -> MeterSegment {
    let beat = segment.beat_at(index).unwrap().unwrap();
    MeterSegment {
        first_frame: segment.applicability_range.first_frame,
        end_frame: segment.applicability_range.end_frame,
        downbeat_frame: beat.position.whole_frame,
        downbeat_ordinal: beat.ordinal,
        numerator: 4,
        denominator: 4,
        state: FeatureState::Final,
        confidence: 80,
        segment_id: id,
    }
}
fn storage(meter: MeterSegment) -> BoundMeterSegment {
    BoundMeterSegment {
        meter,
        grid_segment_id: u32::MAX,
        grid_revision_id: u32::MAX,
        downbeat_position: FractionalFrame::default(),
    }
}
fn rejects(grid: HistoryGrid<'_>, revision: u32, labels: &[MeterSegment], expected: Error) {
    let sentinel = storage(label(grid.segments[0], 0, 1));
    let mut output = [sentinel; 4];
    assert!(
        matches!(bind_meter_into(grid, revision, labels, &mut output), Err(error) if error == expected)
    );
    assert_eq!(output, [sentinel; 4]);
}

#[test]
fn meter_binding_keeps_exact_fractional_position_revision_and_provisional_state() {
    with_grid(true, |grid| {
        let mut labels = [label(grid.segments[0], 3, 1), label(grid.segments[1], 4, 2)];
        labels[1].confidence = 255;
        labels[1].state = FeatureState::Partial;
        let sentinel = storage(labels[0]);
        let mut output = [sentinel; 3];
        let bound = bind_meter_into(grid, 9, &labels, &mut output).unwrap();
        assert_eq!(bound.grid_revision_id, 9);
        assert_eq!(bound.sample_rate, 48000);
        for (i, record) in bound.segments.iter().enumerate() {
            assert_eq!(
                record.meter,
                MeterSegment {
                    state: if labels[i].state == FeatureState::Partial {
                        FeatureState::Partial
                    } else {
                        FeatureState::Provisional
                    },
                    ..labels[i]
                }
            );
            assert_eq!(record.grid_segment_id, if i == 0 { 1 } else { 3 });
            assert_eq!(record.grid_revision_id, 9);
            let beat = grid.segments[i]
                .beat_at(if i == 0 { 3 } else { 4 })
                .unwrap()
                .unwrap();
            assert_eq!(record.downbeat_position, beat.position);
            assert_ne!(record.downbeat_position.fraction_q32, 0);
        }
        assert_eq!(output[2], sentinel);
    });
}

#[test]
fn meter_binding_rejects_wrong_revision_coordinates_ordinals_and_gap() {
    with_grid(true, |grid| {
        let labels = [label(grid.segments[0], 3, 1), label(grid.segments[1], 4, 2)];
        rejects(grid, 8, &labels, Error::Conflict);
        rejects(grid, 0, &labels, Error::InvalidArgument);
        for change in 0..5 {
            let mut changed = labels;
            match change {
                0 => changed[1].downbeat_frame += 1,
                1 => changed[1].downbeat_ordinal += 1,
                2 => changed[1].downbeat_ordinal = i64::MAX,
                3 => changed[1].first_frame = WINDOW_FRAMES,
                _ => {
                    changed[1].first_frame = WINDOW_FRAMES;
                    changed[1].end_frame = 2 * WINDOW_FRAMES;
                    changed[1].downbeat_frame = WINDOW_FRAMES + 2048;
                }
            }
            rejects(grid, 9, &changed, Error::Conflict);
        }
        let mut changed = labels;
        changed[0].downbeat_ordinal = -1;
        rejects(grid, 9, &changed[..1], Error::Conflict);
    });
    with_grid(false, |grid| {
        let mut wide = label(grid.segments[0], 3, 1);
        wide.end_frame = grid.segments[1].applicability_range.end_frame;
        rejects(grid, 9, &[wide], Error::Conflict);
    });
}

#[test]
fn meter_binding_invalid_labels_and_short_capacity_preserve_output() {
    with_grid(true, |grid| {
        let labels = [label(grid.segments[0], 3, 1), label(grid.segments[1], 4, 2)];
        for change in 0..9 {
            let mut invalid = labels;
            match change {
                0 => invalid[1].end_frame = invalid[1].first_frame,
                1 => invalid[1].downbeat_frame = invalid[1].end_frame,
                2 => invalid[1].numerator = 0,
                3 => invalid[1].denominator = 3,
                4 => invalid[1].confidence = 101,
                5 => invalid[1].segment_id = 0,
                6 => invalid[1].segment_id = invalid[0].segment_id,
                7 => invalid[1].first_frame = invalid[0].first_frame,
                _ => invalid[1].downbeat_ordinal = invalid[0].downbeat_ordinal,
            }
            rejects(grid, 9, &invalid, Error::InvalidArgument);
        }
        let sentinel = storage(labels[0]);
        let mut output = [sentinel];
        assert!(matches!(
            bind_meter_into(grid, 9, &labels, &mut output),
            Err(Error::BufferTooSmall)
        ));
        assert_eq!(output, [sentinel]);
        assert!(bind_meter_into(grid, 9, &[], &mut [])
            .unwrap()
            .segments
            .is_empty());
    });
}

#[test]
fn meter_binding_validates_public_grid_before_using_it() {
    with_grid(true, |grid| {
        let labels = [label(grid.segments[0], 3, 1)];
        for change in 0..10 {
            let mut segments = grid.segments.to_vec();
            let mut ranges = grid.supported_ranges.to_vec();
            let mut invalid = grid;
            match change {
                0 => segments[1].revision += 1,
                1 => segments[1].frames_per_beat.fraction_q32 += 1,
                2 => segments[1].beat_count += 1,
                3 => segments[1].anchor_ordinal += 1,
                4 => ranges[1].first_frame += 1,
                5 => segments[1].state = FeatureState::Final,
                6 => invalid.represented_beats += 1,
                7 => invalid.unsupported_windows = 0,
                8 => invalid.sample_rate = 0,
                _ => invalid.inspected_frames -= 1,
            }
            invalid.segments = &segments;
            invalid.supported_ranges = &ranges;
            rejects(invalid, 9, &labels, Error::InvalidArgument);
        }
    });
}

#[test]
fn meter_binding_output_outlives_grid_and_old_revision_remains_unchanged() {
    let template = MeterSegment {
        first_frame: 0,
        end_frame: 1,
        downbeat_frame: 0,
        downbeat_ordinal: 0,
        numerator: 4,
        denominator: 4,
        state: FeatureState::Provisional,
        confidence: 0,
        segment_id: 1,
    };
    let mut saved = [storage(template)];
    with_grid(true, |grid| {
        bind_meter_into(grid, 9, &[label(grid.segments[0], 3, 1)], &mut saved).unwrap();
    });
    let retained = saved;
    with_grid(true, |grid| {
        let mut revised_segments = grid.segments.to_vec();
        for segment in &mut revised_segments {
            segment.revision = 10;
        }
        let revised = HistoryGrid {
            revision_id: 10,
            segments: &revised_segments,
            ..grid
        };
        let labels = [label(revised.segments[0], 3, 1)];
        rejects(revised, 9, &labels, Error::Conflict);
        let mut next = [storage(template)];
        bind_meter_into(revised, 10, &labels, &mut next).unwrap();
        assert_eq!(next[0].grid_revision_id, 10);
    });
    assert_eq!(saved, retained);
    assert_eq!(saved[0].grid_revision_id, 9);
}
