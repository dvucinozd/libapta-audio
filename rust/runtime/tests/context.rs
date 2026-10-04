// SPDX-License-Identifier: Apache-2.0
use libapta::{session::*, waveform::PcmView, Error};
use libapta_runtime::{ContextLimits, GrowingLimits, RuntimeContext};
fn config() -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: TOTAL_FRAMES_UNKNOWN,
        frames_per_column: 64,
    }
}
#[test]
fn context_stays_busy_until_writer_channel_and_cross_thread_reader_release() {
    let c = RuntimeContext::new(ContextLimits::default());
    let clone = c.clone();
    let mut s = c
        .create_session(config(), GrowingLimits::default())
        .unwrap();
    assert_eq!(c.usage().unwrap().sessions, 1);
    assert_eq!(c.usage().unwrap().results, 1);
    assert_eq!(clone.close(), Err(Error::Busy));
    s.push_pcm(PcmView::F32Interleaved(&[0.5; 129])).unwrap();
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    let channel = s.results();
    let r = channel.acquire().unwrap();
    let bytes = r.retained_bytes();
    drop(s);
    assert_eq!(c.usage().unwrap().sessions, 0);
    assert_eq!(c.close(), Err(Error::Busy));
    drop(channel);
    assert_eq!(c.usage().unwrap().results, 1);
    assert_eq!(c.usage().unwrap().retained_bytes, bytes);
    std::thread::spawn(move || {
        assert_eq!(r.view().overview.unwrap().columns.len(), 2);
        assert_eq!(clone.close(), Err(Error::Busy));
        drop(r);
    })
    .join()
    .unwrap();
    assert_eq!(c.usage().unwrap().retained_bytes, 0);
    c.close().unwrap();
    assert_eq!(
        c.create_session(config(), GrowingLimits::default()).err(),
        Some(Error::InvalidState)
    );
    assert_eq!(c.close(), Err(Error::InvalidState));
}
#[test]
fn retained_graph_quota_failure_preserves_latest_and_retries_after_release() {
    let c = RuntimeContext::new(ContextLimits {
        maximum_results: 2,
        ..ContextLimits::default()
    });
    let mut s = c
        .create_session(config(), GrowingLimits::default())
        .unwrap();
    let channel = s.results();
    let initial = channel.acquire().unwrap();
    s.push_pcm(PcmView::F32Interleaved(&[0.5; 64])).unwrap();
    let current = channel.acquire().unwrap();
    let before = c.usage().unwrap();
    assert_eq!(
        s.process(WorkBudget::default(), &CancellationToken::new()),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.session().processed_frames(), 64);
    assert_eq!(c.usage().unwrap(), before);
    assert_eq!(
        channel.acquire().unwrap().info().generation,
        current.info().generation
    );
    drop(initial);
    s.refresh().unwrap();
    assert_eq!(c.usage().unwrap().results, 2);
    assert!(current.view().overview.is_none());
    drop(current);
    assert_eq!(c.usage().unwrap().results, 1);
    drop(channel);
    drop(s);
    c.close().unwrap();
}
#[test]
fn rejected_creation_releases_registration_and_context_close_is_serialized() {
    let c = RuntimeContext::new(ContextLimits {
        maximum_sessions: 1,
        ..ContextLimits::default()
    });
    assert_eq!(
        c.create_session(
            config(),
            GrowingLimits {
                maximum_working_bytes: 0,
                ..GrowingLimits::default()
            }
        )
        .err(),
        Some(Error::LimitExceeded)
    );
    assert_eq!(c.usage().unwrap().sessions, 0);
    assert_eq!(c.usage().unwrap().results, 0);
    let s = c
        .create_session(config(), GrowingLimits::default())
        .unwrap();
    assert_eq!(
        c.create_session(config(), GrowingLimits::default()).err(),
        Some(Error::LimitExceeded)
    );
    drop(s);
    let clone = c.clone();
    let t = std::thread::spawn(move || clone.close());
    let result = c.create_session(config(), GrowingLimits::default());
    match result {
        Ok(s) => {
            let closed = t.join().unwrap();
            assert_eq!(closed, Err(Error::Busy));
            drop(s);
            c.close().unwrap();
        }
        Err(Error::InvalidState) => {
            t.join().unwrap().unwrap();
        }
        Err(e) => panic!("{e:?}"),
    }
}
#[test]
fn byte_quota_rejects_initial_result_without_leaking_context_resources() {
    let c = RuntimeContext::new(ContextLimits {
        maximum_retained_bytes: 1,
        ..ContextLimits::default()
    });
    assert_eq!(
        c.create_session(config(), GrowingLimits::default()).err(),
        Some(Error::LimitExceeded)
    );
    let usage = c.usage().unwrap();
    assert_eq!(
        (usage.sessions, usage.results, usage.retained_bytes),
        (0, 0, 0)
    );
    c.close().unwrap();
}
#[test]
#[ignore = "requires APTA_C_CONTEXT_LIFETIME_ORACLE"]
fn context_busy_lifetime_matches_public_c() {
    let output =
        std::process::Command::new(std::env::var_os("APTA_C_CONTEXT_LIFETIME_ORACLE").unwrap())
            .output()
            .unwrap();
    assert!(output.status.success());
    let c = RuntimeContext::new(ContextLimits::default());
    let s = c
        .create_session(config(), GrowingLimits::default())
        .unwrap();
    let mut trace = String::new();
    assert_eq!(c.close(), Err(Error::Busy));
    trace.push_str("busy\n");
    let r = s.results().acquire().unwrap();
    drop(s);
    assert_eq!(c.close(), Err(Error::Busy));
    trace.push_str("busy\n");
    assert_eq!(r.info().generation, 1);
    drop(r);
    c.close().unwrap();
    trace.push_str("closed\n");
    assert_eq!(trace.as_bytes(), output.stdout);
}

#[test]
fn sparse_identity_registration_and_close_race_leave_no_resources() {
    use libapta_runtime::SparseLimits;
    use std::sync::{Arc, Barrier};
    for _ in 0..32 {
        let context = RuntimeContext::new(ContextLimits::default());
        let other = context.clone();
        let start = Arc::new(Barrier::new(2));
        let ready = start.clone();
        let closer = std::thread::spawn(move || {
            ready.wait();
            other.close()
        });
        let mut c = config();
        c.total_frames = 256;
        start.wait();
        let writer = context.create_sparse_session_with_identity(
            c,
            SparseLimits::default(),
            SourceIdentity::new(1, [7; 32]).unwrap(),
        );
        match writer {
            Ok(writer) => {
                assert_eq!(closer.join().unwrap(), Err(Error::Busy));
                let result = writer.results().acquire().unwrap();
                drop(writer);
                assert_eq!(context.close(), Err(Error::Busy));
                std::thread::spawn(move || {
                    assert_eq!(result.source().fingerprint, [7; 32]);
                    drop(result);
                })
                .join()
                .unwrap();
                context.close().unwrap();
            }
            Err(Error::InvalidState) => {
                closer.join().unwrap().unwrap();
            }
            Err(e) => panic!("{e:?}"),
        }
        let usage = context.usage().unwrap();
        assert_eq!(
            (usage.sessions, usage.results, usage.retained_bytes),
            (0, 0, 0)
        );
        assert!(usage.closed);
    }
}
