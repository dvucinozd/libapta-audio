// SPDX-License-Identifier: Apache-2.0
use libapta::{
    analysis::{OnsetBin, BIN_CAPACITY},
    session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget},
    waveform::NormalizedSample,
    WaveformColumn,
};
fn pcm(rate: u32, count: usize, tempo: u32) -> Vec<f32> {
    (0..count)
        .map(|i| {
            let period = u64::from(rate) * 60 / u64::from(tempo);
            let phase = i as u64 % period;
            if phase < 64 {
                ((64 - phase) as f32 / 64.0) * 0.75
            } else {
                0.0
            }
        })
        .collect()
}
#[test]
fn cooperative_analysis_commits_atomically_and_rejects_invalid_pcm() {
    let samples = pcm(8000, 8000 * 40, 120);
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); 10];
    let mut s = Session::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: samples.len() as u64,
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
    )
    .unwrap();
    s.enable_tempo(&mut bins, &mut flux).unwrap();
    let token = CancellationToken::new();
    assert!(s.push_interleaved(&[f32::NAN]).is_err());
    assert_eq!(s.accepted_frames(), 0);
    for block in samples.chunks(4096) {
        assert_eq!(s.push_interleaved(block).unwrap(), block.len());
        s.process(WorkBudget::default(), &token).unwrap();
    }
    s.finish_input().unwrap();
    let old = s.tempo().unwrap().selected;
    for _ in 0..1000 {
        s.process(
            WorkBudget {
                maximum_steps: 1,
                maximum_input_frames: 0,
            },
            &token,
        )
        .unwrap();
        if s.state() == SessionState::Complete {
            break;
        }
        assert_eq!(s.tempo().unwrap().selected, old);
    }
    assert_eq!(s.state(), SessionState::Complete);
    assert!(s.local_grid().is_some());
    let snapshot = s.tempo().unwrap().selected;
    token.cancel();
    assert_eq!(
        s.process(WorkBudget::default(), &token)
            .unwrap()
            .completed_steps,
        0
    );
    assert_eq!(s.tempo().unwrap().selected, snapshot);
}
#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn native_tempo_and_grid_payloads_match_public_c_analysis() {
    use std::{fs, process::Command};
    let oracle = std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").expect("oracle");
    for (rate, count, tempo, steps, mode) in [
        (8000, 320000, 120, 0, ""),
        (44100, 44100 * 15, 128, 0, ""),
        (48000, 48000 * 24 + 37, 95, 1, ""),
        (8000, 256 * 4300 + 17, 140, 2, ""),
        (8000, 256 * 700, 120, 0, ""),
        (8000, 320000, 120, 0, "focus"),
        (8000, 320000, 120, 0, "lock"),
    ] {
        let samples = pcm(rate, count, tempo);
        let path = std::env::temp_dir().join(format!(
            "apta-tempo-{}-{rate}-{steps}.pcm",
            std::process::id()
        ));
        let bytes: Vec<_> = samples.iter().flat_map(|v| v.to_le_bytes()).collect();
        fs::write(&path, bytes).unwrap();
        let mut command = Command::new(&oracle);
        command.args([
            rate.to_string(),
            steps.to_string(),
            path.to_string_lossy().into_owned(),
        ]);
        if !mode.is_empty() {
            command.arg(mode);
        }
        let c = command.output().unwrap();
        fs::remove_file(path).unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let container = libapta::container::Container::parse(
            &c.stdout,
            libapta::container::ParseOptions::default(),
        )
        .unwrap();
        let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
        let mut flux = vec![0.0; BIN_CAPACITY];
        let mut queue = vec![NormalizedSample::default(); count];
        let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
        let mut s = Session::new(
            SessionConfig {
                sample_rate: rate,
                channel_count: 1,
                total_frames: count as u64,
                frames_per_column: 32768,
            },
            &mut queue,
            &mut columns,
        )
        .unwrap();
        s.enable_tempo(&mut bins, &mut flux).unwrap();
        if mode == "focus" {
            s.set_tempo_focus(libapta::Focus {
                feature_mask: libapta::result::BPM
                    | libapta::result::LOCAL_BEATGRID
                    | libapta::result::GRID_LOCKING,
                playhead_frame: count as u64 / 2,
                lookbehind_frames: count as u64 / 4,
                lookahead_frames: count as u64 / 8,
                priority: 0,
            })
            .unwrap();
        }
        let cancel = CancellationToken::new();
        let budget = WorkBudget {
            maximum_steps: steps,
            maximum_input_frames: 0,
        };
        for block in samples.chunks(4096) {
            assert_eq!(s.push_interleaved(block).unwrap(), block.len());
            s.process(budget, &cancel).unwrap();
        }
        s.finish_input().unwrap();
        for _ in 0..100000 {
            s.process(budget, &cancel).unwrap();
            if s.state() == SessionState::Complete {
                break;
            }
        }
        assert_eq!(s.state(), SessionState::Complete);
        if mode == "lock" {
            let range = libapta::FrameRange {
                first_frame: count as u64 / 4,
                end_frame: count as u64 * 3 / 4,
            };
            s.lock_grid_range(range).unwrap();
            s.lock_grid_range(range).unwrap();
        }
        let t = s
            .tempo()
            .unwrap_or_else(|| panic!("missing rate {rate} count {count} steps {steps}"));
        let mut temp = [0u8; 104];
        let size = libapta::tempo::write_tempo_payload(&t, &mut temp).unwrap();
        let mut local = [0u8; 144];
        libapta::tempo::write_local_grid(
            &s.local_grid().unwrap(),
            t.selected.tempo_millibpm,
            &mut local,
        )
        .unwrap();
        assert_eq!(
            (0..container.section_count())
                .filter_map(|i| container.section(i))
                .find(|s| s.fourcc == *b"TEMP")
                .unwrap()
                .payload,
            &temp[..size],
            "rate {rate} steps {steps}"
        );
        assert_eq!(
            (0..container.section_count())
                .filter_map(|i| container.section(i))
                .find(|s| s.fourcc == *b"LGRD")
                .unwrap()
                .payload,
            local,
            "rate {rate} steps {steps}"
        );
    }
}

