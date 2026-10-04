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
    features: u64,
    result: &OwnedResult<'_>,
) {
    let overview = result.overview();
    // Preserve the documented native availability policy; only this known
    // bounded-C mask difference is normalized in the exact trace below.
    let has_bands = overview.is_some_and(|w| w.columns.iter().any(|c| c.flags & 8 != 0));
    assert_eq!(
        result.available_features() & result::WAVEFORM_3BAND != 0,
        has_bands
    );
    writeln!(
        trace,
        "H {event} {status} {accepted} {} {} {} {} {} {} {} {}",
        state(session_state),
        result.info().generation,
        result.info().session_state as u8,
        result.available_features() & !result::WAVEFORM_3BAND,
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
    if features & result::WAVEFORM_DETAIL != 0 {
        let detail = result.detail();
        let tile = detail.and_then(|d| d.tiles.first());
        writeln!(trace, "D {}", tile.map_or(0, |t| t.column_count)).unwrap();
        if let Some(t) = tile {
            writeln!(
                trace,
                "T {} {} {} {} {}",
                t.first_frame, t.end_frame, t.first_column_index, t.state as u8, t.confidence
            )
            .unwrap();
            for c in detail.unwrap().columns {
                writeln!(
                    trace,
                    "E {} {} {} {} {} {} {}",
                    c.minimum, c.maximum, c.rms, c.low, c.mid, c.high, c.flags
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
    features: u64,
) -> Result<(), Error> {
    let mut fingerprint = [0; 32];
    fingerprint[0] = if id == 19 { 0 } else { 0x41 };
    let total = match id {
        10 | 18 => None,
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
            flags: if id == 16 {
                1
            } else if index == 5 {
                5
            } else {
                9
            },
            low: if index == 5 || id == 16 { 0 } else { 12 },
            mid: if index == 5 || id == 16 { 0 } else { 23 },
            high: if index == 5 || id == 16 { 0 } else { 34 },
        };
    }
    let first_column_index = if id == 18 {
        4
    } else if id == 11 {
        2
    } else if id == 2 && second {
        1
    } else {
        0
    };
    let column_count = if id == 13 {
        8
    } else if id == 16 {
        4
    } else if id == 2 {
        2
    } else {
        1
    };
    let first_frame = u64::from(first_column_index) * 1024;
    let span = WaveformSpan {
        first_frame,
        end_frame: if id == 11 {
            2500
        } else {
            first_frame + u64::from(column_count) * 1024
        },
        first_column_index,
        column_count,
        data_column_offset: 0,
    };
    let spans = [
        span,
        WaveformSpan {
            first_frame: 3072,
            end_frame: 4096,
            first_column_index: 3,
            column_count: 1,
            data_column_offset: 1,
        },
    ];
    let detail_column = [WaveformColumn {
        flags: 1,
        low: 0,
        mid: 0,
        high: 0,
        ..columns[0]
    }];
    let detail_tile = [NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 256,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 42,
        data_column_offset: 0,
        column_count: 1,
    }];
    let input = NativeResultInput {
        source: SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: total,
            fingerprint_kind: if id == 19 { 0 } else { 1 },
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
            spans: &spans[..if id == 17 { 2 } else { 1 }],
            columns: &columns,
        }),
        detail: (features & result::WAVEFORM_DETAIL != 0).then_some(NativeDetail {
            tiles: &detail_tile,
            columns: &detail_column,
        }),
        metadata: None,
        tempo: None,
        local_grid: None,
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    };
    let mut owned_spans = [WaveformSpan::default(); 2];
    let mut owned_columns = [WaveformColumn::default(); 8];
    let mut text = [0; 4];
    let mut odt = detail_tile;
    let mut odc = detail_column;
    let checkpoint = owned_result::copy(
        &input,
        Storage {
            overview_spans: &mut owned_spans,
            overview_columns: &mut owned_columns,
            text_bytes: &mut text,
            detail_tiles: &mut odt,
            detail_columns: &mut odc,
            ..Default::default()
        },
        NativeLimits::default(),
    )
    .unwrap();
    // Both owned checkpoint and original backing disappear before processing.
    session.seed_from_result(&checkpoint, id == 9 || id == 19)
}

fn seed_status(result: Result<(), Error>) -> i32 {
    match result {
        Ok(()) => 0,
        Err(Error::Conflict) => -10,
        Err(Error::InvalidState) => -12,
        Err(error) => panic!("unexpected seed status {error}"),
    }
}

fn native_trace(id: u32, range_capacity: usize, features: u64) -> String {
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
    let tile = NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 255,
        data_column_offset: 0,
        column_count: 0,
    };
    let mut dt0 = [tile; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let pool = ResultPool::new(
        source,
        [
            Storage {
                overview_columns: &mut a_columns,
                overview_spans: &mut a_spans,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_columns: &mut b_columns,
                overview_spans: &mut b_spans,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
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
    let mut requests = [scheduler::RequestSlot::default(); 4];
    let (trace, final_result, retained_columns, retained_detail) = {
        let mut session = PublishedSparseSession::new_scheduled(
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
            &mut requests,
        )
        .unwrap();
        let mut bands = [band::BandSums::default(); 8];
        let mut cache = [detail_analysis::DetailTile::default(); 4];
        let mut dt = [tile; 4];
        let mut dc = [WaveformColumn::default(); 256];
        if features & result::WAVEFORM_3BAND != 0 {
            session.enable_three_band(&mut bands).unwrap();
        }
        if features & result::WAVEFORM_DETAIL != 0 {
            session.enable_detail(&mut cache, &mut dt, &mut dc).unwrap();
        }
        let mut trace = String::new();
        snapshot(
            &mut trace,
            0,
            0,
            0,
            session.session().state(),
            features,
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
                features,
                &pool.acquire().unwrap(),
            );
        }
        let result = install(&mut session, id, false, features);
        snapshot(
            &mut trace,
            2,
            seed_status(result),
            0,
            session.session().state(),
            features,
            &pool.acquire().unwrap(),
        );
        if result.is_err() {
            assert!(session.session().accepted_ranges().is_empty() || id == 12);
            assert_eq!(session.session().complete_columns(), 0);
            assert_eq!(session.session().processed_frames(), 0);
            assert_eq!(
                session.session().detail_mutation_serial(),
                if id == 12 && features & result::WAVEFORM_DETAIL != 0 {
                    1
                } else {
                    0
                }
            );
            assert_eq!(pool.generation(), if id == 12 { 2 } else { 1 });
            if id != 12 {
                let samples =
                    vec![1000; config.frames_per_column as usize * config.channel_count as usize];
                session
                    .push_at(0, PcmView::S16Interleaved(&samples))
                    .unwrap();
                session
                    .process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
                assert_eq!(session.session().complete_columns(), 1);
                let result = pool.acquire().unwrap();
                assert_eq!(result.overview().unwrap().columns[0].minimum, 1000);
            } else {
                session
                    .process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
                assert_eq!(session.session().processed_frames(), 1024);
                assert_eq!(
                    pool.acquire().unwrap().overview().unwrap().columns[0].minimum,
                    1000
                );
            }
            return trace;
        }
        if id == 1 || id == 2 || (id == 14 || id == 15) {
            if range_capacity == 1 {
                let ranges_before = session.session().accepted_ranges().to_vec();
                let completed_before = session.session().complete_columns();
                assert_eq!(
                    install(&mut session, id, true, features),
                    Err(Error::BufferTooSmall)
                );
                assert_eq!(session.session().accepted_ranges(), ranges_before);
                assert_eq!(session.session().complete_columns(), completed_before);
                assert_eq!(pool.generation(), 1);
                assert!(pool.acquire().unwrap().overview().is_none());
                assert_eq!(session.session().detail_mutation_serial(), 0);
                session.finish_input().unwrap();
                session
                    .process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
                assert_eq!(session.session().state(), SessionState::Complete);
                assert_eq!(
                    pool.acquire().unwrap().overview().unwrap().columns.len(),
                    completed_before
                );
                return trace;
            }
            let result = install(&mut session, id, true, features);
            assert_eq!(result, Ok(()));
            snapshot(
                &mut trace,
                3,
                0,
                0,
                session.session().state(),
                features,
                &pool.acquire().unwrap(),
            );
        }
        let ranges_before = session.session().accepted_ranges().to_vec();
        let serial = session.session().detail_mutation_serial();
        for value in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
            assert_eq!(
                session.push_at(0, PcmView::F32Interleaved(&[value])),
                Err(Error::InvalidArgument)
            );
        }
        assert_eq!(session.session().accepted_ranges(), ranges_before);
        assert_eq!(session.session().detail_mutation_serial(), serial);
        assert_eq!(session.session().processed_frames(), 0);
        assert_eq!(pool.generation(), 1);
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
            features,
            &pool.acquire().unwrap(),
        );
        if id != 13 && id != 16 {
            let first = if id == 11 {
                0
            } else if id == 2 {
                3072
            } else {
                1024
            };
            let count = if id == 14 || id == 15 {
                1024
            } else if id == 11 || id == 17 {
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
                features,
                &pool.acquire().unwrap(),
            );
        }
        if id == 14 || id == 15 {
            let old = pool.acquire().unwrap();
            let old_columns = old.overview().unwrap().columns.to_vec();
            session.process(budget, &cancellation).unwrap();
            snapshot(
                &mut trace,
                8,
                0,
                0,
                session.session().state(),
                features,
                &pool.acquire().unwrap(),
            );
            if features & result::WAVEFORM_DETAIL != 0 {
                session
                    .request_region(RegionRequest {
                        range: FrameRange {
                            first_frame: 0,
                            end_frame: 1024,
                        },
                        feature_mask: result::WAVEFORM_DETAIL,
                        priority: 200,
                        request_id: 0,
                        soft_deadline_monotonic_ns: 0,
                    })
                    .unwrap();
                let demand = session.next_pcm_request().unwrap();
                writeln!(
                    trace,
                    "Q {} {} {}",
                    demand.range.first_frame, demand.range.end_frame, demand.feature_mask
                )
                .unwrap();
                let processed = session.session().processed_frames();
                let before = pool.acquire().unwrap().overview().unwrap().columns.to_vec();
                assert_eq!(
                    session.push_at(0, PcmView::S16Interleaved(&[-25000; 1024])),
                    Ok(1024)
                );
                snapshot(
                    &mut trace,
                    9,
                    0,
                    1024,
                    session.session().state(),
                    features,
                    &pool.acquire().unwrap(),
                );
                assert_eq!(
                    session.process(budget, &cancellation),
                    Err(Error::ResultSlotsExhausted)
                );
                assert_eq!(session.session().processed_frames(), processed);
                assert_eq!(pool.acquire().unwrap().overview().unwrap().columns, before);
                snapshot(
                    &mut trace,
                    10,
                    -14,
                    0,
                    session.session().state(),
                    features,
                    &pool.acquire().unwrap(),
                );
            }
            if id == 15 {
                assert_eq!(
                    session.push_at(3072, PcmView::S16Interleaved(&[25000; 1024])),
                    Ok(1024)
                );
                snapshot(
                    &mut trace,
                    11,
                    0,
                    1024,
                    session.session().state(),
                    features,
                    &pool.acquire().unwrap(),
                );
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
                    features,
                    &pool.acquire().unwrap(),
                );
            }
            assert_eq!(old.overview().unwrap().columns, old_columns);
            drop(old);
            session.process(budget, &cancellation).unwrap();
            snapshot(
                &mut trace,
                13,
                if features & result::WAVEFORM_DETAIL != 0 {
                    0
                } else {
                    2
                },
                0,
                session.session().state(),
                features,
                &pool.acquire().unwrap(),
            );
            if id == 14 {
                assert_eq!(
                    session.push_at(3072, PcmView::S16Interleaved(&[25000; 1024])),
                    Ok(1024)
                );
                snapshot(
                    &mut trace,
                    14,
                    0,
                    1024,
                    session.session().state(),
                    features,
                    &pool.acquire().unwrap(),
                );
            }
            assert_eq!(
                session.push_at(2048, PcmView::S16Interleaved(&[1000; 1024])),
                Ok(1024)
            );
            snapshot(
                &mut trace,
                15,
                0,
                1024,
                session.session().state(),
                features,
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
            features,
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
            features,
            &final_result,
        );
        assert_ne!(
            (
                final_result.info().lineage_id_high,
                final_result.info().lineage_id_low
            ),
            (123, 456)
        );
        let retained_columns = final_result.overview().unwrap().columns.to_vec();
        let retained_detail = final_result.detail().map(|d| d.columns.to_vec());
        (trace, final_result, retained_columns, retained_detail)
    };
    assert_eq!(
        final_result.detail().map(|d| d.columns.to_vec()),
        retained_detail
    );
    assert_eq!(final_result.overview().unwrap().columns, retained_columns);
    trace
}

fn c_trace(id: u32, features: u64) -> String {
    let oracle = std::env::var_os("APTA_C_SEED_ORACLE").expect("set APTA_C_SEED_ORACLE");
    let output = std::process::Command::new(oracle)
        .arg(id.to_string())
        .arg(features.to_string())
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
        assert_eq!(native_trace(id, 8, 1), c_trace(id, 1), "scenario {id}");
    }
}

