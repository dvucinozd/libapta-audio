// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-s6-temporal-trace")]
use libapta::{
    analysis::OnsetBin,
    attack_grid::SelectionRecord,
    global_analysis::{BIN_CAPACITY, MAX_BEATS},
    global_history::{WindowHistory, WindowRecord, WINDOW_FRAMES},
    history_meter::{AttackBin, MeterHistory, MeterRecord, ATTACK_BIN_CAPACITY},
    session::{
        CancellationToken, Session, SessionConfig, SessionState, WorkBudget, TOTAL_FRAMES_UNKNOWN,
    },
    waveform::NormalizedSample,
    Beat, Error, WaveformColumn,
};

fn capture(chunk: usize, unknown: bool, cancel: bool, silence: bool) -> Vec<SelectionRecord> {
    let total = if cancel {
        WINDOW_FRAMES + 4096
    } else {
        2 * WINDOW_FRAMES + 17
    };
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut beats = vec![Beat::default(); MAX_BEATS];
    let mut attacks = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
    let mut records = [WindowRecord::default(); 3];
    let mut meters = [MeterRecord::default(); 3];
    let mut selections = [SelectionRecord::default(); 3];
    let mut queue = [NormalizedSample::default(); 4096];
    let mut columns = [WaveformColumn::default(); 17];
    let mut meter = MeterHistory::new(8000, &mut attacks, &mut meters).unwrap();
    let mut history = WindowHistory::new(8000, &mut records).unwrap();
    history.enable_meter(&mut meter).unwrap();
    history.enable_onset_grid(&mut selections).unwrap();
    let mut session = Session::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: if unknown { TOTAL_FRAMES_UNKNOWN } else { total },
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
    )
    .unwrap();
    session
        .enable_global_grid(false, &mut bins, &mut flux, &mut beats)
        .unwrap();
    session.enable_global_history(&mut history).unwrap();
    let token = CancellationToken::new();
    let budget = WorkBudget {
        maximum_input_frames: 0,
        maximum_steps: 1,
    };
    let mut offset = 0;
    let mut retained = None;
    while offset < total {
        let count = (total - offset).min(chunk as u64) as usize;
        let pcm: Vec<f32> = (0..count)
            .map(|n| {
                let frame = offset + n as u64;
                let amplitude = if silence {
                    0.0
                } else if frame % 4096 < 512 {
                    0.06
                } else {
                    0.05
                };
                amplitude * [0.0, 0.5, 1.0, 0.5, 0.0, -0.5, -1.0, -0.5][frame as usize % 8]
            })
            .collect();
        assert_eq!(session.push_interleaved(&pcm).unwrap(), count);
        offset += count as u64;
        while session.queued_frames() > 0 {
            session.process(budget, &token).unwrap();
        }
        session.process(budget, &token).unwrap();
        let before = session.accepted_frames();
        for invalid in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY]
            .into_iter()
            .filter(|_| offset < total)
        {
            assert_eq!(
                session.push_interleaved(&[0.1, invalid]),
                Err(Error::InvalidArgument)
            );
            assert_eq!(session.accepted_frames(), before);
        }
        if retained.is_none() {
            retained = session.global_history_selection().unwrap().first().copied();
        }
    }
    if cancel {
        let before = session.global_history_selection().unwrap().to_vec();
        token.cancel();
        assert_eq!(session.process(budget, &token), Err(Error::Cancelled));
        assert_eq!(session.state(), SessionState::Cancelled);
        assert_eq!(session.global_history_selection().unwrap(), before);
        assert!(!session.global_history().unwrap().finished);
        return before;
    }
    session.finish_input().unwrap();
    for _ in 0..10000 {
        session.process(budget, &token).unwrap();
        if session.state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(session.state(), SessionState::Complete);
    assert!(session.global_history().unwrap().finished);
    let result = session.global_history_selection().unwrap().to_vec();
    assert_eq!(result.first(), retained.as_ref());
    result
}

#[test]
fn dense_actual_session_is_chunk_independent_and_preserves_partial_eof() {
    let small = capture(257, true, false, false);
    let large = capture(4096, false, false, false);
    assert_eq!(small, large);
    assert_eq!(small.len(), 3);
    for (n, record) in small.iter().enumerate() {
        assert!(record.dense.complete);
        assert_eq!(record.range.first_frame, n as u64 * WINDOW_FRAMES);
        assert_eq!(record.dense.count, if n == 2 { 1 } else { 1024 });
        assert_eq!(record.dense.startup_bins, if n == 0 { 3 } else { 0 });
    }
    assert_eq!(small[2].range.end_frame, 2 * WINDOW_FRAMES + 17);
    assert!(small[0].dense.values.iter().any(|v| *v > 0.0 && *v <= 0.20));
}

#[test]
fn cancelled_dense_prefix_is_retained_and_new_source_clears_evidence() {
    let cancelled = capture(4096, true, true, false);
    assert_eq!(cancelled.len(), 1);
    assert!(cancelled[0].dense.complete);
    let fresh = capture(4096, false, false, true);
    assert_eq!(fresh.len(), 3);
    assert!(fresh
        .iter()
        .all(|record| record.dense.complete && record.dense.values.iter().all(|v| *v == 0.0)));
    assert!(cancelled[0].dense.values.iter().any(|v| *v > 0.0));
}
