// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-s6-history")]
use libapta::{
    global_history::{HistoryView, WindowEstimate, WindowOutcome, WindowRecord, WINDOW_FRAMES},
    history_grid::{convert_into, requirements},
    Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment,
};

fn segment_storage() -> GridSegment {
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
fn record(index: u64, tempo: Option<u32>) -> WindowRecord {
    WindowRecord {
        range: FrameRange {
            first_frame: index * WINDOW_FRAMES,
            end_frame: (index + 1) * WINDOW_FRAMES,
        },
        outcome: tempo.map_or(WindowOutcome::Rejected, |tempo_millibpm| {
            WindowOutcome::Accepted(WindowEstimate {
                anchor_frame: index * WINDOW_FRAMES + 2048,
                tempo_millibpm,
                confidence: 80,
            })
        }),
    }
}
fn view(records: &[WindowRecord]) -> HistoryView<'_> {
    let end = records.last().map_or(0, |r| r.range.end_frame);
    HistoryView {
        sample_rate: 48000,
        records,
        observed_frames: end,
        inspected_frames: end,
        capacity_exhausted: false,
        discontinuous_input: false,
        finished: true,
    }
}

#[test]
fn history_grid_exact_q32_ordinals_ranges_and_gaps() {
    let records = [
        record(0, Some(127999)),
        record(1, None),
        record(2, Some(96001)),
    ];
    let mut segments = [segment_storage(); 3];
    let mut ranges = [FrameRange::default(); 3];
    let grid = convert_into(view(&records), 7, &mut segments, &mut ranges).unwrap();
    assert_eq!(grid.segments.len(), 2);
    assert_eq!(grid.unsupported_windows, 1);
    assert_eq!(grid.supported_ranges, [records[0].range, records[2].range]);
    assert!(grid.finished);
    let mut ordinal = 0;
    let mut previous = 0u128;
    for (i, s) in grid.segments.iter().enumerate() {
        assert_eq!(s.anchor_ordinal, ordinal);
        assert_eq!(s.segment_id, if i == 0 { 1 } else { 3 });
        assert_eq!(s.revision, 7);
        assert_eq!(s.state, FeatureState::Provisional);
        assert_eq!(s.flags, 1);
        let period = ((48000u128 * 60000) << 32) / u128::from(s.nominal_tempo_millibpm);
        let anchor = u128::from(s.anchor_position.whole_frame) << 32;
        for n in 0..s.beat_count {
            let beat = s.beat_at(n).unwrap().unwrap();
            let expected = anchor + u128::from(n) * period;
            let actual = (u128::from(beat.position.whole_frame) << 32)
                | u128::from(beat.position.fraction_q32);
            assert_eq!(actual, expected);
            assert!(actual > previous);
            previous = actual;
            assert_eq!(beat.ordinal, ordinal);
            ordinal += 1;
            assert!(beat.position.whole_frame >= s.applicability_range.first_frame);
            assert!(beat.position.whole_frame < s.applicability_range.end_frame);
        }
        assert!(
            anchor + u128::from(s.beat_count) * period
                >= u128::from(s.applicability_range.end_frame) << 32
        );
        assert_eq!(s.beat_at(s.beat_count), Ok(None));
    }
    assert_eq!(grid.represented_beats, ordinal as u64);
}

#[test]
fn history_grid_append_preserves_exact_prefix_and_revision_is_explicit() {
    let records = [
        record(0, Some(123456)),
        record(1, None),
        record(2, Some(123457)),
    ];
    let mut old_s = [segment_storage(); 1];
    let mut old_r = [FrameRange::default(); 1];
    let old = convert_into(view(&records[..1]), 11, &mut old_s, &mut old_r).unwrap();
    let mut new_s = [segment_storage(); 2];
    let mut new_r = [FrameRange::default(); 2];
    let new = convert_into(view(&records), 11, &mut new_s, &mut new_r).unwrap();
    assert_eq!(new.segments[0], old.segments[0]);
    assert_eq!(new.supported_ranges[0], old.supported_ranges[0]);
    let retained = old.segments[0];
    let mut revised_s = [segment_storage(); 2];
    let mut revised_r = [FrameRange::default(); 2];
    let revised = convert_into(view(&records), 12, &mut revised_s, &mut revised_r).unwrap();
    assert_eq!(revised.segments[0].revision, 12);
    assert_eq!(old.segments[0], retained);
    assert_eq!(old.revision_id, 11);
    let mut independent_s = [segment_storage(); 1];
    let mut independent_r = [FrameRange::default(); 1];
    let independent = {
        let temporary_input = vec![record(0, Some(123456))];
        convert_into(
            view(&temporary_input),
            11,
            &mut independent_s,
            &mut independent_r,
        )
        .unwrap()
    };
    assert_eq!(independent.segments[0], retained);
}