#[test]
#[ignore = "requires APTA_C_SEED_ORACLE"]
fn seeded_tail_preserves_valid_native_extent_despite_c_transient_bug() {
    let reference = c_trace(11, 1);
    // C marks the short seeded tail complete before EOF, but only clips the
    // published span at EOF. Its Active result temporarily exceeds total2500.
    // Rust keeps that span valid throughout; every other observable is exact.
    assert_eq!(reference.matches("S 2048 3072 1\n").count(), 1);
    let expected = reference.replace("S 2048 3072 1\n", "S 2048 2500 1\n");
    assert_eq!(native_trace(11, 8, 1), expected);
}

#[test]
fn native_seed_capacity_failure_precedes_mutation() {
    // C allocation failure has no rollback guarantee. The bounded native API
    // explicitly preflights capacity, including repeated/overlapping seeds.
    native_trace(1, 1, 1);
    native_trace(2, 1, 1);
}

#[test]
#[ignore = "requires APTA_C_SEED_ORACLE"]
fn feature_enabled_overview_seeding_matches_c() {
    for features in [1, 3, 5, 7] {
        for id in 0..20 {
            let reference = c_trace(id, features);
            let expected = if id == 11 {
                reference.replace("S 2048 3072 1\n", "S 2048 2500 1\n")
            } else {
                reference
            };
            assert_eq!(
                native_trace(id, 8, features),
                expected,
                "scenario {id}, features {features}"
            );
        }
    }
}

#[test]
fn feature_enabled_seed_capacity_preflight_is_atomic() {
    for features in [3, 5, 7] {
        native_trace(1, 1, features);
        native_trace(2, 1, features);
    }
}

#[test]
fn native_feature_seed_resumed_lifecycle() {
    for features in [1, 3, 5, 7] {
        for id in [0, 11, 13, 14, 15, 16, 17, 18, 19] {
            native_trace(id, 8, features);
        }
    }
}
