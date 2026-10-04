// SPDX-License-Identifier: Apache-2.0
use libapta::{session::*, waveform::*, *};
use libapta_runtime::*;
fn config(total_frames: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 8000,
        channel_count: 1,
        total_frames,
        frames_per_column: 256,
    }
}
fn run(s: &mut OwnedSparseSession) {
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
}
#[test]
fn sparse_owned_priority_partial_coverage_and_retained_results() {
    let mut s = OwnedSparseSession::new(config(4096), SparseLimits::default()).unwrap();
    s.enable_three_band().unwrap();
    s.enable_detail().unwrap();
    let id = s
        .request_region(RegionRequest {
            range: FrameRange {
                first_frame: 2048,
                end_frame: 3072,
            },
            feature_mask: result::WAVEFORM_OVERVIEW,
            priority: 240,
            soft_deadline_monotonic_ns: 0,
            request_id: 0,
        })
        .unwrap();
    let initial = s.results().acquire().unwrap();
    s.push_at(0, PcmView::F32Interleaved(&[0.25; 1024]))
        .unwrap();
    s.push_at(2048, PcmView::F32Interleaved(&[-0.5; 1024]))
        .unwrap();
    s.process(
        WorkBudget {
            maximum_input_frames: 256,
            maximum_steps: 1,
        },
        &CancellationToken::new(),
    )
    .unwrap();
    let first = s.results().acquire().unwrap();
    assert_eq!(first.view().overview.unwrap().spans[0].first_frame, 2048);
    assert_eq!(
        s.request_progress(id).unwrap().state,
        RequestState::PartiallySatisfied
    );
    run(&mut s);
    assert_eq!(
        s.request_progress(id).unwrap().state,
        RequestState::Satisfied
    );
    s.finish_input().unwrap();
    run(&mut s);
    assert_eq!(s.session().state(), SessionState::Complete);
    let final_result = s.results().acquire().unwrap();
    assert_eq!(final_result.view().overview.unwrap().spans.len(), 2);
    assert_eq!(
        final_result.view().overview.unwrap().state,
        FeatureState::Partial
    );
    assert!(initial.view().overview.is_none());
    drop(s);
    std::thread::spawn(move || {
        assert_eq!(first.view().overview.unwrap().columns.len(), 1);
        assert_eq!(final_result.view().overview.unwrap().columns.len(), 8);
    })
    .join()
    .unwrap();
}
#[test]
fn sparse_detail_replay_preserves_overview_music_and_filter_history() {
    let mut s = OwnedSparseSession::new(config(98304), SparseLimits::default()).unwrap();
    s.enable_three_band().unwrap();
    s.enable_detail().unwrap();
    s.enable_default_music().unwrap();
    for first in (0..81920).step_by(4096) {
        s.push_at(first, PcmView::F32Interleaved(&[0.25; 4096]))
            .unwrap();
        run(&mut s);
    }
    let before = s.results().acquire().unwrap();
    assert!(before
        .view()
        .detail
        .unwrap()
        .tiles
        .iter()
        .all(|t| t.tile_index != 0));
    let processed = s.session().processed_frames();
    let id = s
        .request_region(RegionRequest {
            range: FrameRange {
                first_frame: 0,
                end_frame: 256,
            },
            feature_mask: result::WAVEFORM_DETAIL,
            priority: 240,
            soft_deadline_monotonic_ns: 0,
            request_id: 0,
        })
        .unwrap();
    assert_eq!(
        s.next_pcm_request().unwrap().range,
        FrameRange {
            first_frame: 0,
            end_frame: 256
        }
    );
    assert_eq!(
        s.push_at(0, PcmView::F32Interleaved(&[f32::NAN; 256])),
        Err(Error::InvalidArgument)
    );
    s.push_at(0, PcmView::F32Interleaved(&[-0.75; 256]))
        .unwrap();
    run(&mut s);
    let after = s.results().acquire().unwrap();
    assert_eq!(processed, s.session().processed_frames());
    assert_eq!(before.view().overview, after.view().overview);
    assert_eq!(before.view().tempo, after.view().tempo);
    assert_eq!(before.view().global_grid, after.view().global_grid);
    assert!(after
        .view()
        .detail
        .unwrap()
        .tiles
        .iter()
        .any(|t| t.tile_index == 0));
    assert_eq!(
        s.request_progress(id).unwrap().state,
        RequestState::Satisfied
    );
    assert!(before
        .view()
        .detail
        .unwrap()
        .tiles
        .iter()
        .all(|t| t.tile_index != 0));
}
#[test]
fn sparse_mirror_failure_blocks_mutations_context_lives_through_readers() {
    let ctx = RuntimeContext::new(ContextLimits {
        maximum_results: 2,
        ..ContextLimits::default()
    });
    let mut s = ctx
        .create_sparse_session(config(512), SparseLimits::default())
        .unwrap();
    let initial = s.results().acquire().unwrap();
    s.push_at(256, PcmView::F32Interleaved(&[0.5; 256]))
        .unwrap();
    assert_eq!(
        s.process(WorkBudget::default(), &CancellationToken::new()),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.session().processed_frames(), 256);
    assert_eq!(
        s.push_at(0, PcmView::F32Interleaved(&[0.25; 256])),
        Err(Error::InvalidState)
    );
    assert_eq!(s.next_pcm_request(), Err(Error::InvalidState));
    drop(initial);
    assert_eq!(s.refresh(), Ok(true));
    let last = s.results().acquire().unwrap();
    drop(s);
    assert_eq!(ctx.usage().unwrap().sessions, 0);
    assert_eq!(ctx.close(), Err(Error::Busy));
    drop(last);
    assert_eq!(ctx.close(), Ok(()));
}
#[test]
fn sparse_queue_range_exhaustion_and_cancellation() {
    let mut s = OwnedSparseSession::new(
        config(1024),
        SparseLimits {
            queue_nodes: 1,
            range_capacity: 1,
            ..SparseLimits::default()
        },
    )
    .unwrap();
    assert_eq!(
        s.push_at(u64::MAX - 2, PcmView::S16Interleaved(&[0; 4])),
        Err(Error::InvalidArgument)
    );
    s.push_at(0, PcmView::S16Interleaved(&[123; 256])).unwrap();
    assert_eq!(s.push_at(512, PcmView::S16Interleaved(&[123; 256])), Ok(0));
    run(&mut s);
    assert_eq!(s.push_at(256, PcmView::S16Interleaved(&[123; 256])), Ok(0));
    let cancel = CancellationToken::new();
    cancel.cancel();
    assert_eq!(
        s.process(WorkBudget::default(), &cancel),
        Err(Error::Cancelled)
    );
    assert_eq!(
        s.process(WorkBudget::default(), &CancellationToken::new()),
        Err(Error::Cancelled)
    );
}
#[test]
#[ignore = "requires APTA_C_DETAIL_SESSION_ORACLE"]
fn sparse_owned_detail_matches_public_c_partial_eof() {
    use std::fmt::Write;
    let out = std::process::Command::new(std::env::var_os("APTA_C_DETAIL_SESSION_ORACLE").unwrap())
        .output()
        .unwrap();
    assert!(out.status.success());
    let mut c = config(513);
    c.sample_rate = 48000;
    c.frames_per_column = 64;
    let mut s = OwnedSparseSession::new(c, SparseLimits::default()).unwrap();
    s.enable_detail().unwrap();
    s.push_at(256, PcmView::S16Interleaved(&[123; 257]))
        .unwrap();
    let mut actual = String::new();
    for final_step in [false, true] {
        if final_step {
            s.finish_input().unwrap();
            run(&mut s);
        } else {
            s.process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1,
                },
                &CancellationToken::new(),
            )
            .unwrap();
        }
        let r = s.results().acquire().unwrap();
        let v = r.view();
        let d = v.detail.unwrap();
        writeln!(actual, "R {}", d.tiles.len()).unwrap();
        for t in d.tiles {
            writeln!(
                actual,
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
                    actual,
                    "C {} {} {} {} {} {} {}",
                    c.minimum, c.maximum, c.rms, c.low, c.mid, c.high, c.flags
                )
                .unwrap();
            }
        }
    }
    assert_eq!(actual, String::from_utf8(out.stdout).unwrap());
}

