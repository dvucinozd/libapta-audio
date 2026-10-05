// SPDX-License-Identifier: Apache-2.0
//! Interior rejection, real S6 replacement, and the explicit native/C EOF difference.
//! Synthetic invariants do not establish musical accuracy.
use libapta::{
    analysis::{OnsetBin, BIN_CAPACITY},
    container::{Container, ParseOptions},
    owned_result::{GridStorage, OwnedResult, Storage},
    result,
    session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget},
    waveform::NormalizedSample,
    Beat, FeatureState, FractionalFrame, FramePeriod, FrameRange, GlobalGrid, GridRepresentation,
    GridRevision, GridSegment, NativeLimits, RevisionState, WaveformColumn,
};
use std::{fs, process::Command};

fn wire(session: &Session<'_>) -> Vec<u8> {
    let snapshot = session.snapshot(1).unwrap();
    let view = result::from_session_snapshot(&snapshot, &mut [], NativeLimits::default()).unwrap();
    let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
    result::write(&view, &mut bytes, result::Limits::default()).unwrap();
    bytes
}

fn retained_wire(owned: &OwnedResult<'_>) -> Vec<u8> {
    let view = result::from_session_result(owned, &mut [], NativeLimits::default()).unwrap();
    let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
    result::write(&view, &mut bytes, result::Limits::default()).unwrap();
    bytes
}

fn payload<'a>(container: &Container<'a>, id: [u8; 4]) -> Option<&'a [u8]> {
    (0..container.section_count())
        .filter_map(|i| container.section(i))
        .find(|s| s.fourcc == id)
        .map(|s| s.payload)
}

// Independent expected geometry: the estimator is unchanged (96154 millibpm),
// but support is two 128-bin islands. Keep complete original C bytes checked too.
fn same_tempo_payloads(corrected: bool, dynamic: bool) -> (Vec<u8>, Vec<u8>) {
    let range = FrameRange {
        first_frame: 0,
        end_frame: 786432,
    };
    let representation = if dynamic {
        GridRepresentation::Hybrid
    } else {
        GridRepresentation::Segments
    };
    let mut segments = vec![GridSegment {
        applicability_range: FrameRange {
            first_frame: 0,
            end_frame: if corrected { 262144 } else { 786432 },
        },
        anchor_position: FractionalFrame {
            whole_frame: 2048,
            fraction_q32: 0,
        },
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod {
            whole_frames: 4991,
            fraction_q32: 4260662588,
        },
        beat_count: if corrected { 53 } else { 158 },
        nominal_tempo_millibpm: 96154,
        confidence: 86,
        state: FeatureState::Final,
        flags: 0,
        segment_id: 1,
        revision: 6,
    }];
    if corrected {
        segments.push(GridSegment {
            applicability_range: FrameRange {
                first_frame: 524288,
                end_frame: 786432,
            },
            anchor_position: FractionalFrame {
                whole_frame: 526336,
                fraction_q32: 0,
            },
            anchor_ordinal: if dynamic { 53 } else { 0 },
            confidence: 87,
            segment_id: 2,
            ..segments[0]
        });
    }
    let mut beats = Vec::new();
    if dynamic {
        for s in &segments {
            for n in 0..s.beat_count {
                let q32 = (u128::from(s.anchor_position.whole_frame) << 32)
                    + u128::from(n) * ((4991u128 << 32) + 4260662588);
                beats.push(Beat {
                    position: FractionalFrame {
                        whole_frame: (q32 >> 32) as u64,
                        fraction_q32: q32 as u32,
                    },
                    ordinal: s.anchor_ordinal + i64::from(n),
                    revision: 6,
                    flags: 0,
                    confidence: s.confidence,
                });
            }
        }
    }
    let grid = GlobalGrid {
        state: FeatureState::Final,
        confidence: 86,
        flags: 0,
        representation,
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_range: range,
        segments: &segments,
        beats: &beats,
    };
    let revision = GridRevision {
        state: RevisionState::Applied,
        confidence: 86,
        flags: 0,
        revision_id: 6,
        previous_revision_id: 5,
        proposed_representation: representation,
        proposed_segment_count: segments.len() as u32,
        proposed_beat_count: beats.len() as u32,
        affected_range: range,
    };
    let mut g = vec![0; 96 + segments.len() * 80 + beats.len() * 40];
    libapta::grid::write_payload(&grid, true, &mut g).unwrap();
    let mut r = vec![0; 80];
    libapta::grid::write_revision(&revision, &grid, true, &mut r).unwrap();
    (g, r)
}

