// SPDX-License-Identifier: Apache-2.0
use libapta::{band::BandSums, detail_analysis::*, session::*, waveform::*, *};
use libapta_runtime::{GrowingLimits, GrowingSession};
fn config(total_frames: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames,
        frames_per_column: 64,
    }
}
fn input() -> Vec<f32> {
    (0..70001)
        .map(|i| ((i * 17 % 997) as f32 - 498.0) / 997.0)
        .collect()
}
fn run(input: &[f32], known: bool, detail: bool) -> libapta_runtime::HeapResult {
    let mut s = GrowingSession::new(
        config(if known {
            input.len() as u64
        } else {
            TOTAL_FRAMES_UNKNOWN
        }),
        GrowingLimits::default(),
    )
    .unwrap();
    s.enable_three_band().unwrap();
    if detail {
        s.enable_detail().unwrap();
    }
    let token = CancellationToken::new();
    let mut retained = None;
    for chunk in input.chunks(311) {
        assert_eq!(s.push_pcm(PcmView::F32Interleaved(chunk)), Ok(chunk.len()));
        s.process(
            WorkBudget {
                maximum_input_frames: 129,
                maximum_steps: 1,
            },
            &token,
        )
        .unwrap();
        s.process(WorkBudget::default(), &token).unwrap();
        if retained.is_none() && s.session().processed_frames() > 1024 {
            let r = s.results().acquire().unwrap();
            retained = Some((r.view().overview.unwrap().columns.to_vec(), r));
        }
    }
    s.finish_input().unwrap();
    while s.session().state() != SessionState::Complete {
        s.process(
            WorkBudget {
                maximum_input_frames: 19,
                maximum_steps: 1,
            },
            &token,
        )
        .unwrap();
    }
    let (columns, old) = retained.unwrap();
    assert_eq!(old.view().overview.unwrap().columns, columns);
    if detail {
        assert_eq!(old.view().detail.unwrap().tiles[0].tile_index, 0);
    }
    let result = s.results().acquire().unwrap();
    libapta_runtime::HeapResult::copy_snapshot(
        &s.session().snapshot(result.info().generation).unwrap(),
        NativeLimits::default(),
    )
    .unwrap()
}
#[test]
fn growing_bands_detail_eviction_and_partial_eof_match_fixed_session() {
    let input = input();
    let mut queue = vec![NormalizedSample::default(); input.len()];
    let mut columns = vec![WaveformColumn::default(); input.len().div_ceil(64)];
    let mut bands = vec![BandSums::default(); columns.len()];
    let mut tiles = vec![DetailTile::default(); TILE_COUNT];
    let mut fixed = Session::new(config(input.len() as u64), &mut queue, &mut columns).unwrap();
    fixed.enable_three_band(&mut bands).unwrap();
    fixed.enable_detail(&mut tiles).unwrap();
    // Match acceptance chunks: detail eviction occurs during acceptance.
    for chunk in input.chunks(311) {
        fixed.push_interleaved(chunk).unwrap();
        fixed
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
    }
    fixed.finish_input().unwrap();
    fixed
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    let expected = fixed.snapshot(1).unwrap();
    for known in [false, true] {
        let actual = run(&input, known, true);
        assert_eq!(
            actual.view().overview.unwrap().columns,
            expected.view().overview.unwrap().columns
        );
        assert_eq!(actual.view().detail, expected.view().detail);
        assert!(actual
            .view()
            .detail
            .unwrap()
            .tiles
            .iter()
            .all(|t| t.tile_index != 0));
        assert_eq!(
            actual.available_features(),
            result::WAVEFORM_OVERVIEW | result::WAVEFORM_3BAND | result::WAVEFORM_DETAIL
        );
    }
}
#[test]
#[ignore = "requires APTA_C_BAND_ORACLE"]
fn owning_growing_bands_match_public_c_columns() {
    use std::io::Write;
    let samples = input()[..4097].to_vec();
    let mut child = std::process::Command::new(std::env::var_os("APTA_C_BAND_ORACLE").unwrap())
        .args(["48000", "64"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(
            &samples
                .iter()
                .flat_map(|n| n.to_ne_bytes())
                .collect::<Vec<_>>(),
        )
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success());
    let text = String::from_utf8(output.stdout).unwrap();
    let c: Vec<Vec<i32>> = text
        .lines()
        .filter_map(|l| l.strip_prefix("C "))
        .map(|l| l.split_whitespace().map(|v| v.parse().unwrap()).collect())
        .collect();
    for known in [false, true] {
        let result = run(&samples, known, false);
        let view = result.view();
        let columns = view.overview.unwrap().columns;
        assert_eq!(c.len(), 2 * columns.len());
        for (i, expected) in c.iter().enumerate() {
            let v = columns[i % columns.len()];
            assert_eq!(
                expected.as_slice(),
                [
                    v.minimum as i32,
                    v.maximum as i32,
                    v.rms as i32,
                    v.low as i32,
                    v.mid as i32,
                    v.high as i32,
                    v.flags as i32
                ]
            );
        }
    }
}

#[test]
#[ignore = "requires APTA_C_DETAIL_SESSION_ORACLE"]
fn owning_detail_publication_matches_public_c_before_and_after_eof() {
    fn record(s: &GrowingSession, out: &mut String) {
        use std::fmt::Write;
        let r = s.results().acquire().unwrap();
        let view = r.view();
        let d = view.detail.unwrap();
        writeln!(out, "R {}", d.tiles.len()).unwrap();
        for t in d.tiles {
            writeln!(
                out,
                "T {} {} {} {} {} {} {} {}",
                t.level_id,
                t.tile_index,
                t.first_frame,
                t.end_frame,
                t.first_column_index,
                t.column_count,
                t.state as u32,
                t.confidence
            )
            .unwrap();
            for c in &d.columns[t.data_column_offset..t.data_column_offset + t.column_count] {
                writeln!(
                    out,
                    "C {} {} {} {} {} {} {}",
                    c.minimum, c.maximum, c.rms, c.low, c.mid, c.high, c.flags
                )
                .unwrap();
            }
        }
    }
    for known in [false, true] {
        let output =
            std::process::Command::new(std::env::var_os("APTA_C_DETAIL_SESSION_ORACLE").unwrap())
                .arg(if known { "sequential" } else { "unknown" })
                .output()
                .unwrap();
        assert!(output.status.success());
        let mut s = GrowingSession::new(
            config(if known { 513 } else { TOTAL_FRAMES_UNKNOWN }),
            GrowingLimits::default(),
        )
        .unwrap();
        s.enable_detail().unwrap();
        s.push_pcm(PcmView::S16Interleaved(&[123; 513])).unwrap();
        let token = CancellationToken::new();
        s.process(
            WorkBudget {
                maximum_input_frames: 1,
                maximum_steps: 1,
            },
            &token,
        )
        .unwrap();
        let mut actual = String::new();
        record(&s, &mut actual);
        s.finish_input().unwrap();
        s.process(WorkBudget::default(), &token).unwrap();
        record(&s, &mut actual);
        assert_eq!(actual, String::from_utf8(output.stdout).unwrap());
    }
}

#[test]
fn owning_clock_matches_core_and_failed_mirror_does_not_repeat_work() {
    let samples = input();
    let mut queue = vec![NormalizedSample::default(); 513];
    let mut output = vec![WaveformColumn::default(); 9];
    let mut core = Session::new(config(513), &mut queue, &mut output).unwrap();
    let mut owning = GrowingSession::new(config(513), GrowingLimits::default()).unwrap();
    core.push_interleaved(&samples[..513]).unwrap();
    owning
        .push_pcm(PcmView::F32Interleaved(&samples[..513]))
        .unwrap();
    let token = CancellationToken::new();
    let mut core_time = 1;
    let mut owning_time = 1;
    let expected = core
        .process_with_clock(
            WorkBudget::default(),
            1,
            &mut || {
                core_time += 1000;
                core_time
            },
            &token,
        )
        .unwrap();
    owning.set_result_limits(NativeLimits {
        maximum_storage_bytes: 1,
        ..NativeLimits::default()
    });
    assert_eq!(
        owning.process_with_clock(
            WorkBudget::default(),
            1,
            &mut || {
                owning_time += 1000;
                owning_time
            },
            &token
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(core_time, owning_time);
    assert_eq!(core.processed_frames(), owning.session().processed_frames());
    assert_eq!(core.columns(), owning.session().columns());
    assert!(expected.consumed_input_frames != 0);
    owning.set_result_limits(NativeLimits::default());
    assert_eq!(owning.refresh(), Ok(true));
    assert_eq!(core.processed_frames(), owning.session().processed_frames());
}
