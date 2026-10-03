// SPDX-License-Identifier: Apache-2.0
//! Checkpoint seeding against the unchanged public C implementation.
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

fn install(
    session: &mut PublishedSparseSession<'_, '_, '_>,
    id: u32,
    second: bool,
) -> Result<(), Error> {
    let mut fingerprint = [0; 32];
    fingerprint[0] = 0x41;
    let total = match id {
        10 => None,
        11 => Some(2500),
        13 => Some(8192),
        _ => Some(4096),
    };
    let mut columns = [WaveformColumn::default(); 8];
    let energy = [0, 1, 32767, 32768, 65534, 65535, 32000, 17];
    for (index, column) in columns.iter_mut().enumerate() {
        *column = WaveformColumn {
            minimum: if index == 5 {
                i16::MIN
            } else {
                -12000 - index as i16 * 1000
            },
            maximum: if index == 5 {
                i16::MAX
            } else {
                19000 + index as i16 * 1000
            },
            rms: if id == 13 { energy[index] } else { 32000 },
            flags: if index == 5 { 5 } else { 9 },
            low: if index == 5 { 0 } else { 12 },
            mid: if index == 5 { 0 } else { 23 },
            high: if index == 5 { 0 } else { 34 },
        };
    }
    let first_column_index = if id == 11 {
        2
    } else if id == 2 && second {
        1
    } else {
        0
    };
    let column_count = if id == 13 {
        8
    } else if id == 2 {
        2
    } else {
        1
    };
    let first_frame = u64::from(first_column_index) * 1024;
    let spans = [WaveformSpan {
        first_frame,
        end_frame: if id == 11 {
            2500
        } else {
            first_frame + u64::from(column_count) * 1024
        },
        first_column_index,
        column_count,
        data_column_offset: 0,
    }];
    let input = NativeResultInput {
        source: SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: total,
            fingerprint_kind: 1,
            fingerprint,
        },
        info: NativeResultInfo {
            generation: 77,
            session_state: ResultSessionState::AcceptingInput,
            lineage_id_high: 123,
            lineage_id_low: 456,
            ..Default::default()
        },
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "seed",
            source_version: "",
        },
        overview: Some(NativeOverview {
            frames_per_column: 1024,
            origin_frame: 0,
            state: FeatureState::Partial,
            confidence: 42,
            spans: &spans,
            columns: &columns,
        }),
        detail: None,
        metadata: None,
        tempo: None,
        local_grid: None,
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    };
    let mut owned_spans = [WaveformSpan::default(); 1];
    let mut owned_columns = [WaveformColumn::default(); 8];
    let mut text = [0; 4];
    let checkpoint = owned_result::copy(
        &input,
        Storage {
            overview_spans: &mut owned_spans,
            overview_columns: &mut owned_columns,
            text_bytes: &mut text,
            ..Default::default()
        },
        NativeLimits::default(),
    )
    .unwrap();
    // Both owned checkpoint and original backing disappear before processing.
    session.seed_from_result(&checkpoint, id == 9)
}

fn seed_status(result: Result<(), Error>) -> i32 {
    match result {
        Ok(()) => 0,
        Err(Error::Conflict) => -10,
        Err(Error::InvalidState) => -12,
        Err(error) => panic!("unexpected seed status {error}"),
    }
}

