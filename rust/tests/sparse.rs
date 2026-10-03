// SPDX-License-Identifier: Apache-2.0
use libapta::session::{CancellationToken, SessionConfig, SessionState, WorkBudget};
use libapta::sparse::{QueuedBlock, SparseAccumulator, SparseSession, Workspace, NODE_FRAMES};
use libapta::waveform::{NormalizedSample, PcmView};
use libapta::{Error, FeatureState, FrameRange, WaveformColumn, WaveformSpan};

fn run(total: u64, ranges: usize, nodes: usize, f: impl FnOnce(&mut SparseSession<'_>)) {
    let n = total.div_ceil(64) as usize;
    let mut accumulators = vec![SparseAccumulator::default(); n];
    let mut ranges = vec![
        FrameRange {
            first_frame: 0,
            end_frame: 0
        };
        ranges
    ];
    let mut nodes = vec![QueuedBlock::default(); nodes];
    let mut pcm = vec![NormalizedSample::default(); nodes.len() * NODE_FRAMES];
    let mut spans = vec![WaveformSpan::default(); n];
    let mut columns = vec![WaveformColumn::default(); n];
    let mut session = SparseSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: total,
            frames_per_column: 64,
        },
        Workspace {
            accumulators: &mut accumulators,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut columns,
        },
    )
    .unwrap();
    f(&mut session);
}
fn process(s: &mut SparseSession<'_>) {
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
}

