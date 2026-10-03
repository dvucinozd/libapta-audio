// SPDX-License-Identifier: Apache-2.0
use libapta::{meta, Error, Metadata, SourceId};

fn unknown(value: &[u8]) -> Vec<u8> {
    let mut bytes = vec![0xa1, 8];
    bytes.extend_from_slice(value);
    bytes
}
#[test]
fn canonical_c_fixture_and_ownership() {
    // Exact payload asserted by unchanged tests/unit/meta_roundtrip.c.
    let expected = b"\xa7\x01\x67libapta\x02\x650.1.0\x03\x69reference\x04\x60\x05\x1a\x65\x53\xf1\x00\x06\x68track:42\x07\x69roundtrip";
    let m = Metadata {
        producer_name: Some("libapta"),
        producer_version_string: Some("0.1.0"),
        backend_name: Some("reference"),
        backend_version: Some(""),
        creation_unix_time: Some(1700000000),
        application_source_id: Some(SourceId::Text("track:42")),
        comments: Some("roundtrip"),
    };
    assert_eq!(meta::parse(expected).unwrap(), m);
    let mut output = [0; 128];
    let size = meta::write(&m, &mut output).unwrap();
    assert_eq!(&output[..size], expected);
    let mut source = String::from("owned");
    let mut storage = [0; 64];
    let copied = meta::copy_to(
        &Metadata {
            comments: Some(&source),
            ..Metadata::default()
        },
        &mut storage,
    )
    .unwrap();
    source.clear();
    assert_eq!(copied.comments, Some("owned"));
}
#[test]
fn empty_presence_and_bytes_are_distinct() {
    assert_eq!(meta::parse(&[0xa0]).unwrap(), Metadata::default());
    for source in [
        SourceId::Text(""),
        SourceId::Bytes(&[]),
        SourceId::Bytes(&[0xff, 0]),
    ] {
        let m = Metadata {
            producer_name: Some(""),
            application_source_id: Some(source),
            ..Metadata::default()
        };
        let mut output = [0; 32];
        let size = meta::write(&m, &mut output).unwrap();
        assert_eq!(meta::parse(&output[..size]).unwrap(), m);
    }
}
#[test]
fn integer_boundaries_roundtrip() {
    for value in [
        0,
        23,
        24,
        255,
        256,
        65535,
        65536,
        u32::MAX as u64,
        u32::MAX as u64 + 1,
        u64::MAX,
    ] {
        let m = Metadata {
            creation_unix_time: Some(value),
            ..Metadata::default()
        };
        let mut output = [0; 16];
        let size = meta::write(&m, &mut output).unwrap();
        assert_eq!(size, meta::serialized_size(&m).unwrap());
        assert_eq!(meta::parse(&output[..size]).unwrap(), m);
        for end in 0..size {
            assert!(meta::parse(&output[..end]).is_err());
        }
    }
}
#[test]
fn rejects_malformed_recognized_fields() {
    let cases: &[&[u8]] = &[
        &[],
        &[0x80],
        &[0xbf, 0xff],
        &[0xa0, 0],
        &[0xa1, 1, 0x40],
        &[0xa2, 1, 0x60, 1, 0x60],
        &[0xa2, 2, 0x60, 1, 0x60],
        &[0xa1, 0x18, 1, 0x60],
        &[0xa1, 1, 0x78, 0],
        &[0xa1, 5, 0x20],
        &[0xa1, 6, 0x80],
        &[0xa1, 1, 0x61, 0xff],
        &[0xa1, 1, 0x62, 0xc0, 0x80],
        &[0xa1, 1, 0x63, 0xed, 0xa0, 0x80],
        &[0xa1, 1, 0x64, 0xf4, 0x90, 0x80, 0x80],
        &[0xa1, 6, 0x61, 0xff],
        &[0xa1, 0x60, 0],
    ];
    for bytes in cases {
        assert_eq!(meta::validate(bytes), Err(Error::Corrupt), "{bytes:x?}");
    }
}
#[test]
fn validates_unknown_values_canonically_and_preserves_them() {
    for value in [
        &[0][..],
        &[0x20],
        &[0x42, 0xff, 0],
        &[0x62, 0xc4, 0x8d],
        &[0x82, 0xf4, 0xf6],
        &[0xa2, 0, 0, 0x60, 0],
        &[0xc0, 0],
        &[0xf8, 32],
        &[0xf9, 0x7e, 0],
        &[0xf9, 0x80, 0],
        &[0xf9, 0x7c, 0],
        &[0xfa, 0x3f, 0x80, 0, 1],
        &[0xfb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 1],
    ] {
        let payload = unknown(value);
        assert!(meta::validate(&payload).is_ok(), "{payload:x?}");
        let mut output = vec![0; payload.len()];
        meta::copy_canonical(&payload, &mut output).unwrap();
        assert_eq!(output, payload);
        assert_eq!(meta::parse(&payload).unwrap(), Metadata::default());
    }
    for value in [
        &[0x61, 0xff][..],
        &[0xa2, 1, 0, 0, 0],
        &[0xa2, 0, 0, 0, 1],
        &[0x18, 0],
        &[0x19, 0, 255],
        &[0x1a, 0, 0, 255, 255],
        &[0x1b, 0, 0, 0, 0, 255, 255, 255, 255],
        &[0x9f, 0xff],
        &[0xf8, 24],
        &[0xff],
        &[0xf9, 0x7e, 1],
        &[0xf9, 0xfe, 0],
        &[0xfa, 0x3f, 0x80, 0, 0],
        &[0xfa, 0x33, 0x80, 0, 0],
        &[0xfa, 0x7f, 0x80, 0, 0],
        &[0xfb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0],
    ] {
        let payload = unknown(value);
        assert_eq!(
            meta::validate(&payload),
            Err(Error::Corrupt),
            "{payload:x?}"
        );
    }
}
#[test]
fn all_half_values_accept_and_widened_values_reject() {
    // Exhaust the binary16 domain, checking canonical NaNs and shortest-width floats.
    for bits in 0u16..=u16::MAX {
        let exponent = (bits >> 10) & 31;
        let mantissa = bits & 1023;
        let bytes = unknown(&[0xf9, (bits >> 8) as u8, bits as u8]);
        let nan = exponent == 31 && mantissa != 0;
        assert_eq!(meta::validate(&bytes).is_ok(), !nan || bits == 0x7e00);
        let value = if exponent == 31 {
            if mantissa == 0 {
                f32::INFINITY
            } else {
                f32::NAN
            }
        } else if exponent == 0 {
            (mantissa as f32) * (1.0 / 16777216.0)
        } else {
            (1.0 + mantissa as f32 / 1024.0) * 2.0f32.powi(exponent as i32 - 15)
        };
        let value = if bits & 0x8000 != 0 { -value } else { value };
        let mut wider = vec![0xfa];
        wider.extend_from_slice(&value.to_bits().to_be_bytes());
        assert_eq!(
            meta::validate(&unknown(&wider)),
            Err(Error::Corrupt),
            "half bits {bits:x}"
        );
    }
}
#[test]
fn bounded_depth_items_maps_strings() {
    let mut value = vec![0x81; 7];
    value.push(0);
    assert!(meta::validate(&unknown(&value)).is_ok());
    value.insert(0, 0x81);
    assert_eq!(meta::validate(&unknown(&value)), Err(Error::LimitExceeded));
    let mut value = vec![0x98, 255];
    value.extend([0; 255]);
    assert!(meta::validate(&unknown(&value)).is_ok());
    value = vec![0x99, 1, 0];
    value.extend([0; 256]);
    assert_eq!(meta::validate(&unknown(&value)), Err(Error::LimitExceeded));
    assert_eq!(meta::validate(&[0xb8, 65]), Err(Error::LimitExceeded));
    let mut value = vec![0x59, 0x20, 0];
    value.extend([0; 8192]);
    assert!(meta::validate(&unknown(&value)).is_ok());
    value[2] = 1;
    value.push(0);
    assert_eq!(meta::validate(&unknown(&value)), Err(Error::LimitExceeded));
}
#[test]
fn field_limits_and_atomic_buffer_errors() {
    for (index, limit) in [(0, 255), (1, 127), (2, 255), (3, 127), (4, 4096), (5, 1024)] {
        let mut text = "a".repeat(limit);
        for oversized in [false, true] {
            if oversized {
                text.push('a');
            }
            let mut m = Metadata::default();
            match index {
                0 => m.producer_name = Some(&text),
                1 => m.producer_version_string = Some(&text),
                2 => m.backend_name = Some(&text),
                3 => m.backend_version = Some(&text),
                4 => m.comments = Some(&text),
                _ => m.application_source_id = Some(SourceId::Text(&text)),
            }
            let mut output = vec![0xaa; 8192];
            if oversized {
                assert_eq!(meta::write(&m, &mut output), Err(Error::LimitExceeded));
                assert!(output.iter().all(|&b| b == 0xaa));
            } else {
                let size = meta::write(&m, &mut output).unwrap();
                assert_eq!(meta::parse(&output[..size]).unwrap(), m);
                let mut short = vec![0xaa; size - 1];
                assert_eq!(meta::write(&m, &mut short), Err(Error::BufferTooSmall));
                assert!(short.iter().all(|&b| b == 0xaa));
                assert_eq!(
                    meta::copy_canonical(&output[..size], &mut short),
                    Err(Error::BufferTooSmall)
                );
            }
        }
    }
}

