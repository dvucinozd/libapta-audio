// SPDX-License-Identifier: Apache-2.0
//! Public PF-02 geometry examples. No detector, product admission or scorer.
use libapta::{dj, grid, *};

fn segment(first: u64, end: u64, count: u32) -> GridSegment {
    GridSegment {
        applicability_range: FrameRange {
            first_frame: first,
            end_frame: end,
        },
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod {
            whole_frames: 24000,
            fraction_q32: 0,
        },
        beat_count: count,
        nominal_tempo_millibpm: 120000,
        confidence: 0,
        state: FeatureState::Final,
        flags: 0,
        segment_id: 1,
        revision: 1,
    }
}

fn context() -> GridSegment {
    GridSegment {
        anchor_position: FractionalFrame {
            whole_frame: 12000,
            fraction_q32: 1 << 31,
        },
        anchor_ordinal: -4,
        frames_per_beat: FramePeriod {
            whole_frames: 14062,
            fraction_q32: 1 << 31,
        },
        nominal_tempo_millibpm: 204800,
        ..segment(48000, 120000, 5)
    }
}

fn points(s: GridSegment) -> Vec<Beat> {
    (0..s.beat_count)
        .map(|i| s.beat_at(i).unwrap().unwrap())
        .collect()
}

fn global<'a>(segments: &'a [GridSegment], beats: &'a [Beat], end: u64) -> GlobalGrid<'a> {
    let range = FrameRange {
        first_frame: 0,
        end_frame: end,
    };
    GlobalGrid {
        state: FeatureState::Final,
        confidence: 0,
        flags: 0,
        representation: if segments.is_empty() {
            GridRepresentation::Explicit
        } else {
            GridRepresentation::Segments
        },
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_range: range,
        segments,
        beats,
    }
}

fn meter(record: &[MeterSegment]) -> Meter<'_> {
    let first = record[0];
    Meter {
        state: FeatureState::Final,
        confidence: 0,
        numerator: first.numerator,
        denominator: first.denominator,
        downbeat_frame: first.downbeat_frame,
        downbeat_ordinal: first.downbeat_ordinal,
        segments: record,
    }
}

#[test]
fn context_anchor_keeps_global_frames_signed_ordinals_and_fractional_carry() {
    let p = points(context());
    let expected = [
        (54188, 0, -1),
        (68250, 1 << 31, 0),
        (82313, 0, 1),
        (96375, 1 << 31, 2),
        (110438, 0, 3),
    ];
    assert_eq!(
        ((48000u128 * 60000) << 32) / u128::from(context().nominal_tempo_millibpm),
        (14062u128 << 32) + (1 << 31)
    );
    for (beat, (whole, fraction, ordinal)) in p.iter().zip(expected) {
        assert_eq!(
            (
                beat.position.whole_frame,
                beat.position.fraction_q32,
                beat.ordinal
            ),
            (whole, fraction, ordinal)
        );
    }
    assert_eq!(
        p.iter()
            .map(|b| b.position.rounded_milliseconds(48000).unwrap())
            .collect::<Vec<_>>(),
        [1129, 1422, 1715, 2008, 2301]
    );
    assert_eq!(context().beat_at(5), Ok(None));
}

#[test]
fn three_and_four_meter_phase_do_not_treat_ordinal_zero_as_an_implicit_downbeat() {
    let p = points(context());
    for (numerator, expected) in [(3i128, [2, 0, 1, 2, 0]), (4, [3, 0, 1, 2, 3])] {
        let phase: Vec<_> = p
            .iter()
            .map(|b| i128::from(b.ordinal).rem_euclid(numerator))
            .collect();
        assert_eq!(phase, expected);
        // A different phase class changes the downbeat, even on the same grid.
        assert_ne!(
            phase,
            p.iter()
                .map(|b| (i128::from(b.ordinal) - 1).rem_euclid(numerator))
                .collect::<Vec<_>>()
        );
    }
}

#[test]
fn lower_endpoint_is_owned_and_exclusive_endpoint_is_not() {
    let s = segment(48000, 144000, 4);
    let p = points(s);
    assert_eq!(
        p.iter().map(|b| b.position.whole_frame).collect::<Vec<_>>(),
        [48000, 72000, 96000, 120000]
    );
    assert_eq!(s.beat_at(4), Ok(None));
    let too_many = GridSegment { beat_count: 5, ..s };
    assert_eq!(too_many.beat_at(4), Err(Error::InvalidArgument));
}

