// SPDX-License-Identifier: Apache-2.0
use libapta::{tempo::*, *};
fn selected() -> TempoValue {
    TempoValue {
        state: FeatureState::Final,
        confidence: 80,
        flags: 0x80000000,
        tempo_millibpm: 120000,
        candidate_set_id: 7,
        evidence_range: FrameRange {
            first_frame: 10,
            end_frame: 1000,
        },
        applicability_range: FrameRange {
            first_frame: 0,
            end_frame: 2000,
        },
    }
}
fn candidates() -> [TempoCandidate; 2] {
    [
        TempoCandidate {
            tempo_millibpm: 120000,
            score: 65535,
            confidence: 80,
            relation_to_selected: 0,
            flags: 0x01020304,
        },
        TempoCandidate {
            tempo_millibpm: 60000,
            score: 40000,
            confidence: 70,
            relation_to_selected: 1,
            flags: 8,
        },
    ]
}
fn bytes() -> Vec<u8> {
    let mut b = vec![0; 88];
    write_tempo_payload(
        &TempoView {
            selected: selected(),
            candidates: &candidates(),
        },
        &mut b,
    )
    .unwrap();
    b
}
fn grid() -> LocalGrid {
    let r = FrameRange {
        first_frame: 0,
        end_frame: 2000,
    };
    LocalGrid {
        requested_range: r,
        evidence_range: selected().evidence_range,
        applicability_range: r,
        coverage: r,
        state: FeatureState::Final,
        confidence: 70,
        flags: 3,
        segment: GridSegment {
            applicability_range: r,
            anchor_position: FractionalFrame {
                whole_frame: 20,
                fraction_q32: 0x12345678,
            },
            anchor_ordinal: -7,
            frames_per_beat: FramePeriod {
                whole_frames: 24000,
                fraction_q32: 0x87654321,
            },
            beat_count: 0,
            nominal_tempo_millibpm: 120000,
            segment_id: 4,
            revision: 9,
            flags: 0x80000000,
            state: FeatureState::Final,
            confidence: 66,
        },
    }
}
fn grid_bytes() -> Vec<u8> {
    let mut b = vec![0; 144];
    write_local_grid(&grid(), 120000, &mut b).unwrap();
    b
}
#[test]
fn canonical_tempo_and_independent_copy() {
    let b = bytes();
    let p = TempoPayload::parse(&b, false, true).unwrap();
    assert_eq!(p.selected(), selected());
    assert_eq!(p.candidate_count(), 2);
    assert_eq!(p.candidate(1), Some(candidates()[1]));
    assert_eq!(p.candidate(usize::MAX), None);
    assert_eq!(
        &b[..16],
        &[1, 0, 4, 80, 0, 0, 0, 128, 192, 212, 1, 0, 7, 0, 0, 0]
    );
    assert_eq!(
        &b[56..72],
        &[192, 212, 1, 0, 255, 255, 80, 0, 4, 3, 2, 1, 0, 0, 0, 0]
    );
    let mut copied = [candidates()[1]; 2];
    p.copy_candidates(&mut copied).unwrap();
    assert_eq!(copied, candidates());
    drop(b);
    assert_eq!(copied, candidates());
    let mut short = [0x55; 87];
    assert_eq!(
        write_tempo_payload(
            &TempoView {
                selected: selected(),
                candidates: &candidates()
            },
            &mut short
        ),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(short, [0x55; 87]);
}
#[test]
fn tempo_malformed_and_truncations() {
    let b = bytes();
    for n in 0..b.len() {
        assert!(
            TempoPayload::parse(&b[..n], true, true).is_err(),
            "prefix {n}"
        );
    }
    for (offset, value) in [
        (0, 2),
        (2, 1),
        (3, 101),
        (10, 255),
        (48, 0),
        (48, 4),
        (52, 1),
        (62, 255),
        (63, 9),
        (68, 1),
    ] {
        let mut v = b.clone();
        v[offset] = value;
        assert!(
            TempoPayload::parse(&v, true, true).is_err(),
            "offset {offset}"
        );
    }
    for offset in [24, 40] {
        let mut v = b.clone();
        v[offset..offset + 8].fill(0);
        assert!(TempoPayload::parse(&v, true, true).is_err());
    }
    let mut v = b.clone();
    v[16..24].copy_from_slice(&1000u64.to_le_bytes());
    assert!(TempoPayload::parse(&v, true, true).is_err());
    let mut v = b.clone();
    v[60..62].copy_from_slice(&1u16.to_le_bytes());
    assert!(TempoPayload::parse(&v, true, true).is_err());
    let mut v = b.clone();
    v[48..52].copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(TempoPayload::parse(&v, true, true).is_err());
}
#[test]
fn tempo_permissive_reserved_normalization_and_partial() {
    let mut b = bytes();
    b[52] = 1;
    b[68] = 1;
    assert!(TempoPayload::parse(&b, false, true).is_err());
    let p = TempoPayload::parse(&b, false, false).unwrap();
    let mut c = candidates();
    p.copy_candidates(&mut c).unwrap();
    let mut out = vec![0; 88];
    write_tempo_payload(
        &TempoView {
            selected: p.selected(),
            candidates: &c,
        },
        &mut out,
    )
    .unwrap();
    assert_eq!(out, bytes());
    for state in [2, 3] {
        b = bytes();
        b[2] = state;
        assert!(TempoPayload::parse(&b, false, true).is_err());
        assert!(TempoPayload::parse(&b, true, true).is_ok());
    }
    for relation in 0..=8 {
        b = bytes();
        b[63] = relation;
        assert_eq!(
            TempoPayload::parse(&b, false, true)
                .unwrap()
                .candidate(0)
                .unwrap()
                .relation_to_selected,
            relation
        );
    }
    let b = bytes();
    let p = TempoPayload::parse(&b, false, true).unwrap();
    let mut c = [candidates()[1]; 1];
    assert_eq!(p.copy_candidates(&mut c), Err(Error::BufferTooSmall));
    assert_eq!(c, [candidates()[1]; 1]);
}
#[test]
fn canonical_local_grid_and_validation() {
    let b = grid_bytes();
    assert_eq!(parse_local_grid(&b, false, true, 120000), Ok(grid()));
    assert_eq!(&b[88..96], &[0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0]);
    assert_eq!(&b[96..104], &(-7i64).to_le_bytes());
    assert_eq!(&b[112..116], &[0x21, 0x43, 0x65, 0x87]);
    for n in 0..144 {
        assert!(parse_local_grid(&b[..n], true, true, 120000).is_err());
    }
    for (offset, value) in [
        (0, 2),
        (2, 1),
        (3, 255),
        (8, 2),
        (12, 0),
        (92, 1),
        (136, 1),
        (137, 101),
        (138, 1),
        (140, 1),
    ] {
        let mut v = b.clone();
        v[offset] = value;
        assert!(
            parse_local_grid(&v, true, true, 120000).is_err(),
            "offset {offset}"
        );
    }
    for offset in [24, 40, 56, 72, 104] {
        let mut v = b.clone();
        v[offset..offset + 8].fill(0);
        assert!(parse_local_grid(&v, true, true, 120000).is_err());
    }
    assert!(parse_local_grid(&b, false, true, 120001).is_err());
    let mut out = [0x55; 143];
    assert_eq!(
        write_local_grid(&grid(), 120000, &mut out),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(out, [0x55; 143]);
    let mut g = grid();
    g.segment.applicability_range.first_frame = 1;
    let mut out = [0x55; 144];
    assert_eq!(
        write_local_grid(&g, 120000, &mut out),
        Err(Error::InvalidArgument)
    );
    assert_eq!(out, [0x55; 144]);
}
#[test]
fn local_grid_partial_and_permissive() {
    let mut b = grid_bytes();
    for offset in [92, 138, 139, 140] {
        b[offset] = 1;
    }
    assert!(parse_local_grid(&b, false, true, 120000).is_err());
    let g = parse_local_grid(&b, false, false, 120000).unwrap();
    let mut out = vec![0; 144];
    write_local_grid(&g, 120000, &mut out).unwrap();
    assert_eq!(out, grid_bytes());
    for offset in [2, 136] {
        for s in [2, 3] {
            let mut b = grid_bytes();
            b[offset] = s;
            assert!(parse_local_grid(&b, false, true, 120000).is_err());
            assert!(parse_local_grid(&b, true, true, 120000).is_ok());
        }
    }
}
