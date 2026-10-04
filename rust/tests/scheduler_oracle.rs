// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::{GridStorage, Storage},
    publication::{PublishedSparseSession, ResultPool},
    scheduler::RequestSlot,
    session::{CancellationToken, SessionConfig, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace},
    waveform::{NormalizedSample, PcmView},
    *,
};
use std::io::Write;
fn status(e: Error) -> i128 {
    match e {
        Error::InvalidArgument => -1,
        Error::Unsupported => -3,
        Error::Cancelled => -7,
        Error::Conflict => -10,
        Error::LimitExceeded => -11,
        Error::InvalidState => -12,
        Error::NotAvailable => 4,
        e => panic!("unexpected {e:?}"),
    }
}
fn native(commands: &str, features: u64, request_mask: u64) -> Vec<Vec<i128>> {
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(8192),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let mut ca = [WaveformColumn::default(); 8];
    let mut cb = ca;
    let mut sa = [WaveformSpan::default(); 8];
    let mut sb = sa;
    let mut tempo_a = [TempoCandidate::default(); 3];
    let mut tempo_b = tempo_a;
    let mut coverage_a = [FrameRange::default(); 1];
    let mut coverage_b = coverage_a;
    let segment = GridSegment {
        applicability_range: FrameRange::default(),
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod::default(),
        beat_count: 0,
        nominal_tempo_millibpm: 0,
        confidence: 0,
        state: FeatureState::Provisional,
        flags: 0,
        segment_id: 0,
        revision: 0,
    };
    let mut local_a = [segment];
    let mut local_b = [segment];
    let mut key_a = [KeyCandidate::default(); 3];
    let mut key_b = key_a;
    let mut global_coverage_a = [FrameRange::default(); 1];
    let mut global_coverage_b = global_coverage_a;
    let mut global_a = [segment; 8];
    let mut global_b = global_a;
    let mut beats_a = vec![Beat::default(); 3072];
    let mut beats_b = beats_a.clone();
    let pool = ResultPool::new(
        source,
        [
            Storage {
                overview_columns: &mut ca,
                overview_spans: &mut sa,
                tempo_candidates: &mut tempo_a,
                key_candidates: &mut key_a,
                global_grid: GridStorage {
                    coverage_ranges: &mut global_coverage_a,
                    segments: &mut global_a,
                    beats: &mut beats_a,
                },
                local_grid: GridStorage {
                    coverage_ranges: &mut coverage_a,
                    segments: &mut local_a,
                    beats: &mut [],
                },
                ..Default::default()
            },
            Storage {
                overview_columns: &mut cb,
                overview_spans: &mut sb,
                tempo_candidates: &mut tempo_b,
                key_candidates: &mut key_b,
                global_grid: GridStorage {
                    coverage_ranges: &mut global_coverage_b,
                    segments: &mut global_b,
                    beats: &mut beats_b,
                },
                local_grid: GridStorage {
                    coverage_ranges: &mut coverage_b,
                    segments: &mut local_b,
                    beats: &mut [],
                },
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut acc = [SparseAccumulator::default(); 8];
    let mut ranges = [FrameRange::default(); 16];
    let mut nodes = [QueuedBlock::default(); 16];
    let mut pcm = vec![NormalizedSample::default(); 16 * 4096];
    let mut spans = [WaveformSpan::default(); 8];
    let mut columns = [WaveformColumn::default(); 8];
    let mut slots = [RequestSlot::default(); 16];
    let mut s = PublishedSparseSession::new_scheduled(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 8192,
            frames_per_column: 1024,
        },
        Workspace {
            accumulators: &mut acc,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut columns,
        },
        &pool,
        &mut slots,
    )
    .unwrap();
    let mut bins = vec![libapta::analysis::OnsetBin::default(); 4096];
    let mut flux = vec![0.0; 4096];
    let mut global_bins = vec![analysis::OnsetBin::default(); 16384];
    let mut global_flux = vec![0.0; 16384];
    let mut beats = vec![Beat::default(); 3072];
    if features & result::BPM != 0 {
        s.enable_tempo(&mut bins, &mut flux).unwrap();
    }
    if features & result::GLOBAL_BEATGRID != 0 {
        s.enable_global_grid(false, &mut global_bins, &mut global_flux, &mut beats)
            .unwrap();
    }
    if features & result::MUSICAL_KEY != 0 {
        s.enable_key().unwrap();
    }
    let cancel = CancellationToken::new();
    let mut rows = vec![];
    for line in commands.lines() {
        let words: Vec<_> = line.split_whitespace().collect();
        let v: Vec<u64> = words[1..].iter().map(|s| s.parse().unwrap()).collect();
        let [a, b, c, d, e]: [u64; 5] = v.try_into().unwrap();
        let mut row = vec![0i128; 9];
        let result: Result<(), Error> = match words[0] {
            "A" => s
                .request_region(RegionRequest {
                    range: FrameRange {
                        first_frame: a,
                        end_frame: b,
                    },
                    feature_mask: request_mask,
                    priority: c as u8,
                    soft_deadline_monotonic_ns: d,
                    request_id: e as u32,
                })
                .map(|id| row[1] = id as i128),
            "C" => s.cancel_region_request(a as u32),
            "F" => s.set_focus(Focus {
                playhead_frame: a,
                lookbehind_frames: b,
                lookahead_frames: c,
                priority: d as u8,
                feature_mask: e,
            }),
            "D" => s.next_pcm_request().map(|r| {
                row[1] = r.range.first_frame as i128;
                row[2] = r.range.end_frame as i128;
                row[3] = r.priority as i128;
                row[4] = r.request_token as i128;
                row[5] = r.feature_mask as i128;
            }),
            "R" => s.request_progress(a as u32).map(|r| {
                row[1] = r.request_id as i128;
                row[2] = r.state as u8 as i128;
                row[3] = r.requested_range.first_frame as i128;
                row[4] = r.requested_range.end_frame as i128;
                row[5] = r.requested_features as i128;
                row[6] = r.satisfied_features as i128;
                row[7] = r.progress_permille as i128;
                row[8] = r.diagnostic_code as i128;
            }),
            "P" => s
                .push_at(a, PcmView::S16Interleaved(&vec![c as i16; b as usize]))
                .map(|n| {
                    row[0] = if n == b as usize {
                        0
                    } else if n == 0 {
                        2
                    } else {
                        1
                    };
                    row[1] = n as i128;
                }),
            "W" => s
                .process(
                    WorkBudget {
                        maximum_input_frames: a as u32,
                        maximum_steps: b as u32,
                    },
                    &cancel,
                )
                .map(|p| {
                    // Rust progress exposes queued work separately from the C status.
                    row[0] = if s.session().queued_frames() != 0 {
                        1
                    } else if p.consumed_input_frames == 0 {
                        2
                    } else {
                        0
                    };
                    let r = pool.acquire().unwrap();
                    if let Some(w) = r.overview() {
                        row[1] = w.spans.len() as i128;
                        for span in w.spans {
                            for i in
                                span.first_column_index..span.first_column_index + span.column_count
                            {
                                row[2] |= 1i128 << i;
                            }
                        }
                    }
                }),
            op => panic!("unknown {op}"),
        };
        if let Err(e) = result {
            row[0] = status(e);
        }
        rows.push(row);
    }
    rows
}
#[test]
#[ignore = "requires APTA_C_SCHEDULER_ORACLE"]
fn public_scheduler_demand_progress_and_process_order_match_c() {
    let oracle = std::env::var_os("APTA_C_SCHEDULER_ORACLE").expect("set APTA_C_SCHEDULER_ORACLE");
    let mut scenarios=vec![
 // Equal priorities: earliest nonzero deadline, then FIFO after cancellation.
 "A 0 1024 96 0 0\nA 2048 3072 96 5000 0\nA 4096 5120 96 1000 0\nA 6144 7168 96 0 0\nD 0 0 0 0 0\nC 3 0 0 0 0\nD 0 0 0 0 0\nC 2 0 0 0 0\nD 0 0 0 0 0\nC 1 0 0 0 0\nD 0 0 0 0 0\n".to_string(),
 // Fully accepted explicit target blocks focus fallback; cancellation restores it.
 "F 4096 0 1024 240 1\nA 0 1024 32 0 0\nD 0 0 0 0 0\nP 0 1024 123 0 0\nD 0 0 0 0 0\nC 1 0 0 0 0\nD 0 0 0 0 0\n".to_string(),
 // Sparse nodes deliberately arrive opposite deadline order. Inspect completed columns and progress.
 "A 0 2048 96 5000 0\nA 4096 5120 96 1000 0\nP 0 2048 123 0 0\nP 4096 1024 456 0 0\nR 1 0 0 0 0\nW 1024 4 0 0 0\nR 1 0 0 0 0\nR 2 0 0 0 0\nW 1024 4 0 0 0\nR 1 0 0 0 0\nW 1024 4 0 0 0\nR 1 0 0 0 0\nC 1 0 0 0 0\nR 1 0 0 0 0\n".to_string(),
 // Validation, sparse missing gaps, source-end clipping and stalled request.
 "P 1024 1024 1 0 0\nD 0 0 0 0 0\nA 8192 9000 96 0 0\nD 0 0 0 0 0\nC 1 0 0 0 0\nF 8192 100 500 250 1\nD 0 0 0 0 0\nA 1 1 96 0 0\nC 0 0 0 0 0\nR 999 0 0 0 0\n".to_string()];
    let mut aging = "A 0 1024 32 0 0\nA 4096 5120 96 0 0\n".to_string();
    for _ in 0..40 {
        aging.push_str("D 0 0 0 0 0\n");
    }
    scenarios.push(aging);
    // Near-limit public coordinates/deadlines and explicit IDs on a small
    // source: this qualifies validation/clipping, not giant workspace capacity.
    scenarios.push("A 4294967295 4294967551 96 18446744073709551615 4294967294\nD 0 0 0 0 0\nR 4294967294 0 0 0 0\nC 4294967294 0 0 0 0\nA 18446744073709551359 18446744073709551615 255 18446744073709551614 4294967295\nD 0 0 0 0 0\nC 4294967295 0 0 0 0\nF 18446744073709551614 18446744073709551615 18446744073709551615 240 1\nD 0 0 0 0 0\nP 18446744073709551615 1 123 0 0\n".to_string());
    let mut slots = String::new();
    for id in 1..=16 {
        slots += &format!("A 0 1024 96 0 {id}\nC {id} 0 0 0 0\n");
    }
    slots.push_str("A 0 1024 96 0 1\nA 0 1024 96 0 0\n");
    scenarios.push(slots);
    for (i, commands) in scenarios.iter().enumerate() {
        let mut child = std::process::Command::new(&oracle)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
            .unwrap();
        child
            .stdin
            .take()
            .unwrap()
            .write_all(commands.as_bytes())
            .unwrap();
        let out = child.wait_with_output().unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        let expected: Vec<Vec<i128>> = std::str::from_utf8(&out.stdout)
            .unwrap()
            .lines()
            .map(|l| l.split_whitespace().map(|n| n.parse().unwrap()).collect())
            .collect();
        assert_eq!(
            native(
                commands,
                result::WAVEFORM_OVERVIEW,
                result::WAVEFORM_OVERVIEW
            ),
            expected,
            "scenario {i}"
        );
    }
}

#[test]
#[ignore = "requires APTA_C_SCHEDULER_ORACLE"]
fn musical_focus_requests_and_sparse_processing_match_public_c() {
    let oracle = std::env::var_os("APTA_C_SCHEDULER_ORACLE").unwrap();
    let mask =
        result::WAVEFORM_OVERVIEW | result::BPM | result::LOCAL_BEATGRID | result::GRID_LOCKING;
    let commands = [
        "F 4096 0 1024 240 24\nD 0 0 0 0 0\nA 0 1024 32 0 0\nD 0 0 0 0 0\nP 0 1024 123 0 0\nD 0 0 0 0 0\nC 1 0 0 0 0\nD 0 0 0 0 0\n",
        "A 0 2048 96 5000 0\nA 4096 5120 96 1000 0\nP 0 2048 123 0 0\nP 4096 1024 456 0 0\nR 1 0 0 0 0\nW 1024 4 0 0 0\nR 1 0 0 0 0\nR 2 0 0 0 0\nW 1024 4 0 0 0\nR 1 0 0 0 0\nW 1024 4 0 0 0\nR 1 0 0 0 0\nC 1 0 0 0 0\nR 1 0 0 0 0\n",
        "F 8192 100 18446744073709551615 250 24\nD 0 0 0 0 0\nA 8192 9000 96 0 0\nD 0 0 0 0 0\nR 1 0 0 0 0\nC 1 0 0 0 0\nD 0 0 0 0 0\n",
    ];
    for request_mask in [
        24,
        result::MUSICAL_KEY,
        result::GLOBAL_BEATGRID,
        result::MUSICAL_KEY | result::BPM,
    ] {
        let mask = mask | result::MUSICAL_KEY | result::GLOBAL_BEATGRID;
        for commands in commands {
            let commands = commands.replace(" 24\n", &format!(" {request_mask}\n"));
            let mut child = std::process::Command::new(&oracle)
                .args([mask.to_string(), request_mask.to_string()])
                .stdin(std::process::Stdio::piped())
                .stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::piped())
                .spawn()
                .unwrap();
            child
                .stdin
                .take()
                .unwrap()
                .write_all(commands.as_bytes())
                .unwrap();
            let out = child.wait_with_output().unwrap();
            assert!(
                out.status.success(),
                "{}",
                String::from_utf8_lossy(&out.stderr)
            );
            let expected: Vec<Vec<i128>> = std::str::from_utf8(&out.stdout)
                .unwrap()
                .lines()
                .map(|l| l.split_whitespace().map(|n| n.parse().unwrap()).collect())
                .collect();
            assert_eq!(
                native(&commands, mask, request_mask),
                expected,
                "{commands}"
            );
        }
    }
}
