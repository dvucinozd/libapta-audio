// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::Storage,
    publication::{PublishedSession, ResultPool},
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    waveform::{NormalizedSample, PcmView},
    *,
};
use libapta_runtime::ConcurrentResults;
use std::sync::{Arc, Barrier};
struct Buffers {
    spans: [WaveformSpan; 1],
    columns: [WaveformColumn; 16],
}
impl Buffers {
    fn new() -> Self {
        Self {
            spans: [WaveformSpan::default(); 1],
            columns: [WaveformColumn::default(); 16],
        }
    }
    fn storage(&mut self) -> Storage<'_> {
        Storage {
            overview_spans: &mut self.spans,
            overview_columns: &mut self.columns,
            ..Storage::default()
        }
    }
}
fn source(total: Option<u64>) -> SourceInfo {
    SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: total,
        channel_layout: 1,
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

#[test]
fn concurrent_acquire_release_does_not_pin_native_slots_and_survives_destruction() {
    // Exactly two threads; all typed result storage is independent of native slots.
    let mut snapshots: Vec<_> = (0..18).map(|_| Buffers::new()).collect();
    let (initial, remaining) = snapshots.split_first_mut().unwrap();
    let mut a = Buffers::new();
    let mut b = Buffers::new();
    let pool = ResultPool::new(
        source(Some(1024)),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 64];
    let mut columns = [WaveformColumn::default(); 16];
    let mut s = PublishedSession::new(config(1024), &mut queue, &mut columns, &pool).unwrap();
    let channel = ConcurrentResults::new(
        s.acquire_result()
            .unwrap()
            .copy_to(initial.storage())
            .unwrap(),
    );
    let old = channel.acquire().unwrap();
    let barrier = Arc::new(Barrier::new(2));
    let rendezvous = barrier.clone();
    let reader = channel.clone();
    let retained = std::thread::scope(|scope| {
        let worker = scope.spawn(move || {
            let mut retained = vec![];
            for index in 0..16 {
                // Exercise concurrent short acquisitions while the writer copies,
                // publishes and updates the native pool, then retain each snapshot.
                for _ in 0..1000 {
                    let acquired = reader.acquire().unwrap();
                    assert!(acquired.info().generation >= 1);
                }
                rendezvous.wait();
                let r = reader.acquire().unwrap();
                assert_eq!(r.info().generation, index + 3);
                assert_eq!(r.overview().unwrap().columns.len(), index as usize + 1);
                retained.push(r);
                rendezvous.wait();
            }
            retained
        });
        let token = CancellationToken::new();
        let mut destinations = remaining.iter_mut();
        for _ in 0..16 {
            s.push_pcm(PcmView::F32Interleaved(&[0.25; 64])).unwrap();
            channel
                .process(
                    &mut s,
                    WorkBudget::default(),
                    &token,
                    destinations.next().unwrap().storage(),
                )
                .unwrap();
            barrier.wait();
            barrier.wait();
        }
        s.finish_input().unwrap();
        channel
            .process(
                &mut s,
                WorkBudget::default(),
                &token,
                destinations.next().unwrap().storage(),
            )
            .unwrap();
        assert_eq!(s.session().state(), SessionState::Complete);
        worker.join().unwrap()
    });
    #[allow(clippy::drop_non_drop)]
    drop(s);
    #[allow(clippy::drop_non_drop)]
    drop(pool);
    assert_eq!(old.info().generation, 1);
    assert!(old.overview().is_none());
    for (index, r) in retained.iter().enumerate() {
        assert_eq!(r.info().generation, index as u64 + 3);
        assert_eq!(r.overview().unwrap().columns.len(), index + 1);
        assert!(r
            .overview()
            .unwrap()
            .columns
            .iter()
            .all(|c| c.minimum == 8192 && c.maximum == 8192));
    }
    assert_eq!(
        channel.acquire().unwrap().info().session_state,
        ResultSessionState::Completed
    );
    let latest = channel.acquire().unwrap();
    drop(channel);
    assert_eq!(latest.overview().unwrap().state, FeatureState::Final);
}

#[test]
fn short_copy_retry_unknown_eof_and_conflicts_leave_current_generation_intact() {
    let mut initial = Buffers::new();
    let mut running = Buffers::new();
    let mut final_result = Buffers::new();
    let mut a = Buffers::new();
    let mut b = Buffers::new();
    let pool = ResultPool::new(
        source(None),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 64];
    let mut columns = [WaveformColumn::default(); 16];
    let mut s = PublishedSession::new(
        config(session::TOTAL_FRAMES_UNKNOWN),
        &mut queue,
        &mut columns,
        &pool,
    )
    .unwrap();
    let channel = ConcurrentResults::new(
        s.acquire_result()
            .unwrap()
            .copy_to(initial.storage())
            .unwrap(),
    );
    // Equal generation identities from another source must not skip validation.
    let mut other_a = Buffers::new();
    let mut other_b = Buffers::new();
    let mut other_source = source(None);
    other_source.fingerprint_kind = 1;
    other_source.fingerprint = [1; 32];
    let other_pool = ResultPool::new(
        other_source,
        [other_a.storage(), other_b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut other_queue = [NormalizedSample::default(); 64];
    let mut other_columns = [WaveformColumn::default(); 16];
    let other_session = PublishedSession::new(
        config(session::TOTAL_FRAMES_UNKNOWN),
        &mut other_queue,
        &mut other_columns,
        &other_pool,
    )
    .unwrap();
    assert_eq!(
        channel.refresh_session(&other_session, Storage::default()),
        Err(Error::Conflict)
    );
    let old = channel.acquire().unwrap();
    s.push_pcm(PcmView::F32Interleaved(&[0.5; 64])).unwrap();
    assert_eq!(
        channel.process(
            &mut s,
            WorkBudget::default(),
            &CancellationToken::new(),
            Storage::default()
        ),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(s.session().processed_frames(), 64);
    assert_eq!(channel.acquire().unwrap().info().generation, 1);
    assert!(channel.refresh_session(&s, running.storage()).unwrap());
    assert!(!channel.refresh_session(&s, Storage::default()).unwrap());
    let before = channel.acquire().unwrap();
    assert_eq!(before.source().total_frames, None);
    s.finish_input().unwrap();
    channel
        .process(
            &mut s,
            WorkBudget::default(),
            &CancellationToken::new(),
            final_result.storage(),
        )
        .unwrap();
    assert_eq!(channel.acquire().unwrap().source().total_frames, Some(64));
    assert_eq!(old.source().total_frames, None);
    assert_eq!(before.source().total_frames, None);
    let mut copy = Buffers::new();
    let current = channel.acquire().unwrap();
    assert_eq!(
        channel.publish(current.copy_to(copy.storage()).unwrap()),
        Err(Error::Conflict)
    );
    assert_eq!(
        channel.acquire().unwrap().info().generation,
        current.info().generation
    );
}

#[test]
fn cancellation_publication_is_mirrored_before_error_returns() {
    let mut initial = Buffers::new();
    let mut cancelled = Buffers::new();
    let mut a = Buffers::new();
    let mut b = Buffers::new();
    let pool = ResultPool::new(
        source(Some(1024)),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 64];
    let mut columns = [WaveformColumn::default(); 16];
    let mut s = PublishedSession::new(config(1024), &mut queue, &mut columns, &pool).unwrap();
    let channel = ConcurrentResults::new(
        s.acquire_result()
            .unwrap()
            .copy_to(initial.storage())
            .unwrap(),
    );
    let token = CancellationToken::new();
    token.cancel();
    assert_eq!(
        channel.process(&mut s, WorkBudget::default(), &token, cancelled.storage()),
        Err(Error::Cancelled)
    );
    let result = channel.acquire().unwrap();
    assert_eq!(result.info().session_state, ResultSessionState::Cancelled);
    assert_eq!(result.info().generation, 2);
}

#[test]
#[allow(clippy::drop_non_drop)]
fn heap_owned_concurrent_generations_release_all_caller_lifetimes() {
    use libapta_runtime::{HeapResult, HeapResults};
    let (channel, retained) = {
        let mut a = Buffers::new();
        let mut b = Buffers::new();
        let pool = ResultPool::new(
            source(None),
            [a.storage(), b.storage()],
            NativeLimits::default(),
        )
        .unwrap();
        let mut queue = [NormalizedSample::default(); 64];
        let mut columns = [WaveformColumn::default(); 16];
        let mut s = PublishedSession::new(
            config(session::TOTAL_FRAMES_UNKNOWN),
            &mut queue,
            &mut columns,
            &pool,
        )
        .unwrap();
        let channel = HeapResults::new(
            HeapResult::copy_from(&s.acquire_result().unwrap(), NativeLimits::default()).unwrap(),
        );
        let barrier = Arc::new(Barrier::new(2));
        let retained = std::thread::scope(|scope| {
            let readers = channel.clone();
            let sync = Arc::clone(&barrier);
            let reader = scope.spawn(move || {
                let mut retained = vec![readers.acquire().unwrap()];
                sync.wait();
                for _ in 0..16 {
                    sync.wait();
                    retained.push(readers.acquire().unwrap());
                    sync.wait();
                }
                retained
            });
            barrier.wait();
            for _ in 0..16 {
                s.push_pcm(PcmView::F32Interleaved(&[0.5; 64])).unwrap();
                channel
                    .process(
                        &mut s,
                        WorkBudget::default(),
                        &CancellationToken::new(),
                        NativeLimits::default(),
                    )
                    .unwrap();
                barrier.wait();
                barrier.wait();
            }
            s.finish_input().unwrap();
            channel
                .process(
                    &mut s,
                    WorkBudget::default(),
                    &CancellationToken::new(),
                    NativeLimits::default(),
                )
                .unwrap();
            reader.join().unwrap()
        });
        drop(s);
        drop(pool);
        (channel, retained)
    }; // All caller arrays have gone out of scope.
    let final_result = channel.acquire().unwrap();
    assert_eq!(final_result.source().total_frames, Some(1024));
    assert_eq!(final_result.view().overview.unwrap().columns.len(), 16);
    assert_eq!(
        final_result.info().session_state,
        ResultSessionState::Completed
    );
    drop(channel);
    for (i, result) in retained.iter().enumerate() {
        assert_eq!(result.view().overview.map_or(0, |w| w.columns.len()), i);
        assert_eq!(result.source().total_frames, None);
        assert!(result.retained_bytes() <= NativeLimits::default().maximum_storage_bytes);
    }
}

#[test]
fn heap_mirror_limits_retry_and_cancel_preserve_retained_graphs() {
    use libapta_runtime::{HeapResult, HeapResults};
    let mut a = Buffers::new();
    let mut b = Buffers::new();
    let pool = ResultPool::new(
        source(Some(1024)),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 64];
    let mut columns = [WaveformColumn::default(); 16];
    let mut s = PublishedSession::new(config(1024), &mut queue, &mut columns, &pool).unwrap();
    let channel = HeapResults::new(
        HeapResult::copy_from(&s.acquire_result().unwrap(), NativeLimits::default()).unwrap(),
    );
    let old = channel.acquire().unwrap();
    s.push_pcm(PcmView::F32Interleaved(&[0.5; 64])).unwrap();
    let short = NativeLimits {
        maximum_storage_bytes: 0,
        ..NativeLimits::default()
    };
    assert_eq!(
        channel.process(
            &mut s,
            WorkBudget::default(),
            &CancellationToken::new(),
            short
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.session().processed_frames(), 64);
    assert_eq!(channel.acquire().unwrap().info(), old.info());
    let count_short = NativeLimits {
        maximum_waveform_columns: 0,
        ..NativeLimits::default()
    };
    assert_eq!(
        channel.refresh_session(&s, count_short),
        Err(Error::LimitExceeded)
    );
    assert!(channel
        .refresh_session(&s, NativeLimits::default())
        .unwrap());
    assert!(!channel.refresh_session(&s, short).unwrap()); // No allocation for unchanged generation.
    let current = channel.acquire().unwrap();
    let exact = NativeLimits {
        maximum_storage_bytes: current.retained_bytes(),
        ..NativeLimits::default()
    };
    let copy = HeapResult::copy_from(&s.acquire_result().unwrap(), exact).unwrap();
    assert_eq!(copy.retained_bytes(), current.retained_bytes());
    let below = NativeLimits {
        maximum_storage_bytes: exact.maximum_storage_bytes - 1,
        ..exact
    };
    assert!(matches!(
        HeapResult::copy_from(&s.acquire_result().unwrap(), below),
        Err(Error::LimitExceeded)
    ));
    assert_eq!(channel.publish(copy), Err(Error::Conflict));
    let cancel = CancellationToken::new();
    cancel.cancel();
    assert_eq!(
        channel.process(
            &mut s,
            WorkBudget::default(),
            &cancel,
            NativeLimits::default()
        ),
        Err(Error::Cancelled)
    );
    assert_eq!(
        channel.acquire().unwrap().info().session_state,
        ResultSessionState::Cancelled
    );
    assert_eq!(old.info().session_state, ResultSessionState::Created);
    assert_eq!(current.view().overview.unwrap().columns.len(), 1);
}

#[test]
fn heap_sparse_gap_generations_outlive_all_working_storage() {
    use libapta::{
        publication::PublishedSparseSession,
        sparse::{QueuedBlock, SparseAccumulator, Workspace},
    };
    use libapta_runtime::{HeapResult, HeapResults};
    let (before, after) = {
        let mut ca = [WaveformColumn::default(); 16];
        let mut cb = ca;
        let mut sa = [WaveformSpan::default(); 16];
        let mut sb = sa;
        let pool = ResultPool::new(
            source(Some(1024)),
            [
                Storage {
                    overview_spans: &mut sa,
                    overview_columns: &mut ca,
                    ..Storage::default()
                },
                Storage {
                    overview_spans: &mut sb,
                    overview_columns: &mut cb,
                    ..Storage::default()
                },
            ],
            NativeLimits::default(),
        )
        .unwrap();
        let mut accumulators = [SparseAccumulator::default(); 16];
        let mut ranges = [FrameRange::default(); 4];
        let mut nodes = [QueuedBlock::default(); 2];
        let mut pcm = vec![NormalizedSample::default(); 8192];
        let mut spans = [WaveformSpan::default(); 16];
        let mut columns = [WaveformColumn::default(); 16];
        let mut s = PublishedSparseSession::new(
            config(1024),
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
        let channel = HeapResults::new(
            HeapResult::copy_from(&s.acquire_result().unwrap(), NativeLimits::default()).unwrap(),
        );
        s.push_at(512, PcmView::F32Interleaved(&[0.5; 64])).unwrap();
        channel
            .process_sparse(
                &mut s,
                WorkBudget::default(),
                &CancellationToken::new(),
                NativeLimits::default(),
            )
            .unwrap();
        let before = channel.acquire().unwrap();
        s.push_at(0, PcmView::F32Interleaved(&[-0.5; 64])).unwrap();
        let zero = NativeLimits {
            maximum_storage_bytes: 0,
            ..NativeLimits::default()
        };
        assert_eq!(
            channel.process_sparse(
                &mut s,
                WorkBudget::default(),
                &CancellationToken::new(),
                zero
            ),
            Err(Error::LimitExceeded)
        );
        assert_eq!(channel.acquire().unwrap().info(), before.info());
        assert!(channel.refresh_sparse(&s, NativeLimits::default()).unwrap());
        (before, channel.acquire().unwrap())
    };
    let first = before.view().overview.unwrap();
    let second = after.view().overview.unwrap();
    assert_eq!(first.spans.len(), 1);
    assert_eq!(first.spans[0].first_frame, 512);
    assert_eq!(second.spans.len(), 2);
    assert_eq!(second.spans[0].first_frame, 0);
    assert_eq!(second.spans[1].first_frame, 512);
    assert_eq!(first.columns[0].maximum, second.columns[1].maximum);
    assert!(second.columns[0].minimum < 0);
}