#[test]
fn sparse_gaps_prefix_and_range_merges() {
    run(256, 4, 4, |s| {
        assert_eq!(s.push_at(128, PcmView::S16Interleaved(&[100; 64])), Ok(64));
        assert_eq!(s.push_at(64, PcmView::S16Interleaved(&[-100; 128])), Ok(64));
        assert_eq!(
            s.push_at(100, PcmView::S16Interleaved(&[0; 32])),
            Err(Error::Conflict)
        );
        process(s);
        let view = s.snapshot().unwrap();
        assert_eq!(view.spans.len(), 1);
        assert_eq!(view.spans[0].first_frame, 64);
        assert_eq!(view.spans[0].end_frame, 192);
        assert_eq!(view.columns.len(), 2);
        assert_eq!(view.state, FeatureState::Partial);
        assert_eq!(s.push_at(0, PcmView::S16Interleaved(&[0; 64])), Ok(64));
        assert_eq!(
            s.accepted_ranges(),
            &[FrameRange {
                first_frame: 0,
                end_frame: 192
            }]
        );
        s.finish_input().unwrap();
        process(s);
        assert_eq!(s.state(), SessionState::Complete);
        assert_eq!(s.snapshot().unwrap().state, FeatureState::Partial);
    });
}
#[test]
fn partial_column_fragments_tail_and_complete() {
    run(70, 4, 3, |s| {
        s.push_at(32, PcmView::S16Interleaved(&[100; 38])).unwrap();
        process(s);
        assert!(s.snapshot().is_none());
        s.push_at(0, PcmView::S16Interleaved(&[100; 32])).unwrap();
        process(s);
        assert_eq!(s.snapshot().unwrap().columns.len(), 1);
        s.finish_input().unwrap();
        process(s);
        let view = s.snapshot().unwrap();
        assert_eq!(view.columns.len(), 2);
        assert_eq!(view.spans[0].end_frame, 70);
        assert_eq!(view.state, FeatureState::Final);
        assert_eq!(view.columns[0], view.columns[1]);
        assert_eq!(s.processed_frames(), 70);
    });
}
#[test]
fn exhaustion_and_invalid_prefix_leave_state_unchanged() {
    run(256, 2, 1, |s| {
        assert_eq!(
            s.push_at(0, PcmView::F32Interleaved(&[f32::NAN; 64])),
            Err(Error::InvalidArgument)
        );
        assert!(s.accepted_ranges().is_empty());
        assert_eq!(s.state(), SessionState::Created);
        s.push_at(64, PcmView::S16Interleaved(&[0; 64])).unwrap();
        assert_eq!(s.push_at(192, PcmView::S16Interleaved(&[0; 64])), Ok(0));
        process(s);
        s.push_at(192, PcmView::S16Interleaved(&[0; 64])).unwrap();
        process(s);
        assert_eq!(s.push_at(128, PcmView::S16Interleaved(&[0; 64])), Ok(0));
        assert_eq!(s.accepted_ranges().len(), 2);
        assert_eq!(s.processed_frames(), 128);
        assert_eq!(
            s.push_at(240, PcmView::S16Interleaved(&[0; 32])),
            Err(Error::Conflict)
        );
    });
}
#[test]
fn fifo_reused_slots_budgets_and_cancellation() {
    run(640, 4, 2, |s| {
        s.push_at(0, PcmView::S16Interleaved(&[1; 64])).unwrap();
        s.push_at(64, PcmView::S16Interleaved(&[2; 320])).unwrap();
        let p = s
            .process(
                WorkBudget {
                    maximum_steps: 1,
                    maximum_input_frames: 0,
                },
                &CancellationToken::new(),
            )
            .unwrap();
        assert_eq!(p.completed_steps, 1);
        assert_eq!(p.consumed_input_frames, 64);
        s.push_at(384, PcmView::S16Interleaved(&[3; 256])).unwrap();
        s.process(
            WorkBudget {
                maximum_steps: 1,
                maximum_input_frames: 100,
            },
            &CancellationToken::new(),
        )
        .unwrap();
        assert_eq!(s.snapshot().unwrap().spans[0].end_frame, 128);
        let token = CancellationToken::new();
        token.cancel();
        assert_eq!(
            s.process(WorkBudget::default(), &token),
            Err(Error::Cancelled)
        );
        assert_eq!(s.processed_frames(), 164);
        assert_eq!(s.state(), SessionState::Cancelled);
        assert_eq!(
            s.push_at(0, PcmView::S16Interleaved(&[0; 1])),
            Err(Error::Cancelled)
        );
    });
}
#[test]
fn empty_eof_and_full_node_prefix() {
    run(0, 0, 0, |s| {
        s.finish_input().unwrap();
        process(s);
        assert_eq!(s.state(), SessionState::Complete);
        assert_eq!(s.finish_input(), Ok(()));
        assert!(s.snapshot().is_none());
        assert!(s.coverage_complete());
    });
    run(8192, 3, 2, |s| {
        assert_eq!(s.push_at(0, PcmView::S16Interleaved(&[0; 8192])), Ok(4096));
        assert_eq!(s.queued_frames(), 4096);
    });
}

#[test]
fn sorted_spans_and_fragment_order_preserve_exact_columns() {
    run(256, 5, 4, |s| {
        let samples: Vec<i16> = (0..64).map(|i| (i * 997 - 31000) as i16).collect();
        s.push_at(224, PcmView::S16Interleaved(&samples[32..]))
            .unwrap();
        s.push_at(0, PcmView::S16Interleaved(&samples)).unwrap();
        s.push_at(192, PcmView::S16Interleaved(&samples[..32]))
            .unwrap();
        process(s);
        let view = s.snapshot().unwrap();
        assert_eq!(view.spans.len(), 2);
        assert_eq!(view.spans[0].first_frame, 0);
        assert_eq!(view.spans[1].first_frame, 192);
        assert_eq!(view.spans[1].data_column_offset, 1);
        assert_eq!(view.columns[0], view.columns[1]);
    });
}
#[test]
fn unaccepted_nonfinite_suffix_is_not_scanned() {
    run(128, 4, 2, |s| {
        s.push_at(64, PcmView::F32Interleaved(&[0.; 64])).unwrap();
        let mut samples = [0.; 128];
        samples[64] = f32::NAN;
        assert_eq!(s.push_at(0, PcmView::F32Interleaved(&samples)), Ok(64));
        process(s);
        assert_eq!(s.snapshot().unwrap().columns.len(), 2);
    });
}