#[test]
fn last_q32_unit_before_end_is_owned_without_rounding_it_into_source_eof() {
    let s = GridSegment {
        anchor_position: FractionalFrame {
            whole_frame: 47999,
            fraction_q32: u32::MAX,
        },
        ..segment(0, 48000, 1)
    };
    let b = s.beat_at(0).unwrap().unwrap();
    assert_eq!(b.position.whole_frame, 47999);
    assert_eq!(b.position.rounded_milliseconds(48000), Ok(1000));
    let at_end = GridSegment {
        anchor_position: FractionalFrame {
            whole_frame: 48000,
            fraction_q32: 0,
        },
        ..s
    };
    assert_eq!(at_end.beat_at(0), Err(Error::InvalidArgument));
}

#[test]
fn adjacent_tempo_segments_own_the_boundary_once_with_consecutive_ordinals() {
    let a = segment(0, 96000, 4);
    let b = GridSegment {
        anchor_position: FractionalFrame {
            whole_frame: 96000,
            fraction_q32: 0,
        },
        anchor_ordinal: 4,
        frames_per_beat: FramePeriod {
            whole_frames: 16000,
            fraction_q32: 0,
        },
        nominal_tempo_millibpm: 180000,
        segment_id: 2,
        ..segment(96000, 192000, 6)
    };
    let segments = [a, b];
    assert!(grid::validate_external(&global(&segments, &[], 192000), Some(192000), false).is_ok());
    let p: Vec<_> = segments.into_iter().flat_map(points).collect();
    assert_eq!(
        p.iter().map(|b| b.ordinal).collect::<Vec<_>>(),
        (0..10).collect::<Vec<_>>()
    );
    assert_eq!(
        p.iter().filter(|b| b.position.whole_frame == 96000).count(),
        1
    );
    assert_eq!(p[9].position.whole_frame, 176000);
}

#[test]
fn generic_gapped_representation_does_not_establish_product_continuity() {
    let a = segment(0, 96000, 4);
    let b = GridSegment {
        anchor_position: FractionalFrame {
            whole_frame: 120000,
            fraction_q32: 0,
        },
        anchor_ordinal: 4,
        segment_id: 2,
        ..segment(120000, 192000, 3)
    };
    let segments = [a, b];
    // Generic wire validity is deliberately weaker than the product profile.
    assert!(grid::validate(&global(&segments, &[], 192000), false).is_ok());
    assert_ne!(
        a.applicability_range.end_frame,
        b.applicability_range.first_frame
    );
    assert!(segments
        .into_iter()
        .flat_map(points)
        .all(|b| !(96000..120000).contains(&b.position.whole_frame)));
}

#[test]
fn fractional_witness_roundtrips_and_requires_exact_grid_whole_frame_and_ordinal() {
    let s = context();
    let segments = [s];
    let mut g = global(&segments, &[], 120000);
    g.requested_range = s.applicability_range;
    g.evidence_range = s.applicability_range;
    g.applicability_range = s.applicability_range;
    g.coverage_range = s.applicability_range;
    let mut bytes = [0; 176];
    grid::write_payload(&g, false, &mut bytes).unwrap();
    let parsed = grid::GridPayload::parse(&bytes, false, grid::GridOptions::default()).unwrap();
    let record = MeterSegment {
        first_frame: 48000,
        end_frame: 120000,
        downbeat_frame: 68250,
        downbeat_ordinal: 0,
        numerator: 4,
        denominator: 4,
        state: FeatureState::Final,
        confidence: 0,
        segment_id: 1,
    };
    for (frame, ordinal, valid) in [
        (68250, 0, true),
        (68251, 0, false),
        (68250, 1, false),
        (54188, -1, true),
    ] {
        let records = [MeterSegment {
            downbeat_frame: frame,
            downbeat_ordinal: ordinal,
            ..record
        }];
        let mut payload = [0; 104];
        dj::write_meter(meter(&records), Some(120000), false, &mut payload).unwrap();
        let view = dj::parse_meter(&payload, Some(120000), false, 1).unwrap();
        assert_eq!(
            view.validate_grid(|f, o| parsed.matches_downbeat(f, o))
                .is_ok(),
            valid
        );
    }
    assert_eq!(
        parsed.segment(0).unwrap().anchor_position.fraction_q32,
        1 << 31
    );
}

