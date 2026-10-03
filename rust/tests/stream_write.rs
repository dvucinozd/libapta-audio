// SPDX-License-Identifier: Apache-2.0
use libapta::{result::*, stream::Output, stream_write, *};
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

struct Sink {
    bytes: Vec<u8>,
    pos: usize,
    progress: usize,
    largest: usize,
    writes: usize,
    seeks: usize,
    flushes: usize,
    fail: u8,
}
impl Sink {
    fn new(n: usize) -> Self {
        Self {
            bytes: vec![0xa5; n],
            pos: 0,
            progress: 3,
            largest: 0,
            writes: 0,
            seeks: 0,
            flushes: 0,
            fail: 0,
        }
    }
}
impl Output for Sink {
    fn write(&mut self, b: &[u8]) -> Result<usize, Error> {
        self.writes += 1;
        self.largest = self.largest.max(b.len());
        match self.fail {
            1 => return Err(Error::Cancelled),
            2 => return Ok(0),
            3 => return Ok(b.len() + 1),
            _ => {}
        }
        let n = b.len().min(self.progress);
        self.bytes[self.pos..self.pos + n].copy_from_slice(&b[..n]);
        self.pos += n;
        Ok(n)
    }
    fn seek(&mut self, p: u64) -> Result<(), Error> {
        self.seeks += 1;
        if self.fail == 4 || (self.fail == 6 && self.seeks == 2) {
            return Err(Error::Source);
        }
        self.pos = usize::try_from(p).map_err(|_| Error::Source)?;
        Ok(())
    }
    fn flush(&mut self) -> Result<(), Error> {
        self.flushes += 1;
        if self.fail == 5 {
            Err(Error::Cancelled)
        } else {
            Ok(())
        }
    }
}
#[test]
fn all_fixtures_exact_with_partial_callback_progress() {
    for name in FIXTURES {
        let bytes = fixture(name);
        with_input(&bytes, |r| {
            let mut sink = Sink::new(bytes.len());
            assert_eq!(
                stream_write::write(&r, &mut sink, Limits::default()).unwrap(),
                bytes.len()
            );
            assert_eq!(sink.bytes, bytes, "{name}");
            assert!(sink.largest <= 144);
            assert!(sink.writes > 10);
            assert_eq!((sink.seeks, sink.flushes, sink.pos), (2, 1, bytes.len()));
        });
    }
}
#[test]
fn callback_errors_stalls_and_overreported_progress_propagate() {
    let bytes = fixture("v1-all-standard-sections.apta.hex");
    with_input(&bytes, |r| {
        for fail in 1..=6 {
            let mut sink = Sink::new(bytes.len());
            sink.fail = fail;
            assert_eq!(
                stream_write::write(&r, &mut sink, Limits::default()),
                Err(if fail == 1 || fail == 5 {
                    Error::Cancelled
                } else {
                    Error::Source
                })
            );
            if fail != 5 {
                assert_eq!(sink.flushes, 0);
            }
        }
    });
}
#[test]
fn invalid_input_and_limits_fail_before_callbacks() {
    let bytes = fixture("dj-sections-v1-combined.apta.hex");
    with_input(&bytes, |r| {
        for limits in [
            Limits {
                maximum_key_candidates: 0,
                ..Default::default()
            },
            Limits {
                container: libapta::container::ParseOptions {
                    maximum_file_bytes: 1,
                    ..Default::default()
                },
                ..Default::default()
            },
            Limits {
                container: libapta::container::ParseOptions {
                    maximum_section_count: 1,
                    ..Default::default()
                },
                ..Default::default()
            },
        ] {
            let mut sink = Sink::new(bytes.len());
            assert_eq!(
                stream_write::write(&r, &mut sink, limits),
                Err(Error::LimitExceeded)
            );
            assert_eq!((sink.writes, sink.seeks, sink.flushes), (0, 0, 0));
        }
        let mut bad = r;
        bad.key.as_mut().unwrap().tonic = 12;
        let mut sink = Sink::new(bytes.len());
        assert_eq!(
            stream_write::write(&bad, &mut sink, Limits::default()),
            Err(Error::InvalidArgument)
        );
        assert_eq!((sink.writes, sink.seeks, sink.flushes), (0, 0, 0));
    });
}
#[test]
fn metadata_boundaries_empty_present_and_quality_sorting() {
    let bytes = fixture("dj-sections-v1-combined.apta.hex");
    with_input(&bytes, |r| {
        let text = "x".repeat(4096);
        let source = [0xff; 1024];
        for metadata in [
            Metadata::default(),
            Metadata {
                producer_name: Some("čćž"),
                producer_version_string: Some(""),
                backend_name: Some("native"),
                backend_version: Some("1"),
                creation_unix_time: Some(u64::MAX),
                application_source_id: Some(SourceId::Bytes(&source)),
                comments: Some(&text),
            },
        ] {
            let mut quality = r.quality.to_vec();
            quality.reverse();
            let input = ResultInput {
                metadata: Some(metadata),
                quality: &quality,
                ..r
            };
            let n = serialized_size(&input).unwrap();
            let mut expected = vec![0; n];
            write(&input, &mut expected, Limits::default()).unwrap();
            let mut sink = Sink::new(n);
            stream_write::write(&input, &mut sink, Limits::default()).unwrap();
            assert_eq!(sink.bytes, expected);
            assert!(sink.largest <= 144);
        }
    });
}

