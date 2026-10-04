// SPDX-License-Identifier: Apache-2.0
use libapta::{
    band::{BandFilter, BandSums},
    waveform::WaveformAccumulator,
    Error, WaveformColumn,
};

fn column() -> WaveformColumn {
    WaveformColumn {
        minimum: -12000,
        maximum: 19000,
        rms: 32000,
        flags: 5,
        ..Default::default()
    }
}

#[test]
fn rejects_invalid_values_without_changing_filter_or_sums() {
    for rate in [0, 768001, u32::MAX] {
        assert!(matches!(BandFilter::new(rate), Err(Error::InvalidArgument)));
    }
    let mut filter = BandFilter::new(48000).unwrap();
    filter.split(0.5).unwrap();
    let mut expected = filter;
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1.01, -1.01] {
        assert_eq!(filter.split(bad), Err(Error::InvalidArgument));
    }
    assert_eq!(
        filter.split(-0.25).unwrap().map(f32::to_bits),
        expected.split(-0.25).unwrap().map(f32::to_bits)
    );
    let mut sums = BandSums::default();
    sums.add([0.5, -0.5, 0.25]).unwrap();
    let expected = sums;
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
        assert_eq!(sums.add([0.5, bad, 0.5]), Err(Error::InvalidArgument));
        assert_eq!(sums, expected);
    }
    for count in [0, 65537, u32::MAX] {
        assert_eq!(sums.apply(column(), count), Err(Error::InvalidArgument));
    }
    assert_eq!(
        sums.apply(WaveformColumn::default(), 1),
        Err(Error::InvalidArgument)
    );
}

#[test]
fn sums_clamp_truncate_and_cover_maximum_column_size() {
    let mut sums = BandSums::default();
    for _ in 0..65536 {
        sums.add([2.0, -2.0, 0.5]).unwrap();
    }
    let output = sums.apply(column(), 65536).unwrap();
    assert_eq!(
        (output.low, output.mid, output.high, output.flags),
        (255, 255, 127, 13)
    );
    assert_eq!(
        (output.minimum, output.maximum, output.rms),
        (-12000, 19000, 32000)
    );
    let mut sums = BandSums::default();
    sums.add([f32::from_bits(1), 1.0 / 32768.0, 1.0]).unwrap();
    let output = sums.apply(column(), 1).unwrap();
    assert_eq!((output.low, output.mid, output.high), (0, 0, 255));
}

fn signals() -> Vec<Vec<f32>> {
    let mut signals = vec![
        vec![0.0; 257],
        vec![1.0; 257],
        vec![-1.0; 257],
        (0..1025)
            .map(|i| if i % 2 == 0 { 1.0 } else { -1.0 })
            .collect(),
        (0..1025)
            .map(|i| if i == 0 || i == 512 { 1.0 } else { 0.0 })
            .collect(),
        (0..257)
            .map(|i| f32::from_bits(if i % 2 == 0 { 1 } else { 0x8000_0001 }))
            .collect(),
    ];
    let mut seed = 0x1234_5678u32;
    signals.push(
        (0..4097)
            .map(|_| {
                seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                ((seed >> 8) as f32 / 8388608.0) - 1.0
            })
            .collect(),
    );
    signals
}

