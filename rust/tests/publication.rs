// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::Storage,
    publication::{PublishedSession, ResultPool},
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    waveform::{NormalizedSample, PcmView},
    *,
};
fn source(total: u64) -> SourceInfo {
    SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(total),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    }
}
fn config(total: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: total,
        frames_per_column: 64,
    }
}
fn storage<'a>(spans: &'a mut [WaveformSpan], columns: &'a mut [WaveformColumn]) -> Storage<'a> {
    Storage {
        overview_spans: spans,
        overview_columns: columns,
        ..Default::default()
    }
}
fn step() -> WorkBudget {
    WorkBudget {
        maximum_input_frames: 64,
        maximum_steps: 1,
    }
}
#[test]
fn retained_generations_cloned_leases_and_retry_are_immutable() {
    let mut s0 = [WaveformSpan::default(); 1];
    let mut s1 = s0;
    let mut c0 = [WaveformColumn::default(); 3];
    let mut c1 = c0;
    let pool = ResultPool::new(
        source(192),
        [storage(&mut s0, &mut c0), storage(&mut s1, &mut c1)],
        NativeLimits::default(),
    )
    .unwrap();
    let initial = pool.acquire().unwrap();
    let mut queue = [NormalizedSample::default(); 192];
    let mut columns = [WaveformColumn::default(); 3];
    let (complete, first_column) = {
        let mut session =
            PublishedSession::new(config(192), &mut queue, &mut columns, &pool).unwrap();
        let cancel = CancellationToken::new();
        session
            .push_pcm(PcmView::F32Interleaved(&[0.25; 64]))
            .unwrap();
        let active = pool.acquire().unwrap();
        assert_eq!(active.info().generation, 2);
        assert!(active.overview().is_none());
        assert_eq!(
            session.process(step(), &cancel),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(session.session().processed_frames(), 64);
        assert!(session.publication_pending());
        assert_eq!(pool.generation(), 2);
        assert_eq!(
            session.process(step(), &cancel),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(session.session().processed_frames(), 64);
        assert!(initial.overview().is_none());
        drop(initial);
        assert_eq!(
            session
                .process(step(), &cancel)
                .unwrap()
                .consumed_input_frames,
            0
        );
        assert_eq!(pool.generation(), 3);
        let first = pool.acquire().unwrap();
        let cloned = first.clone();
        let first_column = first.overview().unwrap().columns[0];
        drop(active);
        session
            .push_pcm(PcmView::F32Interleaved(&[0.25; 64]))
            .unwrap();
        session.process(step(), &cancel).unwrap();
        let second = pool.acquire().unwrap();
        assert_eq!(second.info().generation, 4);
        assert_eq!(second.overview().unwrap().columns.len(), 2);
        session
            .push_pcm(PcmView::F32Interleaved(&[-0.5; 64]))
            .unwrap();
        assert_eq!(session.finish_input(), Err(Error::ResultSlotsExhausted));
        assert_eq!(session.session().state(), SessionState::Running);
        assert_eq!(
            session.process(step(), &cancel),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(session.session().processed_frames(), 192);
        drop(first);
        assert_eq!(
            session.process(step(), &cancel),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(cloned.overview().unwrap().columns, &[first_column]);
        drop(cloned);
        session.process(step(), &cancel).unwrap();
        assert_eq!(pool.generation(), 5);
        assert_eq!(second.overview().unwrap().columns.len(), 2);
        drop(second);
        session.finish_input().unwrap();
        assert_eq!(
            pool.acquire().unwrap().info().session_state,
            ResultSessionState::Draining
        );
        session.process(step(), &cancel).unwrap();
        let complete = pool.acquire().unwrap();
        assert_eq!(complete.info().generation, 7);
        assert_eq!(complete.info().session_state, ResultSessionState::Completed);
        assert_eq!(complete.changed_features(), 0);
        assert_eq!(complete.overview().unwrap().state, FeatureState::Final);
        assert_eq!(complete.overview().unwrap().columns.len(), 3);
        (complete, first_column)
    };
    assert_eq!(complete.overview().unwrap().columns[0], first_column);
}
#[test]
fn cancellation_rolls_back_when_pending_slot_is_retained() {
    let mut s0 = [WaveformSpan::default(); 1];
    let mut s1 = s0;
    let mut c0 = [WaveformColumn::default(); 3];
    let mut c1 = c0;
    let pool = ResultPool::new(
        source(192),
        [storage(&mut s0, &mut c0), storage(&mut s1, &mut c1)],
        NativeLimits::default(),
    )
    .unwrap();
    let initial = pool.acquire().unwrap();
    let mut queue = [NormalizedSample::default(); 192];
    let mut columns = [WaveformColumn::default(); 3];
    let mut session = PublishedSession::new(config(192), &mut queue, &mut columns, &pool).unwrap();
    let token = CancellationToken::new();
    session
        .push_pcm(PcmView::F32Interleaved(&[0.25; 192]))
        .unwrap();
    let active = pool.acquire().unwrap();
    assert_eq!(
        session.process(step(), &token),
        Err(Error::ResultSlotsExhausted)
    );
    token.cancel();
    assert_eq!(
        session.process(step(), &token),
        Err(Error::ResultSlotsExhausted)
    );
    assert_eq!(session.session().state(), SessionState::Running);
    assert_eq!(session.session().processed_frames(), 64);
    assert!(session.publication_pending());
    drop(initial);
    assert_eq!(session.process(step(), &token), Err(Error::Cancelled));
    assert!(!session.publication_pending());
    assert_eq!(pool.generation(), 3);
    let cancelled = pool.acquire().unwrap();
    assert_eq!(
        cancelled.info().session_state,
        ResultSessionState::Cancelled
    );
    // The C bounded pool marks complete columns pending after exhaustion;
    // cancellation before another process publishes an empty generation.
    assert!(cancelled.overview().is_none());
    assert_eq!(cancelled.changed_features(), 0);
    assert!(active.overview().is_none());
    assert_eq!(
        active.info().session_state,
        ResultSessionState::AcceptingInput
    );
    assert_eq!(session.process(step(), &token), Err(Error::Cancelled));
    assert_eq!(pool.generation(), 3);
    assert_eq!(session.session().processed_frames(), 64);
}
#[test]
fn empty_completion_and_cancellation_publish_without_waveform() {
    for cancelled in [false, true] {
        let pool = ResultPool::new(
            source(0),
            [Storage::default(), Storage::default()],
            NativeLimits::default(),
        )
        .unwrap();
        let mut queue = [NormalizedSample::default(); 1];
        let mut columns = [];
        let retained = {
            let mut session =
                PublishedSession::new(config(0), &mut queue, &mut columns, &pool).unwrap();
            let token = CancellationToken::new();
            if cancelled {
                token.cancel();
                assert_eq!(session.process(step(), &token), Err(Error::Cancelled));
            } else {
                session.finish_input().unwrap();
                assert_eq!(pool.generation(), 3);
                assert_eq!(
                    session
                        .process(step(), &token)
                        .unwrap()
                        .consumed_input_frames,
                    0
                );
            }
            let retained = pool.acquire().unwrap();
            assert_eq!(retained.info().generation, if cancelled { 2 } else { 4 });
            assert_eq!(retained.available_features(), 0);
            assert_eq!(retained.changed_features(), 0);
            assert!(retained.overview().is_none());
            assert_eq!(
                retained.info().session_state,
                if cancelled {
                    ResultSessionState::Cancelled
                } else {
                    ResultSessionState::Completed
                }
            );
            retained
        };
        assert_eq!(retained.source().total_frames, Some(0));
    }
}
#[test]
fn capacities_source_identity_and_one_session_attachment_are_checked() {
    let mut s0 = [WaveformSpan::default(); 1];
    let mut s1 = s0;
    let mut c0 = [WaveformColumn::default(); 2];
    let mut c1 = [WaveformColumn::default(); 1];
    let pool = ResultPool::new(
        source(128),
        [storage(&mut s0, &mut c0), storage(&mut s1, &mut c1)],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 128];
    let mut columns = [WaveformColumn::default(); 2];
    assert!(matches!(
        PublishedSession::new(config(128), &mut queue, &mut columns, &pool),
        Err(Error::BufferTooSmall)
    ));
    assert!(matches!(
        PublishedSession::new(config(64), &mut queue, &mut columns, &pool),
        Err(Error::InvalidArgument)
    ));
    assert_eq!(pool.generation(), 1);
    let pool = ResultPool::new(
        source(0),
        [Storage::default(), Storage::default()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut q0 = [NormalizedSample::default(); 1];
    let mut q1 = q0;
    let mut c0 = [];
    let mut c1 = [];
    let _first = PublishedSession::new(config(0), &mut q0, &mut c0, &pool).unwrap();
    assert!(matches!(
        PublishedSession::new(config(0), &mut q1, &mut c1, &pool),
        Err(Error::InvalidState)
    ));
}

#[test]
fn workspace_plan_covers_full_graph_and_rejects_impossible_limits() {
    use libapta::publication::plan;
    let p = plan(config(129), NativeLimits::default()).unwrap();
    assert_eq!(
        (p.working_columns, p.columns_per_slot, p.spans_per_slot),
        (3, 3, 1)
    );
    assert_eq!(
        p.retained_bytes_per_slot,
        core::mem::size_of::<libapta::owned_result::OwnedResult<'_>>()
            + core::mem::size_of::<WaveformSpan>()
            + 3 * core::mem::size_of::<WaveformColumn>()
    );
    assert_eq!(
        plan(
            config(129),
            NativeLimits {
                maximum_storage_bytes: p.retained_bytes_per_slot - 1,
                ..Default::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        plan(
            config(129),
            NativeLimits {
                maximum_waveform_columns: 2,
                ..Default::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        plan(
            config(129),
            NativeLimits {
                maximum_overview_spans: 0,
                ..Default::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(
        plan(config(u64::MAX), NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        plan(config(u64::MAX - 1), NativeLimits::default()),
        Err(Error::LimitExceeded)
    );
    let empty = plan(config(0), NativeLimits::default()).unwrap();
    assert_eq!(
        (
            empty.working_columns,
            empty.columns_per_slot,
            empty.spans_per_slot
        ),
        (0, 0, 0)
    );
}

#[test]
fn completion_exhaustion_preserves_draining_and_stable_retained_result() {
    let mut s0 = [WaveformSpan::default(); 1];
    let mut s1 = s0;
    let mut c0 = [WaveformColumn::default(); 1];
    let mut c1 = c0;
    let pool = ResultPool::new(
        source(64),
        [storage(&mut s0, &mut c0), storage(&mut s1, &mut c1)],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 64];
    let mut columns = [WaveformColumn::default(); 1];
    let mut session = PublishedSession::new(config(64), &mut queue, &mut columns, &pool).unwrap();
    let token = CancellationToken::new();
    session
        .push_pcm(PcmView::F32Interleaved(&[0.25; 64]))
        .unwrap();
    session.process(step(), &token).unwrap();
    let partial = pool.acquire().unwrap();
    assert_eq!(partial.overview().unwrap().state, FeatureState::Partial);
    session.finish_input().unwrap();
    let draining = pool.acquire().unwrap();
    assert_eq!(draining.info().generation, 4);
    assert_eq!(draining.overview().unwrap().state, FeatureState::Stable);
    assert_eq!(
        session.process(step(), &token),
        Err(Error::ResultSlotsExhausted)
    );
    assert_eq!(session.session().state(), SessionState::Draining);
    assert_eq!(pool.generation(), 4);
    assert_eq!(partial.overview().unwrap().state, FeatureState::Partial);
    drop(partial);
    session.process(step(), &token).unwrap();
    let complete = pool.acquire().unwrap();
    assert_eq!(complete.info().generation, 5);
    assert_eq!(complete.overview().unwrap().state, FeatureState::Final);
    assert_eq!(complete.changed_features(), 0);
    assert_eq!(draining.overview().unwrap().state, FeatureState::Stable);
}

#[test]
fn full_retained_byte_limit_is_checked_before_session_attachment() {
    let p = libapta::publication::plan(config(129), NativeLimits::default()).unwrap();
    let limits = NativeLimits {
        maximum_storage_bytes: p.retained_bytes_per_slot - 1,
        ..Default::default()
    };
    let mut s0 = [WaveformSpan::default(); 1];
    let mut s1 = s0;
    let mut c0 = [WaveformColumn::default(); 3];
    let mut c1 = c0;
    let pool = ResultPool::new(
        source(129),
        [storage(&mut s0, &mut c0), storage(&mut s1, &mut c1)],
        limits,
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 129];
    let mut columns = [WaveformColumn::default(); 3];
    assert!(matches!(
        PublishedSession::new(config(129), &mut queue, &mut columns, &pool),
        Err(Error::LimitExceeded)
    ));
    assert_eq!(pool.generation(), 1);
    assert!(pool.acquire().unwrap().overview().is_none());
    let mut bad = config(64);
    bad.sample_rate = 768001;
    assert_eq!(
        libapta::publication::plan(bad, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
}
