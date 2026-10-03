// SPDX-License-Identifier: Apache-2.0
use libapta::{container::*, *};
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
fn roundtrip(bytes: &[u8]) -> Vec<u8> {
    let c = Container::parse(bytes, ParseOptions::default()).unwrap();
    let result = c.parse_waveform_result(ParseOptions::default()).unwrap();
    let mut spans = vec![WaveformSpan::default(); result.overview.span_count()];
    let mut columns = vec![WaveformColumn::default(); result.overview.column_count()];
    let overview = result.overview.copy_into(&mut spans, &mut columns).unwrap();
    let tile_columns: Vec<Vec<_>> = (0..result.tile_count())
        .map(|i| {
            let t = result.tile(i).unwrap();
            (0..t.column_count())
                .map(|j| t.column(j).unwrap())
                .collect()
        })
        .collect();
    let tiles: Vec<_> = tile_columns
        .iter()
        .enumerate()
        .map(|(i, cols)| {
            let t = result.tile(i).unwrap();
            WaveformTile {
                level_id: t.level_id,
                tile_index: t.tile_index,
                first_frame: t.first_frame,
                end_frame: t.end_frame,
                first_column_index: t.first_column_index,
                state: t.state,
                confidence: t.confidence,
                columns: cols,
            }
        })
        .collect();
    let mut out = vec![
        0;
        waveform_result_size(&c.source, &overview, &tiles, result.metadata.as_ref())
            .unwrap()
    ];
    assert_eq!(
        write_waveform_result(
            &c.source,
            &overview,
            &tiles,
            result.metadata.as_ref(),
            &mut out
        )
        .unwrap(),
        out.len()
    );
    out
}
fn generated() -> Vec<u8> {
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(17001),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let cols = [WaveformColumn {
        minimum: -12000,
        maximum: 13000,
        rms: 7000,
        flags: 1,
        ..Default::default()
    }; 2];
    let span = [WaveformSpan {
        first_frame: 0,
        end_frame: 17001,
        first_column_index: 0,
        column_count: 1,
        data_column_offset: 0,
    }];
    let overview = WaveformOverview {
        level_id: 0,
        frames_per_column: 32768,
        origin_frame: 0,
        logical_column_count: 1,
        state: FeatureState::Final,
        spans: &span,
        columns: &cols[..1],
    };
    let tiles = [
        WaveformTile {
            level_id: 1,
            tile_index: 0,
            first_frame: 0,
            end_frame: 512,
            first_column_index: 0,
            state: FeatureState::Provisional,
            confidence: 77,
            columns: &cols,
        },
        WaveformTile {
            level_id: 1,
            tile_index: 1,
            first_frame: 16896,
            end_frame: 17001,
            first_column_index: 66,
            state: FeatureState::Final,
            confidence: 255,
            columns: &cols[..1],
        },
    ];
    let m = Metadata {
        producer_name: Some("Rust test"),
        producer_version_string: Some("0.1"),
        backend_name: Some("native"),
        backend_version: Some(""),
        creation_unix_time: Some(u64::MAX),
        application_source_id: Some(SourceId::Bytes(&[0, 255, 2])),
        comments: Some("UTF-8: čćž 😀"),
    };
    let mut out = vec![0; waveform_result_size(&source, &overview, &tiles, Some(&m)).unwrap()];
    write_waveform_result(&source, &overview, &tiles, Some(&m), &mut out).unwrap();
    assert_eq!(
        write_waveform_result(&source, &overview, &tiles, Some(&m), &mut []).unwrap_err(),
        Error::BufferTooSmall
    );
    out
}
fn generated_empty_metadata() -> Vec<u8> {
    let bytes = fixture("v1-wovr-only.apta.hex");
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    let parsed = c.parse_waveform(ParseOptions::default()).unwrap();
    let mut spans = vec![WaveformSpan::default(); parsed.span_count()];
    let mut columns = vec![WaveformColumn::default(); parsed.column_count()];
    let overview = parsed.copy_into(&mut spans, &mut columns).unwrap();
    let metadata = Metadata::default();
    let mut output =
        vec![0; waveform_result_size(&c.source, &overview, &[], Some(&metadata)).unwrap()];
    write_waveform_result(&c.source, &overview, &[], Some(&metadata), &mut output).unwrap();
    output
}
#[test]
fn empty_metadata_preserves_presence_and_canonical_roundtrip() {
    let absent = fixture("v1-wovr-only.apta.hex");
    let c = Container::parse(&absent, ParseOptions::default()).unwrap();
    assert!(c
        .parse_waveform_result(ParseOptions::default())
        .unwrap()
        .metadata
        .is_none());
    let present = generated_empty_metadata();
    let c = Container::parse(&present, ParseOptions::default()).unwrap();
    assert_eq!(c.section_count(), 2);
    assert_eq!(c.section(1).unwrap().payload, &[0xa0]);
    assert_eq!(
        c.parse_waveform_result(ParseOptions::default())
            .unwrap()
            .metadata,
        Some(Metadata::default())
    );
    assert_eq!(roundtrip(&present), present);
}
#[test]
fn exact_c_waveform_fixtures() {
    for f in [
        "v1-wovr-only.apta.hex",
        "v1-wovr-meta.apta.hex",
        "v1-wovr-wdtl.apta.hex",
    ] {
        let bytes = fixture(f);
        assert_eq!(roundtrip(&bytes), bytes, "{f}");
    }
}
#[test]
fn combined_meta_detail_roundtrip_and_aggregate_limits() {
    let bytes = generated();
    assert_eq!(roundtrip(&bytes), bytes);
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    assert_eq!(c.flags, 1);
    let result = c.parse_waveform_result(ParseOptions::default()).unwrap();
    assert_eq!(result.tile_count(), 2);
    assert!(result.tile(2).is_none());
    assert_eq!(result.metadata.unwrap().comments, Some("UTF-8: čćž 😀"));
    for options in [
        ParseOptions {
            maximum_detail_tiles: 1,
            ..Default::default()
        },
        ParseOptions {
            maximum_waveform_columns: 3,
            ..Default::default()
        },
    ] {
        assert_eq!(
            c.parse_waveform_result(options).unwrap_err(),
            Error::LimitExceeded
        );
    }
}
fn put32(b: &mut [u8], p: usize, v: u32) {
    b[p..p + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(b: &mut [u8], p: usize, v: u64) {
    b[p..p + 8].copy_from_slice(&v.to_le_bytes());
}
#[test]
fn duplicate_detail_identity_across_sections_rejected() {
    let orig = generated();
    // Replace META with a second copy of WDTL and repair all framing.
    let c = Container::parse(&orig, ParseOptions::default()).unwrap();
    let payload = c.section(1).unwrap().payload;
    let offset = (orig.len() + 7) & !7;
    let mut bytes = orig.clone();
    bytes.resize(offset, 0);
    bytes.extend_from_slice(payload);
    let len = bytes.len();
    bytes[176..180].copy_from_slice(b"WDTL");
    put64(&mut bytes, 184, offset as u64);
    put64(&mut bytes, 192, payload.len() as u64);
    put64(&mut bytes, 200, payload.len() as u64);
    put32(&mut bytes, 208, crc32c(payload));
    // Removed META payload is now padding.
    let meta = c.section(2).unwrap().payload;
    let meta_start = orig.len() - meta.len();
    bytes[meta_start..orig.len()].fill(0);
    put64(&mut bytes, 32, len as u64);
    let crc = crc32c(&bytes[..92]);
    put32(&mut bytes, 92, crc);
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    assert_eq!(
        c.parse_waveform_result(ParseOptions::default())
            .unwrap_err(),
        Error::Corrupt
    );
}
#[test]
fn malformed_meta_cannot_hide_behind_valid_overview() {
    let mut bytes = generated();
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    let start = bytes.len() - c.section(2).unwrap().payload.len();
    bytes[start] = 0xff;
    let crc = crc32c(&bytes[start..]);
    put32(&mut bytes, 208, crc);
    let c = Container::parse(&bytes, ParseOptions::default()).unwrap();
    assert_eq!(
        c.parse_waveform(ParseOptions::default()).unwrap_err(),
        Error::Corrupt
    );
}
#[test]
#[ignore = "requires APTA_C_CONTAINER_ORACLE"]
fn c_reserialization_is_byte_exact() {
    use std::io::Write;
    use std::process::{Command, Stdio};
    let oracle = std::env::var_os("APTA_C_CONTAINER_ORACLE").expect("set APTA_C_CONTAINER_ORACLE");
    for bytes in [
        generated(),
        generated_empty_metadata(),
        fixture("v1-wovr-meta.apta.hex"),
        fixture("v1-wovr-wdtl.apta.hex"),
    ] {
        let mut child = Command::new(&oracle)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        child.stdin.take().unwrap().write_all(&bytes).unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(
            output.status.success(),
            "C oracle failed {:?}: {}",
            output.status,
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(output.stdout, bytes);
    }
}
