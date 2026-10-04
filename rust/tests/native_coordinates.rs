// SPDX-License-Identifier: Apache-2.0
//! Consumer coordinate helpers preserve Q32 precision and reject overflow.
use libapta::{Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment};

#[test]
fn milliseconds_round_once_after_rate_conversion() {
    for rate in [8000, 44100, 48000, 192000] {
        assert_eq!(
            FractionalFrame {
                whole_frame: rate as u64,
                fraction_q32: 0
            }
            .rounded_milliseconds(rate),
            Ok(1000)
        );
    }
    assert_eq!(
        FractionalFrame {
            whole_frame: 0,
            fraction_q32: 1 << 31
        }
        .rounded_milliseconds(1000),
        Ok(1)
    );
    assert_eq!(
        FractionalFrame {
            whole_frame: 0,
            fraction_q32: (1 << 31) - 1
        }
        .rounded_milliseconds(1000),
        Ok(0)
    );
    assert_eq!(
        FractionalFrame {
            whole_frame: u32::MAX as u64 + 1,
            fraction_q32: 0
        }
        .rounded_milliseconds(1000),
        Ok(u32::MAX as u64 + 1)
    );
}

#[test]
fn milliseconds_reject_invalid_rate_and_wide_overflow() {
    assert_eq!(
        FractionalFrame::default().rounded_milliseconds(0),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        FractionalFrame {
            whole_frame: u64::MAX,
            fraction_q32: u32::MAX
        }
        .rounded_milliseconds(1),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        FractionalFrame {
            whole_frame: u64::MAX,
            fraction_q32: 0
        }
        .rounded_milliseconds(1000),
        Ok(u64::MAX)
    );
}

fn segment() -> GridSegment {
    GridSegment {
        applicability_range: FrameRange {
            first_frame: 0,
            end_frame: 100,
        },
        anchor_position: FractionalFrame {
            whole_frame: 3,
            fraction_q32: 1 << 31,
        },
        anchor_ordinal: -1,
        frames_per_beat: FramePeriod {
            whole_frames: 5,
            fraction_q32: 1 << 31,
        },
        beat_count: 3,
        nominal_tempo_millibpm: 120000,
        confidence: 80,
        state: FeatureState::Final,
        flags: 4,
        segment_id: 1,
        revision: 7,
    }
}

#[test]
fn segment_beat_is_bounded_and_preserves_reference_arithmetic() {
    let s = segment();
    for i in 0..s.beat_count {
        let b = s.beat_at(i).unwrap().unwrap();
        assert_eq!(
            Some(b.position),
            libapta::grid::segment_position_at_ordinal(&s, -1 + i64::from(i))
        );
        assert_eq!(
            (b.ordinal, b.revision, b.flags, b.confidence),
            (-1 + i64::from(i), 7, 4, 80)
        );
    }
    assert_eq!(
        s.beat_at(1).unwrap().unwrap().position,
        FractionalFrame {
            whole_frame: 9,
            fraction_q32: 0
        }
    );
    assert_eq!(s.beat_at(3), Ok(None));
    assert_eq!(s.beat_at(u32::MAX), Ok(None));
}

#[test]
fn segment_beat_rejects_ordinal_and_coordinate_overflow() {
    let mut s = segment();
    s.anchor_ordinal = i64::MAX;
    assert_eq!(s.beat_at(1), Err(Error::LimitExceeded));
    s.anchor_ordinal = 0;
    s.anchor_position.whole_frame = u64::MAX;
    assert_eq!(s.beat_at(1), Err(Error::LimitExceeded));
}

#[test]
fn segment_skips_context_anchor_and_rejects_inconsistent_count() {
    let mut s = segment();
    s.applicability_range.first_frame = 10;
    let beat = s.beat_at(0).unwrap().unwrap();
    assert_eq!(beat.ordinal, 1);
    assert_eq!(
        beat.position,
        FractionalFrame {
            whole_frame: 14,
            fraction_q32: 1 << 31
        }
    );
    s.applicability_range.end_frame = 15;
    assert_eq!(s.beat_at(1), Err(Error::InvalidArgument));
    s.frames_per_beat = FramePeriod::default();
    assert_eq!(s.beat_at(0), Err(Error::InvalidArgument));
}
