// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::{OwnedResult, Storage},
    publication::{PublishedSession, ResultPool},
    session::{CancellationToken, Progress, SessionConfig, SessionState, WorkBudget},
    waveform::{NormalizedSample, PcmView},
    *,
};
fn state(s: SessionState) -> i128 {
    match s {
        SessionState::Created => 0,
        SessionState::Running => 1,
        SessionState::Draining => 2,
        SessionState::Complete => 3,
        SessionState::Cancelled => 4,
        SessionState::Failed => 5,
    }
}
fn process_status(p: Result<Progress, Error>, s: SessionState) -> i128 {
    match p {
        Err(Error::ResultSlotsExhausted) => -14,
        Err(Error::Cancelled) => -7,
        Err(e) => panic!("unexpected {e}"),
        Ok(_) if s == SessionState::Complete => 3,
        Ok(p) if p.consumed_input_frames == 0 => 2,
        Ok(_) => 0,
    }
}
fn snapshot(
    event: i128,
    status: i128,
    accepted: i128,
    session_state: i128,
    r: &OwnedResult<'_>,
) -> [i128; 15] {
    let w = r.overview();
    let c = w.map(|w| w.columns[0]).unwrap_or_default();
    [
        event,
        status,
        accepted,
        session_state,
        r.info().generation as i128,
        r.info().session_state as u8 as i128,
        r.available_features() as i128,
        r.changed_features() as i128,
        w.map_or(0, |w| w.state as u8 as i128),
        w.map_or(0, |w| w.columns.len() as i128),
        c.minimum as i128,
        c.maximum as i128,
        c.rms as i128,
        c.flags as i128,
        w.map_or(0, |w| w.confidence as i128),
    ]
}
fn trace(id: u32) -> Vec<[i128; 15]> {
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(1024),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let config = SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: 1024,
        frames_per_column: 1024,
    };
    let mut a_columns = [WaveformColumn::default(); 1];
    let mut b_columns = [WaveformColumn::default(); 1];
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
    let mut queue = [NormalizedSample::default(); 1024];
    let mut work = [WaveformColumn::default(); 1];
    let (mut rows, initial, final_result) = {
        let mut session = PublishedSession::new(config, &mut queue, &mut work, &pool).unwrap();
        let cancel = CancellationToken::new();
        let budget = WorkBudget {
            maximum_input_frames: 1024,
            maximum_steps: 4,
        };
        let mut rows = vec![];
        let mut initial = if (1..=3).contains(&id) {
            Some(pool.acquire().unwrap())
        } else {
            None
        };
        rows.push(snapshot(
            0,
            0,
            0,
            state(session.session().state()),
            &pool.acquire().unwrap(),
        ));
        if id == 2 {
            cancel.cancel();
            let p = session.process(budget, &cancel);
            rows.push(snapshot(
                1,
                process_status(p, session.session().state()),
                0,
                state(session.session().state()),
                &pool.acquire().unwrap(),
            ));
        } else {
            let pcm: Vec<i16> = (0..1024)
                .map(|i| if i & 1 != 0 { i16::MAX } else { i16::MIN })
                .collect();
            let accepted = session.push_pcm(PcmView::S16Interleaved(&pcm)).unwrap();
            rows.push(snapshot(
                1,
                0,
                accepted as i128,
                state(session.session().state()),
                &pool.acquire().unwrap(),
            ));
            if id == 3 {
                cancel.cancel();
                let p = session.process(budget, &cancel);
                rows.push(snapshot(
                    2,
                    process_status(p, session.session().state()),
                    0,
                    state(session.session().state()),
                    &pool.acquire().unwrap(),
                ));
                drop(initial.take());
                let p = session.process(budget, &cancel);
                rows.push(snapshot(
                    3,
                    process_status(p, session.session().state()),
                    0,
                    state(session.session().state()),
                    &pool.acquire().unwrap(),
                ));
            } else {
                if id != 4 {
                    let p = session.process(budget, &cancel);
                    rows.push(snapshot(
                        2,
                        process_status(p, session.session().state()),
                        0,
                        state(session.session().state()),
                        &pool.acquire().unwrap(),
                    ));
                }
                if id == 1 {
                    rows.push(snapshot(
                        20,
                        0,
                        0,
                        state(session.session().state()),
                        initial.as_ref().unwrap(),
                    ));
                    drop(initial.take());
                    let p = session.process(budget, &cancel);
                    rows.push(snapshot(
                        3,
                        process_status(p, session.session().state()),
                        0,
                        state(session.session().state()),
                        &pool.acquire().unwrap(),
                    ));
                }
                let partial = if id == 1 {
                    Some(pool.acquire().unwrap())
                } else {
                    None
                };
                session.finish_input().unwrap();
                rows.push(snapshot(
                    4,
                    0,
                    0,
                    state(session.session().state()),
                    &pool.acquire().unwrap(),
                ));
                let p = session.process(budget, &cancel);
                rows.push(snapshot(
                    5,
                    process_status(p, session.session().state()),
                    0,
                    state(session.session().state()),
                    &pool.acquire().unwrap(),
                ));
                if let Some(p) = partial {
                    rows.push(snapshot(21, 0, 0, state(session.session().state()), &p));
                    drop(p);
                    let p = session.process(budget, &cancel);
                    rows.push(snapshot(
                        6,
                        process_status(p, session.session().state()),
                        0,
                        state(session.session().state()),
                        &pool.acquire().unwrap(),
                    ));
                }
            }
        }
        let final_result = pool.acquire().unwrap();
        (rows, initial, final_result)
    };
    rows.push(snapshot(9, 0, 0, -1, &final_result));
    if let Some(initial) = initial {
        rows.push(snapshot(22, 0, 0, -1, &initial));
    }
    rows
}
#[test]
#[ignore = "requires APTA_C_PUBLICATION_ORACLE"]
fn bounded_publication_lifecycle_and_retention_match_c() {
    let oracle =
        std::env::var_os("APTA_C_PUBLICATION_ORACLE").expect("set APTA_C_PUBLICATION_ORACLE");
    for id in 0..5 {
        let output = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let expected: Vec<[i128; 15]> = std::str::from_utf8(&output.stdout)
            .unwrap()
            .lines()
            .map(|l| {
                l.split_whitespace()
                    .map(|v| v.parse::<i128>().unwrap())
                    .collect::<Vec<_>>()
                    .try_into()
                    .unwrap()
            })
            .collect();
        assert_eq!(trace(id), expected, "scenario{id}");
    }
}
