// SPDX-License-Identifier: Apache-2.0
use libapta::{grid::*, *};
fn segment() -> GridSegment {
    GridSegment {
        applicability_range: FrameRange {
            first_frame: 0,
            end_frame: 1000,
        },
        anchor_position: FractionalFrame {
            whole_frame: 0,
            fraction_q32: 0x80000000,
        },
        anchor_ordinal: -1,
        frames_per_beat: FramePeriod {
            whole_frames: 100,
            fraction_q32: 0x80000000,
        },
        beat_count: 10,
        nominal_tempo_millibpm: 120000,
        confidence: 80,
        state: FeatureState::Final,
        flags: 0,
        segment_id: 1,
        revision: 7,
    }
}
fn grid<'a>(s: &'a [GridSegment], b: &'a [Beat]) -> GlobalGrid<'a> {
    let r = FrameRange {
        first_frame: 0,
        end_frame: 1000,
    };
    GlobalGrid {
        state: FeatureState::Final,
        confidence: 80,
        flags: 0,
        representation: if b.is_empty() {
            GridRepresentation::Segments
        } else if s.is_empty() {
            GridRepresentation::Explicit
        } else {
            GridRepresentation::Hybrid
        },
        requested_range: r,
        evidence_range: r,
        applicability_range: r,
        coverage_range: r,
        segments: s,
        beats: b,
    }
}
fn rev(g: &GlobalGrid<'_>) -> GridRevision {
    GridRevision {
        state: RevisionState::Applied,
        confidence: 80,
        flags: 0,
        revision_id: 7,
        previous_revision_id: 6,
        proposed_representation: g.representation,
        proposed_segment_count: g.segments.len() as u32,
        proposed_beat_count: g.beats.len() as u32,
        affected_range: g.applicability_range,
    }
}
fn payload() -> Vec<u8> {
    let s = [segment()];
    let mut b = vec![0; 176];
    write_payload(&grid(&s, &[]), false, &mut b).unwrap();
    b
}
fn parse(b: &[u8]) -> Result<GridPayload<'_>, Error> {
    GridPayload::parse(b, false, GridOptions::default())
}
#[test]
fn native_roundtrip_and_exact_layout() {
    let s = [segment()];
    let g = grid(&s, &[]);
    let b = payload();
    let p = parse(&b).unwrap();
    assert_eq!(p.segment(0), Some(s[0]));
    assert_eq!(p.segment(usize::MAX), None);
    assert_eq!(p.beat(usize::MAX), None);
    let mut out = [0; 176];
    assert_eq!(p.write_canonical(&mut out), Ok(176));
    assert_eq!(b, out);
    assert_eq!(&b[128..136], &(-1i64).to_le_bytes());
    assert_eq!(&b[152..156], &120000u32.to_le_bytes());
    let mut copied = [segment()];
    let owned = p.copy_into(&mut copied, &mut []).unwrap();
    assert_eq!(owned, g);
    let mut r = [0; 80];
    write_revision(&rev(&g), &g, false, &mut r).unwrap();
    assert_eq!(parse_revision(&r, &p, false, true), Ok(rev(&g)));
    assert!(p.matches_downbeat(101, 0));
    assert!(!p.matches_downbeat(100, 0));
}
#[test]
fn strict_reserved_and_permissive_canonicalization() {
    for offset in [88, 95, 124, 127, 170, 175] {
        let mut b = payload();
        b[offset] = 1;
        assert_eq!(parse(&b).unwrap_err(), Error::Corrupt);
        let p = GridPayload::parse(
            &b,
            false,
            GridOptions {
                strict: false,
                ..GridOptions::default()
            },
        )
        .unwrap();
        let mut out = [0; 176];
        p.write_canonical(&mut out).unwrap();
        assert_eq!(out.as_slice(), payload());
    }
}
#[test]
fn malformed_lengths_states_ranges_and_limits() {
    let b = payload();
    for n in 0..b.len() {
        assert_eq!(parse(&b[..n]).unwrap_err(), Error::Corrupt)
    }
    for (o, v) in [
        (0, 2),
        (2, 1),
        (3, 101),
        (8, 0),
        (12, 0),
        (16, 9),
        (20, 1),
        (168, 1),
        (169, 101),
    ] {
        let mut bad = b.clone();
        bad[o] = v;
        assert_eq!(parse(&bad).unwrap_err(), Error::Corrupt, "offset {o}")
    }
    let mut bad = b.clone();
    bad[104..112].fill(0);
    assert_eq!(parse(&bad).unwrap_err(), Error::Corrupt);
    assert_eq!(
        GridPayload::parse(
            &b,
            false,
            GridOptions {
                maximum_segments: 0,
                ..GridOptions::default()
            }
        )
        .unwrap_err(),
        Error::LimitExceeded
    );
    let mut bad = b.clone();
    bad[16..20].copy_from_slice(&u32::MAX.to_le_bytes());
    assert_eq!(parse(&bad).unwrap_err(), Error::Corrupt);
}
#[test]
fn short_buffers_unchanged() {
    let s = [segment()];
    let g = grid(&s, &[]);
    let mut out = [0xaa; 175];
    assert_eq!(
        write_payload(&g, false, &mut out),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(out, [0xaa; 175]);
    let mut r = [0xaa; 79];
    assert_eq!(
        write_revision(&rev(&g), &g, false, &mut r),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(r, [0xaa; 79]);
}
#[test]
fn explicit_beats_order_revision_and_reserved() {
    let beat = Beat {
        position: FractionalFrame {
            whole_frame: 100,
            fraction_q32: 1,
        },
        ordinal: 1,
        revision: 7,
        flags: 0,
        confidence: 80,
    };
    let beats = [beat, Beat { ordinal: 2, ..beat }];
    let g = grid(&[], &beats);
    let mut out = [0; 176];
    write_payload(&g, false, &mut out).unwrap();
    let p = parse(&out).unwrap();
    assert_eq!(p.beat(1), Some(beats[1]));
    assert_eq!(
        validate_external(&g, Some(1000), false),
        Err(Error::InvalidArgument)
    );
    let mut r = [0; 80];
    write_revision(&rev(&g), &g, false, &mut r).unwrap();
    r[8] = 8;
    assert_eq!(parse_revision(&r, &p, false, true), Err(Error::Corrupt));
    for o in [108, 129, 148, 169] {
        let mut bad = out;
        bad[o] = 1;
        assert_eq!(parse(&bad).unwrap_err(), Error::Corrupt)
    }
    let mut bad = out;
    bad[152..160].copy_from_slice(&1i64.to_le_bytes());
    assert_eq!(parse(&bad).unwrap_err(), Error::Corrupt);
}
#[test]
fn external_hybrid_exact_fraction_and_overflow() {
    let s = [segment()];
    let b = [Beat {
        position: segment_position_at_ordinal(&s[0], 1).unwrap(),
        ordinal: 1,
        revision: 7,
        flags: 0,
        confidence: 80,
    }];
    let g = grid(&s, &b);
    assert_eq!(validate_external(&g, Some(1000), false), Ok(216));
    assert_eq!(
        validate_external(&g, Some(999), false),
        Err(Error::InvalidArgument)
    );
    let bad = [Beat {
        position: FractionalFrame {
            whole_frame: 200,
            fraction_q32: 0,
        },
        ..b[0]
    }];
    assert_eq!(
        validate_external(&grid(&s, &bad), None, false),
        Err(Error::InvalidArgument)
    );
    let mut s = segment();
    s.anchor_ordinal = i64::MIN;
    s.frames_per_beat.whole_frames = 1;
    s.frames_per_beat.fraction_q32 = 0;
    s.anchor_position = FractionalFrame {
        whole_frame: 0,
        fraction_q32: 0,
    };
    assert_eq!(
        segment_position_at_ordinal(&s, i64::MAX),
        Some(FractionalFrame {
            whole_frame: u64::MAX,
            fraction_q32: 0
        })
    );
    s.anchor_position.whole_frame = 1;
    assert_eq!(segment_position_at_ordinal(&s, i64::MAX), None);
}
#[test]
fn pending_revision_requires_partial_and_nonfinal_grid() {
    let mut s = [segment()];
    s[0].state = FeatureState::Stable;
    let mut g = grid(&s, &[]);
    g.state = FeatureState::Stable;
    let mut r = rev(&g);
    r.state = RevisionState::Pending;
    let mut b = [0; 176];
    write_payload(&g, true, &mut b).unwrap();
    let p = GridPayload::parse(&b, true, GridOptions::default()).unwrap();
    let mut rb = [0; 80];
    write_revision(&r, &g, true, &mut rb).unwrap();
    assert_eq!(parse_revision(&rb, &p, true, true), Ok(r));
    assert_eq!(parse_revision(&rb, &p, false, true), Err(Error::Corrupt));
    rb[28] = 1;
    assert_eq!(parse_revision(&rb, &p, true, true), Err(Error::Corrupt));
    assert_eq!(parse_revision(&rb, &p, true, false), Ok(r));
}