#[test]
fn existing_c_conformance_fixtures_reencode_exactly() {
    for hex in [
        include_str!("../../tests/fixtures/reference-wovr-meta.apta.hex"),
        include_str!("../../tests/fixtures/container-v1-suite/v1-wovr-meta.apta.hex"),
    ] {
        let digits: String = hex.chars().filter(|c| !c.is_whitespace()).collect();
        let bytes: Vec<u8> = digits
            .as_bytes()
            .chunks_exact(2)
            .map(|p| u8::from_str_radix(core::str::from_utf8(p).unwrap(), 16).unwrap())
            .collect();
        let count = u32::from_le_bytes(bytes[20..24].try_into().unwrap());
        let directory = u64::from_le_bytes(bytes[24..32].try_into().unwrap()) as usize;
        let mut found = false;
        for i in 0..count as usize {
            let entry = &bytes[directory + i * 40..directory + (i + 1) * 40];
            if &entry[..4] != b"META" {
                continue;
            }
            found = true;
            let offset = u64::from_le_bytes(entry[8..16].try_into().unwrap()) as usize;
            let size = u64::from_le_bytes(entry[16..24].try_into().unwrap()) as usize;
            let payload = &bytes[offset..offset + size];
            let m = meta::parse(payload).unwrap();
            let mut output = vec![0; meta::serialized_size(&m).unwrap()];
            meta::write(&m, &mut output).unwrap();
            assert_eq!(output, payload);
        }
        assert!(found);
    }
}

#[test]
fn deterministic_adversarial_nested_values_do_not_panic() {
    // A bounded reproducible mutation corpus, not external fuzz campaign evidence.
    let mut state = 0x5a17_893du32;
    let mut next = || {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        state
    };
    for case in 0..32768 {
        let mut value = Vec::new();
        for _ in 0..next() % 12 {
            match next() % 3 {
                0 => value.push(0x81),        // single-element array
                1 => value.extend([0xa1, 0]), // map with unsigned key
                _ => value.push(0xc0),        // tag
            }
        }
        let length = next() as usize % 128;
        for _ in 0..length {
            value.push(next() as u8);
        }
        let mut payload = unknown(&value);
        if case % 3 == 0 {
            let index = next() as usize % payload.len();
            payload[index] = next() as u8;
        }
        let parsed = meta::parse(&payload);
        let mut output = vec![0xaa; payload.len()];
        match meta::copy_canonical(&payload, &mut output) {
            Ok(size) => {
                assert!(parsed.is_ok());
                assert_eq!(size, payload.len());
                assert_eq!(output, payload);
            }
            Err(error) => {
                assert_eq!(parsed.unwrap_err(), error);
                assert!(output.iter().all(|&b| b == 0xaa));
            }
        }
    }
}
