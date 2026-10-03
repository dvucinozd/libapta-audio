// SPDX-License-Identifier: Apache-2.0
use libapta::{
    container::{crc32c, Container, ParseOptions},
    result::*,
    *,
};
const FIXTURES: &[&str] = &[
    "v1-wovr-only.apta.hex",
    "v1-wovr-meta.apta.hex",
    "v1-wovr-wdtl.apta.hex",
    "v1-wovr-temp.apta.hex",
    "v1-wovr-temp-lgrd.apta.hex",
    "v1-wovr-temp-ggrd-revn.apta.hex",
    "v1-all-standard-sections.apta.hex",
    "dj-sections-v1-combined.apta.hex",
];
fn fixture(name: &str) -> Vec<u8> {
    let dir = if name.starts_with("dj-") {
        "../tests/fixtures"
    } else {
        "../conformance/fixtures/container-v1-suite"
    };
    let text = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join(dir)
            .join(name),
    )
    .unwrap();
    let h: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    (0..h.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
        .collect()
}
fn with_input<T>(bytes: &[u8], f: impl FnOnce(ResultInput<'_>) -> T) -> T {
    let p = parse(bytes, Limits::default()).unwrap();
    let w = p.waveform;
    let mut spans = vec![WaveformSpan::default(); w.overview.span_count()];
    let mut columns = vec![WaveformColumn::default(); w.overview.column_count()];
    let overview = w.overview.copy_into(&mut spans, &mut columns).unwrap();
    let tc: Vec<Vec<_>> = (0..w.tile_count())
        .map(|i| {
            let t = w.tile(i).unwrap();
            (0..t.column_count())
                .map(|j| t.column(j).unwrap())
                .collect()
        })
        .collect();
    let tiles: Vec<_> = tc
        .iter()
        .enumerate()
        .map(|(i, c)| {
            let t = w.tile(i).unwrap();
            WaveformTile {
                level_id: t.level_id,
                tile_index: t.tile_index,
                first_frame: t.first_frame,
                end_frame: t.end_frame,
                first_column_index: t.first_column_index,
                state: t.state,
                confidence: t.confidence,
                columns: c,
            }
        })
        .collect();
    let candidates: Vec<_> = p
        .tempo
        .map(|t| {
            (0..t.candidate_count())
                .map(|i| t.candidate(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let tempo = p.tempo.map(|t| TempoView {
        selected: t.selected(),
        candidates: &candidates,
    });
    let mut gs: Vec<_> = p
        .global_grid
        .map(|g| {
            (0..g.segment_count())
                .map(|i| g.segment(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let mut gb: Vec<_> = p
        .global_grid
        .map(|g| (0..g.beat_count()).map(|i| g.beat(i).unwrap()).collect())
        .unwrap_or_default();
    let global_grid = p
        .global_grid
        .map(|g| g.copy_into(&mut gs, &mut gb).unwrap());
    let mut kc: Vec<_> = p
        .key
        .map(|k| {
            (0..k.candidate_count())
                .map(|i| k.candidate(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let key = p.key.map(|k| k.copy_into(&mut kc).unwrap());
    let mut ms: Vec<_> = p
        .meter
        .map(|m| {
            (0..m.segment_count())
                .map(|i| m.segment(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let meter = p.meter.map(|m| m.copy_into(&mut ms).unwrap());
    let qr: Vec<_> = p
        .quality
        .map(|q| {
            (0..q.record_count())
                .map(|i| q.record(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    f(ResultInput {
        source: p.source,
        overview,
        tiles: &tiles,
        metadata: w.metadata,
        tempo,
        local_grid: p.local_grid,
        global_grid,
        revision: p.revision,
        key,
        meter,
        quality: &qr,
    })
}
fn encode(r: &ResultInput<'_>) -> Vec<u8> {
    let mut out = vec![0xa5; serialized_size(r).unwrap()];
    assert_eq!(write(r, &mut out, Limits::default()).unwrap(), out.len());
    out
}
fn roundtrip(b: &[u8]) -> Vec<u8> {
    with_input(b, |r| encode(&r))
}
fn mutate(b: &mut [u8], tag: &[u8; 4], f: impl FnOnce(&mut [u8])) {
    let n = u32::from_le_bytes(b[20..24].try_into().unwrap()) as usize;
    let entry = (0..n)
        .map(|i| 96 + i * 40)
        .find(|p| &b[*p..*p + 4] == tag)
        .unwrap();
    let p = u64::from_le_bytes(b[entry + 8..entry + 16].try_into().unwrap()) as usize;
    let n = u64::from_le_bytes(b[entry + 16..entry + 24].try_into().unwrap()) as usize;
    f(&mut b[p..p + n]);
    let crc = crc32c(&b[p..p + n]);
    b[entry + 32..entry + 36].copy_from_slice(&crc.to_le_bytes());
    assert!(Container::parse(b, ParseOptions::default()).is_ok());
}
#[test]
fn all_canonical_full_results_roundtrip_exactly() {
    for name in FIXTURES {
        let b = fixture(name);
        assert_eq!(roundtrip(&b), b, "{name}");
    }
}
#[test]
fn dependencies_and_cross_feature_conflicts_are_rejected() {
    let mut b = fixture("v1-wovr-temp-lgrd.apta.hex");
    mutate(&mut b, b"LGRD", |p| {
        p[120..124].copy_from_slice(&121000u32.to_le_bytes())
    });
    assert_eq!(parse(&b, Limits::default()).unwrap_err(), Error::Corrupt);
    let mut b = fixture("v1-wovr-temp-ggrd-revn.apta.hex");
    mutate(&mut b, b"REVN", |p| {
        p[8..12].copy_from_slice(&2u32.to_le_bytes())
    });
    assert_eq!(parse(&b, Limits::default()).unwrap_err(), Error::Corrupt);
    let mut b = fixture("dj-sections-v1-combined.apta.hex");
    mutate(&mut b, b"CONF", |p| {
        p[16..24].copy_from_slice(&BPM.to_le_bytes())
    });
    assert_eq!(parse(&b, Limits::default()).unwrap_err(), Error::Corrupt);
}
fn combined() -> Vec<u8> {
    let base = fixture("v1-all-standard-sections.apta.hex");
    let dj = fixture("dj-sections-v1-combined.apta.hex");
    with_input(&base, |r| {
        with_input(&dj, |d| {
            let mut segments = d.meter.unwrap().segments.to_vec();
            segments.truncate(1);
            segments[0].first_frame = 0;
            segments[0].end_frame = r.source.total_frames.unwrap();
            segments[0].downbeat_frame = 0;
            segments[0].downbeat_ordinal = 0;
            let mut meter = d.meter.unwrap();
            meter.downbeat_frame = 0;
            meter.downbeat_ordinal = 0;
            meter.segments = &segments;
            let mut key = d.key.unwrap();
            key.end_frame = r.source.total_frames.unwrap();
            encode(&ResultInput {
                key: Some(key),
                meter: Some(meter),
                quality: d.quality,
                ..r
            })
        })
    })
}
#[test]
fn all_features_meter_grid_links_and_quality_roundtrip() {
    let b = combined();
    let p = parse(&b, Limits::default()).unwrap();
    assert!(
        p.tempo.is_some()
            && p.local_grid.is_some()
            && p.global_grid.is_some()
            && p.key.is_some()
            && p.meter.is_some()
            && p.quality.is_some()
    );
    assert_eq!(roundtrip(&b), b);
    let mut bad = b.clone();
    mutate(&mut bad, b"MTRD", |p| {
        p[24..32].copy_from_slice(&1i64.to_le_bytes());
        p[72..80].copy_from_slice(&1i64.to_le_bytes());
    });
    assert_eq!(parse(&bad, Limits::default()).unwrap_err(), Error::Corrupt);
}
#[test]
fn full_result_limits_and_output_exhaustion() {
    let b = combined();
    for l in [
        Limits {
            maximum_grid_segments: 0,
            ..Default::default()
        },
        Limits {
            maximum_key_candidates: 1,
            ..Default::default()
        },
        Limits {
            maximum_meter_segments: 0,
            ..Default::default()
        },
        Limits {
            maximum_quality_records: 1,
            ..Default::default()
        },
        Limits {
            container: ParseOptions {
                maximum_file_bytes: b.len() - 1,
                ..Default::default()
            },
            ..Default::default()
        },
    ] {
        assert_eq!(parse(&b, l).unwrap_err(), Error::LimitExceeded);
    }
    with_input(&b, |r| {
        let n = serialized_size(&r).unwrap();
        let mut out = vec![0xa5; n - 1];
        assert_eq!(
            write(&r, &mut out, Limits::default()),
            Err(Error::BufferTooSmall)
        );
        assert!(out.iter().all(|x| *x == 0xa5));
    });
}
#[test]
#[ignore = "requires APTA_C_CONTAINER_ORACLE"]
fn c_reserialization_is_byte_exact() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    let oracle = std::env::var_os("APTA_C_CONTAINER_ORACLE").expect("set APTA_C_CONTAINER_ORACLE");
    for b in FIXTURES
        .iter()
        .map(|name| roundtrip(&fixture(name)))
        .chain(core::iter::once(combined()))
    {
        let mut child = Command::new(&oracle)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        child.stdin.take().unwrap().write_all(&b).unwrap();
        let o = child.wait_with_output().unwrap();
        assert!(
            o.status.success(),
            "C oracle failed {:?}: {}",
            o.status,
            String::from_utf8_lossy(&o.stderr)
        );
        assert_eq!(o.stdout, b);
    }
}
