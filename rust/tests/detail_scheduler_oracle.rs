// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::Storage,
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
        Error::ResultSlotsExhausted => -14,
        e => panic!("unexpected {e:?}"),
    }
}
fn native(commands: &str) -> String {
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(98305),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let mut ca = [WaveformColumn::default(); 385];
    let mut cb = ca;
    let mut sa = [WaveformSpan::default(); 385];
    let mut sb = sa;
    let tile = NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 255,
        data_column_offset: 0,
        column_count: 0,
    };
    let mut dt0 = [tile; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let pool = ResultPool::new(
        source,
        [
            Storage {
                overview_columns: &mut ca,
                overview_spans: &mut sa,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_columns: &mut cb,
                overview_spans: &mut sb,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut acc = [SparseAccumulator::default(); 385];
    let mut ranges = [FrameRange::default(); 16];
    let mut nodes = [QueuedBlock::default(); 16];
    let mut pcm = vec![NormalizedSample::default(); 16 * 4096];
    let mut spans = [WaveformSpan::default(); 385];
    let mut columns = [WaveformColumn::default(); 385];
    let mut slots = [RequestSlot::default(); 16];
    let mut s = PublishedSparseSession::new_scheduled(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 98305,
            frames_per_column: 256,
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
    let mut cache = [libapta::detail_analysis::DetailTile::default(); 4];
    let mut dt = [tile; 4];
    let mut dc = [WaveformColumn::default(); 256];
    s.enable_detail(&mut cache, &mut dt, &mut dc).unwrap();
    let mut retained = [None, None];
    let cancel = CancellationToken::new();
    let mut rows = String::new();
    use std::fmt::Write as _;
    for line in commands.lines() {
        let words: Vec<_> = line.split_whitespace().collect();
        let v: Vec<u64> = words[1..].iter().map(|s| s.parse().unwrap()).collect();
        let [a, b, c, d, e]: [u64; 5] = v.try_into().unwrap();
        let mut row = [0i128; 9];
        let before_generation = pool.generation();
        let result: Result<(), Error> = match words[0] {
            "A" | "M" => s
                .request_region(RegionRequest {
                    range: FrameRange {
                        first_frame: a,
                        end_frame: b,
                    },
                    feature_mask: if words[0] == "M" { 3 } else { 2 },
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
                    row[0] = if s.session().state() == libapta::session::SessionState::Complete {
                        3
                    } else if s.session().queued_frames() != 0 {
                        1
                    } else if p.consumed_input_frames == 0 && pool.generation() == before_generation
                    {
                        2
                    } else {
                        0
                    };
                }),
            "E" => s.finish_input(),
            "H" => {
                retained[a as usize] = Some(pool.acquire().unwrap());
                Ok(())
            }
            "L" => {
                retained[a as usize] = None;
                Ok(())
            }
            "Z" | "Y" => {
                let current = pool.acquire().unwrap();
                let r = if words[0] == "Y" {
                    retained[a as usize].as_ref().unwrap()
                } else {
                    &current
                };
                let detail = r.detail();
                row[1] = detail.map_or(0, |d| d.tiles.len()) as i128;
                row[2] = r.info().session_state as u8 as i128;
                row[3] = r.available_features() as i128;
                row[4] = r.changed_features() as i128;
                row[5] = r.info().generation as i128;
                if let Some(d) = detail {
                    for t in d.tiles {
                        writeln!(
                            rows,
                            "T {} {} {} {} {} {} {} {}",
                            t.level_id,
                            t.tile_index,
                            t.first_frame,
                            t.end_frame,
                            t.first_column_index,
                            t.column_count,
                            t.state as u8,
                            t.confidence
                        )
                        .unwrap();
                        for v in
                            &d.columns[t.data_column_offset..t.data_column_offset + t.column_count]
                        {
                            writeln!(rows, "V {} {} {} {}", v.minimum, v.maximum, v.rms, v.flags)
                                .unwrap();
                        }
                    }
                }
                Ok(())
            }
            op => panic!("unknown {op}"),
        };
        if let Err(e) = result {
            row[0] = status(e);
        }
        writeln!(
            rows,
            "{}",
            row.iter()
                .map(|n| n.to_string())
                .collect::<Vec<_>>()
                .join(" ")
        )
        .unwrap();
    }
    rows
}

#[test]
#[ignore = "requires APTA_C_DETAIL_SCHEDULER_ORACLE"]
fn public_detail_protection_replay_progress_and_publication_match_c() {
    let oracle = std::env::var_os("APTA_C_DETAIL_SCHEDULER_ORACLE")
        .expect("set APTA_C_DETAIL_SCHEDULER_ORACLE");
    let mut scenarios=vec![
        "A 0 16384 96 0 0\nA 16384 32768 96 0 0\nA 32768 49152 96 0 0\nA 49152 65536 96 0 0\nP 0 256 1 0 0\nP 16384 256 2 0 0\nP 32768 256 3 0 0\nP 49152 256 4 0 0\nW 0 0 0 0 0\nP 65536 256 5 0 0\nW 0 0 0 0 0\nZ 0 0 0 0 0\nA 65536 65792 250 0 0\nD 0 0 0 0 0\nP 65536 256 5 0 0\nW 0 0 0 0 0\nZ 0 0 0 0 0\nR 1 0 0 0 0\nC 2 0 0 0 0\nP 81920 256 6 0 0\nW 0 0 0 0 0\nZ 0 0 0 0 0\n".to_string(),
        "P 0 1 1 0 0\nA 0 512 96 0 0\nD 0 0 0 0 0\nP 256 256 123 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nA 98304 98305 250 0 0\nD 0 0 0 0 0\nP 98304 1 123 0 0\nE 0 0 0 0 0\nW 0 0 0 0 0\nZ 0 0 0 0 0\n".to_string(),
        "M 0 512 96 5000 0\nM 16384 16640 96 1000 0\nP 0 512 1 0 0\nP 16384 256 2 0 0\nW 256 1 0 0 0\nR 1 0 0 0 0\nR 2 0 0 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nZ 0 0 0 0 0\n".to_string(),
        "H 0 0 0 0 0\nP 0 256 1 0 0\nH 1 0 0 0 0\nW 0 0 0 0 0\nL 0 0 0 0 0\nW 0 0 0 0 0\nZ 0 0 0 0 0\nY 1 0 0 0 0\nL 1 0 0 0 0\n".to_string(),
    ];
    scenarios.push("F 16384 0 512 240 2\nD 0 0 0 0 0\nA 0 512 32 0 0\nD 0 0 0 0 0\nP 0 512 123 0 0\nD 0 0 0 0 0\nC 1 0 0 0 0\nD 0 0 0 0 0\nW 0 0 0 0 0\nZ 0 0 0 0 0\n".to_string());
    scenarios.push("A 0 512 96 0 0\nA 16384 16640 96 5000 0\nA 32768 33024 96 1000 0\nD 0 0 0 0 0\nC 1 0 0 0 0\nD 0 0 0 0 0\nC 2 0 0 0 0\nD 0 0 0 0 0\n".to_string());
    scenarios.push("A 0 512 96 0 0\nP 0 512 1 0 0\nW 0 0 0 0 0\nP 16384 256 2 0 0\nP 32768 256 3 0 0\nP 49152 256 4 0 0\nP 65536 256 5 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nA 0 512 250 0 0\nD 0 0 0 0 0\nP 1 256 123 0 0\nP 0 513 123 0 0\nP 0 128 123 0 0\nD 0 0 0 0 0\nW 0 0 0 0 0\nR 2 0 0 0 0\nZ 0 0 0 0 0\n".to_string());
    // Failed overview publication stops before detail refresh and request aging.
    scenarios.push("H 0 0 0 0 0\nM 0 256 96 0 0\nA 16384 16640 95 0 0\nP 0 256 1 0 0\nH 1 0 0 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nR 2 0 0 0 0\nD 0 0 0 0 0\nZ 0 0 0 0 0\nL 0 0 0 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nR 2 0 0 0 0\nD 0 0 0 0 0\nL 1 0 0 0 0\n".to_string());
    // Failed detail-only publication happens after detail request refresh.
    scenarios.push("P 0 256 1 0 0\nW 0 0 0 0 0\nH 0 0 0 0 0\nP 16384 256 2 0 0\nP 32768 256 3 0 0\nP 49152 256 4 0 0\nP 65536 256 5 0 0\nW 0 0 0 0 0\nH 1 0 0 0 0\nA 0 256 96 0 0\nA 81920 82176 95 0 0\nP 0 256 123 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nR 2 0 0 0 0\nD 0 0 0 0 0\nZ 0 0 0 0 0\nL 0 0 0 0 0\nW 0 0 0 0 0\nR 1 0 0 0 0\nD 0 0 0 0 0\nL 1 0 0 0 0\n".to_string());
    // Detail degradation leaves temporary overview completion relevant to aging.
    scenarios.push("A 0 16384 96 0 0\nA 16384 32768 96 0 0\nA 32768 49152 96 0 0\nA 49152 65536 96 0 0\nP 0 256 1 0 0\nP 16384 256 2 0 0\nP 32768 256 3 0 0\nP 49152 256 4 0 0\nW 0 0 0 0 0\nP 65536 256 5 0 0\nM 65536 65792 250 0 0\nA 81920 82176 249 0 0\nW 0 0 0 0 0\nR 5 0 0 0 0\nR 6 0 0 0 0\nD 0 0 0 0 0\nD 0 0 0 0 0\nZ 0 0 0 0 0\n".to_string());
    let mut aging = "A 0 512 32 0 0\nA 16384 16640 96 0 0\n".to_string();
    for _ in 0..40 {
        aging.push_str("D 0 0 0 0 0\n");
    }
    scenarios.push(aging);
    // Public demand ages/stages requests; C replay acceptance uses raw priority.
    let mut replay_priority = "A 32768 49152 96 0 0\nA 49152 65536 96 0 0\nA 65536 81920 96 0 0\nA 81920 98304 96 0 0\nP 32768 256 3 0 0\nP 49152 256 4 0 0\nP 65536 256 5 0 0\nP 81920 256 6 0 0\nW 0 0 0 0 0\nP 0 256 1 0 0\nP 16384 256 2 0 0\nW 0 0 0 0 0\nC 1 0 0 0 0\nC 2 0 0 0 0\nC 3 0 0 0 0\nC 4 0 0 0 0\nA 0 256 32 0 0\nA 16384 16640 96 0 0\n".to_string();
    for _ in 0..40 {
        replay_priority.push_str("D 0 0 0 0 0\n");
    }
    replay_priority.push_str("P 0 256 123 0 0\nP 16384 256 456 0 0\nW 0 0 0 0 0\nR 5 0 0 0 0\nR 6 0 0 0 0\nZ 0 0 0 0 0\n");
    scenarios.push(replay_priority.clone());
    let mut replay_deadline = replay_priority
        .split("A 0 256 32 0 0")
        .next()
        .unwrap()
        .to_string();
    replay_deadline.push_str("A 0 256 96 5000 0\nA 16384 16640 96 1000 0\nD 0 0 0 0 0\nP 16384 256 456 0 0\nP 0 256 123 0 0\nW 0 0 0 0 0\nR 5 0 0 0 0\nR 6 0 0 0 0\nZ 0 0 0 0 0\n");
    scenarios.push(replay_deadline);

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
        assert_eq!(
            native(commands),
            String::from_utf8(out.stdout).unwrap(),
            "scenario {i}"
        );
    }
}
