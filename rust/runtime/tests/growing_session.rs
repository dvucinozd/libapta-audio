// SPDX-License-Identifier: Apache-2.0
use libapta::{session::*, waveform::*, *};
use libapta_runtime::{GrowingLimits, GrowingSession};
fn config(frames: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: frames,
        frames_per_column: 64,
    }
}
fn pcm(n: usize) -> Vec<f32> {
    (0..n)
        .map(|i| {
            if i % 311 == 0 {
                1.5
            } else {
                ((i * 17 % 997) as f32 - 498.0) / 997.0
            }
        })
        .collect()
}
#[test]
fn owning_growth_preserves_wrapped_queue_partial_columns_and_retained_graphs() {
    let input = pcm(8193);
    let mut baseline_queue = vec![NormalizedSample::default(); 8193];
    let mut baseline_columns = vec![WaveformColumn::default(); 129];
    let mut baseline =
        Session::new(config(8193), &mut baseline_queue, &mut baseline_columns).unwrap();
    baseline.push_interleaved(&input).unwrap();
    baseline.finish_input().unwrap();
    let token = CancellationToken::new();
    baseline.process(WorkBudget::default(), &token).unwrap();
    let retained;
    let final_result;
    {
        let mut s =
            GrowingSession::new(config(TOTAL_FRAMES_UNKNOWN), GrowingLimits::default()).unwrap();
        let results = s.results();
        let initial = results.acquire().unwrap();
        assert_eq!(
            s.push_pcm(PcmView::F32Interleaved(&input[..300])).unwrap(),
            300
        );
        s.process(
            WorkBudget {
                maximum_input_frames: 129,
                maximum_steps: 0,
            },
            &token,
        )
        .unwrap();
        retained = results.acquire().unwrap();
        let saved = retained.view().overview.unwrap().columns.to_vec();
        assert_eq!(
            s.push_pcm(PcmView::F32Interleaved(&input[300..511]))
                .unwrap(),
            211
        );
        s.process(
            WorkBudget {
                maximum_input_frames: 257,
                maximum_steps: 0,
            },
            &token,
        )
        .unwrap();
        assert_eq!(
            s.push_pcm(PcmView::F32Interleaved(&input[511..])).unwrap(),
            input.len() - 511
        );
        s.finish_input().unwrap();
        while s.session().state() != SessionState::Complete {
            s.process(
                WorkBudget {
                    maximum_input_frames: 511,
                    maximum_steps: 0,
                },
                &token,
            )
            .unwrap();
        }
        final_result = results.acquire().unwrap();
        assert!(initial.view().overview.is_none());
        assert_eq!(initial.source().total_frames, None);
        assert_eq!(retained.view().overview.unwrap().columns, saved);
        assert_eq!(final_result.source().total_frames, Some(8193));
        assert_eq!(
            final_result.view().overview.unwrap().columns,
            baseline.columns()
        );
        assert_eq!(
            final_result.view().overview.unwrap().state,
            FeatureState::Final
        );
        assert_eq!(s.refresh(), Ok(false));
    }
    std::thread::spawn(move || {
        assert_eq!(retained.view().overview.unwrap().columns.len(), 2);
        assert_eq!(final_result.view().overview.unwrap().columns.len(), 129);
    })
    .join()
    .unwrap();
}
#[test]
fn working_limit_and_invalid_pcm_leave_processing_and_publication_unchanged() {
    let limits = GrowingLimits {
        maximum_queue_frames: 512,
        maximum_columns: 2,
        ..GrowingLimits::default()
    };
    let mut s = GrowingSession::new(config(TOTAL_FRAMES_UNKNOWN), limits).unwrap();
    let results = s.results();
    assert_eq!(
        s.push_pcm(PcmView::F32Interleaved(&[0.0, f32::NAN])),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        s.push_pcm(PcmView::F32Interleaved(&[0.5; 129])),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.session().accepted_frames(), 0);
    assert_eq!(s.session().input_capacity_frames(), 0);
    assert_eq!(results.acquire().unwrap().info().generation, 1);
    assert_eq!(s.push_pcm(PcmView::F32Interleaved(&[0.5; 128])), Ok(128));
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(
        s.push_pcm(PcmView::F32Interleaved(&[0.1])),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.session().processed_frames(), 128);
    s.finish_input().unwrap();
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(s.session().state(), SessionState::Complete);
}
#[test]
fn snapshot_failure_retries_only_mirror_and_cancellation_keeps_old_readers() {
    let mut s =
        GrowingSession::new(config(TOTAL_FRAMES_UNKNOWN), GrowingLimits::default()).unwrap();
    s.push_pcm(PcmView::F32Interleaved(&[0.5; 129])).unwrap();
    let results = s.results();
    let old = results.acquire().unwrap();
    s.set_result_limits(NativeLimits {
        maximum_waveform_columns: 0,
        ..NativeLimits::default()
    });
    let token = CancellationToken::new();
    assert_eq!(
        s.process(WorkBudget::default(), &token),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.session().processed_frames(), 129);
    assert_eq!(
        results.acquire().unwrap().info().generation,
        old.info().generation
    );
    assert_eq!(
        s.process(WorkBudget::default(), &token),
        Err(Error::InvalidState)
    );
    s.set_result_limits(NativeLimits::default());
    assert_eq!(s.refresh(), Ok(true));
    assert_eq!(s.session().processed_frames(), 129);
    let before_cancel = results.acquire().unwrap();
    token.cancel();
    assert_eq!(
        s.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(
        results.acquire().unwrap().info().session_state,
        ResultSessionState::Cancelled
    );
    assert!(old.view().overview.is_none());
    assert_eq!(before_cancel.view().overview.unwrap().columns.len(), 2);
    assert_eq!(
        s.push_pcm(PcmView::F32Interleaved(&[0.1])),
        Err(Error::InvalidState)
    );
}
#[test]
fn growing_key_processing_matches_borrowed_session_after_workspace_growth() {
    let input: Vec<f32> = (0..240017)
        .map(|i| {
            let t = i as f32 / 48000.0;
            (core::f32::consts::TAU * 261.6256 * t).sin() * 0.2
                + (core::f32::consts::TAU * 329.6276 * t).sin() * 0.15
        })
        .collect();
    let token = CancellationToken::new();
    let mut queue = vec![NormalizedSample::default(); input.len()];
    let mut columns = vec![WaveformColumn::default(); input.len().div_ceil(64)];
    let mut baseline = Session::new(config(input.len() as u64), &mut queue, &mut columns).unwrap();
    baseline
        .enable_key_with_math(libapta::key_analysis::KeyMath {
            cos: f32::cos,
            log: f32::ln,
            sqrt: f32::sqrt,
        })
        .unwrap();
    baseline.push_interleaved(&input).unwrap();
    baseline.finish_input().unwrap();
    baseline.process(WorkBudget::default(), &token).unwrap();
    let mut s =
        GrowingSession::new(config(TOTAL_FRAMES_UNKNOWN), GrowingLimits::default()).unwrap();
    s.enable_key().unwrap();
    for part in input.chunks(513) {
        s.push_pcm(PcmView::F32Interleaved(part)).unwrap();
        s.process(WorkBudget::default(), &token).unwrap();
    }
    s.finish_input().unwrap();
    while s.session().state() != SessionState::Complete {
        s.process(WorkBudget::default(), &token).unwrap();
    }
    let r = s.results().acquire().unwrap();
    let view = r.view();
    assert_eq!(view.overview.unwrap().columns, baseline.columns());
    assert_eq!(view.key, baseline.key());
    assert!(view.key.is_some());
}

#[test]
fn default_music_attachment_preflight_is_atomic_and_budgeted_with_output_growth() {
    let limits = GrowingLimits {
        maximum_working_bytes: 4096,
        ..GrowingLimits::default()
    };
    let mut s = GrowingSession::new(config(TOTAL_FRAMES_UNKNOWN), limits).unwrap();
    assert_eq!(s.enable_default_music(), Err(Error::LimitExceeded));
    // Failed aggregate attachment leaves key and all stages uninitialized.
    assert_eq!(s.session().publication_serials(), [0; 5]);
    s.enable_key().unwrap();
    s.push_pcm(PcmView::F32Interleaved(&[0.5; 64])).unwrap();
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(s.session().processed_frames(), 64);
}

#[test]
#[ignore = "requires APTA_C_TEMPO_ANALYSIS_ORACLE"]
fn owning_default_music_matches_complete_public_c_containers() {
    use std::{fs, process::Command};
    let oracle = std::env::var_os("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap();
    for (profile, count, unknown) in [
        (0, 320000, false),
        (1, 320017, true),
        (2, 256 * 4800 + 17, true),
    ] {
        let pcm: Vec<f32> = (0..count)
            .map(|i| {
                let tempo = if profile == 2 {
                    [80, 160, 120, 200][i / 65536 % 4]
                } else {
                    120
                };
                let period = 8000 * 60 / tempo;
                let phase = i % period;
                if phase < 64 {
                    (64 - phase) as f32 / 64.0 * 0.75
                } else {
                    0.0
                }
            })
            .collect();
        let path =
            std::env::temp_dir().join(format!("apta-owning-{}-{profile}.pcm", std::process::id()));
        fs::write(
            &path,
            pcm.iter().flat_map(|v| v.to_le_bytes()).collect::<Vec<_>>(),
        )
        .unwrap();
        let c = Command::new(&oracle)
            .args(["8000", "0"])
            .arg(&path)
            .arg(if unknown { "all-unknown" } else { "all" })
            .output()
            .unwrap();
        fs::remove_file(path).unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let retained;
        {
            let mut s = GrowingSession::new(
                SessionConfig {
                    sample_rate: 8000,
                    channel_count: 1,
                    total_frames: if unknown {
                        TOTAL_FRAMES_UNKNOWN
                    } else {
                        count as u64
                    },
                    frames_per_column: 32768,
                },
                GrowingLimits::default(),
            )
            .unwrap();
            s.enable_default_music().unwrap();
            let token = CancellationToken::new();
            for part in pcm.chunks(4096) {
                assert_eq!(s.push_pcm(PcmView::F32Interleaved(part)), Ok(part.len()));
                s.process(WorkBudget::default(), &token).unwrap();
            }
            s.finish_input().unwrap();
            for _ in 0..10000 {
                if s.session().state() == SessionState::Complete {
                    break;
                }
                s.process(
                    WorkBudget {
                        maximum_input_frames: 0,
                        maximum_steps: 1,
                    },
                    &token,
                )
                .unwrap();
            }
            assert_eq!(s.session().state(), SessionState::Complete);
            let snapshot = s.session().snapshot(100).unwrap();
            let mut tiles = [];
            let wire =
                result::from_session_snapshot(&snapshot, &mut tiles, NativeLimits::default())
                    .unwrap();
            let mut native = vec![0; result::serialized_size(&wire).unwrap()];
            result::write(&wire, &mut native, result::Limits::default()).unwrap();
            if native != c.stdout {
                let nc = libapta::container::Container::parse(&native, Default::default()).unwrap();
                let cc =
                    libapta::container::Container::parse(&c.stdout, Default::default()).unwrap();
                for i in 0..nc.section_count() {
                    let n = nc.section(i).unwrap();
                    let c = cc.section(i).unwrap();
                    if n.payload != c.payload {
                        panic!(
                            "profile {profile}, section {:?}, first difference {:?}",
                            n.fourcc,
                            n.payload.iter().zip(c.payload).position(|(a, b)| a != b)
                        );
                    }
                }
                panic!("profile {profile}: container header/directory discrepancy");
            }
            retained = s.results().acquire().unwrap();
            assert_eq!(retained.view().tempo, snapshot.view().tempo);
            assert_eq!(retained.view().global_grid, snapshot.view().global_grid);
            assert_eq!(retained.view().key, snapshot.view().key);
            assert_eq!(retained.view().meter, snapshot.view().meter);
            // Generic external conversion retains strict provenance validation.
            assert!(matches!(
                result::from_native(&retained.view(), &mut [], NativeLimits::default()),
                Err(Error::Unsupported)
            ));
        }
        std::thread::spawn(move || {
            let v = retained.view();
            assert!(
                v.tempo.is_some()
                    && v.global_grid.is_some()
                    && v.key.is_some()
                    && v.meter.is_some()
            );
            assert_eq!(v.quality[0].calibration_model_id, 1867860160);
        })
        .join()
        .unwrap();
    }
}
