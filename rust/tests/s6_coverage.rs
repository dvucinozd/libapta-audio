// SPDX-License-Identifier: Apache-2.0
//! Characterize inherited S6 coverage, not musical accuracy or a new algorithm.
use libapta::{
    analysis::{OnsetBin, BIN_CAPACITY},
    container::{Container, ParseOptions},
    result,
    session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget},
    waveform::NormalizedSample,
    Beat, FeatureState, NativeLimits, WaveformColumn,
};
use std::{fs, process::Command};

fn wire(session: &Session<'_>) -> Vec<u8> {
    let snapshot = session.snapshot(1).unwrap();
    let view = result::from_session_snapshot(&snapshot, &mut [], NativeLimits::default()).unwrap();
    let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
    result::write(&view, &mut bytes, result::Limits::default()).unwrap();
    bytes
}

fn payload<'a>(container: &Container<'a>, id: [u8; 4]) -> Option<&'a [u8]> {
    (0..container.section_count())
        .filter_map(|i| container.section(i))
        .find(|s| s.fourcc == id)
        .map(|s| s.payload)
}

#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn s6_window_tail_coverage_matches_c_without_extrapolation() {
    let oracle = std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").expect("oracle");
    // Optional raw artifacts are external, unique per invocation, and never overwritten.
    let output = std::env::var_os("APTA_S6_EVIDENCE_DIR").map(std::path::PathBuf::from);
    if let Some(path) = &output {
        fs::create_dir(path).unwrap();
    }
    let temporary = std::env::temp_dir().join(format!("apta-s6-coverage-{}", std::process::id()));
    fs::create_dir(&temporary).unwrap();
    let mut rows = String::from("frames,silent_tail,unknown,steps,dynamic,state,declared_end,segment_end,segments,global_tempo,local_tempo,meter_binds_global,global_anchor,meter_frame,meter_ordinal\n");
    for (count, silent_tail, expected_end) in [
        (63usize * 2048, false, 0),
        (64 * 2048, false, 131072),
        (128 * 2048, false, 262144),
        (128 * 2048 + 1, false, 262144),
        (320000, false, 262144),
        (191 * 2048, false, 262144),
        (191 * 2048 + 1, false, 391169),
        (192 * 2048, false, 393216),
        (256 * 2048 + 37, false, 524288),
        (256 * 2048, true, 262144),
    ] {
        let pcm: Vec<f32> = (0..count)
            .map(|i| {
                let phase = i % 4000;
                if phase < 64 && !(silent_tail && i >= 128 * 2048) {
                    (64 - phase) as f32 / 64.0 * 0.75
                } else {
                    0.0
                }
            })
            .collect();
        let path = temporary.join("input.pcm");
        let pcm_bytes: Vec<_> = pcm.iter().flat_map(|v| v.to_le_bytes()).collect();
        fs::write(&path, &pcm_bytes).unwrap();
        if let Some(output) = &output {
            fs::write(
                output.join(format!("{count}-{silent_tail}.pcm")),
                &pcm_bytes,
            )
            .unwrap();
        }
        // Unlimited processing matches the existing C oracle. A one-step C
        // run can stall on short evidence; see the coverage evaluation document.
        for (unknown, steps, dynamic) in [(false, 0, false), (true, 0, false), (false, 0, true)] {
            let name = format!("{count}-{silent_tail}-{unknown}-{steps}-{dynamic}");
            let mask = result::WAVEFORM_OVERVIEW
                | result::BPM
                | result::LOCAL_BEATGRID
                | result::GLOBAL_BEATGRID
                | result::METER_DOWNBEAT
                | if dynamic { result::DYNAMIC_TEMPO } else { 0 };
            let c = Command::new(&oracle)
                .args([
                    "8000".to_owned(),
                    steps.to_string(),
                    path.to_string_lossy().into_owned(),
                    format!("project:{mask}{}", if unknown { ":unknown" } else { "" }),
                ])
                .output()
                .unwrap();
            assert!(
                c.status.success(),
                "{name}: {}",
                String::from_utf8_lossy(&c.stderr)
            );
            let reference = Container::parse(&c.stdout, ParseOptions::default()).unwrap();
            let mut queue = vec![NormalizedSample::default(); 4096];
            let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
            let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
            let mut flux = vec![0.0; BIN_CAPACITY];
            let mut global_bins = vec![OnsetBin::default(); libapta::global_analysis::BIN_CAPACITY];
            let mut global_flux = vec![0.0; global_bins.len()];
            let mut beats = vec![Beat::default(); libapta::global_analysis::MAX_BEATS];
            let mut session = Session::new(
                SessionConfig {
                    sample_rate: 8000,
                    channel_count: 1,
                    total_frames: if unknown { u64::MAX } else { count as u64 },
                    frames_per_column: 32768,
                },
                &mut queue,
                &mut columns,
            )
            .unwrap();
            session.enable_tempo(&mut bins, &mut flux).unwrap();
            session
                .enable_global_grid(dynamic, &mut global_bins, &mut global_flux, &mut beats)
                .unwrap();
            session.enable_meter().unwrap();
            let token = CancellationToken::new();
            let budget = WorkBudget {
                maximum_input_frames: 0,
                maximum_steps: steps,
            };
            for block in pcm.chunks(4096) {
                assert_eq!(session.push_interleaved(block).unwrap(), block.len());
                session.process(budget, &token).unwrap();
            }
            session.finish_input().unwrap();
            for _ in 0..100000 {
                session.process(budget, &token).unwrap();
                if session.state() == SessionState::Complete {
                    break;
                }
            }
            assert_eq!(session.state(), SessionState::Complete, "{name}");
            assert_eq!(session.processed_frames(), count as u64);
            let native_bytes = wire(&session);
            let native = Container::parse(&native_bytes, ParseOptions::default()).unwrap();
            for id in [*b"WOVR", *b"TEMP", *b"LGRD", *b"GGRD", *b"REVN", *b"MTRD"] {
                assert_eq!(
                    payload(&native, id),
                    payload(&reference, id),
                    "{name} {id:?}"
                );
            }
            if let Some(g) = session.global_grid() {
                assert_eq!(g.state, FeatureState::Final);
                assert_eq!(g.coverage_range.end_frame, count as u64);
                let end = g.segments.last().unwrap().applicability_range.end_frame;
                assert_eq!(end, expected_end, "{name}");
                let m = session.meter().unwrap();
                let binds = g.segments.iter().any(|s| {
                    (0..s.beat_count).any(|i| {
                        s.beat_at(i).unwrap().is_some_and(|b| {
                            b.ordinal == m.downbeat_ordinal
                                && b.position.whole_frame == m.downbeat_frame
                        })
                    })
                });
                rows.push_str(&format!("{count},{silent_tail},{unknown},{steps},{dynamic},{:?},{},{end},{},{},{},{binds},{},{},{}\n",
                    g.state, g.coverage_range.end_frame, g.segments.len(), g.segments[0].nominal_tempo_millibpm,
                    session.tempo().unwrap().selected.tempo_millibpm, g.segments[0].anchor_position.whole_frame, m.downbeat_frame, m.downbeat_ordinal));
                if count == 320000 {
                    assert!(!binds, "original consumer meter rejection");
                }
            } else {
                assert_eq!(count, 63 * 2048);
                rows.push_str(&format!(
                    "{count},{silent_tail},{unknown},{steps},{dynamic},Absent,0,0,0,0,0,false,0,0,0\n"
                ));
            }
            if let Some(output) = &output {
                fs::write(output.join(format!("{name}-c.apta")), &c.stdout).unwrap();
                fs::write(output.join(format!("{name}-rust.apta")), &native_bytes).unwrap();
            }
        }
    }
    print!("{rows}");
    if let Some(output) = output {
        fs::write(output.join("coverage.csv"), rows).unwrap();
    }
    fs::remove_dir_all(temporary).unwrap();
}
