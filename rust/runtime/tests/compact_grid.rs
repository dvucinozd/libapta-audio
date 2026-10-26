// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-compact-grid")]
use libapta_runtime::compact_grid::{synthetic as fx, *};
use std::sync::atomic::{AtomicBool, Ordering};

#[test]
fn every_frozen_fixture_matches_under_all_work_quotas() {
    for i in 0..fx::FIXTURE_COUNT {
        let c = fx::fixture(i);
        for quota in [1, 7, 256, 4096, usize::MAX] {
            let mut scratch = vec![Event::default(); c.scratch_entries];
            let actual = match c.session(&mut scratch) {
                Err(e) => fx::error_json(e),
                Ok(mut s) => {
                    while s.status() == Status::Running {
                        assert!(s.result().is_none());
                        match s.step(quota, &AtomicBool::new(false)) {
                            Ok(r) => assert!(r.units <= quota),
                            Err(_) => break,
                        }
                    }
                    assert!(s.total_units() <= MAX_UNITS);
                    if let Some(result) = s.result() {
                        match result {
                            ResultView::Grid { summary, .. } => assert_eq!(
                                fx::summary_hash(summary),
                                c.expected_summary_hash.unwrap()
                            ),
                            ResultView::Events(events) => {
                                assert_eq!(fx::event_hash(events), c.expected_event_hash.unwrap())
                            }
                        }
                    }
                    fx::observation(&s)
                }
            };
            assert_eq!(actual, c.expected, "{} quota{quota}", c.name);
        }
    }
}
#[test]
fn zero_quota_cancel_all_phases_and_scratch_reuse() {
    let c = fx::full("full4-48000").unwrap();
    for phase in [
        Phase::ValidateProbabilitiesAndSupport,
        Phase::ValidatePath,
        Phase::ExtractEvents,
        Phase::ValidateEvents,
        Phase::AccumulateFit,
        Phase::ComputeModel,
        Phase::CheckCoverage,
        Phase::CheckResiduals,
        Phase::BuildSummary,
    ] {
        let mut scratch = vec![Event::default(); MAX_EVENTS];
        let flag = AtomicBool::new(false);
        {
            let mut s = c.session(&mut scratch).unwrap();
            assert!(matches!(s.step(0, &flag), Err(Error::ZeroBudget)));
            assert_eq!(s.total_units(), 0);
            while s.phase() != phase {
                assert_eq!(s.status(), Status::Running);
                s.step(1, &flag).unwrap();
            }
            assert!(s.result().is_none());
            flag.store(true, Ordering::Release);
            s.step(usize::MAX, &flag).unwrap();
            assert_eq!(s.status(), Status::Cancelled);
            assert!(s.result().is_none());
            flag.store(false, Ordering::Release);
            s.step(1, &flag).unwrap();
            assert_eq!(s.status(), Status::Cancelled);
        }
        let mut s = c.session(&mut scratch).unwrap();
        s.step(usize::MAX, &flag).unwrap();
        assert_eq!(fx::observation(&s), c.expected);
        s.cancel();
        flag.store(true, Ordering::Release);
        s.step(1, &flag).unwrap();
        assert_eq!(fx::observation(&s), c.expected);
    }
}
#[test]
fn scratch_bounds_and_event_layout() {
    assert_eq!(std::mem::size_of::<Event>(), 8);
    let c = fx::full("full3-44100").unwrap();
    let sentinel = Event {
        global_row: u32::MAX,
        beat_number: 255,
        winning_class: 255,
        reserved: [255; 2],
    };
    let mut storage = vec![sentinel; MAX_EVENTS + 2];
    {
        let mut s = c.session(&mut storage[1..=MAX_EVENTS]).unwrap();
        s.step(usize::MAX, &AtomicBool::new(false)).unwrap();
        assert_eq!(fx::observation(&s), c.expected);
    }
    assert_eq!(storage[0], sentinel);
    assert_eq!(storage[MAX_EVENTS + 1], sentinel);
}
#[test]
fn executable_refuses_arbitrary_paths_and_case_names() {
    for args in [
        vec!["--input", "music.wav"],
        vec!["--case", "music"],
        vec!["--case", "fixtures", "extra"],
    ] {
        let out = std::process::Command::new(env!("CARGO_BIN_EXE_apta-compact-grid-synthetic"))
            .args(args)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(out.stdout.is_empty());
    }
}

