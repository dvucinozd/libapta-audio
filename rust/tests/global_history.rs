// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-s6-history")]
use libapta::{
    analysis::OnsetBin,
    global_analysis::{BIN_CAPACITY, BIN_FRAMES, MAX_BEATS},
    global_history::{WindowHistory, WindowOutcome, WindowRecord, WINDOW_FRAMES},
    session::{
        CancellationToken, Session, SessionConfig, SessionState, WorkBudget, TOTAL_FRAMES_UNKNOWN,
    },
    waveform::NormalizedSample,
    Beat, Error, WaveformColumn,
};

fn run(
    count: usize,
    unknown: bool,
    steps: u32,
    timed: bool,
    capacity: usize,
) -> (Vec<WindowRecord>, bool, bool, u64) {
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut beats = vec![Beat::default(); MAX_BEATS];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
    let mut records = vec![WindowRecord::default(); capacity];
    let mut history = WindowHistory::new(8000, &mut records).unwrap();
    let mut session = Session::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: if unknown {
                TOTAL_FRAMES_UNKNOWN
            } else {
                count as u64
            },
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
        maximum_steps: steps,
    };
    let mut block = [0.0f32; 4096];
    let mut accepted = 0;
    let mut retained_prefix = None;
    while accepted < count {
        let length = (count - accepted).min(block.len());
        for (i, sample) in block[..length].iter_mut().enumerate() {
            let phase = (accepted + i) % 4000;
            *sample = if phase < 64 {
                (64 - phase) as f32 / 64.0 * 0.75
            } else {
                0.0
            };
        }
        accepted += session.push_interleaved(&block[..length]).unwrap();
        let progress = if timed {
            let mut time = 1;
            let mut clock = || {
                time += 1000;
                time
            };
            session.process_with_clock(budget, 1, &mut clock, &token)
        } else {
            session.process(budget, &token)
        }
        .unwrap();
        if steps != 0 {
            assert!(progress.completed_steps <= steps);
        }
        if retained_prefix.is_none() {
            retained_prefix = session.global_history().unwrap().records.first().copied();
        }
    }
    session.finish_input().unwrap();
    for _ in 0..4096 {
        let progress = session.process(budget, &token).unwrap();
        if steps != 0 {
            assert!(progress.completed_steps <= steps);
        }
        if session.state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(session.state(), SessionState::Complete);
    assert_eq!(session.processed_frames(), count as u64);
    let view = session.global_history().unwrap();
    if let Some(prefix) = retained_prefix {
        assert_eq!(view.records.first(), Some(&prefix));
    }
    assert!(!view.discontinuous_input);
    let needed = libapta::history_grid::requirements(view, 19).unwrap();
    let template = session.global_grid().unwrap().segments[0];
    let mut segments = vec![template; needed.segments];
    let mut ranges = vec![libapta::FrameRange::default(); needed.supported_ranges];
    let converted =
        libapta::history_grid::convert_into(view, 19, &mut segments, &mut ranges).unwrap();
    let mut expected_ordinal = 0;
    for segment in converted.segments {
        assert_eq!(segment.anchor_ordinal, expected_ordinal);
        for n in 0..segment.beat_count {
            let beat = segment.beat_at(n).unwrap().unwrap();
            assert_eq!(beat.ordinal, expected_ordinal);
            assert_eq!(beat.revision, 19);
            assert!(beat.position.whole_frame >= segment.applicability_range.first_frame);
            assert!(beat.position.whole_frame < segment.applicability_range.end_frame);
            expected_ordinal += 1;
        }
    }
    assert_eq!(expected_ordinal as u64, converted.represented_beats);
    (
        view.records.to_vec(),
        view.finished,
        view.capacity_exhausted,
        session
            .global_grid()
            .map_or(0, |g| g.evidence_range.first_frame),
    )
}

#[test]
fn global_history_public_eof_budget_and_retained_copy() {
    for unknown in [false, true] {
        for steps in [0, 1] {
            for timed in [false, true] {
                let (records, finished, exhausted, _) = run(320000, unknown, steps, timed, 2);
                assert!(finished);
                assert!(!exhausted);
                assert_eq!(records.len(), 2);
                assert_eq!(records[0].range.first_frame, 0);
                assert_eq!(records[0].range.end_frame, WINDOW_FRAMES);
                assert!(matches!(records[0].outcome, WindowOutcome::Accepted(_)));
                assert_eq!(records[1].range.first_frame, WINDOW_FRAMES);
                assert_eq!(records[1].range.end_frame, 320000);
                assert_eq!(records[1].outcome, WindowOutcome::TooShort);
            }
        }
    }
}

#[test]
fn global_history_full_capacity_does_not_block_session_completion() {
    let (records, finished, exhausted, _) = run(2 * WINDOW_FRAMES as usize + 17, true, 1, false, 1);
    assert!(!finished);
    assert!(exhausted);
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].range.end_frame, WINDOW_FRAMES);
}

#[test]
fn global_history_invalid_attach_pcm_and_cancellation_are_transactional() {
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut beats = vec![Beat::default(); MAX_BEATS];
    let mut queue = [NormalizedSample::default(); 256];
    let mut columns = [WaveformColumn::default(); 1];
    let mut records = [WindowRecord::default(); 1];
    let mut wrong_records = records;
    let mut spare_records = records;
    let mut history = WindowHistory::new(8000, &mut records).unwrap();
    let mut wrong = WindowHistory::new(48000, &mut wrong_records).unwrap();
    let mut spare = WindowHistory::new(8000, &mut spare_records).unwrap();
    let mut s = Session::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: 256,
            frames_per_column: 256,
        },
        &mut queue,
        &mut columns,
    )
    .unwrap();
    s.enable_global_grid(false, &mut bins, &mut flux, &mut beats)
        .unwrap();
    assert_eq!(
        s.enable_global_history(&mut wrong),
        Err(Error::InvalidArgument)
    );
    s.enable_global_history(&mut history).unwrap();
    assert_eq!(
        s.enable_global_history(&mut spare),
        Err(Error::InvalidState)
    );
    assert!(s.push_interleaved(&[0.1, f32::NAN]).is_err());
    assert_eq!(s.global_history().unwrap().observed_frames, 0);
    assert!(s.global_history().unwrap().records.is_empty());
    s.push_interleaved(&[0.1]).unwrap();
    let token = CancellationToken::new();
    token.cancel();
    assert_eq!(
        s.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(s.global_history().unwrap().observed_frames, 1);
    assert!(s.global_history().unwrap().records.is_empty());
}

#[test]
#[ignore = "large real-PCM ring-turnover integration; run explicitly in release"]
fn global_history_real_pcm_survives_resident_prefix_eviction() {
    let count = BIN_CAPACITY * BIN_FRAMES as usize + WINDOW_FRAMES as usize;
    for unknown in [false, true] {
        let (records, finished, exhausted, resident_first) = run(count, unknown, 0, false, 129);
        assert!(finished);
        assert!(!exhausted);
        assert!(resident_first > 0);
        assert_eq!(records.len(), 129);
        assert_eq!(records[0].range.first_frame, 0);
        assert_eq!(records.last().unwrap().range.end_frame, count as u64);
        assert!(records
            .iter()
            .all(|r| matches!(r.outcome, WindowOutcome::Accepted(_))));
    }
}