#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn native_global_grid_and_revisions_match_c_windows() {
    use std::{fs, process::Command};
    let oracle = std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap();
    for (rate, count, tempo, steps, change, lock) in [
        (8000, 320000, 120, 0, false, false),
        (44100, 44100 * 20 + 37, 128, 2, false, false),
        (8000, 8000 * 90, 95, 2, true, false),
        (8000, 8000 * 80, 125, 0, true, true),
        (8000, 256 * 16400 + 17, 120, 0, false, false),
        (2000, 256 * 16400 + 17, 120, 0, false, false),
        (8000, 256 * 4800 + 17, 120, 0, false, false),
        (8000, 256 * 16000 + 17, 120, 0, false, false),
    ] {
        let mut samples = pcm(rate, count, tempo);
        if count == 256 * 4800 + 17 || count == 256 * 16000 + 17 {
            for (i, sample) in samples.iter_mut().enumerate() {
                let tempo = [80, 160, 120, 200][i / 65536 % 4];
                let period = rate as usize * 60 / tempo;
                let phase = i % period;
                *sample = if phase < 64 {
                    (64 - phase) as f32 / 64.0 * 0.75
                } else {
                    0.0
                };
            }
        }
        if change {
            let tail = pcm(rate, count / 2, if lock { 80 } else { 150 });
            samples[count / 2..].copy_from_slice(&tail);
        }
        let path = std::env::temp_dir().join(format!(
            "apta-global-{}-{rate}-{steps}.pcm",
            std::process::id()
        ));
        let bytes: Vec<_> = samples.iter().flat_map(|v| v.to_le_bytes()).collect();
        fs::write(&path, bytes).unwrap();
        let c = Command::new(&oracle)
            .args([
                rate.to_string(),
                steps.to_string(),
                path.to_string_lossy().into_owned(),
                if lock {
                    "revision".into()
                } else {
                    "global".into()
                },
            ])
            .output()
            .unwrap();
        fs::remove_file(path).unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let container = libapta::container::Container::parse(
            &c.stdout,
            libapta::container::ParseOptions::default(),
        )
        .unwrap();
        let mut bins = vec![OnsetBin::default(); libapta::global_analysis::BIN_CAPACITY];
        let mut flux = vec![0.0; bins.len()];
        let mut beats = vec![libapta::Beat::default(); libapta::global_analysis::MAX_BEATS];
        let mut queue = vec![NormalizedSample::default(); count];
        let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
        let mut s = Session::new(
            SessionConfig {
                sample_rate: rate,
                channel_count: 1,
                total_frames: count as u64,
                frames_per_column: 32768,
            },
            &mut queue,
            &mut columns,
        )
        .unwrap();
        let mut local_bins = vec![OnsetBin::default(); BIN_CAPACITY];
        let mut local_flux = vec![0.0; BIN_CAPACITY];
        s.enable_tempo(&mut local_bins, &mut local_flux).unwrap();
        s.enable_global_grid(true, &mut bins, &mut flux, &mut beats)
            .unwrap();
        let cancel = CancellationToken::new();
        let budget = WorkBudget {
            maximum_steps: steps,
            maximum_input_frames: 0,
        };
        let mut locked = false;
        for block in samples.chunks(4096) {
            assert_eq!(s.push_interleaved(block).unwrap(), block.len());
            s.process(budget, &cancel).unwrap();
            if lock && !locked && s.accepted_frames() >= count as u64 / 2 {
                s.lock_grid_range(libapta::FrameRange {
                    first_frame: 0,
                    end_frame: count as u64 / 2 - 8192,
                })
                .unwrap();
                locked = true;
            }
        }
        s.finish_input().unwrap();
        for _ in 0..100000 {
            s.process(budget, &cancel).unwrap();
            if s.state() == SessionState::Complete {
                break;
            }
        }
        assert_eq!(s.state(), SessionState::Complete);
        if lock {
            let revision = s.grid_revision().unwrap();
            assert_eq!(revision.state, libapta::RevisionState::Pending);
            assert_eq!(
                s.apply_grid_revision(0),
                Err(libapta::Error::InvalidArgument)
            );
            assert_eq!(
                s.apply_grid_revision(revision.revision_id + 1),
                Err(libapta::Error::Conflict)
            );
            s.apply_grid_revision(revision.revision_id).unwrap();
            assert_eq!(
                s.apply_grid_revision(revision.revision_id),
                Err(libapta::Error::InvalidState)
            );
            let mut local = [0; 144];
            libapta::tempo::write_local_grid(
                &s.local_grid().unwrap(),
                s.tempo().unwrap().selected.tempo_millibpm,
                &mut local,
            )
            .unwrap();
            let reference = (0..container.section_count())
                .filter_map(|i| container.section(i))
                .find(|s| s.fourcc == *b"LGRD")
                .unwrap()
                .payload;
            assert_eq!(reference, local);
        }
        let mut temp = [0u8; 104];
        let size = libapta::tempo::write_tempo_payload(&s.tempo().unwrap(), &mut temp).unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"TEMP")
            .unwrap()
            .payload;
        assert_eq!(
            reference,
            &temp[..size],
            "ensemble rate {rate} steps {steps}"
        );
        let grid = s.global_grid().unwrap();
        if rate == 2000 {
            assert_eq!(grid.beats.len(), libapta::global_analysis::MAX_BEATS);
            assert_ne!(grid.flags & 128, 0);
        }
        if count == 256 * 4800 + 17 {
            assert_eq!(grid.segments.len(), 5);
        }
        if count == 256 * 16000 + 17 {
            assert_eq!(grid.segments.len(), libapta::global_analysis::MAX_SEGMENTS);
            assert_ne!(grid.flags & 128, 0);
        }
        let mut native = vec![0u8; 200000];
        let size = libapta::grid::write_payload(&grid, true, &mut native).unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"GGRD")
            .unwrap()
            .payload;
        assert_eq!(reference, &native[..size], "rate {rate} steps {steps}");
        let mut revision = [0u8; 96];
        let size =
            libapta::grid::write_revision(&s.grid_revision().unwrap(), &grid, true, &mut revision)
                .unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"REVN")
            .unwrap()
            .payload;
        assert_eq!(
            reference,
            &revision[..size],
            "revision rate {rate} steps {steps}"
        );
    }
}

