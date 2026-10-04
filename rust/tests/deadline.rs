// SPDX-License-Identifier: Apache-2.0
use libapta::{
    session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, SparseSession, Workspace, NODE_FRAMES},
    waveform::{NormalizedSample, PcmView},
    *,
};
fn config(total: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: total,
        frames_per_column: 64,
    }
}
fn sequential(soft: u32, times: &[u64], budget: WorkBudget, expected: u32, calls: usize) {
    let mut queue = [NormalizedSample::default(); 1024];
    let mut output = [WaveformColumn::default(); 16];
    let mut session = Session::new(config(1024), &mut queue, &mut output).unwrap();
    session
        .push_pcm(PcmView::S16Interleaved(&[123; 1024]))
        .unwrap();
    let mut index = 0;
    let mut clock = || {
        let value = times[index.min(times.len() - 1)];
        index += 1;
        value
    };
    let progress = session
        .process_with_clock(budget, soft, &mut clock, &CancellationToken::new())
        .unwrap();
    assert_eq!(progress.consumed_input_frames, expected);
    assert_eq!(index, calls + usize::from(soft != 0 && times[0] != 0) * 4);
}
#[test]
fn exact_boundary_and_saturation() {
    sequential(1, &[1000, 2000], WorkBudget::default(), 256, 2);
    sequential(1, &[1000, 1999, 2000], WorkBudget::default(), 512, 3);
    sequential(
        u32::MAX,
        &[u64::MAX - 50, u64::MAX - 1, u64::MAX],
        WorkBudget::default(),
        512,
        3,
    );
}
#[test]
fn zero_initial_or_zero_budget_disables_checks() {
    sequential(1, &[0, 999999], WorkBudget::default(), 1024, 1);
    sequential(0, &[1000], WorkBudget::default(), 1024, 0);
}
#[test]
fn frame_and_step_limits_stop_before_later_deadline() {
    sequential(
        100,
        &[1000, 1001],
        WorkBudget {
            maximum_input_frames: 17,
            maximum_steps: 0,
        },
        17,
        2,
    );
    sequential(
        100,
        &[1000, 1001],
        WorkBudget {
            maximum_input_frames: 0,
            maximum_steps: 1,
        },
        256,
        2,
    );
}
#[test]
fn initial_clock_runs_before_cancellation_and_terminal_checks() {
    let mut queue = [NormalizedSample::default(); 1];
    let mut output = [];
    let mut session = Session::new(config(0), &mut queue, &mut output).unwrap();
    session.finish_input().unwrap();
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    let mut calls = 0;
    let mut clock = || {
        calls += 1;
        1000
    };
    assert_eq!(
        session
            .process_with_clock(
                WorkBudget::default(),
                1,
                &mut clock,
                &CancellationToken::new()
            )
            .unwrap()
            .completed_steps,
        0
    );
    assert_eq!(calls, 1);
    let mut queue = [NormalizedSample::default(); 1];
    let mut output = [];
    let mut session = Session::new(config(0), &mut queue, &mut output).unwrap();
    let token = CancellationToken::new();
    token.cancel();
    let mut calls = 0;
    let mut clock = || {
        calls += 1;
        1000
    };
    assert_eq!(
        session.process_with_clock(WorkBudget::default(), 1, &mut clock, &token),
        Err(Error::Cancelled)
    );
    assert_eq!(calls, 1);
}
#[test]
fn sparse_short_nodes_check_after_each_chunk_and_finish_at_deadline() {
    let mut acc = [SparseAccumulator::default(); 2];
    let mut ranges = [FrameRange::default(); 4];
    let mut nodes = [QueuedBlock::default(); 3];
    let mut pcm = [NormalizedSample::default(); 3 * NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 2];
    let mut cols = [WaveformColumn::default(); 2];
    let mut session = SparseSession::new(
        config(70),
        Workspace {
            accumulators: &mut acc,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut cols,
        },
    )
    .unwrap();
    session
        .push_at(0, PcmView::S16Interleaved(&[1; 10]))
        .unwrap();
    session
        .push_at(10, PcmView::S16Interleaved(&[1; 20]))
        .unwrap();
    session
        .push_at(30, PcmView::S16Interleaved(&[1; 40]))
        .unwrap();
    let mut time = 0;
    let mut clock = || {
        time += 1000;
        time
    };
    let progress = session
        .process_with_clock(
            WorkBudget::default(),
            1,
            &mut clock,
            &CancellationToken::new(),
        )
        .unwrap();
    assert_eq!(progress.consumed_input_frames, 10);
    assert_eq!(progress.completed_steps, 1);
    session.finish_input().unwrap();
    let mut time = 0;
    let mut clock = || {
        time += 1000;
        time
    };
    let progress = session
        .process_with_clock(
            WorkBudget::default(),
            2,
            &mut clock,
            &CancellationToken::new(),
        )
        .unwrap();
    assert_eq!(progress.consumed_input_frames, 60);
    assert_eq!(progress.completed_steps, 2);
    assert_eq!(session.state(), SessionState::Complete);
    assert_eq!(session.snapshot().unwrap().state, FeatureState::Final);
}

