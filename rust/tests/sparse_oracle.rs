// SPDX-License-Identifier: Apache-2.0
//! Exact public-C sparse publication traces. The adapter maps Rust progress and
//! state to the C status spelling; all feature payloads are compared unchanged.
use libapta::{
    owned_result::{OwnedResult, Storage},
    publication::{PublishedSparseSession, ResultPool},
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace},
    waveform::{NormalizedSample, PcmView},
    *,
};
use std::fmt::Write;

fn state(value: SessionState) -> u8 {
    match value {
        SessionState::Created => 0,
        SessionState::Running => 1,
        SessionState::Draining => 2,
        SessionState::Complete => 3,
        SessionState::Cancelled => 4,
        SessionState::Failed => 5,
    }
}

fn snapshot(
    trace: &mut String,
    event: u32,
    status: i32,
    accepted: usize,
    session_state: SessionState,
    result: &OwnedResult<'_>,
) {
    let overview = result.overview();
    writeln!(
        trace,
        "H {event} {status} {accepted} {} {} {} {} {} {} {} {}",
        state(session_state),
        result.info().generation,
        result.info().session_state as u8,
        result.available_features(),
        result.changed_features(),
        overview.map_or(0, |w| w.state as u8),
        overview.map_or(0, |w| w.confidence),
        overview.map_or(0, |w| w.spans.len()),
    )
    .unwrap();
    if let Some(overview) = overview {
        for span in overview.spans {
            writeln!(
                trace,
                "S {} {} {}",
                span.first_frame, span.end_frame, span.column_count
            )
            .unwrap();
            let first = span.data_column_offset as usize;
            for column in &overview.columns[first..first + span.column_count as usize] {
                writeln!(
                    trace,
                    "C {} {} {} {} {} {} {}",
                    column.minimum,
                    column.maximum,
                    column.rms,
                    column.low,
                    column.mid,
                    column.high,
                    column.flags
                )
                .unwrap();
            }
        }
    }
}