#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn native_key_matches_c_audio_analysis() {
    use std::{fs, process::Command};
    let oracle = std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap();
    for (rate, count, kind, steps) in [
        (8000, 8000 * 6 + 17, 0, 0),
        (44100, 44100 * 10, 1, 1),
        (48000, 48000 * 9, 2, 2),
    ] {
        let samples: Vec<f32> = (0..count)
            .map(|i| {
                if kind == 2 {
                    ((i * 127 % 65536) as i32 - 32768) as f32 / 32768.0
                } else {
                    let t = i as f32 / rate as f32;
                    let pitches = if kind == 0 {
                        [261.6256, 329.6276, 391.9954]
                    } else {
                        [220.0, 261.6256, 329.6276]
                    };
                    pitches
                        .into_iter()
                        .map(|f| libm::sinf(core::f32::consts::TAU * f * t) * 0.2)
                        .sum()
                }
            })
            .collect();
        let path = std::env::temp_dir().join(format!("apta-key-{}-{rate}.pcm", std::process::id()));
        let bytes: Vec<_> = samples.iter().flat_map(|v| v.to_le_bytes()).collect();
        fs::write(&path, bytes).unwrap();
        let c = Command::new(&oracle)
            .args([
                rate.to_string(),
                steps.to_string(),
                path.to_string_lossy().into_owned(),
                "key".into(),
            ])
            .output()
            .unwrap();
        fs::remove_file(path).unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let container = libapta::container::Container::parse(
            &c.stdout,
            libapta::container::ParseOptions::default(),
        )
        .unwrap();
        let mut queue = vec![NormalizedSample::default(); count];
        let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
        let mut s = Session::new(
            SessionConfig {
                sample_rate: rate,
                channel_count: 1,
                total_frames: count as u64,
                frames_per_column: 32768,
            },
            &mut queue,
            &mut columns,
        )
        .unwrap();
        s.enable_key().unwrap();
        let token = CancellationToken::new();
        let budget = WorkBudget {
            maximum_steps: steps,
            maximum_input_frames: 0,
        };
        for block in samples.chunks(4096) {
            s.push_interleaved(block).unwrap();
            s.process(budget, &token).unwrap();
        }
        s.finish_input().unwrap();
        for _ in 0..100000 {
            s.process(budget, &token).unwrap();
            if s.state() == SessionState::Complete {
                break;
            }
        }
        assert_eq!(s.state(), SessionState::Complete);
        let mut native = [0u8; 128];
        let size = libapta::dj::write_key(s.key().unwrap(), Some(count as u64), true, &mut native)
            .unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"MKEY")
            .unwrap()
            .payload;
        assert_eq!(reference, &native[..size], "key rate {rate} kind {kind}");
    }
}

