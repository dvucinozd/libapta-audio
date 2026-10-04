// SPDX-License-Identifier: Apache-2.0
use libapta::{session::*, waveform::*, *};
use libapta_runtime::*;
fn config(total_frames: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 8000,
        channel_count: 1,
        total_frames,
        frames_per_column: 32768,
    }
}
fn pcm(first: usize, count: usize) -> Vec<f32> {
    (first..first + count)
        .map(|i| {
            let phase = i % if i < 320000 { 3840 } else { 6000 };
            if phase < 64 {
                (64 - phase) as f32 / 64.0 * 0.75
            } else {
                0.0
            }
        })
        .collect()
}
#[test]
fn sequential_owning_lock_rollback_and_revision_acceptance_survive_mirror_failure() {
    let mut s = GrowingSession::new(config(640000), GrowingLimits::default()).unwrap();
    s.enable_default_music().unwrap();
    let token = CancellationToken::new();
    for first in (0..640000).step_by(4096) {
        let count = (640000 - first).min(4096);
        let block = pcm(first, count);
        s.push_pcm(PcmView::F32Interleaved(&block)).unwrap();
        s.process(WorkBudget::default(), &token).unwrap();
        if first < 320000 && first + count >= 320000 {
            let old = s.results().acquire().unwrap();
            let before = s.session().local_grid();
            let range = FrameRange {
                first_frame: 0,
                end_frame: 311808,
            };
            s.set_result_limits(NativeLimits {
                maximum_storage_bytes: 1,
                ..NativeLimits::default()
            });
            assert_eq!(s.lock_grid_range(range), Err(Error::LimitExceeded));
            assert_eq!(s.session().local_grid(), before);
            assert_eq!(
                s.results().acquire().unwrap().info().generation,
                old.info().generation
            );
            assert_eq!(s.refresh(), Ok(false));
            s.set_result_limits(NativeLimits::default());
            s.lock_grid_range(range).unwrap();
            let locked = s.results().acquire().unwrap();
            s.lock_grid_range(range).unwrap();
            assert_eq!(
                locked.info().generation,
                s.results().acquire().unwrap().info().generation
            );
            assert_eq!(old.view().local_grid.unwrap().flags & 1, 0);
        }
    }
    s.finish_input().unwrap();
    for _ in 0..1000 {
        s.process(WorkBudget::default(), &token).unwrap();
        if s.session().state() == SessionState::Complete {
            break;
        }
    }
    let old = s.results().acquire().unwrap();
    let revision = s.session().grid_revision().unwrap();
    assert_eq!(revision.state, RevisionState::Pending);
    assert_eq!(
        s.apply_grid_revision(revision.revision_id + 1),
        Err(Error::Conflict)
    );
    s.set_result_limits(NativeLimits {
        maximum_storage_bytes: 1,
        ..NativeLimits::default()
    });
    assert_eq!(
        s.apply_grid_revision(revision.revision_id),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        s.session().grid_revision().unwrap().state,
        RevisionState::Applied
    );
    assert_eq!(
        s.apply_grid_revision(revision.revision_id),
        Err(Error::InvalidState)
    );
    assert_eq!(old.view().revision.unwrap().state, RevisionState::Pending);
    s.set_result_limits(NativeLimits::default());
    assert_eq!(s.refresh(), Ok(true));
    assert_eq!(
        s.results()
            .acquire()
            .unwrap()
            .view()
            .revision
            .unwrap()
            .state,
        RevisionState::Applied
    );
    assert_eq!(
        s.apply_grid_revision(revision.revision_id),
        Err(Error::InvalidState)
    );
}
#[test]
fn sparse_owning_lock_rollback_and_revision_acceptance_survive_mirror_failure() {
    let mut s = OwnedSparseSession::new(config(640000), SparseLimits::default()).unwrap();
    s.enable_default_music().unwrap();
    let token = CancellationToken::new();
    for first in (0..640000).step_by(4096) {
        let count = (640000 - first).min(4096);
        let block = pcm(first, count);
        s.push_at(first as u64, PcmView::F32Interleaved(&block))
            .unwrap();
        s.process(WorkBudget::default(), &token).unwrap();
        if first < 320000 && first + count >= 320000 {
            let old = s.results().acquire().unwrap();
            let before = s.session().local_grid();
            let range = FrameRange {
                first_frame: 0,
                end_frame: 311808,
            };
            s.set_result_limits(NativeLimits {
                maximum_storage_bytes: 1,
                ..NativeLimits::default()
            });
            assert_eq!(s.lock_grid_range(range), Err(Error::LimitExceeded));
            assert_eq!(s.session().local_grid(), before);
            assert_eq!(
                s.results().acquire().unwrap().info().generation,
                old.info().generation
            );
            assert_eq!(s.refresh(), Ok(false));
            s.set_result_limits(NativeLimits::default());
            s.lock_grid_range(range).unwrap();
            let locked = s.results().acquire().unwrap();
            s.lock_grid_range(range).unwrap();
            assert_eq!(
                locked.info().generation,
                s.results().acquire().unwrap().info().generation
            );
            assert_eq!(old.view().local_grid.unwrap().flags & 1, 0);
        }
    }
    s.finish_input().unwrap();
    for _ in 0..1000 {
        s.process(WorkBudget::default(), &token).unwrap();
        if s.session().state() == SessionState::Complete {
            break;
        }
    }
    let old = s.results().acquire().unwrap();
    let revision = s.session().grid_revision().unwrap();
    assert_eq!(revision.state, RevisionState::Pending);
    assert_eq!(
        s.apply_grid_revision(revision.revision_id + 1),
        Err(Error::Conflict)
    );
    s.set_result_limits(NativeLimits {
        maximum_storage_bytes: 1,
        ..NativeLimits::default()
    });
    assert_eq!(
        s.apply_grid_revision(revision.revision_id),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        s.session().grid_revision().unwrap().state,
        RevisionState::Applied
    );
    assert_eq!(
        s.apply_grid_revision(revision.revision_id),
        Err(Error::InvalidState)
    );
    assert_eq!(old.view().revision.unwrap().state, RevisionState::Pending);
    s.set_result_limits(NativeLimits::default());
    assert_eq!(s.refresh(), Ok(true));
    assert_eq!(
        s.results()
            .acquire()
            .unwrap()
            .view()
            .revision
            .unwrap()
            .state,
        RevisionState::Applied
    );
    assert_eq!(
        s.apply_grid_revision(revision.revision_id),
        Err(Error::InvalidState)
    );
}
#[test]
fn requested_snapshot_projection_preserves_validation_and_native_defaults() {
    let masks = [
        result::WAVEFORM_OVERVIEW | result::BPM,
        result::WAVEFORM_OVERVIEW | result::MUSICAL_KEY,
        result::WAVEFORM_OVERVIEW | result::BPM | result::CONFIDENCE | result::CALIBRATED_QUALITY,
    ];
    for unknown in [false, true] {
        for requested in masks {
            let mut s = GrowingSession::new(
                config(if unknown {
                    TOTAL_FRAMES_UNKNOWN
                } else {
                    320000
                }),
                GrowingLimits::default(),
            )
            .unwrap();
            s.set_requested_features(requested).unwrap();
            s.enable_default_music().unwrap();
            assert_eq!(
                s.lock_grid_range(FrameRange::default()),
                Err(Error::InvalidArgument)
            );
            assert_eq!(
                s.lock_grid_range(FrameRange {
                    first_frame: 0,
                    end_frame: 1
                }),
                Err(Error::Unsupported)
            );
            for first in (0..320000).step_by(4096) {
                let block = pcm(first, (320000 - first).min(4096));
                s.push_pcm(PcmView::F32Interleaved(&block)).unwrap();
                s.process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
            }
            let before = s.results().acquire().unwrap();
            assert!(before.view().quality.is_empty());
            if unknown && requested & result::BPM != 0 {
                assert!(before.view().local_grid.is_some());
            } else {
                assert!(before.view().local_grid.is_none());
            }
            s.finish_input().unwrap();
            s.process(WorkBudget::default(), &CancellationToken::new())
                .unwrap();
            let result = s.results().acquire().unwrap();
            let v = result.view();
            assert_eq!(v.tempo.is_some(), requested & result::BPM != 0);
            assert_eq!(v.key.is_some(), requested & result::MUSICAL_KEY != 0);
            assert!(v.global_grid.is_none() && v.meter.is_none());
            assert_eq!(
                v.local_grid.is_some(),
                unknown && requested & result::BPM != 0
            );
            assert_eq!(
                !v.quality.is_empty(),
                requested & result::CALIBRATED_QUALITY != 0
            );
            assert_eq!(
                result.available_features() & !requested,
                if unknown && requested & result::BPM != 0 {
                    result::LOCAL_BEATGRID
                } else {
                    0
                }
            );
        }
    }
}