fn native_trace(id: u32) -> String {
    let total = match id {
        3 => 2500,
        4 => 8192,
        _ => 4096,
    };
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(total),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let config = SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: total,
        frames_per_column: 1024,
    };
    let mut a_columns = [WaveformColumn::default(); 8];
    let mut b_columns = [WaveformColumn::default(); 8];
    let mut a_spans = [WaveformSpan::default(); 8];
    let mut b_spans = [WaveformSpan::default(); 8];
    let pool = ResultPool::new(
        source,
        [
            Storage {
                overview_columns: &mut a_columns,
                overview_spans: &mut a_spans,
                ..Default::default()
            },
            Storage {
                overview_columns: &mut b_columns,
                overview_spans: &mut b_spans,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut accumulators = [SparseAccumulator::default(); 8];
    let mut ranges = [FrameRange::default(); 8];
    let mut nodes = [QueuedBlock::default(); 4];
    let mut pcm = [NormalizedSample::default(); 4 * 4096];
    let mut spans = [WaveformSpan::default(); 8];
    let mut columns = [WaveformColumn::default(); 8];
    let mut session = PublishedSparseSession::new(
        config,
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
    let mut trace = String::new();
    snapshot(
        &mut trace,
        0,
        0,
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    let initial = if id >= 7 {
        Some(pool.acquire().unwrap())
    } else {
        None
    };
    let pushes: &[(u64, usize)] = match id {
        0 => &[(2048, 1024), (0, 1024), (3072, 1024), (1024, 1024)],
        1 => &[(0, 1024), (3072, 1024)],
        2 => &[(1024, 1024), (0, 2048), (512, 128), (2048, 2048)],
        3 => &[(2048, 452), (0, 1024), (1024, 1024)],
        4 => &[(0, 8192), (4096, 4096)],
        5 => &[(0, 512), (1024, 1024), (3072, 1024)],
        6 => &[(4096, 1), (u64::MAX, 1), (0, 0), (0, 4096)],
        7..=9 => &[(0, 4096)],
        _ => panic!("unknown scenario"),
    };
    for (index, &(first, count)) in pushes.iter().enumerate() {
        let input: Vec<i16> = (0..count)
            .map(|index| {
                let frame = first.wrapping_add(index as u64) % 8192;
                if frame % 1024 < 512 {
                    (-30000 + (frame / 1024) as i32 * 1000) as i16
                } else {
                    (25000 - (frame / 1024) as i32 * 1000) as i16
                }
            })
            .collect();
        let (status, accepted) = match session.push_at(first, PcmView::S16Interleaved(&input)) {
            Ok(accepted) => (
                if accepted == count {
                    0
                } else if accepted == 0 {
                    2
                } else {
                    1
                },
                accepted,
            ),
            Err(Error::Conflict) => (-10, 0),
            Err(Error::InvalidArgument) => (-1, 0),
            Err(error) => panic!("unexpected push failure: {error}"),
        };
        snapshot(
            &mut trace,
            index as u32 + 1,
            status,
            accepted,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
    }
    if let Some(initial) = initial {
        let budget = WorkBudget {
            maximum_input_frames: 1024,
            maximum_steps: 4,
        };
        let cancellation = CancellationToken::new();
        assert_eq!(
            session.process(budget, &cancellation),
            Err(Error::ResultSlotsExhausted)
        );
        snapshot(
            &mut trace,
            12,
            -14,
            0,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
        drop(initial);
        if id == 8 {
            cancellation.cancel();
            assert_eq!(
                session.process(budget, &cancellation),
                Err(Error::Cancelled)
            );
            snapshot(
                &mut trace,
                14,
                -7,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
            assert_eq!(
                session.process(budget, &CancellationToken::new()),
                Err(Error::Cancelled)
            );
            snapshot(
                &mut trace,
                15,
                -7,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
            return trace;
        }
        if id != 9 {
            let progress = session.process(budget, &cancellation).unwrap();
            assert_eq!(progress.consumed_input_frames, 1024);
            snapshot(
                &mut trace,
                13,
                1,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
        }
    }
    session.finish_input().unwrap();
    snapshot(
        &mut trace,
        10,
        0,
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    session.finish_input().unwrap();
    snapshot(
        &mut trace,
        11,
        0,
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    let budget = WorkBudget {
        maximum_input_frames: 1024,
        maximum_steps: 4,
    };
    let cancellation = CancellationToken::new();
    for event in 20..40 {
        let progress = session.process(budget, &cancellation).unwrap();
        let current = session.session().state();
        let status = if current == SessionState::Complete {
            3
        } else if progress.consumed_input_frames != 0 {
            1
        } else {
            2
        };
        snapshot(
            &mut trace,
            event,
            status,
            0,
            current,
            &pool.acquire().unwrap(),
        );
        if current == SessionState::Complete {
            break;
        }
    }
    assert_eq!(session.session().state(), SessionState::Complete);
    session.finish_input().unwrap();
    snapshot(
        &mut trace,
        40,
        0,
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    trace
}

#[test]
#[ignore = "requires APTA_C_SPARSE_ORACLE"]
fn sparse_publication_matches_unchanged_c() {
    let oracle = std::env::var_os("APTA_C_SPARSE_ORACLE").expect("set APTA_C_SPARSE_ORACLE");
    for id in 0..10 {
        let output = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let expected = String::from_utf8(output.stdout).unwrap();
        assert_eq!(native_trace(id), expected, "scenario {id}");
    }
}

#[test]
#[ignore = "requires APTA_C_SPARSE_ORACLE"]
fn sequential_pending_publication_matches_c() {
    use libapta::publication::PublishedSession;
    let oracle = std::env::var_os("APTA_C_SPARSE_ORACLE").expect("set APTA_C_SPARSE_ORACLE");
    for id in [7, 8, 9] {
        let output = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let source = SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(4096),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        };
        let config = SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 4096,
            frames_per_column: 1024,
        };
        let mut a_columns = [WaveformColumn::default(); 4];
        let mut b_columns = [WaveformColumn::default(); 4];
        let mut a_spans = [WaveformSpan::default(); 1];
        let mut b_spans = [WaveformSpan::default(); 1];
        let pool = ResultPool::new(
            source,
            [
                Storage {
                    overview_columns: &mut a_columns,
                    overview_spans: &mut a_spans,
                    ..Default::default()
                },
                Storage {
                    overview_columns: &mut b_columns,
                    overview_spans: &mut b_spans,
                    ..Default::default()
                },
            ],
            NativeLimits::default(),
        )
        .unwrap();
        let mut queue = [NormalizedSample::default(); 4096];
        let mut columns = [WaveformColumn::default(); 4];
        let mut session = PublishedSession::new(config, &mut queue, &mut columns, &pool).unwrap();
        let initial = pool.acquire().unwrap();
        let mut trace = String::new();
        snapshot(&mut trace, 0, 0, 0, session.session().state(), &initial);
        let pcm: Vec<i16> = (0..4096)
            .map(|frame| {
                if frame % 1024 < 512 {
                    (-30000 + frame / 1024 * 1000) as i16
                } else {
                    (25000 - frame / 1024 * 1000) as i16
                }
            })
            .collect();
        assert_eq!(
            session.push_pcm(PcmView::S16Interleaved(&pcm)).unwrap(),
            4096
        );
        snapshot(
            &mut trace,
            1,
            0,
            4096,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
        let budget = WorkBudget {
            maximum_input_frames: 1024,
            maximum_steps: 4,
        };
        let cancellation = CancellationToken::new();
        assert_eq!(
            session.process(budget, &cancellation),
            Err(Error::ResultSlotsExhausted)
        );
        snapshot(
            &mut trace,
            12,
            -14,
            0,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
        drop(initial);
        if id == 8 {
            cancellation.cancel();
            assert_eq!(
                session.process(budget, &cancellation),
                Err(Error::Cancelled)
            );
            snapshot(
                &mut trace,
                14,
                -7,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
            assert_eq!(
                session.process(budget, &CancellationToken::new()),
                Err(Error::Cancelled)
            );
            snapshot(
                &mut trace,
                15,
                -7,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
            assert_eq!(trace, String::from_utf8(output.stdout).unwrap());
            continue;
        }
        if id != 9 {
            assert_eq!(
                session
                    .process(budget, &cancellation)
                    .unwrap()
                    .consumed_input_frames,
                1024
            );
            snapshot(
                &mut trace,
                13,
                1,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
        }
        for event in [10, 11] {
            session.finish_input().unwrap();
            snapshot(
                &mut trace,
                event,
                0,
                0,
                session.session().state(),
                &pool.acquire().unwrap(),
            );
        }
        for event in 20..40 {
            let progress = session.process(budget, &cancellation).unwrap();
            let current = session.session().state();
            let status = if current == SessionState::Complete {
                3
            } else if progress.consumed_input_frames != 0 {
                1
            } else {
                2
            };
            snapshot(
                &mut trace,
                event,
                status,
                0,
                current,
                &pool.acquire().unwrap(),
            );
            if current == SessionState::Complete {
                break;
            }
        }
        assert_eq!(session.session().state(), SessionState::Complete);
        session.finish_input().unwrap();
        snapshot(
            &mut trace,
            40,
            0,
            0,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
        assert_eq!(trace, String::from_utf8(output.stdout).unwrap());
    }
}