#[test]
fn partial_and_unknown_duration_framing_matches_buffer_output() {
    let bytes = fixture("v1-all-standard-sections.apta.hex");
    with_input(&bytes, |r| {
        for variant in 0..4 {
            let mut input = r;
            match variant {
                0 => input.overview.state = FeatureState::Provisional,
                1 => input.tempo.as_mut().unwrap().selected.state = FeatureState::Stable,
                2 => input.local_grid.as_mut().unwrap().state = FeatureState::Provisional,
                _ => {
                    input.global_grid.as_mut().unwrap().state = FeatureState::Provisional;
                    input.revision.as_mut().unwrap().state = RevisionState::Pending;
                }
            }
            let n = serialized_size(&input).unwrap();
            let mut expected = vec![0; n];
            write(&input, &mut expected, Limits::default()).unwrap();
            let mut sink = Sink::new(n);
            stream_write::write(&input, &mut sink, Limits::default()).unwrap();
            assert_eq!(sink.bytes, expected);
        }
    });
    let bytes = fixture("v1-wovr-only.apta.hex");
    with_input(&bytes, |mut r| {
        r.overview.state = FeatureState::Provisional;
        r.source.total_frames = None;
        let mut expected = vec![0; bytes.len()];
        write(&r, &mut expected, Limits::default()).unwrap();
        let mut sink = Sink::new(bytes.len());
        stream_write::write(&r, &mut sink, Limits::default()).unwrap();
        assert_eq!(sink.bytes, expected);
    });
}

fn encode(r: &ResultInput<'_>) -> Vec<u8> {
    let mut out = vec![0; serialized_size(r).unwrap()];
    write(r, &mut out, Limits::default()).unwrap();
    out
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
fn all_ten_sections_and_cross_feature_preflight() {
    let bytes = combined();
    with_input(&bytes, |r| {
        let mut sink = Sink::new(bytes.len());
        stream_write::write(&r, &mut sink, Limits::default()).unwrap();
        assert_eq!(sink.bytes, bytes);
        let mut quality = r.quality.to_vec();
        quality[0].feature = GRID_LOCKING;
        let invalid = ResultInput {
            quality: &quality,
            ..r
        };
        let mut sink = Sink::new(bytes.len());
        assert_eq!(
            stream_write::write(&invalid, &mut sink, Limits::default()),
            Err(Error::InvalidArgument)
        );
        assert_eq!((sink.writes, sink.seeks, sink.flushes), (0, 0, 0));
        let mut segments = r.meter.unwrap().segments.to_vec();
        segments[0].downbeat_ordinal = 1;
        let mut meter = r.meter.unwrap();
        meter.downbeat_ordinal = 1;
        meter.segments = &segments;
        let invalid = ResultInput {
            meter: Some(meter),
            ..r
        };
        assert_eq!(
            stream_write::write(&invalid, &mut sink, Limits::default()),
            Err(Error::InvalidArgument)
        );
        assert_eq!((sink.writes, sink.seeks, sink.flushes), (0, 0, 0));
    });
}

#[test]
fn unreferenced_waveform_storage_fails_before_output() {
    let bytes = fixture("v1-wovr-only.apta.hex");
    with_input(&bytes, |r| {
        let mut columns = r.overview.columns.to_vec();
        columns.push(columns[0]);
        let mut overview = r.overview;
        overview.columns = &columns;
        let invalid = ResultInput { overview, ..r };
        let mut sink = Sink::new(bytes.len() + 10);
        assert_eq!(
            stream_write::write(&invalid, &mut sink, Limits::default()),
            Err(Error::InvalidArgument)
        );
        assert_eq!((sink.writes, sink.seeks, sink.flushes), (0, 0, 0));
    });
}