fn untouched(history: HistoryView<'_>, revision: u32, error: Error) {
    let sentinel = record(9, None).range;
    let mut segments = [segment_storage(); 4];
    let before = segments;
    let mut ranges = [sentinel; 4];
    assert!(
        matches!(convert_into(history, revision, &mut segments, &mut ranges), Err(e) if e == error)
    );
    assert_eq!(segments, before);
    assert_eq!(ranges, [sentinel; 4]);
}

#[test]
fn history_grid_malformed_input_is_transactional() {
    let valid = [record(0, Some(120000)), record(1, Some(120000))];
    for bad in 0..9 {
        let mut records = valid;
        match bad {
            0 => records[1].range.first_frame += 1,
            1 => records[1].range.end_frame = records[1].range.first_frame,
            2 => records[1].range.end_frame += 1,
            3 => {
                records[1].outcome = WindowOutcome::Accepted(WindowEstimate {
                    anchor_frame: 0,
                    tempo_millibpm: 120000,
                    confidence: 80,
                })
            }
            4 => {
                records[1].outcome = WindowOutcome::Accepted(WindowEstimate {
                    anchor_frame: records[1].range.end_frame,
                    tempo_millibpm: 120000,
                    confidence: 80,
                })
            }
            5 => {
                records[1].outcome = WindowOutcome::Accepted(WindowEstimate {
                    anchor_frame: records[1].range.first_frame,
                    tempo_millibpm: 0,
                    confidence: 80,
                })
            }
            6 => {
                records[1].outcome = WindowOutcome::Accepted(WindowEstimate {
                    anchor_frame: records[1].range.first_frame,
                    tempo_millibpm: 300001,
                    confidence: 80,
                })
            }
            7 => {
                records[1].outcome = WindowOutcome::Accepted(WindowEstimate {
                    anchor_frame: records[1].range.first_frame,
                    tempo_millibpm: 120000,
                    confidence: 101,
                })
            }
            _ => records[0].range.end_frame -= 1,
        }
        untouched(view(&records), 1, Error::InvalidArgument);
    }
    untouched(view(&valid), 0, Error::InvalidArgument);
    let mut invalid = view(&valid);
    invalid.discontinuous_input = true;
    untouched(invalid, 1, Error::InvalidState);
    invalid = view(&valid);
    invalid.inspected_frames -= 1;
    untouched(invalid, 1, Error::InvalidArgument);
    invalid = view(&valid);
    invalid.sample_rate = 0;
    untouched(invalid, 1, Error::InvalidArgument);
    invalid.sample_rate = 1;
    untouched(invalid, 1, Error::InvalidArgument);
    invalid = view(&valid);
    invalid.capacity_exhausted = true;
    untouched(invalid, 1, Error::InvalidArgument);
}

#[test]
fn history_grid_capacity_empty_and_uninspected_tail_are_explicit() {
    let records = [record(0, Some(120000)), record(1, Some(120000))];
    for insufficient_segments in [false, true] {
        let mut segments = [segment_storage(); 2];
        let mut ranges = [FrameRange::default(); 2];
        let before = segments;
        let error = if insufficient_segments {
            convert_into(view(&records), 1, &mut segments[..1], &mut ranges)
        } else {
            convert_into(view(&records), 1, &mut segments, &mut ranges[..1])
        };
        assert!(matches!(error, Err(Error::BufferTooSmall)));
        assert_eq!(segments, before);
        assert_eq!(ranges, [FrameRange::default(); 2]);
    }
    let mut partial = view(&records);
    partial.observed_frames += WINDOW_FRAMES;
    partial.finished = false;
    partial.capacity_exhausted = true;
    let mut segments = [segment_storage(); 3];
    let mut ranges = [FrameRange::default(); 3];
    let grid = convert_into(partial, 1, &mut segments, &mut ranges).unwrap();
    assert!(!grid.finished);
    assert!(grid.capture_capacity_exhausted);
    assert!(grid.inspected_frames < grid.observed_frames);
    let empty = convert_into(view(&[]), 1, &mut [], &mut []).unwrap();
    assert!(empty.segments.is_empty());
    let rejected = [record(0, None)];
    let empty = convert_into(view(&rejected), 1, &mut [], &mut []).unwrap();
    assert_eq!(empty.unsupported_windows, 1);
    assert_eq!(requirements(view(&records), 1).unwrap().segments, 2);
    assert_eq!(segments[2], segment_storage());
    assert_eq!(ranges[2], FrameRange::default());
}

#[test]
fn history_grid_partial_eof_and_more_than_eight_segments() {
    let mut records: Vec<_> = (0..129).map(|i| record(i, Some(127999))).collect();
    records[128].range.end_frame -= 1000;
    let mut segments = vec![segment_storage(); 129];
    let mut ranges = vec![FrameRange::default(); 129];
    let grid = convert_into(view(&records), 2, &mut segments, &mut ranges).unwrap();
    assert_eq!(grid.segments.len(), 129);
    assert_eq!(
        grid.supported_ranges[128].end_frame,
        records[128].range.end_frame
    );
    let mut partial = view(&records);
    partial.finished = false;
    untouched(partial, 2, Error::InvalidArgument);
}
