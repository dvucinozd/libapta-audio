// SPDX-License-Identifier: Apache-2.0
use libapta::{dj::*, Error, FeatureState, KeyCandidate, MeterSegment, QualityRecord};
fn fixture(tag: &[u8; 4]) -> Vec<u8> {
    let hex = include_str!("../../tests/fixtures/dj-sections-v1-combined.apta.hex");
    let compact: String = hex.chars().filter(|c| !c.is_whitespace()).collect();
    let b: Vec<u8> = (0..compact.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&compact[i..i + 2], 16).unwrap())
        .collect();
    for e in b[96..256].chunks_exact(40) {
        if &e[..4] == tag {
            let p = u64::from_le_bytes(e[8..16].try_into().unwrap()) as usize;
            let n = u64::from_le_bytes(e[16..24].try_into().unwrap()) as usize;
            return b[p..p + n].to_vec();
        }
    }
    panic!()
}
const C: KeyCandidate = KeyCandidate {
    tonic: 0,
    mode: 1,
    tuning_offset_cents: 0,
    score: 0,
    confidence: 255,
};
const M: MeterSegment = MeterSegment {
    first_frame: 0,
    end_frame: 1,
    downbeat_frame: 0,
    downbeat_ordinal: 0,
    numerator: 4,
    denominator: 4,
    state: FeatureState::Final,
    confidence: 255,
    segment_id: 1,
};
const Q: QualityRecord = QualityRecord {
    feature: 1,
    calibration_model_id: 0,
    evidence_coverage_permille: 65535,
    confidence: 255,
    state: FeatureState::Final,
    flags: 0,
};
#[test]
fn independent_fixture_exact_bytes_and_caller_owned_copies() {
    let b = fixture(b"MKEY");
    let v = parse_key(&b, Some(96000), false, 24).unwrap();
    let mut candidates = [C; 2];
    let k = v.copy_into(&mut candidates).unwrap();
    assert_eq!(k.tuning_offset_cents, -7);
    assert_eq!(k.candidates[0].score, 62000);
    assert_eq!(v.candidate(usize::MAX), None);
    let mut out = vec![0xa5; b.len() + 1];
    assert_eq!(write_key(k, Some(96000), false, &mut out).unwrap(), b.len());
    assert_eq!(&out[..b.len()], b);
    assert_eq!(out[b.len()], 0xa5);
    let b = fixture(b"MTRD");
    let v = parse_meter(&b, Some(96000), false, 65536).unwrap();
    let mut segments = [M; 2];
    let m = v.copy_into(&mut segments).unwrap();
    assert_eq!(m.downbeat_ordinal, -4);
    let mut calls = Vec::new();
    v.validate_grid(|f, o| {
        calls.push((f, o));
        true
    })
    .unwrap();
    assert_eq!(calls, [(0, -4), (48000, 12)]);
    assert_eq!(v.validate_grid(|_, _| false), Err(Error::Corrupt));
    assert_eq!(v.segment(usize::MAX), None);
    let mut out = vec![0; b.len()];
    write_meter(m, Some(96000), false, &mut out).unwrap();
    assert_eq!(out, b);
    let b = fixture(b"CONF");
    let v = parse_quality(&b, false, (1 << 9) | (1 << 10), 11).unwrap();
    let mut records = [Q; 2];
    v.copy_into(&mut records).unwrap();
    records.reverse();
    let mut out = vec![0; b.len()];
    write_quality(&records, false, (1 << 9) | (1 << 10), &mut out).unwrap();
    assert_eq!(out, b);
    assert_eq!(v.record(usize::MAX), None);
}
#[test]
fn all_truncations_and_trailing_bytes_rejected() {
    for tag in [b"MKEY", b"MTRD", b"CONF"] {
        let mut b = fixture(tag);
        for n in 0..b.len() {
            let err = match tag {
                b"MKEY" => parse_key(&b[..n], None, false, 24).err(),
                b"MTRD" => parse_meter(&b[..n], None, false, 65536).err(),
                _ => parse_quality(&b[..n], false, 2047, 11).err(),
            };
            assert!(err.is_some(), "{tag:?} {n}");
        }
        b.push(0);
        assert!(match tag {
            b"MKEY" => parse_key(&b, None, false, 24).is_err(),
            b"MTRD" => parse_meter(&b, None, false, 65536).is_err(),
            _ => parse_quality(&b, false, 2047, 11).is_err(),
        });
    }
}
#[test]
fn malformed_key_limits_order_identity_and_selection() {
    let original = fixture(b"MKEY");
    for (p, value) in [
        (0, 2),
        (2, 0),
        (3, 101),
        (4, 12),
        (5, 3),
        (6, 101),
        (8, 1),
        (32, 41),
        (36, 1),
        (47, 1),
        (48, 1),
        (52, 1),
    ] {
        let mut b = original.clone();
        b[p] = value;
        assert!(parse_key(&b, None, false, 24).is_err(), "offset {p}");
    }
    for count in [25, u32::MAX] {
        let mut b = original.clone();
        b[12..16].copy_from_slice(&count.to_le_bytes());
        assert_eq!(
            parse_key(&b, None, false, usize::MAX).unwrap_err(),
            Error::LimitExceeded
        );
    }
    assert_eq!(
        parse_key(&original, None, false, 1).unwrap_err(),
        Error::LimitExceeded
    );
    assert!(parse_key(&original, Some(94999), false, 24).is_err());
    let mut b = original.clone();
    b[2] = 1;
    assert!(parse_key(&b, None, false, 24).is_err());
    assert!(parse_key(&b, None, true, 24).is_ok());
    let mut b = original.clone();
    b[60..62].copy_from_slice(&62000u16.to_le_bytes());
    assert!(parse_key(&b, None, false, 24).is_err());
    let mut b = original.clone();
    b[56..60].copy_from_slice(&original[40..44]);
    assert!(parse_key(&b, None, false, 24).is_err());
    let mut b = original.clone();
    b[4] = 8;
    assert!(parse_key(&b, None, false, 24).is_err());
}
#[test]
fn malformed_meter_counts_state_ranges_ids_and_summary() {
    let original = fixture(b"MTRD");
    for (p, value) in [
        (0, 2),
        (3, 101),
        (6, 3),
        (8, 1),
        (32, 49),
        (36, 1),
        (40, 1),
        (84, 0),
        (85, 101),
        (86, 1),
        (88, 1),
        (92, 0),
        (96, 1),
        (136, 0),
        (140, 3),
        (148, 4),
    ] {
        let mut b = original.clone();
        b[p] = value;
        assert!(parse_meter(&b, None, false, 65536).is_err(), "offset {p}");
    }
    for count in [65537, u32::MAX] {
        let mut b = original.clone();
        b[12..16].copy_from_slice(&count.to_le_bytes());
        assert_eq!(
            parse_meter(&b, None, false, usize::MAX).unwrap_err(),
            Error::LimitExceeded
        );
    }
    let mut b = original.clone();
    b[12..16].fill(0);
    assert!(parse_meter(&b, None, false, 65536).is_err());
    let mut b = original.clone();
    b[104..112].copy_from_slice(&47999u64.to_le_bytes());
    assert!(parse_meter(&b, None, false, 65536).is_err());
    let mut b = original.clone();
    b[128..136].copy_from_slice(&(-4i64).to_le_bytes());
    assert!(parse_meter(&b, None, false, 65536).is_err());
    assert_eq!(
        parse_meter(&original, None, false, 1).unwrap_err(),
        Error::LimitExceeded
    );
    assert!(parse_meter(&original, Some(95999), false, 65536).is_err());
}
#[test]
fn quality_target_presence_order_flags_and_sentinels() {
    let original = fixture(b"CONF");
    for (p, value) in [
        (0, 2),
        (2, 31),
        (8, 17),
        (12, 1),
        (16, 1),
        (28, 254),
        (30, 101),
        (31, 0),
        (32, 16),
        (36, 1),
        (40, 1),
    ] {
        let mut b = original.clone();
        b[p] = value;
        assert!(parse_quality(&b, false, 2047, 11).is_err(), "offset {p}");
    }
    for count in [12, u32::MAX] {
        let mut b = original.clone();
        b[4..8].copy_from_slice(&count.to_le_bytes());
        assert_eq!(
            parse_quality(&b, false, 2047, usize::MAX).unwrap_err(),
            Error::LimitExceeded
        );
    }
    assert_eq!(
        parse_quality(&original, false, 2047, 1).unwrap_err(),
        Error::LimitExceeded
    );
    assert!(parse_quality(&original, false, 1 << 9, 11).is_err());
    let mut b = original.clone();
    b[48..56].copy_from_slice(&(1u64 << 9).to_le_bytes());
    assert!(parse_quality(&b, false, 2047, 11).is_err());
}
#[test]
fn exhausted_buffers_and_invalid_native_input_leave_output_unchanged() {
    let b = fixture(b"MKEY");
    let v = parse_key(&b, None, false, 24).unwrap();
    let mut too_short = [C; 1];
    assert_eq!(
        v.copy_into(&mut too_short).unwrap_err(),
        Error::BufferTooSmall
    );
    assert_eq!(too_short, [C]);
    let mut storage = [C; 2];
    let mut k = v.copy_into(&mut storage).unwrap();
    let mut out = [0xa5; 71];
    assert_eq!(
        write_key(k, None, false, &mut out),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(out, [0xa5; 71]);
    k.tonic = 12;
    assert_eq!(
        write_key(k, None, false, &mut out),
        Err(Error::InvalidArgument)
    );
    assert_eq!(out, [0xa5; 71]);
    let b = fixture(b"MTRD");
    let v = parse_meter(&b, None, false, 65536).unwrap();
    let mut storage = [M; 1];
    assert_eq!(
        v.copy_into(&mut storage).unwrap_err(),
        Error::BufferTooSmall
    );
    assert_eq!(storage, [M]);
    let b = fixture(b"CONF");
    let v = parse_quality(&b, false, 2047, 11).unwrap();
    let mut storage = [Q; 1];
    assert_eq!(v.copy_into(&mut storage), Err(Error::BufferTooSmall));
    assert_eq!(storage, [Q]);
}

#[test]
fn arbitrary_single_byte_mutations_never_panic() {
    for tag in [b"MKEY", b"MTRD", b"CONF"] {
        let original = fixture(tag);
        for index in 0..original.len() {
            for value in [0, 1, 100, 127, 255] {
                let mut b = original.clone();
                b[index] = value;
                match tag {
                    b"MKEY" => {
                        let _ = parse_key(&b, None, true, usize::MAX);
                    }
                    b"MTRD" => {
                        let _ = parse_meter(&b, None, true, usize::MAX);
                    }
                    _ => {
                        let _ = parse_quality(&b, true, u64::MAX, usize::MAX);
                    }
                }
            }
        }
    }
}

#[test]
fn meter_and_quality_writes_are_transactional_on_failure() {
    let b = fixture(b"MTRD");
    let v = parse_meter(&b, None, false, 65536).unwrap();
    let mut segments = [M; 2];
    let mut m = v.copy_into(&mut segments).unwrap();
    let mut output = [0xa5; 159];
    assert_eq!(
        write_meter(m, None, false, &mut output),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(output, [0xa5; 159]);
    m.denominator = 3;
    assert_eq!(
        write_meter(m, None, false, &mut output),
        Err(Error::InvalidArgument)
    );
    assert_eq!(output, [0xa5; 159]);
    let b = fixture(b"CONF");
    let v = parse_quality(&b, false, 2047, 11).unwrap();
    let mut records = [Q; 2];
    v.copy_into(&mut records).unwrap();
    let mut output = [0xa5; 79];
    assert_eq!(
        write_quality(&records, false, 2047, &mut output),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(output, [0xa5; 79]);
    records[0].feature = 1 << 11;
    assert_eq!(
        write_quality(&records, false, u64::MAX, &mut output),
        Err(Error::InvalidArgument)
    );
    assert_eq!(output, [0xa5; 79]);
}
