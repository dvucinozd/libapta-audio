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
    assert_eq!(index, calls);
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