#[test]
#[ignore = "requires APTA_C_BAND_ORACLE"]
fn filter_bits_and_quantized_columns_match_c() {
    use std::io::Write;
    let oracle = std::env::var_os("APTA_C_BAND_ORACLE").expect("set APTA_C_BAND_ORACLE");
    // Float tolerance is zero: compare all three f32 output bit patterns.
    // Published integer fields likewise require exact equality in both C paths.
    for rate in [1, 8000, 11025, 22050, 44100, 48000, 96000, 192000, 768000] {
        for (signal_index, samples) in signals().iter().enumerate() {
            let fpc = if signal_index % 2 == 0 { 64 } else { 1024 };
            let mut child = std::process::Command::new(&oracle)
                .args([rate.to_string(), fpc.to_string()])
                .stdin(std::process::Stdio::piped())
                .stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::piped())
                .spawn()
                .unwrap();
            let input: Vec<u8> = samples
                .iter()
                .flat_map(|sample| sample.to_ne_bytes())
                .collect();
            child.stdin.take().unwrap().write_all(&input).unwrap();
            let output = child.wait_with_output().unwrap();
            assert!(
                output.status.success(),
                "rate {rate} signal {signal_index}: {}",
                String::from_utf8_lossy(&output.stderr)
            );
            let output = String::from_utf8(output.stdout).unwrap();
            let mut lines = output.lines();
            let coefficients = lines.next().unwrap();
            assert!(coefficients.starts_with("K "));
            let mut filter = BandFilter::new(rate).unwrap();
            let mut sums = BandSums::default();
            let mut accumulator = WaveformAccumulator::default();
            let mut columns = vec![];
            for (index, sample) in samples.iter().copied().enumerate() {
                let split = filter.split(sample).unwrap();
                let line = lines.next().unwrap();
                let expected: Vec<u32> = line
                    .strip_prefix("F ")
                    .unwrap()
                    .split_whitespace()
                    .map(|v| u32::from_str_radix(v, 16).unwrap())
                    .collect();
                assert_eq!(
                    split.map(f32::to_bits).as_slice(),
                    expected,
                    "rate {rate} signal {signal_index} frame {index}, C {coefficients}"
                );
                sums.add(split).unwrap();
                accumulator
                    .push_normalized(sample, sample.abs() >= 1.0)
                    .unwrap();
                if accumulator.sample_count() == fpc || index + 1 == samples.len() {
                    columns.push(
                        sums.apply(accumulator.column(), accumulator.sample_count())
                            .unwrap(),
                    );
                    sums = BandSums::default();
                    accumulator.clear();
                }
            }
            assert_eq!(sequential_columns(samples, rate, fpc), columns);
            for bounded in [false, true] {
                let header: Vec<u64> = lines
                    .next()
                    .unwrap()
                    .strip_prefix("P ")
                    .unwrap()
                    .split_whitespace()
                    .map(|v| v.parse().unwrap())
                    .collect();
                assert_eq!(header[0], u64::from(bounded));
                // Baseline pooled publication omits the feature bit while its
                // columns do contain HAS_3BAND. Keep this discrepancy explicit.
                let features = libapta::result::WAVEFORM_OVERVIEW
                    | if bounded {
                        0
                    } else {
                        libapta::result::WAVEFORM_3BAND
                    };
                assert_eq!(header[1], features);
                for column in &columns {
                    let line = lines.next().unwrap();
                    let fields: Vec<i32> = line
                        .strip_prefix("C ")
                        .unwrap()
                        .split_whitespace()
                        .map(|v| v.parse().unwrap())
                        .collect();
                    assert_eq!(
                        fields,
                        [
                            i32::from(column.minimum),
                            i32::from(column.maximum),
                            i32::from(column.rms),
                            i32::from(column.low),
                            i32::from(column.mid),
                            i32::from(column.high),
                            i32::from(column.flags)
                        ],
                        "rate {rate} signal {signal_index} bounded {bounded}"
                    );
                }
            }
            assert!(lines.next().is_none());
        }
    }
}

fn sequential_columns(samples: &[f32], rate: u32, fpc: u32) -> Vec<WaveformColumn> {
    use libapta::{session::*, waveform::NormalizedSample};
    let config = SessionConfig {
        sample_rate: rate,
        channel_count: 1,
        total_frames: samples.len() as u64,
        frames_per_column: fpc,
    };
    let n = samples.len().div_ceil(fpc as usize);
    let mut queue = vec![NormalizedSample::default(); 300];
    let mut output = vec![WaveformColumn::default(); n];
    let mut sums = vec![BandSums::default(); n];
    let mut session = Session::new(config, &mut queue, &mut output).unwrap();
    session.enable_three_band(&mut sums).unwrap();
    let token = CancellationToken::new();
    let mut accepted = 0;
    while accepted < samples.len() {
        accepted += session.push_interleaved(&samples[accepted..]).unwrap();
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 73,
                    maximum_steps: 1,
                },
                &token,
            )
            .unwrap();
    }
    session.finish_input().unwrap();
    while session.state() != SessionState::Complete {
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 19,
                    maximum_steps: 1,
                },
                &token,
            )
            .unwrap();
    }
    session.columns().to_vec()
}