#[test]
fn all_positive_fixtures_pass_unchanged_native_and_wire_contracts() {
    for i in 0..fx::FIXTURE_COUNT {
        let c = fx::fixture(i);
        if c.expected_summary_hash.is_none() {
            continue;
        }
        let mut scratch = vec![Event::default(); MAX_EVENTS];
        let mut s = c.session(&mut scratch).unwrap();
        s.step(usize::MAX, &AtomicBool::new(false)).unwrap();
        let Some(ResultView::Grid { summary, .. }) = s.result() else {
            panic!("positive grid");
        };
        let bytes = fx::validate_wire(summary).unwrap_or_else(|e| panic!("{}: {e}", c.name));
        assert!(bytes.len() <= 4096);
        // Corrupt the exact meter witness using a different source coordinate.
        let mut bad = summary;
        bad.downbeat_whole_frame += 1;
        assert!(fx::validate_wire(bad).is_err());
    }
}

#[test]
fn two_small_hmm_compositions_preserve_rows_period_and_phase() {
    use libapta_runtime::compact_hmm as hmm;
    for (meter, rows, fs) in [(3, 175, 44100), (4, 225, 48000)] {
        let probabilities = fx::prob_train(rows, 0, 25, (rows - 1) / 25 + 1, 1, meter);
        let end = ((rows - 1) / 25) * 25 + 1;
        let densities = hmm::synthetic::densities(rows, meter).unwrap();
        let mut scores = Vec::new();
        for m in [3, 4] {
            let g = hmm::synthetic::load(m).unwrap();
            let mut w = hmm::Workspace::allocate(
                hmm::StoragePlan::new(g.graph().classes.len(), end, 256, hmm::Mode::Score).unwrap(),
            )
            .unwrap();
            let mut d =
                hmm::Decoder::new(g.graph(), &densities[..end], 256, hmm::Mode::Score, &mut w)
                    .unwrap();
            d.step(usize::MAX, &AtomicBool::new(false)).unwrap();
            scores.push(d.result().unwrap().score);
        }
        assert_eq!(if scores[1] > scores[0] { 4 } else { 3 }, meter);
        let g = hmm::synthetic::load(meter).unwrap();
        let mut w = hmm::Workspace::allocate(
            hmm::StoragePlan::new(g.graph().classes.len(), end, 256, hmm::Mode::Trace).unwrap(),
        )
        .unwrap();
        let mut d =
            hmm::Decoder::new(g.graph(), &densities[..end], 256, hmm::Mode::Trace, &mut w).unwrap();
        d.step(usize::MAX, &AtomicBool::new(false)).unwrap();
        let path = d.result().unwrap().path.unwrap();
        let mut scratch = vec![Event::default(); MAX_EVENTS];
        let geometry = Geometry {
            sample_rate: fs,
            total_frames: rows as u64 * u64::from(fs) / 50,
        };
        let mut s = Session::from_path(
            PathInput {
                geometry,
                meter,
                probabilities: &probabilities,
                decoded_first_row: 0,
                decoded_end_row: end as u32,
                states: path,
            },
            Mode::Grid,
            &mut scratch,
        )
        .unwrap();
        s.step(usize::MAX, &AtomicBool::new(false)).unwrap();
        let Some(ResultView::Grid { summary, events }) = s.result() else {
            panic!("composition {:?} {:?}", s.status(), s.reason());
        };
        assert_eq!(summary.tempo_millibpm, 120000);
        assert_eq!(summary.anchor_q32, 0);
        assert_eq!(summary.period_q32, (u128::from(fs) / 2) << 32);
        assert_eq!(summary.downbeat_ordinal, 0);
        assert_eq!(
            events.iter().map(|e| e.global_row).collect::<Vec<_>>(),
            (0..end as u32).step_by(25).collect::<Vec<_>>()
        );
        fx::validate_wire(summary).unwrap();
    }
}

#[test]
fn finalized_wire_survives_input_and_scratch_reuse() {
    let mut c = fx::full("full3-44100").unwrap();
    let mut scratch = vec![Event::default(); MAX_EVENTS];
    let (summary, bytes) = {
        let mut s = c.session(&mut scratch).unwrap();
        s.step(usize::MAX, &AtomicBool::new(false)).unwrap();
        let Some(ResultView::Grid { summary, .. }) = s.result() else {
            panic!("complete grid");
        };
        (summary, fx::validate_wire(summary).unwrap())
    };
    scratch.fill(Event {
        global_row: u32::MAX,
        ..Event::default()
    });
    c.probabilities.fill([f32::NAN; 3]);
    c.states.fill(65535);
    let parsed = libapta::result::parse(&bytes, libapta::result::Limits::default()).unwrap();
    let segment = parsed.global_grid.unwrap().segment(0).unwrap();
    assert_eq!(
        segment.anchor_position.whole_frame,
        (summary.anchor_q32 >> 32) as u64
    );
    assert_eq!(
        segment.anchor_position.fraction_q32,
        summary.anchor_q32 as u32
    );
    assert_eq!(segment.beat_count, summary.beat_count);
    assert_eq!(
        parsed.meter.unwrap().segment(0).unwrap().downbeat_ordinal,
        i64::from(summary.downbeat_ordinal)
    );
}