#[test]
fn phase_equivalent_owned_witnesses_do_not_have_identical_reference_support() {
    let p = points(segment(0, 240000, 10));
    let phases = |d: i64| {
        p.iter()
            .map(|b| (i128::from(b.ordinal) - i128::from(d)).rem_euclid(4))
            .collect::<Vec<_>>()
    };
    assert_eq!(phases(0), phases(4));
    // Public annotation-span example only; no scoring or witness selection.
    let support = |frame: u64| (48000..=216000).contains(&frame);
    assert!(!support(p[0].position.whole_frame));
    assert!(support(p[4].position.whole_frame));
    let k: Vec<_> = p.iter().filter(|b| b.ordinal.rem_euclid(4) == 0).collect();
    assert_eq!(k.len(), 3);
    assert_eq!(
        k.iter().filter(|b| support(b.position.whole_frame)).count(),
        2
    );
    assert!(k
        .iter()
        .all(|b| !(240000..=250000).contains(&b.position.whole_frame)));
}

#[test]
fn explicit_and_segment_capacity_fail_without_partial_wire_output() {
    let s = segment(0, 48000, 2);
    let segments = [s; grid::MAX_SEGMENTS + 1];
    let mut bytes = [0xa5; 176];
    assert_eq!(
        grid::write_payload(&global(&segments, &[], 48000), false, &mut bytes),
        Err(Error::LimitExceeded)
    );
    assert_eq!(bytes, [0xa5; 176]);
    let beats = vec![Beat::default(); grid::MAX_BEATS + 1];
    assert_eq!(
        grid::write_payload(&global(&[], &beats, 48000), false, &mut bytes),
        Err(Error::LimitExceeded)
    );
    assert_eq!(bytes, [0xa5; 176]);
}

#[test]
fn ten_minutes_at_300_bpm_retains_3000_owned_points_at_both_source_rates() {
    for rate in [44100u64, 48000] {
        let s = GridSegment {
            frames_per_beat: FramePeriod {
                whole_frames: rate / 5,
                fraction_q32: 0,
            },
            nominal_tempo_millibpm: 300000,
            ..segment(0, rate * 600, 3000)
        };
        let p = points(s);
        assert_eq!(p.len(), 3000);
        assert!(p.len() < grid::MAX_BEATS);
        assert_eq!(p[2999].ordinal, 2999);
        assert_eq!(p[2999].position.whole_frame, rate * 600 - rate / 5);
        assert_eq!(s.beat_at(3000), Ok(None));
    }
}

#[test]
fn signed_ordinal_and_coordinate_overflow_fail_explicitly() {
    let s = GridSegment {
        anchor_ordinal: i64::MAX,
        ..segment(0, 96000, 2)
    };
    assert_eq!(s.beat_at(1), Err(Error::LimitExceeded));
    let s = GridSegment {
        anchor_position: FractionalFrame {
            whole_frame: u64::MAX - 1,
            fraction_q32: 0,
        },
        ..segment(0, u64::MAX, 2)
    };
    assert_eq!(s.beat_at(1), Err(Error::LimitExceeded));
}

#[test]
fn generic_explicit_timing_may_fail_native_selected_tempo_consistency() {
    let range = FrameRange {
        first_frame: 0,
        end_frame: 120000,
    };
    let beats = [0, 24000, 48000].map(|frame| Beat {
        position: FractionalFrame {
            whole_frame: frame,
            fraction_q32: 0,
        },
        ordinal: (frame / 24000) as i64,
        confidence: 0,
        ..Beat::default()
    });
    let coverage = [range];
    let mut input = NativeResultInput {
        source: SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(120000),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        info: NativeResultInfo::default(),
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "PF02 public fixture",
            source_version: "1",
        },
        overview: None,
        detail: None,
        metadata: None,
        tempo: Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Final,
                confidence: 0,
                flags: 0,
                tempo_millibpm: 120000,
                candidate_set_id: 0,
                evidence_range: range,
                applicability_range: range,
            },
            candidates: &[],
        }),
        local_grid: Some(NativeGrid {
            state: FeatureState::Final,
            confidence: 0,
            flags: 0,
            representation: GridRepresentation::Explicit,
            requested_range: range,
            evidence_range: range,
            applicability_range: range,
            coverage_ranges: &coverage,
            segments: &[],
            beats: &beats,
        }),
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    };
    assert!(native_validation::validate(&input, NativeLimits::default()).is_ok());
    let mut varied = beats;
    varied[2].position.whole_frame = 60000;
    assert!(grid::validate(&global(&[], &varied, 120000), false).is_ok());
    input.local_grid.as_mut().unwrap().beats = &varied;
    assert_eq!(
        native_validation::validate(&input, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
}