#[test]
#[ignore = "requires APTA_C_TEMPO_ANALYSIS_ORACLE"]
fn all_stage_sparse_detail_replay_matches_public_c_wire() {
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
    let path = std::env::temp_dir().join(format!("apta-sparse-replay-{}.pcm", std::process::id()));
    std::fs::write(
        &path,
        pcm.iter().flat_map(|f| f.to_ne_bytes()).collect::<Vec<_>>(),
    )
    .unwrap();
    let out = std::process::Command::new(std::env::var_os("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap())
        .args(["8000", "0"])
        .arg(&path)
        .arg("all-replay")
        .output()
        .unwrap();
    std::fs::remove_file(path).unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mut c = config(320000);
    c.frames_per_column = 32768;
    let mut s = OwnedSparseSession::new(c, SparseLimits::default()).unwrap();
    s.enable_three_band().unwrap();
    s.enable_detail().unwrap();
    s.enable_default_music().unwrap();
    for (i, block) in pcm.chunks(4096).enumerate() {
        s.push_at((i * 4096) as u64, PcmView::F32Interleaved(block))
            .unwrap();
        run(&mut s);
    }
    let before = s.results().acquire().unwrap();
    s.request_region(RegionRequest {
        range: FrameRange {
            first_frame: 0,
            end_frame: 256,
        },
        feature_mask: result::WAVEFORM_DETAIL,
        priority: 240,
        soft_deadline_monotonic_ns: 0,
        request_id: 0,
    })
    .unwrap();
    assert_eq!(s.next_pcm_request().unwrap().range.end_frame, 256);
    s.push_at(0, PcmView::F32Interleaved(&[-0.75; 256]))
        .unwrap();
    run(&mut s);
    assert_eq!(s.session().processed_frames(), 320000);
    assert_eq!(
        before.view().overview,
        s.results().acquire().unwrap().view().overview
    );
    s.finish_input().unwrap();
    for _ in 0..1000 {
        run(&mut s);
        if s.session().state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(s.session().state(), SessionState::Complete);
    let snapshot = s.snapshot().unwrap();
    let mut descriptors = [WaveformTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 0,
        columns: &[],
    }; 4];
    let view = result::from_session_snapshot(&snapshot, &mut descriptors, NativeLimits::default())
        .unwrap();
    let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
    result::write(&view, &mut bytes, Default::default()).unwrap();
    assert_eq!(bytes, out.stdout);
}

#[test]
fn owning_seed_resume_ignores_checkpoint_detail_bands_and_preserves_old_graph() {
    let mut original = OwnedSparseSession::new(config(8192), SparseLimits::default()).unwrap();
    original.enable_three_band().unwrap();
    original.enable_detail().unwrap();
    original
        .push_at(0, PcmView::F32Interleaved(&[0.5; 4096]))
        .unwrap();
    run(&mut original);
    let checkpoint = original.results().acquire().unwrap();
    let mut resumed = OwnedSparseSession::new(config(8192), SparseLimits::default()).unwrap();
    resumed.enable_three_band().unwrap();
    resumed.enable_detail().unwrap();
    assert_eq!(
        resumed.seed_from_result(&checkpoint, true),
        Err(Error::Conflict)
    );
    assert!(resumed.session().accepted_ranges().is_empty());
    resumed.seed_from_result(&checkpoint, false).unwrap();
    assert_eq!(resumed.results().acquire().unwrap().info().generation, 1);
    assert_eq!(resumed.enable_default_music(), Err(Error::InvalidState));
    assert_eq!(resumed.session().processed_frames(), 0);
    assert_eq!(resumed.next_pcm_request().unwrap().range.first_frame, 4096);
    resumed
        .push_at(4096, PcmView::F32Interleaved(&[-0.25; 4096]))
        .unwrap();
    resumed.finish_input().unwrap();
    run(&mut resumed);
    let result = resumed.results().acquire().unwrap();
    let view = result.view();
    let w = view.overview.unwrap();
    assert_eq!(w.columns.len(), 32);
    assert_eq!(w.state, FeatureState::Final);
    assert_eq!(resumed.session().processed_frames(), 4096);
    for c in &w.columns[..16] {
        assert_eq!((c.low, c.mid, c.high), (0, 0, 0));
        assert_eq!(c.flags & 8, 8);
    }
    assert_eq!(checkpoint.view().detail.unwrap().tiles[0].first_frame, 0);
    assert_eq!(view.detail.unwrap().tiles[0].first_frame, 4096);
    assert_eq!(checkpoint.view().overview.unwrap().columns.len(), 16);
}

#[test]
fn sparse_reservation_preserves_partial_nodes_and_exact_all_stage_output() {
    fn exercise(grow: bool) -> Vec<u8> {
        let mut s = OwnedSparseSession::new(
            config(16384),
            SparseLimits {
                queue_nodes: if grow { 1 } else { 4 },
                range_capacity: if grow { 1 } else { 8 },
                ..SparseLimits::default()
            },
        )
        .unwrap();
        s.enable_three_band().unwrap();
        s.enable_detail().unwrap();
        s.enable_default_music().unwrap();
        s.push_at(8192, PcmView::S16Interleaved(&[12000; 4096]))
            .unwrap();
        s.process(
            WorkBudget {
                maximum_input_frames: 73,
                maximum_steps: 1,
            },
            &CancellationToken::new(),
        )
        .unwrap();
        let old = s.results().acquire().unwrap();
        let generation = old.info().generation;
        if grow {
            assert_eq!(s.push_at(0, PcmView::S16Interleaved(&[-5000; 4096])), Ok(0));
            let bytes = s.working_bytes();
            assert_eq!(s.reserve_pending(usize::MAX, 8), Err(Error::LimitExceeded));
            assert_eq!(s.working_bytes(), bytes);
            assert_eq!(s.session().processed_frames(), 73);
            s.reserve_pending(4, 8).unwrap();
            assert!(s.working_bytes() > bytes);
            let bytes = s.working_bytes();
            s.reserve_pending(1, 1).unwrap();
            assert_eq!(s.working_bytes(), bytes);
            assert_eq!(s.results().acquire().unwrap().info().generation, generation);
        }
        for (first, sample) in [(0, -5000), (12288, 23000), (4096, 1000)] {
            assert_eq!(
                s.push_at(first, PcmView::S16Interleaved(&[sample; 4096])),
                Ok(4096)
            );
        }
        s.finish_input().unwrap();
        for _ in 0..20 {
            run(&mut s);
            if s.session().state() == SessionState::Complete {
                break;
            }
        }
        assert_eq!(s.session().processed_frames(), 16384);
        assert_eq!(s.session().state(), SessionState::Complete);
        assert_eq!(old.info().generation, generation);
        assert!(old.view().overview.is_none());
        assert_eq!(s.reserve_pending(8, 16), Err(Error::InvalidState));
        let snapshot = s.snapshot().unwrap();
        let mut tiles = [WaveformTile {
            level_id: 1,
            tile_index: 0,
            first_frame: 0,
            end_frame: 0,
            first_column_index: 0,
            state: FeatureState::Partial,
            confidence: 0,
            columns: &[],
        }; 4];
        let view =
            result::from_session_snapshot(&snapshot, &mut tiles, NativeLimits::default()).unwrap();
        let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
        result::write(&view, &mut bytes, Default::default()).unwrap();
        bytes
    }
    assert_eq!(exercise(true), exercise(false));
}

#[test]
fn sparse_reservation_recovers_fragmented_range_backpressure() {
    let mut s = OwnedSparseSession::new(
        config(32768),
        SparseLimits {
            queue_nodes: 1,
            range_capacity: 1,
            ..SparseLimits::default()
        },
    )
    .unwrap();
    // Real processing at 64 separated columns, then filling every hole.
    for index in 0..64 {
        if index != 0 {
            s.reserve_pending(1, index + 1).unwrap();
        }
        assert_eq!(
            s.push_at(index as u64 * 512, PcmView::S16Interleaved(&[7000; 256])),
            Ok(256)
        );
        run(&mut s);
    }
    assert_eq!(s.session().accepted_ranges().len(), 64);
    assert_eq!(s.push_at(256, PcmView::S16Interleaved(&[7000; 256])), Ok(0));
    s.reserve_pending(1, 65).unwrap();
    for index in 0..64 {
        assert_eq!(
            s.push_at(index * 512 + 256, PcmView::S16Interleaved(&[7000; 256])),
            Ok(256)
        );
        run(&mut s);
    }
    s.finish_input().unwrap();
    run(&mut s);
    assert_eq!(
        s.session().accepted_ranges(),
        &[FrameRange {
            first_frame: 0,
            end_frame: 32768
        }]
    );
    assert_eq!(s.session().processed_frames(), 32768);
    assert_eq!(
        s.results()
            .acquire()
            .unwrap()
            .view()
            .overview
            .unwrap()
            .columns
            .len(),
        128
    );
}

#[test]
#[ignore = "requires APTA_C_SPARSE_CAPACITY_ORACLE"]
fn fragmented_capacity_and_merging_match_public_c_wire() {
    for count in [63usize, 4096] {
        let c =
            std::process::Command::new(std::env::var_os("APTA_C_SPARSE_CAPACITY_ORACLE").unwrap())
                .arg(count.to_string())
                .output()
                .unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        let mut cfg = config(count as u64 * 128);
        cfg.frames_per_column = 64;
        let mut s = OwnedSparseSession::new(
            cfg,
            SparseLimits {
                queue_nodes: 1,
                range_capacity: 8,
                ..SparseLimits::default()
            },
        )
        .unwrap();
        for pass in 0..2 {
            for i in 0..count {
                if pass == 0 && i >= 8 && i.is_power_of_two() {
                    s.reserve_pending(1, i * 2).unwrap();
                }
                if pass == 1 && i == 0 {
                    s.reserve_pending(1, count + 1).unwrap();
                }
                let pcm: [i16; 64] = core::array::from_fn(|j| {
                    (((i * 71 + j * 113 + pass * 17000) % 60000) as i32 - 30000) as i16
                });
                assert_eq!(
                    s.push_at((i * 128 + pass * 64) as u64, PcmView::S16Interleaved(&pcm)),
                    Ok(64)
                );
                run(&mut s);
            }
            if pass == 0 {
                assert_eq!(s.session().accepted_ranges().len(), count);
            }
        }
        s.finish_input().unwrap();
        run(&mut s);
        assert_eq!(s.session().processed_frames(), count as u64 * 128);
        assert_eq!(s.session().accepted_ranges().len(), 1);
        let snapshot = s.snapshot().unwrap();
        let mut tiles = [];
        let view =
            result::from_session_snapshot(&snapshot, &mut tiles, NativeLimits::default()).unwrap();
        let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
        result::write(&view, &mut bytes, Default::default()).unwrap();
        assert_eq!(bytes, c.stdout);
    }
}

#[test]
#[ignore = "requires APTA_C_UNKNOWN_SPARSE_ORACLE"]
fn known_sparse_final_wire_matches_unknown_origin_c_with_and_without_holes() {
    // Establish the final overview contract without claiming native unknown
    // sparse construction, growth, scheduling or intermediate generations.
    for holes in [false, true] {
        let out =
            std::process::Command::new(std::env::var_os("APTA_C_UNKNOWN_SPARSE_ORACLE").unwrap())
                .arg(if holes { "1" } else { "0" })
                .output()
                .unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        let mut c = config(320);
        c.frames_per_column = 64;
        let mut s = OwnedSparseSession::new(c, SparseLimits::default()).unwrap();
        let offsets = [128u64, 0, 256, 64, 192];
        for &first in &offsets[..if holes { 3 } else { 5 }] {
            let pcm: [i16; 64] =
                core::array::from_fn(|i| ((first + i as u64) as i32 * 71 - 12000) as i16);
            assert_eq!(s.push_at(first, PcmView::S16Interleaved(&pcm)), Ok(64));
            run(&mut s);
        }
        s.finish_input().unwrap();
        run(&mut s);
        let snapshot = s.snapshot().unwrap();
        let view =
            result::from_session_snapshot(&snapshot, &mut [], NativeLimits::default()).unwrap();
        let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
        result::write(&view, &mut bytes, Default::default()).unwrap();
        assert_eq!(bytes, out.stdout);
    }
}