fn native_trace(id: u32, range_capacity: usize) -> String {
    let total = if id == 6 || id == 13 {
        8192
    } else if id == 11 {
        2500
    } else {
        4096
    };
    let config = SessionConfig {
        sample_rate: if id == 4 { 44100 } else { 48000 },
        channel_count: if id == 5 { 2 } else { 1 },
        total_frames: total,
        frames_per_column: if id == 3 { 2048 } else { 1024 },
    };
    let mut fingerprint = [0; 32];
    if id != 8 && id != 9 {
        fingerprint[0] = if id == 7 { 0x42 } else { 0x41 };
    }
    let source = SourceInfo {
        sample_rate: config.sample_rate,
        channel_count: config.channel_count,
        channel_layout: config.channel_count,
        total_frames: Some(total),
        fingerprint_kind: if id == 8 || id == 9 { 0 } else { 1 },
        fingerprint,
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
    let mut acc = [SparseAccumulator::default(); 8];
    let mut ranges = [FrameRange::default(); 8];
    let mut nodes = [QueuedBlock::default(); 2];
    let mut pcm = [NormalizedSample::default(); 8192];
    let mut spans = [WaveformSpan::default(); 8];
    let mut columns = [WaveformColumn::default(); 8];
    let mut session = PublishedSparseSession::new(
        config,
        Workspace {
            accumulators: &mut acc,
            ranges: &mut ranges[..range_capacity],
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
    if id == 12 {
        let accepted = session
            .push_at(1024, PcmView::S16Interleaved(&[1000; 1024]))
            .unwrap();
        snapshot(
            &mut trace,
            1,
            0,
            accepted,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
    }
    let result = install(&mut session, id, false);
    snapshot(
        &mut trace,
        2,
        seed_status(result),
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    if result.is_err() {
        return trace;
    }
    if id == 1 || id == 2 {
        if range_capacity == 1 {
            let ranges_before = session.session().accepted_ranges().to_vec();
            let completed_before = session.session().complete_columns();
            assert_eq!(install(&mut session, id, true), Err(Error::BufferTooSmall));
            assert_eq!(session.session().accepted_ranges(), ranges_before);
            assert_eq!(session.session().complete_columns(), completed_before);
            assert_eq!(pool.generation(), 1);
            assert!(pool.acquire().unwrap().overview().is_none());
            return trace;
        }
        let result = install(&mut session, id, true);
        assert_eq!(result, Ok(()));
        snapshot(
            &mut trace,
            3,
            0,
            0,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
    }
    let budget = WorkBudget::default();
    let cancellation = CancellationToken::new();
    assert_eq!(
        session
            .process(budget, &cancellation)
            .unwrap()
            .consumed_input_frames,
        0
    );
    snapshot(
        &mut trace,
        4,
        2,
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    if id != 13 {
        let first = if id == 11 {
            0
        } else if id == 2 {
            3072
        } else {
            1024
        };
        let count = if id == 11 {
            2048
        } else {
            4096 - first as usize
        };
        let accepted = session
            .push_at(first, PcmView::S16Interleaved(&vec![1000; count]))
            .unwrap();
        snapshot(
            &mut trace,
            5,
            0,
            accepted,
            session.session().state(),
            &pool.acquire().unwrap(),
        );
    }
    session.finish_input().unwrap();
    snapshot(
        &mut trace,
        6,
        0,
        0,
        session.session().state(),
        &pool.acquire().unwrap(),
    );
    session.process(budget, &cancellation).unwrap();
    assert_eq!(session.session().state(), SessionState::Complete);
    let final_result = pool.acquire().unwrap();
    snapshot(
        &mut trace,
        7,
        3,
        0,
        session.session().state(),
        &final_result,
    );
    assert_ne!(
        (
            final_result.info().lineage_id_high,
            final_result.info().lineage_id_low
        ),
        (123, 456)
    );
    trace
}

fn c_trace(id: u32) -> String {
    let oracle = std::env::var_os("APTA_C_SEED_ORACLE").expect("set APTA_C_SEED_ORACLE");
    let output = std::process::Command::new(oracle)
        .arg(id.to_string())
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    String::from_utf8(output.stdout).unwrap()
}

#[test]
#[ignore = "requires APTA_C_SEED_ORACLE"]
fn checkpoint_rehydration_and_compatibility_match_c() {
    for id in (0..14).filter(|id| *id != 11) {
        assert_eq!(native_trace(id, 8), c_trace(id), "scenario {id}");
    }
}

#[test]
#[ignore = "requires APTA_C_SEED_ORACLE"]
fn seeded_tail_preserves_valid_native_extent_despite_c_transient_bug() {
    let reference = c_trace(11);
    // C marks the short seeded tail complete before EOF, but only clips the
    // published span at EOF. Its Active result temporarily exceeds total2500.
    // Rust keeps that span valid throughout; every other observable is exact.
    assert_eq!(reference.matches("S 2048 3072 1\n").count(), 1);
    let expected = reference.replace("S 2048 3072 1\n", "S 2048 2500 1\n");
    assert_eq!(native_trace(11, 8), expected);
}

#[test]
fn native_seed_capacity_failure_precedes_mutation() {
    // C allocation failure has no rollback guarantee. The bounded native API
    // explicitly preflights capacity, including repeated/overlapping seeds.
    native_trace(1, 1);
    native_trace(2, 1);
}