#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn s6_interior_and_resident_ring_characterize_c_and_native_eof() {
    let oracle = std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").expect("oracle");
    // Optional raw artifacts are external, unique per invocation, and never overwritten.
    let output = std::env::var_os("APTA_S6_EVIDENCE_DIR").map(std::path::PathBuf::from);
    if let Some(path) = &output {
        fs::create_dir(path).unwrap();
    }
    let temporary = std::env::temp_dir().join(format!("apta-s6-ring-{}", std::process::id()));
    fs::create_dir(&temporary).unwrap();
    let mut rows = String::from("fixture,frames,unknown,steps,dynamic,state,evidence_first,evidence_end,segment_first,segment_end,segments,global_tempo,local_tempo,meter_binds_global,revision,previous_revision,gap_frames,c_evidence_first,c_evidence_end,c_revision\n");
    let mut full_reference = Vec::new();
    let capacity = libapta::global_analysis::BIN_CAPACITY * 2048;
    for (fixture, count) in [
        ("interior-same", 384 * 2048),
        ("interior-change", 384 * 2048),
        ("ring-full", capacity),
        ("ring-partial", capacity + 2049),
    ] {
        let interior = fixture.starts_with("interior");
        let pcm: Vec<f32> = (0..count)
            .map(|i| {
                let silent = interior && (128 * 2048..256 * 2048).contains(&i);
                let period = if fixture == "interior-change" && i >= 256 * 2048 {
                    6000
                } else {
                    4000
                };
                let phase = i % period;
                if phase < 64 && !silent {
                    (64 - phase) as f32 / 64.0 * 0.75
                } else {
                    0.0
                }
            })
            .collect();
        let path = temporary.join("input.pcm");
        let pcm_bytes: Vec<_> = pcm.iter().flat_map(|v| v.to_le_bytes()).collect();
        fs::write(&path, &pcm_bytes).unwrap();
        if let Some(output) = &output {
            fs::write(output.join(format!("{fixture}.pcm")), &pcm_bytes).unwrap();
        }
        let mut unlimited_native = Vec::new();
        for (unknown, steps, dynamic) in [
            (false, 0, false),
            (true, 0, false),
            (false, 32, false),
            (true, 32, false),
            (false, 0, true),
        ] {
            // Long hybrid output has a separate explicit-beat capacity limit.
            if !interior && dynamic {
                continue;
            }
            let name = format!("{fixture}-{unknown}-{steps}-{dynamic}");
            eprintln!("starting {name}");
            let mask = result::WAVEFORM_OVERVIEW
                | result::BPM
                | result::LOCAL_BEATGRID
                | result::GLOBAL_BEATGRID
                | result::METER_DOWNBEAT
                | if dynamic { result::DYNAMIC_TEMPO } else { 0 };
            let c = Command::new(&oracle)
                .args([
                    "8000".to_owned(),
                    steps.to_string(),
                    path.to_string_lossy().into_owned(),
                    format!("project:{mask}{}", if unknown { ":unknown" } else { "" }),
                ])
                .output()
                .unwrap();
            assert!(
                c.status.success(),
                "{name}: {}",
                String::from_utf8_lossy(&c.stderr)
            );
            let reference = Container::parse(&c.stdout, ParseOptions::default()).unwrap();
            let mut queue = vec![NormalizedSample::default(); 4096];
            let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
            let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
            let mut flux = vec![0.0; BIN_CAPACITY];
            let mut global_bins = vec![OnsetBin::default(); libapta::global_analysis::BIN_CAPACITY];
            let mut global_flux = vec![0.0; global_bins.len()];
            let mut beats = vec![Beat::default(); libapta::global_analysis::MAX_BEATS];
            let (
                mut text_bytes,
                mut spans,
                mut saved_columns,
                mut candidates,
                mut local_ranges,
                mut local_segments,
                mut global_ranges,
                mut global_segments,
                mut saved_beats,
                mut meter_segments,
            );
            let (retained, retained_bytes) = {
                let mut session = Session::new(
                    SessionConfig {
                        sample_rate: 8000,
                        channel_count: 1,
                        total_frames: if unknown { u64::MAX } else { count as u64 },
                        frames_per_column: 32768,
                    },
                    &mut queue,
                    &mut columns,
                )
                .unwrap();
                session.enable_tempo(&mut bins, &mut flux).unwrap();
                session
                    .enable_global_grid(dynamic, &mut global_bins, &mut global_flux, &mut beats)
                    .unwrap();
                session.enable_meter().unwrap();
                let token = CancellationToken::new();
                let budget = WorkBudget {
                    maximum_input_frames: 0,
                    maximum_steps: steps,
                };
                // Retain an actual caller-storage graph before interior rejection/ring
                // replacement, independently of both the writer and serialization.
                let prefix = 128 * 2048;
                for block in pcm[..prefix].chunks(4096) {
                    assert_eq!(session.push_interleaved(block).unwrap(), block.len());
                    let progress = session.process(budget, &token).unwrap();
                    if steps != 0 {
                        assert!(progress.completed_steps <= steps);
                    }
                }
                let snapshot = session.snapshot(17).unwrap();
                text_bytes = vec![
                    0;
                    snapshot
                        .requirements(NativeLimits::default())
                        .unwrap()
                        .text_bytes
                ];
                let v = snapshot.view();
                spans = v.overview.unwrap().spans.to_vec();
                saved_columns = v.overview.unwrap().columns.to_vec();
                candidates = v.tempo.unwrap().candidates.to_vec();
                local_ranges = v.local_grid.unwrap().coverage_ranges.to_vec();
                local_segments = v.local_grid.unwrap().segments.to_vec();
                global_ranges = v.global_grid.unwrap().coverage_ranges.to_vec();
                global_segments = v.global_grid.unwrap().segments.to_vec();
                saved_beats = v.global_grid.unwrap().beats.to_vec();
                meter_segments = v.meter.unwrap().segments.to_vec();
                let retained = snapshot
                    .copy_to(
                        Storage {
                            text_bytes: &mut text_bytes,
                            overview_spans: &mut spans,
                            overview_columns: &mut saved_columns,
                            tempo_candidates: &mut candidates,
                            local_grid: GridStorage {
                                coverage_ranges: &mut local_ranges,
                                segments: &mut local_segments,
                                beats: &mut [],
                            },
                            global_grid: GridStorage {
                                coverage_ranges: &mut global_ranges,
                                segments: &mut global_segments,
                                beats: &mut saved_beats,
                            },
                            meter_segments: &mut meter_segments,
                            ..Storage::default()
                        },
                        NativeLimits::default(),
                        0,
                    )
                    .unwrap();
                let retained_bytes = retained_wire(&retained);
                assert_eq!(
                    retained.source().total_frames,
                    if unknown { None } else { Some(count as u64) }
                );
                assert_eq!(retained.info().generation, 17);
                assert_eq!(
                    retained.global_grid().unwrap().evidence_range.first_frame,
                    0
                );
                for block in pcm[prefix..].chunks(4096) {
                    assert_eq!(session.push_interleaved(block).unwrap(), block.len());
                    let progress = session.process(budget, &token).unwrap();
                    if steps != 0 {
                        assert!(progress.completed_steps <= steps);
                    }
                }
                assert_eq!(session.accepted_frames(), count as u64);
                assert_eq!(session.processed_frames(), count as u64);
                session.finish_input().unwrap();
                assert_eq!(session.total_frames(), Some(count as u64));
                for _ in 0..100000 {
                    let progress = session.process(budget, &token).unwrap();
                    if steps != 0 {
                        assert!(progress.completed_steps <= steps);
                    }
                    if session.state() == SessionState::Complete {
                        break;
                    }
                }
                assert_eq!(session.state(), SessionState::Complete, "{name}");
                assert_eq!(session.processed_frames(), count as u64);
                let native_bytes = wire(&session);
                let native = Container::parse(&native_bytes, ParseOptions::default()).unwrap();

                if let Some(output) = &output {
                    fs::write(output.join(format!("{name}-c.apta")), &c.stdout).unwrap();
                    fs::write(output.join(format!("{name}-rust.apta")), &native_bytes).unwrap();
                }
                for id in [*b"WOVR", *b"TEMP", *b"LGRD", *b"GGRD", *b"REVN", *b"MTRD"] {
                    if fixture == "interior-same" && [*b"GGRD", *b"REVN"].contains(&id) {
                        let expected_c = same_tempo_payloads(false, dynamic);
                        let expected_rust = same_tempo_payloads(true, dynamic);
                        let select = |pair: &(Vec<u8>, Vec<u8>)| {
                            if id == *b"GGRD" {
                                pair.0.clone()
                            } else {
                                pair.1.clone()
                            }
                        };
                        assert_eq!(
                            payload(&reference, id).unwrap(),
                            select(&expected_c),
                            "original C {name}"
                        );
                        assert_eq!(
                            payload(&native, id).unwrap(),
                            select(&expected_rust),
                            "corrected Rust {name}"
                        );
                    } else if fixture == "ring-partial"
                        && steps == 32
                        && [*b"GGRD", *b"REVN"].contains(&id)
                    {
                        // Explicitly preserve the discovered C/native EOF difference.
                        // C keeps the previous full-ring geometry/revision, updating
                        // only requested EOF. Native consumes the changed resident
                        // evidence and equals its unlimited-work output exactly.
                        let full =
                            Container::parse(&full_reference, ParseOptions::default()).unwrap();
                        let unlimited =
                            Container::parse(&unlimited_native, ParseOptions::default()).unwrap();
                        assert_eq!(
                            payload(&native, id),
                            payload(&unlimited, id),
                            "native EOF {name} {id:?}"
                        );
                        let old = payload(&full, id).unwrap();
                        let stale = payload(&reference, id).unwrap();
                        if id == *b"GGRD" {
                            assert_eq!(&stale[..32], &old[..32]);
                            assert_eq!(&stale[32..40], &(count as u64).to_le_bytes());
                            assert_eq!(&stale[40..], &old[40..]);
                        } else {
                            assert_eq!(stale, old);
                        }
                        assert_ne!(
                            payload(&native, id),
                            payload(&reference, id),
                            "EOF divergence {name} {id:?}"
                        );
                    } else {
                        assert_eq!(
                            payload(&native, id),
                            payload(&reference, id),
                            "{name} {id:?}"
                        );
                    }
                }
                if !unknown && steps == 0 && !dynamic {
                    unlimited_native = native_bytes.clone();
                    if fixture == "ring-full" {
                        full_reference = c.stdout.clone();
                    }
                }
                let g = session.global_grid().unwrap();
                assert_eq!(g.state, FeatureState::Final);
                assert_eq!(g.coverage_range.end_frame, count as u64);
                assert_eq!(g.evidence_range, g.coverage_range);
                assert_eq!(g.requested_range.first_frame, 0);
                assert_eq!(g.requested_range.end_frame, count as u64);
                let first = g.segments[0].applicability_range.first_frame;
                let end = g.segments.last().unwrap().applicability_range.end_frame;
                let gap: u64 = g
                    .segments
                    .windows(2)
                    .map(|s| {
                        s[1].applicability_range.first_frame - s[0].applicability_range.end_frame
                    })
                    .sum();
                let m = session.meter().unwrap();
                let binds = g.segments.iter().any(|s| {
                    (0..s.beat_count).any(|i| {
                        s.beat_at(i).unwrap().is_some_and(|b| {
                            b.ordinal == m.downbeat_ordinal
                                && b.position.whole_frame == m.downbeat_frame
                        })
                    })
                });
                let r = session.grid_revision().unwrap();
                let expected_first = if fixture == "ring-partial" { 4096 } else { 0 };
                assert_eq!(g.evidence_range.first_frame, expected_first, "{name}");
                assert_eq!(first, expected_first, "{name}");
                assert_eq!(end, count as u64, "{name}");
                assert_eq!(g.segments.len(), if interior { 2 } else { 1 });
                assert_eq!(gap, if interior { 262144 } else { 0 });
                if interior {
                    assert_eq!(g.segments[0].applicability_range.end_frame, 262144);
                    assert_eq!(g.segments[1].applicability_range.first_frame, 524288);
                }
                assert_eq!(g.flags, if fixture == "interior-change" { 2 } else { 0 });
                assert!(!binds, "meter must not silently bind {name}");
                assert_eq!(
                    r.revision_id,
                    if interior {
                        6
                    } else if fixture == "ring-full" {
                        384
                    } else {
                        385
                    }
                );
                assert_eq!(r.previous_revision_id, r.revision_id - 1);
                let cg = libapta::grid::GridPayload::parse(
                    payload(&reference, *b"GGRD").unwrap(),
                    true,
                    Default::default(),
                )
                .unwrap();
                let cr = libapta::grid::parse_revision(
                    payload(&reference, *b"REVN").unwrap(),
                    &cg,
                    true,
                    true,
                )
                .unwrap();
                let row = format!("{fixture},{count},{unknown},{steps},{dynamic},{:?},{},{},{first},{end},{},{},{},{binds},{},{},{gap},{},{},{}\n",
                g.state, g.evidence_range.first_frame, g.evidence_range.end_frame, g.segments.len(),
                g.segments[0].nominal_tempo_millibpm, session.tempo().unwrap().selected.tempo_millibpm,
                r.revision_id, r.previous_revision_id, cg.evidence_range().first_frame, cg.evidence_range().end_frame, cr.revision_id);
                eprint!("{row}");
                rows.push_str(&row);
                (retained, retained_bytes)
            };
            assert_eq!(retained_wire(&retained), retained_bytes);
            if let Some(output) = &output {
                fs::write(output.join(format!("{name}-retained.apta")), retained_bytes).unwrap();
            }
        }
    }
    print!("{rows}");
    if let Some(output) = output {
        fs::write(output.join("coverage.csv"), rows).unwrap();
    }
    fs::remove_dir_all(temporary).unwrap();
}
