// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::Storage,
    publication::{plan_sparse, PublishedSparseSession, ResultPool},
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
    waveform::{NormalizedSample, PcmView},
    *,
};
fn config() -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: 256,
        frames_per_column: 64,
    }
}
fn source() -> SourceInfo {
    SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(256),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    }
}
#[derive(Default)]
struct Results {
    a: [WaveformColumn; 4],
    b: [WaveformColumn; 4],
    sa: [WaveformSpan; 2],
    sb: [WaveformSpan; 2],
}
impl Results {
    fn pool(&mut self) -> ResultPool<'_> {
        ResultPool::new(
            source(),
            [
                Storage {
                    overview_columns: &mut self.a,
                    overview_spans: &mut self.sa,
                    ..Default::default()
                },
                Storage {
                    overview_columns: &mut self.b,
                    overview_spans: &mut self.sb,
                    ..Default::default()
                },
            ],
            NativeLimits::default(),
        )
        .unwrap()
    }
}
#[test]
fn holes_and_retained_snapshots_survive_completion_and_session_drop() {
    let mut buffers = Results::default();
    let pool = buffers.pool();
    let initial = pool.acquire().unwrap();
    let mut accumulators = [SparseAccumulator::default(); 4];
    let mut ranges = [FrameRange {
        first_frame: 0,
        end_frame: 0,
    }; 4];
    let mut nodes = [QueuedBlock::default(); 1];
    let mut pcm = [NormalizedSample::default(); NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 4];
    let mut columns = [WaveformColumn::default(); 4];
    let retained = {
        let mut session = PublishedSparseSession::new(
            config(),
            Workspace {
                accumulators: &mut accumulators,
                ranges: &mut ranges,
                nodes: &mut nodes,
                pcm: &mut pcm,
                snapshot_spans: &mut spans,
                snapshot_columns: &mut columns,
            },
            &pool,
        )
        .unwrap();
        let token = CancellationToken::new();
        assert_eq!(
            session.push_at(192, PcmView::S16Interleaved(&[12345; 64])),
            Ok(64)
        );
        assert_eq!(
            session.process(WorkBudget::default(), &token),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(session.session().processed_frames(), 64);
        assert!(session.publication_pending());
        drop(initial);
        assert_eq!(
            session
                .process(WorkBudget::default(), &token)
                .unwrap()
                .consumed_input_frames,
            0
        );
        let sparse = pool.acquire().unwrap();
        let cloned = sparse.clone();
        assert_eq!(sparse.overview().unwrap().spans[0].first_column_index, 3);
        session.finish_input().unwrap();
        assert_eq!(
            session.process(WorkBudget::default(), &token),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(session.session().state(), SessionState::Draining);
        drop(sparse);
        assert_eq!(
            session.process(WorkBudget::default(), &token),
            Err(Error::ResultSlotsExhausted)
        );
        drop(cloned);
        session.process(WorkBudget::default(), &token).unwrap();
        session.finish_input().unwrap();
        assert_eq!(session.session().state(), SessionState::Complete);
        let retained = pool.acquire().unwrap();
        assert_eq!(retained.info().session_state, ResultSessionState::Completed);
        assert_eq!(retained.overview().unwrap().state, FeatureState::Partial);
        retained
    };
    assert_eq!(retained.overview().unwrap().spans[0].first_frame, 192);
    assert_eq!(retained.overview().unwrap().columns[0].minimum, 12345);
    assert_eq!(retained.changed_features(), 0);
}
#[test]
fn sparse_plan_checks_span_and_retained_byte_limits() {
    let p = plan_sparse(config(), NativeLimits::default()).unwrap();
    assert_eq!(p.columns_per_slot, 4);
    assert_eq!(p.spans_per_slot, 2);
    assert!(plan_sparse(
        config(),
        NativeLimits {
            maximum_storage_bytes: p.retained_bytes_per_slot,
            ..NativeLimits::default()
        }
    )
    .is_ok());
    assert_eq!(
        plan_sparse(
            config(),
            NativeLimits {
                maximum_storage_bytes: p.retained_bytes_per_slot - 1,
                ..NativeLimits::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        plan_sparse(
            config(),
            NativeLimits {
                maximum_overview_spans: 1,
                ..NativeLimits::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
}
