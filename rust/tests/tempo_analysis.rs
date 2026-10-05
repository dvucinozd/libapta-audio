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

// Complete independent expected payloads for the existing segment-cap fixture.
// The first seven segments are unchanged; C extends the eighth through all
// subsequent incompatible windows, while Rust retains only its accepted window.
fn capacity_payloads(corrected: bool) -> (Vec<u8>, Vec<u8>) {
    use libapta::*;
    let id = if corrected { 34 } else { 47 };
    let rows = [
        (0, 262144, 0, 3199, 4203341937, 82, 150001, 58),
        (262144, 524288, 262144, 5887, 4214043499, 45, 81522, 58),
        (524288, 786432, 526336, 5504, 81950550, 48, 87209, 57),
        (786432, 1572864, 786432, 3199, 4203341937, 246, 150001, 57),
        (1572864, 2097152, 1574912, 4863, 4127842865, 108, 98685, 59),
        (2097152, 2359296, 2099200, 3071, 4210525343, 85, 156251, 61),
        (2359296, 2621440, 2359296, 4479, 4269312058, 59, 107143, 60),
        (
            2621440,
            if corrected { 2883584 } else { 4096017 },
            2623488,
            3071,
            4210525343,
            if corrected { 85 } else { 480 },
            156251,
            59,
        ),
    ];
    let mut segments = Vec::new();
    let mut beats = Vec::new();
    for (i, (first, end, anchor, whole, fraction, count, tempo, confidence)) in
        rows.into_iter().enumerate()
    {
        let ordinal = beats.len() as i64;
        segments.push(GridSegment {
            applicability_range: FrameRange {
                first_frame: first,
                end_frame: end,
            },
            anchor_position: FractionalFrame {
                whole_frame: anchor,
                fraction_q32: 0,
            },
            anchor_ordinal: ordinal,
            frames_per_beat: FramePeriod {
                whole_frames: whole,
                fraction_q32: fraction,
            },
            beat_count: count,
            nominal_tempo_millibpm: tempo,
            confidence,
            state: FeatureState::Final,
            flags: 130,
            segment_id: i as u32 + 1,
            revision: id,
        });
        for n in 0..count {
            let q32 = (u128::from(anchor) << 32)
                + u128::from(n) * ((u128::from(whole) << 32) + u128::from(fraction));
            beats.push(Beat {
                position: FractionalFrame {
                    whole_frame: (q32 >> 32) as u64,
                    fraction_q32: q32 as u32,
                },
                ordinal: ordinal + i64::from(n),
                revision: id,
                flags: 130,
                confidence,
            });
        }
    }
    let range = FrameRange {
        first_frame: 0,
        end_frame: 4096017,
    };
    let g = GlobalGrid {
        state: FeatureState::Final,
        confidence: 58,
        flags: 130,
        representation: GridRepresentation::Hybrid,
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_range: range,
        segments: &segments,
        beats: &beats,
    };
    let r = GridRevision {
        state: RevisionState::Applied,
        confidence: 58,
        flags: 6,
        revision_id: id,
        previous_revision_id: id - 1,
        proposed_representation: GridRepresentation::Hybrid,
        proposed_segment_count: 8,
        proposed_beat_count: beats.len() as u32,
        affected_range: range,
    };
    let mut grid = vec![0; 96 + 80 * 8 + 40 * beats.len()];
    grid::write_payload(&g, true, &mut grid).unwrap();
    let mut revision = vec![0; 80];
    grid::write_revision(&r, &g, true, &mut revision).unwrap();
    (grid, revision)
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
        if count == 256 * 16000 + 17 {
            let (old, _) = capacity_payloads(false);
            let (corrected, _) = capacity_payloads(true);
            assert_eq!(reference, old, "original overflow C grid");
            assert_eq!(&native[..size], corrected, "corrected bounded grid");
            assert_eq!(
                grid.segments.last().unwrap().applicability_range.end_frame,
                2883584
            );
            assert_eq!(grid.beats.len(), 758);
            if let Some(output) = std::env::var_os("APTA_S6_EVIDENCE_DIR") {
                let output = std::path::PathBuf::from(output);
                fs::create_dir(&output).unwrap();
                fs::write(
                    output.join("cap.pcm"),
                    samples
                        .iter()
                        .flat_map(|v| v.to_le_bytes())
                        .collect::<Vec<_>>(),
                )
                .unwrap();
                fs::write(output.join("cap-c.apta"), &c.stdout).unwrap();
                fs::write(output.join("cap-rust.ggrd"), &native[..size]).unwrap();
                let (_, r) = capacity_payloads(true);
                fs::write(output.join("cap-rust.revn"), r).unwrap();
            }
        } else {
            assert_eq!(reference, &native[..size], "rate {rate} steps {steps}");
        }
        let mut revision = [0u8; 96];
        let size =
            libapta::grid::write_revision(&s.grid_revision().unwrap(), &grid, true, &mut revision)
                .unwrap();
        let reference = (0..container.section_count())
            .filter_map(|i| container.section(i))
            .find(|s| s.fourcc == *b"REVN")
            .unwrap()
            .payload;
        if count == 256 * 16000 + 17 {
            assert_eq!(
                reference,
                capacity_payloads(false).1,
                "original overflow C revision"
            );
            assert_eq!(
                &revision[..size],
                capacity_payloads(true).1,
                "corrected bounded revision"
            );
        } else {
            assert_eq!(
                reference,
                &revision[..size],
                "revision rate {rate} steps {steps}"
            );
        }
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