#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn native_meter_and_calibrated_quality_match_c() {
    use std::{fs, process::Command};
    let oracle = std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap();
    for (rate, count, tempo, meter, steps) in [
        (8000, 320000, 120, 4, 0),
        (44100, 44100 * 20 + 37, 128, 3, 2),
        (48000, 48000 * 12, 95, 4, 0),
    ] {
        let period = u64::from(rate) * 60 / u64::from(tempo);
        let mut samples = pcm(rate, count, tempo);
        for (i, s) in samples.iter_mut().enumerate() {
            if i as u64 / period % meter != 0 {
                *s *= 0.25;
            }
        }
        let path =
            std::env::temp_dir().join(format!("apta-meter-{}-{rate}.pcm", std::process::id()));
        let bytes: Vec<_> = samples.iter().flat_map(|v| v.to_le_bytes()).collect();
        fs::write(&path, bytes).unwrap();
        let c = Command::new(&oracle)
            .args([
                rate.to_string(),
                steps.to_string(),
                path.to_string_lossy().into_owned(),
                "meter".into(),
            ])
            .output()
            .unwrap();
        fs::remove_file(path).unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let container = libapta::container::Container::parse(
            &c.stdout,
            libapta::container::ParseOptions::default(),
        )
        .unwrap();
        let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
        let mut flux = vec![0.0; BIN_CAPACITY];
        let mut queue = vec![NormalizedSample::default(); count];
        let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
        let mut s = Session::new(
            SessionConfig {
                sample_rate: rate,
                channel_count: 1,
                total_frames: count as u64,
                frames_per_column: 32768,
            },
            &mut queue,
            &mut columns,
        )
        .unwrap();
        s.enable_tempo(&mut bins, &mut flux).unwrap();
        s.enable_meter().unwrap();
        s.enable_calibrated_quality().unwrap();
        let token = CancellationToken::new();
        let budget = WorkBudget {
            maximum_steps: steps,
            maximum_input_frames: 0,
        };
        for block in samples.chunks(4096) {
            s.push_interleaved(block).unwrap();
            s.process(budget, &token).unwrap();
        }
        s.finish_input().unwrap();
        for _ in 0..100000 {
            s.process(budget, &token).unwrap();
            if s.state() == SessionState::Complete {
                break;
            }
        }
        assert_eq!(s.state(), SessionState::Complete);
        let mut native = [0u8; 256];
        let size =
            libapta::dj::write_meter(s.meter().unwrap(), Some(count as u64), true, &mut native)
                .unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"MTRD")
            .unwrap()
            .payload;
        assert_eq!(reference, &native[..size], "meter rate {rate}");
        let size = libapta::dj::write_quality(
            &[s.bpm_quality().unwrap()],
            true,
            libapta::result::BPM,
            &mut native,
        )
        .unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"CONF")
            .unwrap()
            .payload;
        assert_eq!(reference, &native[..size], "quality rate {rate}");
    }
}