#[test]
#[ignore = "requires APTA_C_TEMPO_ANALYSIS_ORACLE"]
fn owning_requested_capability_wire_matches_public_c_known_and_unknown() {
    let pcm: Vec<f32> = (0..320000)
        .map(|i| {
            let phase = i % 4000;
            if phase < 64 {
                (64 - phase) as f32 / 64.0 * if i / 4000 % 4 == 0 { 0.75 } else { 0.2 }
            } else {
                0.0
            }
        })
        .collect();
    let path = std::env::temp_dir().join(format!("apta-projection-{}.pcm", std::process::id()));
    std::fs::write(
        &path,
        pcm.iter().flat_map(|x| x.to_ne_bytes()).collect::<Vec<_>>(),
    )
    .unwrap();
    for unknown in [false, true] {
        for requested in [
            result::WAVEFORM_OVERVIEW | result::BPM,
            result::WAVEFORM_OVERVIEW | result::MUSICAL_KEY,
            result::WAVEFORM_OVERVIEW | result::BPM | result::GLOBAL_BEATGRID,
            result::WAVEFORM_OVERVIEW
                | result::BPM
                | result::CONFIDENCE
                | result::CALIBRATED_QUALITY,
        ] {
            let output = std::process::Command::new(
                std::env::var_os("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap(),
            )
            .args(["8000", "0"])
            .arg(&path)
            .arg(format!(
                "project:{requested}{}",
                if unknown { ":unknown" } else { "" }
            ))
            .output()
            .unwrap();
            assert!(
                output.status.success(),
                "{}",
                String::from_utf8_lossy(&output.stderr)
            );
            let mut s = GrowingSession::new(
                config(if unknown {
                    TOTAL_FRAMES_UNKNOWN
                } else {
                    320000
                }),
                GrowingLimits::default(),
            )
            .unwrap();
            s.set_requested_features(requested).unwrap();
            s.enable_default_music().unwrap();
            for chunk in pcm.chunks(4096) {
                s.push_pcm(PcmView::F32Interleaved(chunk)).unwrap();
                s.process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
            }
            s.finish_input().unwrap();
            for _ in 0..1000 {
                s.process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
                if s.session().state() == SessionState::Complete {
                    break;
                }
            }
            let snapshot = s
                .session()
                .snapshot(1)
                .unwrap()
                .with_requested_features(requested)
                .unwrap();
            let mut tiles = [];
            let view =
                result::from_session_snapshot(&snapshot, &mut tiles, NativeLimits::default())
                    .unwrap();
            let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
            result::write(&view, &mut bytes, Default::default()).unwrap();
            assert_eq!(bytes, output.stdout, "mask {requested} unknown {unknown}");
        }
    }
    std::fs::remove_file(path).unwrap();
}
