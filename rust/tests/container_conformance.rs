// SPDX-License-Identifier: Apache-2.0
use libapta::{container::*, Error, WaveformColumn, WaveformSpan};
fn fixture(name: &str) -> Vec<u8> {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../conformance/fixtures/container-v1-suite")
        .join(name);
    let text = std::fs::read_to_string(path).unwrap();
    let hex: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
        .collect()
}
fn valid() -> Vec<u8> {
    fixture("v1-wovr-only.apta.hex")
}
fn put32(b: &mut [u8], p: usize, v: u32) {
    b[p..p + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(b: &mut [u8], p: usize, v: u64) {
    b[p..p + 8].copy_from_slice(&v.to_le_bytes());
}
fn header_crc(b: &mut [u8]) {
    put32(b, 92, crc32c(&b[..92]));
}
fn payload_crc(b: &mut [u8]) {
    put32(b, 128, crc32c(&b[136..]));
}
#[test]
fn castagnoli_known_vector() {
    assert_eq!(crc32c(b"123456789"), 0xe3069283);
}
#[test]
fn c_canonical_fixture_exact_roundtrip() {
    let bytes = valid();
    let container = Container::parse(&bytes, ParseOptions::default()).unwrap();
    let view = container.parse_waveform(ParseOptions::default()).unwrap();
    assert_eq!(view.span_count(), 1);
    assert_eq!(view.column_count(), 1);
    assert_eq!(view.frames_per_column, 1024);
    assert_eq!(view.column(0, 0).unwrap().flags, 1);
    let mut spans = [WaveformSpan::default(); 1];
    let mut columns = [WaveformColumn::default(); 1];
    let overview = view.copy_into(&mut spans, &mut columns).unwrap();
    let mut output = vec![0; waveform_size(&overview).unwrap()];
    write_waveform(&container.source, &overview, &mut output).unwrap();
    assert_eq!(output, bytes);
    assert_eq!(
        view.copy_into(&mut [], &mut []).unwrap_err(),
        Error::BufferTooSmall
    );
    assert_eq!(
        write_waveform(&container.source, &overview, &mut []).unwrap_err(),
        Error::BufferTooSmall
    );
}
#[test]
fn all_committed_container_envelopes() {
    for name in [
        "v1-wovr-only.apta.hex",
        "v1-wovr-meta.apta.hex",
        "v1-wovr-wdtl.apta.hex",
        "v1-wovr-temp.apta.hex",
        "v1-wovr-temp-lgrd.apta.hex",
        "v1-wovr-temp-ggrd-revn.apta.hex",
        "v1-all-standard-sections.apta.hex",
    ] {
        let bytes = fixture(name);
        let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
        if !matches!(
            name,
            "v1-wovr-only.apta.hex" | "v1-wovr-meta.apta.hex" | "v1-wovr-wdtl.apta.hex"
        ) {
            assert_eq!(
                c.parse_waveform(ParseOptions::default()).unwrap_err(),
                Error::Unsupported
            );
        }
    }
}
#[test]
fn every_prefix_and_trailing_bytes_fail() {
    let bytes = valid();
    for n in 0..bytes.len() {
        assert!(
            Container::parse(&bytes[..n], ParseOptions::default()).is_err(),
            "prefix {n}"
        );
    }
    let mut bytes = bytes;
    bytes.push(0);
    assert!(Container::parse(&bytes, ParseOptions::default()).is_err());
}
#[test]
fn bit_corruption_never_panics() {
    let bytes = valid();
    for i in 0..bytes.len() {
        let mut mutated = bytes.clone();
        mutated[i] ^= 0x80;
        assert!(
            Container::parse(&mutated, ParseOptions::default()).is_err(),
            "byte {i}"
        );
    }
}
#[test]
fn limits_are_distinct() {
    let bytes = valid();
    let options = ParseOptions {
        maximum_file_bytes: 1,
        ..Default::default()
    };
    assert_eq!(
        Container::parse(&bytes, options).unwrap_err(),
        Error::LimitExceeded
    );
    let options = ParseOptions {
        maximum_section_count: 0,
        ..Default::default()
    };
    assert_eq!(
        Container::parse(&bytes, options).unwrap_err(),
        Error::LimitExceeded
    );
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    let options = ParseOptions {
        maximum_overview_spans: 0,
        ..Default::default()
    };
    assert_eq!(c.parse_waveform(options).unwrap_err(), Error::LimitExceeded);
}
#[test]
fn semantic_mutations_rejected_after_valid_crc() {
    for (offset, value) in [
        (140, 0),
        (152, 2),
        (156, 0),
        (176, 2),
        (180, 1),
        (192, 512),
        (204, 2),
        (208, 1),
    ] {
        let mut bytes = valid();
        put32(&mut bytes, offset, value);
        payload_crc(&mut bytes);
        let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
        assert!(
            c.parse_waveform(ParseOptions::default()).is_err(),
            "offset {offset}"
        );
    }
}
#[test]
fn reserved_policy_and_versions() {
    let mut bytes = valid();
    put32(&mut bytes, 132, 1);
    assert_eq!(
        Container::parse(&bytes, ParseOptions::default()).unwrap_err(),
        Error::Corrupt
    );
    assert!(Container::parse(
        &bytes,
        ParseOptions {
            strict: false,
            ..Default::default()
        }
    )
    .is_ok());
    let mut bytes = valid();
    bytes[100] = 2;
    assert_eq!(
        Container::parse(&bytes, ParseOptions::default()).unwrap_err(),
        Error::Unsupported
    );
}
#[test]
fn unknown_optional_is_validated_and_skipped() {
    let mut bytes = valid();
    bytes.splice(136..136, std::iter::repeat(0).take(40));
    let size = bytes.len() as u64;
    put32(&mut bytes, 20, 2);
    put64(&mut bytes, 32, size);
    put64(&mut bytes, 104, 176);
    bytes[136..140].copy_from_slice(b"ZZZZ");
    bytes[140] = 1;
    put64(&mut bytes, 144, 0);
    header_crc(&mut bytes);
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    assert!(c.parse_waveform(ParseOptions::default()).is_ok());
    bytes[142] = 1;
    assert_eq!(
        Container::parse(&bytes, ParseOptions::default()).unwrap_err(),
        Error::Unsupported
    );
}
#[test]
fn extended_header_is_opaque() {
    let mut bytes = valid();
    bytes.splice(96..96, [0xa5; 8]);
    bytes[4] = 104;
    put64(&mut bytes, 24, 104);
    let size = bytes.len() as u64;
    put64(&mut bytes, 32, size);
    put64(&mut bytes, 112, 144);
    header_crc(&mut bytes);
    assert!(Container::parse(&bytes, ParseOptions::default())
        .unwrap()
        .parse_waveform(ParseOptions::default())
        .is_ok());
}
#[test]
fn unknown_duration_requires_partial_nonfinal() {
    let mut bytes = valid();
    put64(&mut bytes, 40, u64::MAX);
    header_crc(&mut bytes);
    assert!(Container::parse(&bytes, ParseOptions::default()).is_err());
    put32(&mut bytes, 16, 3);
    header_crc(&mut bytes);
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    assert!(c.parse_waveform(ParseOptions::default()).is_err());
    put32(&mut bytes, 176, 2);
    payload_crc(&mut bytes);
    assert!(Container::parse(&bytes, ParseOptions::default())
        .unwrap()
        .parse_waveform(ParseOptions::default())
        .is_ok());
}

#[test]
fn packed_alias_and_final_gaps_are_rejected() {
    use libapta::{FeatureState, SourceInfo, WaveformOverview};
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(4),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let columns = [WaveformColumn {
        flags: 1,
        ..Default::default()
    }; 2];
    let spans = [
        WaveformSpan {
            first_frame: 0,
            end_frame: 2,
            first_column_index: 0,
            column_count: 1,
            data_column_offset: 0,
        },
        WaveformSpan {
            first_frame: 2,
            end_frame: 4,
            first_column_index: 1,
            column_count: 1,
            data_column_offset: 1,
        },
    ];
    let mut output = [0; 268];
    let overview = WaveformOverview {
        level_id: 0,
        frames_per_column: 2,
        origin_frame: 0,
        logical_column_count: 2,
        state: FeatureState::Final,
        spans: &spans,
        columns: &columns,
    };
    let size = write_waveform(&source, &overview, &mut output).unwrap();
    let mut alias = output;
    put32(&mut alias, 136 + 48 + 32 + 24, 0);
    payload_crc(&mut alias[..size]);
    let c = Container::parse(&alias[..size], ParseOptions::default()).unwrap();
    assert_eq!(
        c.parse_waveform(ParseOptions::default()).unwrap_err(),
        Error::Corrupt
    );
    let sparse = [WaveformSpan {
        data_column_offset: 0,
        ..spans[1]
    }];
    let overview = WaveformOverview {
        spans: &sparse,
        columns: &columns[..1],
        ..overview
    };
    assert_eq!(
        write_waveform(&source, &overview, &mut output).unwrap_err(),
        Error::Corrupt
    );
    let overview = WaveformOverview {
        state: FeatureState::Partial,
        ..overview
    };
    assert!(write_waveform(&source, &overview, &mut output).is_ok());
}

#[test]
fn deterministic_malformed_inputs_do_not_panic() {
    let mut random = 0x12345678u32;
    for len in 0..512 {
        let mut bytes = vec![0; len];
        for byte in &mut bytes {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            *byte = random as u8;
        }
        if let Ok(container) = Container::parse(&bytes, ParseOptions::default()) {
            let _ = container.parse_waveform(ParseOptions::default());
        }
    }
}

/// Run explicitly with a C baseline tool; the normal suite requires no C build.
#[test]
#[ignore = "requires APTA_C_VALIDATOR pointing to the C baseline apta-validate"]
fn rust_writer_is_accepted_by_c_parser() {
    use libapta::{FeatureState, SourceInfo, WaveformOverview};
    let validator = std::env::var_os("APTA_C_VALIDATOR")
        .expect("set APTA_C_VALIDATOR to the C baseline apta-validate executable");
    let source = SourceInfo {
        sample_rate: 44100,
        channel_count: 2,
        channel_layout: 2,
        total_frames: Some(7),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let spans = [WaveformSpan {
        first_frame: 0,
        end_frame: 7,
        first_column_index: 0,
        column_count: 2,
        data_column_offset: 0,
    }];
    let columns = [
        WaveformColumn {
            minimum: -12345,
            maximum: 30001,
            rms: 17000,
            flags: 1,
            ..Default::default()
        },
        WaveformColumn {
            minimum: -2345,
            maximum: 2222,
            rms: 1200,
            flags: 3,
            ..Default::default()
        },
    ];
    let overview = WaveformOverview {
        level_id: 0,
        frames_per_column: 4,
        origin_frame: 0,
        logical_column_count: 2,
        state: FeatureState::Final,
        spans: &spans,
        columns: &columns,
    };
    let mut output = vec![0; waveform_size(&overview).unwrap()];
    write_waveform(&source, &overview, &mut output).unwrap();
    let path = std::env::temp_dir().join(format!(
        "libapta-rust-container-oracle-{}.apta",
        std::process::id()
    ));
    std::fs::write(&path, &output).unwrap();
    let result = std::process::Command::new(validator)
        .arg(&path)
        .args(["--strict", "--quiet"])
        .output();
    let _ = std::fs::remove_file(&path);
    let result = result.unwrap();
    assert!(
        result.status.success(),
        "C parser failed: {}",
        String::from_utf8_lossy(&result.stderr)
    );
}

#[test]
fn payload_before_directory_is_valid_noncanonical() {
    let canonical = valid();
    let mut bytes = vec![0; 232];
    bytes[..96].copy_from_slice(&canonical[..96]);
    bytes[96..186].copy_from_slice(&canonical[136..]);
    bytes[192..232].copy_from_slice(&canonical[96..136]);
    put64(&mut bytes, 24, 192);
    put64(&mut bytes, 32, 232);
    put64(&mut bytes, 200, 96);
    header_crc(&mut bytes);
    assert!(Container::parse(&bytes, ParseOptions::default())
        .unwrap()
        .parse_waveform(ParseOptions::default())
        .is_ok());
    bytes[188] = 1;
    assert_eq!(
        Container::parse(&bytes, ParseOptions::default()).unwrap_err(),
        Error::Corrupt
    );
}
#[test]
fn permissive_column_reserved_values_are_normalized() {
    let mut bytes = valid();
    bytes[225] |= 128;
    bytes[222] = 20;
    payload_crc(&mut bytes);
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    assert_eq!(
        c.parse_waveform(ParseOptions::default()).unwrap_err(),
        Error::Corrupt
    );
    let view = c
        .parse_waveform(ParseOptions {
            strict: false,
            ..Default::default()
        })
        .unwrap();
    let column = view.column(0, 0).unwrap();
    assert_eq!(column.flags, 1);
    assert_eq!(column.low, 0);
}

#[test]
fn crc_repaired_adversarial_offsets_and_counts_never_panic() {
    for offset in [104, 112, 120, 160, 168, 184, 192] {
        for value in [u64::MAX, u64::MAX - 7, 1u64 << 32, 0, 1, 48, 136, 224] {
            let mut bytes = valid();
            put64(&mut bytes, offset, value);
            if offset >= 136 {
                payload_crc(&mut bytes);
            }
            if let Ok(c) = Container::parse(&bytes, ParseOptions::default()) {
                let _ = c.parse_waveform(ParseOptions::default());
            }
        }
    }
}