#[test]
fn sequential_queue_wrap_tail_and_unknown_duration_preserve_bands() {
    use libapta::{session::*, waveform::NormalizedSample};
    for samples in signals() {
        let expected = sequential_columns(&samples, 48000, 64);
        let mut queue = vec![NormalizedSample::default(); samples.len()];
        let mut output = vec![WaveformColumn::default(); expected.len()];
        let mut sums = vec![BandSums::default(); expected.len()];
        let mut session = Session::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: TOTAL_FRAMES_UNKNOWN,
                frames_per_column: 64,
            },
            &mut queue,
            &mut output,
        )
        .unwrap();
        session.enable_three_band(&mut sums).unwrap();
        session.push_interleaved(&samples).unwrap();
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(session.columns(), expected);
    }
}

#[test]
fn sparse_seek_filter_history_follows_processing_and_snapshots_sort_columns() {
    use libapta::{
        session::*,
        sparse::*,
        waveform::{NormalizedSample, PcmView},
        FrameRange, WaveformSpan,
    };
    let mut accumulators = [SparseAccumulator::default(); 4];
    let mut ranges = [FrameRange {
        first_frame: 0,
        end_frame: 0,
    }; 4];
    let mut nodes = [QueuedBlock::default(); 2];
    let mut pcm = [NormalizedSample::default(); NODE_FRAMES * 2];
    let mut spans = [WaveformSpan::default(); 4];
    let mut columns = [WaveformColumn::default(); 4];
    let mut sums = [BandSums::default(); 4];
    let mut session = SparseSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 193,
            frames_per_column: 64,
        },
        Workspace {
            accumulators: &mut accumulators,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut columns,
        },
    )
    .unwrap();
    session.enable_three_band(&mut sums).unwrap();
    let token = CancellationToken::new();
    session
        .push_at(128, PcmView::F32Interleaved(&[0.75; 65]))
        .unwrap();
    session.process(WorkBudget::default(), &token).unwrap();
    session
        .push_at(0, PcmView::F32Interleaved(&[-0.25; 64]))
        .unwrap();
    session.finish_input().unwrap();
    session.process(WorkBudget::default(), &token).unwrap();
    let mut filter = BandFilter::new(48000).unwrap();
    let mut expected = [WaveformColumn::default(); 3];
    for (out, (sample, count)) in expected
        .iter_mut()
        .zip([(0.75, 64), (0.75, 1), (-0.25, 64)])
    {
        let mut sum = BandSums::default();
        let mut accumulator = WaveformAccumulator::default();
        for _ in 0..count {
            sum.add(filter.split(sample).unwrap()).unwrap();
            accumulator.push_normalized(sample, false).unwrap();
        }
        *out = sum.apply(accumulator.column(), count).unwrap();
    }
    let snapshot = session.snapshot().unwrap();
    assert_eq!(snapshot.columns, &[expected[2], expected[0], expected[1]]);
    assert_eq!(snapshot.spans.len(), 2);
}

