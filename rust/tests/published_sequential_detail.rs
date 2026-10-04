// SPDX-License-Identifier: Apache-2.0
use libapta::{
    detail_analysis::DetailTile,
    owned_result::Storage,
    publication::{PublishedSession, ResultPool},
    session::{CancellationToken, SessionConfig, WorkBudget},
    waveform::{NormalizedSample, PcmView},
    *,
};
const EMPTY: NativeTile = NativeTile {
    level_id: 1,
    tile_index: 0,
    first_frame: 0,
    end_frame: 0,
    first_column_index: 0,
    state: FeatureState::Partial,
    confidence: 0,
    data_column_offset: 0,
    column_count: 0,
};
fn run(f: impl FnOnce(PublishedSession<'_, '_, '_>, &ResultPool<'_>)) {
    let mut dt0 = [EMPTY; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let mut os0 = [WaveformSpan::default(); 1];
    let mut os1 = os0;
    let mut oc0 = [WaveformColumn::default(); 9];
    let mut oc1 = oc0;
    let pool = ResultPool::new(
        SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(513),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        [
            Storage {
                overview_spans: &mut os0,
                overview_columns: &mut oc0,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_spans: &mut os1,
                overview_columns: &mut oc1,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut queue = [NormalizedSample::default(); 513];
    let mut overview = [WaveformColumn::default(); 9];
    let mut cache = [DetailTile::default(); 4];
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    let mut session = PublishedSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 513,
            frames_per_column: 64,
        },
        &mut queue,
        &mut overview,
        &pool,
    )
    .unwrap();
    let mut short: [NativeTile; 0] = [];
    let mut bad_cache = [DetailTile::default(); 4];
    let mut bad_columns = [WaveformColumn::default(); 256];
    assert_eq!(
        session.enable_detail(&mut bad_cache, &mut short, &mut bad_columns),
        Err(Error::BufferTooSmall)
    );
    session
        .enable_detail(&mut cache, &mut tiles, &mut columns)
        .unwrap();
    f(session, &pool);
}
fn trace_detail(output: &mut String, detail: NativeDetail<'_>) {
    use std::fmt::Write;
    writeln!(output, "R {}", detail.tiles.len()).unwrap();
    for t in detail.tiles {
        writeln!(
            output,
            "T {} {} {} {} {} {} {} {}",
            t.level_id,
            t.tile_index,
            t.first_frame,
            t.end_frame,
            t.first_column_index,
            t.column_count,
            t.state as u8,
            t.confidence
        )
        .unwrap();
        for c in &detail.columns[t.data_column_offset..t.data_column_offset + t.column_count] {
            writeln!(
                output,
                "C {} {} {} {} {} {} {}",
                c.minimum, c.maximum, c.rms, c.low, c.mid, c.high, c.flags
            )
            .unwrap();
        }
    }
}
#[test]
#[ignore = "requires APTA_C_DETAIL_SESSION_ORACLE"]
fn immutable_sequential_detail_matches_c_eager_and_eof() {
    run(|mut session, pool| {
        let token = CancellationToken::new();
        session
            .push_pcm(PcmView::S16Interleaved(&[123; 513]))
            .unwrap();
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1,
                },
                &token,
            )
            .unwrap();
        let first = pool.acquire().unwrap();
        assert!(first.overview().is_none());
        assert_eq!(first.changed_features(), result::WAVEFORM_DETAIL);
        let mut trace = String::new();
        trace_detail(&mut trace, first.detail().unwrap());
        drop(first);
        session.finish_input().unwrap();
        session.process(WorkBudget::default(), &token).unwrap();
        let retained = pool.acquire().unwrap();
        trace_detail(&mut trace, retained.detail().unwrap());
        assert_eq!(retained.info().session_state, ResultSessionState::Completed);
        // Explicitly end all working-buffer borrows before checking the lease.
        #[allow(clippy::drop_non_drop)]
        drop(session);
        assert_eq!(retained.detail().unwrap().tiles[0].end_frame, 513);
        let c =
            std::process::Command::new(std::env::var_os("APTA_C_DETAIL_SESSION_ORACLE").unwrap())
                .arg("sequential")
                .output()
                .unwrap();
        assert!(c.status.success());
        assert_eq!(trace, String::from_utf8(c.stdout).unwrap());
    });
}
#[test]
fn retained_detail_is_immutable_through_slot_exhaustion_and_retry() {
    run(|mut session, pool| {
        let token = CancellationToken::new();
        session
            .push_pcm(PcmView::S16Interleaved(&[100; 256]))
            .unwrap();
        let old = pool.acquire().unwrap();
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1,
                },
                &token,
            )
            .unwrap();
        let current = pool.acquire().unwrap();
        assert_eq!(current.detail().unwrap().columns.len(), 1);
        let first = current.detail().unwrap().columns[0];
        session
            .push_pcm(PcmView::S16Interleaved(&[200; 257]))
            .unwrap();
        assert_eq!(
            session.process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1
                },
                &token
            ),
            Err(Error::ResultSlotsExhausted)
        );
        assert!(session.publication_pending());
        assert_eq!(current.detail().unwrap().columns, &[first]);
        assert!(old.detail().is_none());
        drop(old);
        drop(current);
        session.process(WorkBudget::default(), &token).unwrap();
        session.finish_input().unwrap();
        session.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(pool.acquire().unwrap().detail().unwrap().columns.len(), 3);
    });
}
#[test]
fn detail_storage_and_aggregate_limits_fail_before_input_mutation() {
    for missing_storage in [true, false] {
        let mut dt0 = [EMPTY; 4];
        let mut dt1 = dt0;
        let mut dc0 = [WaveformColumn::default(); 256];
        let mut dc1 = dc0;
        let mut os0 = [WaveformSpan::default(); 1];
        let mut os1 = os0;
        let mut oc0 = [WaveformColumn::default(); 2];
        let mut oc1 = oc0;
        let mut limits = NativeLimits::default();
        if !missing_storage {
            limits.maximum_waveform_columns = 257;
        }
        let count = if missing_storage { 0 } else { 4 };
        let pool = ResultPool::new(
            SourceInfo {
                sample_rate: 48000,
                channel_count: 1,
                channel_layout: 1,
                total_frames: Some(65),
                fingerprint_kind: 0,
                fingerprint: [0; 32],
            },
            [
                Storage {
                    overview_spans: &mut os0,
                    overview_columns: &mut oc0,
                    detail_tiles: &mut dt0[..count],
                    detail_columns: &mut dc0,
                    ..Default::default()
                },
                Storage {
                    overview_spans: &mut os1,
                    overview_columns: &mut oc1,
                    detail_tiles: &mut dt1[..count],
                    detail_columns: &mut dc1,
                    ..Default::default()
                },
            ],
            limits,
        )
        .unwrap();
        let mut queue = [NormalizedSample::default(); 65];
        let mut overview = [WaveformColumn::default(); 2];
        let mut cache = [DetailTile::default(); 4];
        let mut tiles = [EMPTY; 4];
        let mut columns = [WaveformColumn::default(); 256];
        let mut session = PublishedSession::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 65,
                frames_per_column: 64,
            },
            &mut queue,
            &mut overview,
            &pool,
        )
        .unwrap();
        assert_eq!(
            session.enable_detail(&mut cache, &mut tiles, &mut columns),
            Err(if missing_storage {
                Error::BufferTooSmall
            } else {
                Error::LimitExceeded
            })
        );
        assert_eq!(
            session.session().state(),
            libapta::session::SessionState::Created
        );
        assert_eq!(pool.generation(), 1);
        session
            .push_pcm(PcmView::S16Interleaved(&[123; 65]))
            .unwrap();
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert!(pool.acquire().unwrap().detail().is_none());
    }
}
#[test]
fn retained_slots_roll_back_eof_then_retry_with_detail_tail() {
    run(|mut session, pool| {
        let token = CancellationToken::new();
        session
            .push_pcm(PcmView::S16Interleaved(&[123; 513]))
            .unwrap();
        let old = pool.acquire().unwrap();
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1,
                },
                &token,
            )
            .unwrap();
        let current = pool.acquire().unwrap();
        assert_eq!(session.finish_input(), Err(Error::ResultSlotsExhausted));
        assert_eq!(
            session.session().state(),
            libapta::session::SessionState::Running
        );
        assert_eq!(current.detail().unwrap().columns.len(), 2);
        drop(old);
        drop(current);
        session.finish_input().unwrap();
        session.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(pool.acquire().unwrap().detail().unwrap().columns.len(), 3);
    });
}
#[test]
fn overview_publication_takes_precedence_after_pending_detail_retry() {
    run(|mut session, pool| {
        let created = pool.acquire().unwrap();
        session
            .push_pcm(PcmView::S16Interleaved(&[123; 513]))
            .unwrap();
        let running = pool.acquire().unwrap();
        let token = CancellationToken::new();
        assert_eq!(
            session.process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1
                },
                &token
            ),
            Err(Error::ResultSlotsExhausted)
        );
        assert!(session.publication_pending());
        drop(created);
        session.process(WorkBudget::default(), &token).unwrap();
        let result = pool.acquire().unwrap();
        assert_eq!(result.changed_features(), result::WAVEFORM_OVERVIEW);
        assert!(result.overview().is_some());
        assert!(result.detail().is_some());
        assert!(running.detail().is_none());
    });
}