// Compare the effective C wrapper chain, including source callbacks and every
// clock read. This deliberately keeps callback counts observable.
#[test]
#[ignore = "requires APTA_C_CLOCK_ORACLE"]
fn effective_c_runtime_clock_trace() {
    use libapta::{
        owned_result::Storage,
        publication::{PublishedSparseSession, ResultPool},
        pull::{PullBlock, PullRead, PullSource},
        scheduler::RequestSlot,
        sparse_pull::ScheduledPullSession,
    };
    use std::cell::Cell;
    struct Source<'a> {
        scenario: u32,
        events: &'a Cell<u64>,
        reads: &'a Cell<u32>,
        cancel: &'a CancellationToken,
        release: &'a mut dyn FnMut(),
        samples: [i16; 1024],
    }
    impl PullSource for Source<'_> {
        fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
            self.reads.set(self.reads.get() + 1);
            self.events.set(self.events.get() * 10 + 1);
            if self.scenario == 9 {
                return Err(Error::Source);
            }
            if self.scenario == 10 {
                return Ok(PullRead::WouldBlock);
            }
            if self.scenario == 11 {
                self.cancel.cancel();
            }
            Ok(PullRead::Data(PullBlock::new(
                first,
                PcmView::S16Interleaved(&self.samples[..maximum as usize]),
                self.release,
            )))
        }
    }
    let oracle = std::env::var("APTA_C_CLOCK_ORACLE").expect("clock oracle path");
    for scenario in 0..12 {
        let events = Cell::new(0u64);
        let reads = Cell::new(0u32);
        let releases = Cell::new(0u32);
        let calls = Cell::new(0u32);
        let cancel = CancellationToken::new();
        let mut clock = || {
            calls.set(calls.get() + 1);
            events.set(events.get() * 10 + 3);
            match scenario {
                1 => 0,
                3 => {
                    if calls.get() == 1 {
                        u64::MAX - 500
                    } else {
                        u64::MAX
                    }
                }
                _ => u64::from(calls.get()) * 1000,
            }
        };
        let mut ca = [WaveformColumn::default(); 16];
        let mut cb = ca;
        let mut sa = [WaveformSpan::default(); 16];
        let mut sb = sa;
        let pool = ResultPool::new(
            SourceInfo {
                sample_rate: 48000,
                channel_count: 1,
                channel_layout: 1,
                total_frames: Some(1024),
                fingerprint_kind: 0,
                fingerprint: [0; 32],
            },
            [
                Storage {
                    overview_columns: &mut ca,
                    overview_spans: &mut sa,
                    ..Default::default()
                },
                Storage {
                    overview_columns: &mut cb,
                    overview_spans: &mut sb,
                    ..Default::default()
                },
            ],
            NativeLimits::default(),
        )
        .unwrap();
        let mut acc = [SparseAccumulator::default(); 16];
        let mut ranges = [FrameRange::default(); 16];
        let mut nodes = [QueuedBlock::default(); 4];
        let mut pcm = [NormalizedSample::default(); 4 * NODE_FRAMES];
        let mut spans = [WaveformSpan::default(); 16];
        let mut cols = [WaveformColumn::default(); 16];
        let mut requests = [RequestSlot::default(); 16];
        let mut session = PublishedSparseSession::new_scheduled(
            config(1024),
            Workspace {
                accumulators: &mut acc,
                ranges: &mut ranges,
                nodes: &mut nodes,
                pcm: &mut pcm,
                snapshot_spans: &mut spans,
                snapshot_columns: &mut cols,
            },
            &pool,
            &mut requests,
        )
        .unwrap();
        let mut budget = WorkBudget::default();
        let soft = match scenario {
            2 => 0,
            4 => {
                budget.maximum_input_frames = 17;
                100
            }
            5 => {
                budget.maximum_steps = 1;
                100
            }
            _ => 1,
        };
        if scenario == 7 {
            cancel.cancel();
        }
        let is_pull = scenario == 6 || scenario >= 9;
        if !is_pull {
            session
                .push_at(0, PcmView::S16Interleaved(&[0; 1024]))
                .unwrap();
        }
        if scenario == 8 {
            session.finish_input().unwrap();
            session.process(WorkBudget::default(), &cancel).unwrap();
        }
        let (status, frames, steps, state) = if is_pull {
            let mut release = || {
                releases.set(releases.get() + 1);
                events.set(events.get() * 10 + 2);
            };
            let source = Source {
                scenario,
                events: &events,
                reads: &reads,
                cancel: &cancel,
                release: &mut release,
                samples: [0; 1024],
            };
            let mut pull = ScheduledPullSession::new(session, source).unwrap();
            let result = pull.process_with_clock(budget, soft, &mut clock, &cancel);
            let state = pull.session().session().state();
            let (status, frames, steps) = match result {
                Err(Error::Source) => (-5, 0, 0),
                Err(Error::Cancelled) => (-7, 0, 0),
                Ok(p) => (
                    if p.would_block { 2 } else { 1 },
                    p.processing.consumed_input_frames,
                    p.processing.completed_steps,
                ),
                other => panic!("scenario {scenario}: {other:?}"),
            };
            (status, frames, steps, state)
        } else {
            let result = session.process_with_clock(budget, soft, &mut clock, &cancel);
            let state = session.session().state();
            let (status, frames, steps) = match result {
                Err(Error::Cancelled) => (-7, 0, 0),
                Ok(p) => (
                    if state == SessionState::Complete {
                        3
                    } else if session.session().queued_frames() != 0 {
                        1
                    } else {
                        0
                    },
                    p.consumed_input_frames,
                    p.completed_steps,
                ),
                other => panic!("scenario {scenario}: {other:?}"),
            };
            (status, frames, steps, state)
        };
        let state = match state {
            SessionState::Created => 0,
            SessionState::Running => 1,
            SessionState::Draining => 2,
            SessionState::Complete => 3,
            SessionState::Cancelled => 4,
            SessionState::Failed => 5,
        };
        let native = format!(
            "{status} {frames} {steps} {} {} {state} {} {}\n",
            calls.get(),
            events.get(),
            reads.get(),
            releases.get()
        );
        let reference = std::process::Command::new(&oracle)
            .arg(scenario.to_string())
            .output()
            .unwrap();
        assert!(reference.status.success());
        assert_eq!(
            native,
            String::from_utf8(reference.stdout).unwrap(),
            "scenario {scenario}"
        );
    }
}
#[test]
#[ignore = "requires APTA_C_CLOCK_ORACLE"]
fn sequential_pull_clock_read_release_order_matches_c() {
    use libapta::pull::*;
    use std::cell::Cell;
    struct Source<'a> {
        id: u32,
        events: &'a Cell<u64>,
        cancel: &'a CancellationToken,
        release: &'a mut dyn FnMut(),
        samples: [i16; 256],
    }
    impl PullSource for Source<'_> {
        fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
            self.events.set(self.events.get() * 10 + 1);
            if self.id == 9 {
                return Err(Error::Source);
            }
            if self.id == 10 {
                return Ok(PullRead::WouldBlock);
            }
            if self.id == 11 {
                self.cancel.cancel();
            }
            Ok(PullRead::Data(PullBlock::new(
                first,
                PcmView::S16Interleaved(&self.samples[..maximum as usize]),
                self.release,
            )))
        }
    }
    let oracle = std::env::var_os("APTA_C_CLOCK_ORACLE").unwrap();
    for id in [6, 9, 10, 11] {
        let events = Cell::new(0u64);
        let calls = Cell::new(0u32);
        let cancel = CancellationToken::new();
        let mut release = || events.set(events.get() * 10 + 2);
        let mut clock = || {
            calls.set(calls.get() + 1);
            events.set(events.get() * 10 + 3);
            u64::from(calls.get()) * 1000
        };
        let mut queue = [NormalizedSample::default(); 256];
        let mut columns = [WaveformColumn::default(); 16];
        let mut pull = PullSession::new(
            config(1024),
            &mut queue,
            &mut columns,
            Source {
                id,
                events: &events,
                cancel: &cancel,
                release: &mut release,
                samples: [0; 256],
            },
        )
        .unwrap();
        let result = pull.process_with_clock(WorkBudget::default(), 1, &mut clock, &cancel);
        let (status, frames, steps) = match result {
            Ok(p) => (
                if p.would_block { 2 } else { 1 },
                p.processing.consumed_input_frames,
                p.processing.completed_steps,
            ),
            Err(Error::Source) => (-5, 0, 0),
            Err(Error::Cancelled) => (-7, 0, 0),
            other => panic!("{other:?}"),
        };
        let c = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(c.status.success());
        let fields: Vec<i128> = String::from_utf8(c.stdout)
            .unwrap()
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        let state = match pull.state() {
            PullState::Created => 0,
            PullState::Running => 1,
            PullState::Draining => 2,
            PullState::Complete => 3,
            PullState::Cancelled => 4,
            PullState::Failed => 5,
        };
        let releases = usize::from(id == 6 || id == 11);
        assert_eq!(
            fields,
            [
                i128::from(status),
                i128::from(frames),
                i128::from(steps),
                i128::from(calls.get()),
                i128::from(events.get()),
                state,
                1,
                releases as i128
            ],
            "{id}"
        );
    }
}