#[test]
#[ignore = "requires APTA_C_BAND_ORACLE"]
fn scheduled_sparse_publication_matches_c_quantized_columns() {
    use libapta::{
        owned_result::Storage,
        publication::{PublishedSparseSession, ResultPool},
        scheduler::RequestSlot,
        session::*,
        sparse::*,
        waveform::{NormalizedSample, PcmView},
        *,
    };
    use std::io::Write;
    let samples: Vec<f32> = (0..193)
        .map(|i| if i < 64 { -0.25 } else { 0.75 })
        .collect();
    let mut child = std::process::Command::new(std::env::var_os("APTA_C_BAND_ORACLE").unwrap())
        .args(["48000", "64", "sparse"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(
            &samples
                .iter()
                .flat_map(|v| v.to_ne_bytes())
                .collect::<Vec<_>>(),
        )
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let output = String::from_utf8(output.stdout).unwrap();
    let mut accumulators = [SparseAccumulator::default(); 4];
    let mut ranges = [FrameRange {
        first_frame: 0,
        end_frame: 0,
    }; 4];
    let mut nodes = [QueuedBlock::default(); 2];
    let mut pcm = [NormalizedSample::default(); NODE_FRAMES * 2];
    let mut spans = [WaveformSpan::default(); 4];
    let mut columns = [WaveformColumn::default(); 4];
    let mut sums = [BandSums::default(); 4];
    let mut requests = [RequestSlot::default(); 16];
    let mut slot_columns = [[WaveformColumn::default(); 4]; 2];
    let mut slot_spans = [[WaveformSpan::default(); 2]; 2];
    let [a, b] = &mut slot_columns;
    let [sa, sb] = &mut slot_spans;
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(193),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let pool = ResultPool::new(
        source,
        [
            Storage {
                overview_columns: a,
                overview_spans: sa,
                ..Default::default()
            },
            Storage {
                overview_columns: b,
                overview_spans: sb,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let retained = {
        let mut session = PublishedSparseSession::new_scheduled(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 193,
                frames_per_column: 64,
            },
            Workspace {
                accumulators: &mut accumulators,
                ranges: &mut ranges,
                nodes: &mut nodes,
                pcm: &mut pcm,
                snapshot_spans: &mut spans,
                snapshot_columns: &mut columns,
            },
            &pool,
            &mut requests,
        )
        .unwrap();
        session.enable_three_band(&mut sums).unwrap();
        session
            .request_region(RegionRequest {
                range: FrameRange {
                    first_frame: 128,
                    end_frame: 193,
                },
                priority: 200,
                feature_mask: result::WAVEFORM_OVERVIEW,
                request_id: 0,
                soft_deadline_monotonic_ns: 0,
            })
            .unwrap();
        session
            .push_at(0, PcmView::F32Interleaved(&samples[..64]))
            .unwrap();
        session
            .push_at(128, PcmView::F32Interleaved(&samples[128..]))
            .unwrap();
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        pool.acquire().unwrap()
    };
    let native = retained.view();
    let overview = native.overview.unwrap();
    let c_columns: Vec<Vec<i32>> = output
        .lines()
        .filter_map(|line| line.strip_prefix("C "))
        .map(|line| {
            line.split_whitespace()
                .map(|v| v.parse().unwrap())
                .collect()
        })
        .collect();
    assert_eq!(c_columns.len(), overview.columns.len() * 2);
    for (index, expected) in c_columns.iter().enumerate() {
        let column = overview.columns[index % overview.columns.len()];
        assert_eq!(
            expected.as_slice(),
            [
                i32::from(column.minimum),
                i32::from(column.maximum),
                i32::from(column.rms),
                i32::from(column.low),
                i32::from(column.mid),
                i32::from(column.high),
                i32::from(column.flags)
            ]
        );
    }
    // Native graphs derive the band bit from flags. C's bounded pool omits it.
    assert_eq!(
        retained.available_features(),
        result::WAVEFORM_OVERVIEW | result::WAVEFORM_3BAND
    );
    let headers: Vec<_> = output
        .lines()
        .filter(|line| line.starts_with("P "))
        .collect();
    assert_eq!(headers, ["P 0 5", "P 1 1"]);
}

#[test]
fn band_attachment_preflight_preserves_storage_and_overview_only_output() {
    use libapta::{session::*, waveform::NormalizedSample};
    let mut queue = [NormalizedSample::default(); 64];
    let mut output = [WaveformColumn::default(); 1];
    let mut short = [];
    let mut sums = [BandSums::default(); 1];
    sums[0].add([0.5, 0.5, 0.5]).unwrap();
    let mut session = Session::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 64,
            frames_per_column: 64,
        },
        &mut queue,
        &mut output,
    )
    .unwrap();
    assert_eq!(
        session.enable_three_band(&mut short),
        Err(Error::BufferTooSmall)
    );
    session.enable_three_band(&mut sums).unwrap();
    session.push_interleaved(&[0.0; 64]).unwrap();
    session.finish_input().unwrap();
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(
        (
            session.columns()[0].low,
            session.columns()[0].mid,
            session.columns()[0].high
        ),
        (0, 0, 0)
    );
    assert_eq!(session.columns()[0].flags & 8, 8);
    let mut plain = Session::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 64,
            frames_per_column: 64,
        },
        &mut queue,
        &mut output,
    )
    .unwrap();
    plain.push_interleaved(&[0.5; 64]).unwrap();
    plain.finish_input().unwrap();
    plain
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(plain.columns()[0].flags & 8, 0);
    assert_eq!(
        (
            plain.columns()[0].low,
            plain.columns()[0].mid,
            plain.columns()[0].high
        ),
        (0, 0, 0)
    );
}
