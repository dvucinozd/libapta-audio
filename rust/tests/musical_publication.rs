// SPDX-License-Identifier: Apache-2.0
//! Integrated musical publication, capacity failures and retained immutable data.
use libapta::{
    owned_result::{GridStorage, Storage},
    publication::{PublishedSession, PublishedSparseSession, ResultPool},
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    waveform::{NormalizedSample, PcmView},
    *,
};
fn segment() -> GridSegment {
    GridSegment {
        applicability_range: FrameRange::default(),
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod::default(),
        beat_count: 0,
        nominal_tempo_millibpm: 0,
        confidence: 0,
        state: FeatureState::Provisional,
        flags: 0,
        segment_id: 0,
        revision: 0,
    }
}
struct Buffers {
    spans: Vec<WaveformSpan>,
    columns: Vec<WaveformColumn>,
    tempo: Vec<TempoCandidate>,
    coverage: Vec<FrameRange>,
    local: Vec<GridSegment>,
    global_coverage: Vec<FrameRange>,
    global: Vec<GridSegment>,
    beats: Vec<Beat>,
    key: Vec<KeyCandidate>,
    meter: Vec<MeterSegment>,
    quality: Vec<QualityRecord>,
}
impl Buffers {
    fn new(columns: usize) -> Self {
        Self {
            spans: vec![WaveformSpan::default(); columns],
            columns: vec![WaveformColumn::default(); columns],
            tempo: vec![TempoCandidate::default(); 3],
            coverage: vec![FrameRange::default(); 1],
            local: vec![segment(); 1],
            global_coverage: vec![FrameRange::default(); 1],
            global: vec![segment(); 8],
            beats: vec![Beat::default(); 3072],
            key: vec![KeyCandidate::default(); 3],
            meter: vec![
                MeterSegment {
                    first_frame: 0,
                    end_frame: 0,
                    downbeat_frame: 0,
                    downbeat_ordinal: 0,
                    numerator: 4,
                    denominator: 4,
                    state: FeatureState::Provisional,
                    confidence: 0,
                    segment_id: 0
                };
                1
            ],
            quality: vec![
                QualityRecord {
                    feature: result::BPM,
                    calibration_model_id: 0,
                    evidence_coverage_permille: 0,
                    confidence: 0,
                    state: FeatureState::Provisional,
                    flags: 0
                };
                1
            ],
        }
    }
    fn storage(&mut self) -> Storage<'_> {
        Storage {
            overview_spans: &mut self.spans,
            overview_columns: &mut self.columns,
            tempo_candidates: &mut self.tempo,
            local_grid: GridStorage {
                coverage_ranges: &mut self.coverage,
                segments: &mut self.local,
                beats: &mut [],
            },
            global_grid: GridStorage {
                coverage_ranges: &mut self.global_coverage,
                segments: &mut self.global,
                beats: &mut self.beats,
            },
            key_candidates: &mut self.key,
            meter_segments: &mut self.meter,
            quality: &mut self.quality,
            ..Storage::default()
        }
    }
}
fn source(total: u64) -> SourceInfo {
    SourceInfo {
        sample_rate: 8000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(total),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    }
}
fn samples() -> Vec<f32> {
    (0..320000)
        .map(|i| {
            let phase = i % 4000;
            if phase < 64 {
                (64 - phase) as f32 / 64.0 * if i / 4000 % 4 == 0 { 0.75 } else { 0.2 }
            } else {
                0.05 * libm::sinf(i as f32 * 0.205)
            }
        })
        .collect()
}
fn serialize(result: &libapta::owned_result::OwnedResult<'_>) -> Vec<u8> {
    let mut tiles = [];
    let wire =
        libapta::result::from_session_result(result, &mut tiles, NativeLimits::default()).unwrap();
    let mut bytes = vec![0; libapta::result::serialized_size(&wire).unwrap()];
    libapta::result::write(&wire, &mut bytes, libapta::result::Limits::default()).unwrap();
    libapta::result::parse(&bytes, libapta::result::Limits::default()).unwrap();
    bytes
}
#[test]
fn sequential_musical_results_survive_exhaustion_retry_and_destruction() {
    let pcm = samples();
    let mut a = Buffers::new(10);
    let mut b = Buffers::new(10);
    let pool = ResultPool::new(
        source(320000),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut bins = vec![analysis::OnsetBin::default(); analysis::BIN_CAPACITY];
    let mut flux = vec![0.0; bins.len()];
    let mut global_bins = vec![analysis::OnsetBin::default(); global_analysis::BIN_CAPACITY];
    let mut global_flux = vec![0.0; global_bins.len()];
    let mut beats = vec![Beat::default(); 3072];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); 10];
    let config = SessionConfig {
        sample_rate: 8000,
        channel_count: 1,
        total_frames: 320000,
        frames_per_column: 32768,
    };
    let token = CancellationToken::new();
    let initial = pool.acquire().unwrap();
    let mut s = PublishedSession::new(config, &mut queue, &mut columns, &pool).unwrap();
    s.enable_tempo(&mut bins, &mut flux).unwrap();
    s.enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats)
        .unwrap();
    s.enable_key().unwrap();
    s.enable_meter().unwrap();
    s.enable_calibrated_quality().unwrap();
    s.push_pcm(PcmView::F32Interleaved(&pcm[..4096])).unwrap();
    s.process(WorkBudget::default(), &token)
        .unwrap_or_else(|e| {
            panic!(
                "{e:?} processed {} tempo {:?} local {:?} global {:?} meter {:?}",
                s.session().processed_frames(),
                s.session().tempo(),
                s.session().local_grid(),
                s.session().global_grid(),
                s.session().meter()
            )
        });
    assert!(initial.tempo().is_none());
    drop(initial);
    for block in pcm[4096..163840].chunks(4096) {
        s.push_pcm(PcmView::F32Interleaved(block)).unwrap();
        s.process(WorkBudget::default(), &token)
            .unwrap_or_else(|e| {
                panic!(
                    "{e:?} processed {} tempo {:?} local {:?} global {:?} meter {:?}",
                    s.session().processed_frames(),
                    s.session().tempo(),
                    s.session().local_grid(),
                    s.session().global_grid(),
                    s.session().meter()
                )
            });
    }
    let retained = pool.acquire().unwrap();
    assert!(retained.tempo().is_some());
    let before = serialize(&retained);
    let generation = retained.info().generation;
    let mut first = 163840;
    let mut exhausted = false;
    while first < pcm.len() {
        let end = (first + 4096).min(pcm.len());
        s.push_pcm(PcmView::F32Interleaved(&pcm[first..end]))
            .unwrap();
        first = end;
        match s.process(WorkBudget::default(), &token) {
            Err(Error::ResultSlotsExhausted) => {
                exhausted = true;
                break;
            }
            Ok(_) => (),
            Err(e) => panic!("{e:?}"),
        }
    }
    assert!(exhausted);
    assert_eq!(serialize(&retained), before);
    assert_eq!(retained.info().generation, generation);
    drop(retained);
    s.process(WorkBudget::default(), &token)
        .unwrap_or_else(|e| {
            panic!(
                "{e:?} processed {} tempo {:?} local {:?} global {:?} meter {:?}",
                s.session().processed_frames(),
                s.session().tempo(),
                s.session().local_grid(),
                s.session().global_grid(),
                s.session().meter()
            )
        });
    while first < pcm.len() {
        let end = (first + 4096).min(pcm.len());
        s.push_pcm(PcmView::F32Interleaved(&pcm[first..end]))
            .unwrap();
        s.process(WorkBudget::default(), &token)
            .unwrap_or_else(|e| {
                panic!(
                    "{e:?} processed {} tempo {:?} local {:?} global {:?} meter {:?}",
                    s.session().processed_frames(),
                    s.session().tempo(),
                    s.session().local_grid(),
                    s.session().global_grid(),
                    s.session().meter()
                )
            });
        first = end;
    }
    s.finish_input().unwrap();
    for _ in 0..1000 {
        s.process(
            WorkBudget {
                maximum_steps: 2,
                maximum_input_frames: 0,
            },
            &token,
        )
        .unwrap();
        if s.session().state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(s.session().state(), SessionState::Complete);
    let final_result = pool.acquire().unwrap();
    assert!(final_result.global_grid().is_some());
    assert!(final_result.key().is_some());
    assert!(final_result.meter().is_some());
    assert_eq!(final_result.quality().len(), 1);
    let bytes = serialize(&final_result);
    #[allow(clippy::drop_non_drop)]
    // Explicitly prove retained storage survives session destruction.
    drop(s);
    assert_eq!(serialize(&final_result), bytes);
}
#[test]
fn sparse_partial_evidence_completes_and_serializes_all_native_stages() {
    let pcm = samples();
    let mut a = Buffers::new(10);
    let mut b = Buffers::new(10);
    let pool = ResultPool::new(
        source(320000),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut bins = vec![analysis::OnsetBin::default(); analysis::BIN_CAPACITY];
    let mut flux = vec![0.0; bins.len()];
    let mut global_bins = vec![analysis::OnsetBin::default(); global_analysis::BIN_CAPACITY];
    let mut global_flux = vec![0.0; global_bins.len()];
    let mut beats = vec![Beat::default(); 3072];
    let mut accumulators = vec![sparse::SparseAccumulator::default(); 10];
    let mut ranges = vec![FrameRange::default(); 100];
    let mut nodes = vec![sparse::QueuedBlock::default(); 2];
    let mut queue = vec![NormalizedSample::default(); 8192];
    let mut spans = vec![WaveformSpan::default(); 10];
    let mut columns = vec![WaveformColumn::default(); 10];
    let mut s = PublishedSparseSession::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: 320000,
            frames_per_column: 32768,
        },
        sparse::Workspace {
            accumulators: &mut accumulators,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut queue,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut columns,
        },
        &pool,
    )
    .unwrap();
    s.enable_tempo(&mut bins, &mut flux).unwrap();
    s.enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats)
        .unwrap();
    s.enable_key().unwrap();
    s.enable_meter().unwrap();
    s.enable_calibrated_quality().unwrap();
    let token = CancellationToken::new();
    for first in (32768..294912).step_by(4096) {
        s.push_at(
            first as u64,
            PcmView::F32Interleaved(&pcm[first..first + 4096]),
        )
        .unwrap();
        s.process(WorkBudget::default(), &token)
            .unwrap_or_else(|e| {
                panic!(
                    "{e:?} processed {} tempo {:?} local {:?} global {:?} meter {:?}",
                    s.session().processed_frames(),
                    s.session().tempo(),
                    s.session().local_grid(),
                    s.session().global_grid(),
                    s.session().meter()
                )
            });
    }
    s.finish_input().unwrap();
    for _ in 0..1000 {
        s.process(
            WorkBudget {
                maximum_steps: 2,
                maximum_input_frames: 0,
            },
            &token,
        )
        .unwrap();
        if s.session().state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(s.session().state(), SessionState::Complete);
    let result = pool.acquire().unwrap();
    assert_eq!(result.overview().unwrap().state, FeatureState::Partial);
    assert_eq!(result.global_grid().unwrap().state, FeatureState::Final);
    assert_eq!(result.meter().unwrap().state, FeatureState::Final);
    serialize(&result);
}

#[test]
#[ignore = "requires unchanged compiled C oracle"]
fn integrated_all_stage_container_matches_c() {
    let pcm = samples();
    let path = std::env::temp_dir().join(format!("apta-integrated-{}.pcm", std::process::id()));
    let bytes: Vec<_> = pcm.iter().flat_map(|v| v.to_le_bytes()).collect();
    std::fs::write(&path, bytes).unwrap();
    let c = std::process::Command::new(std::env::var("APTA_C_TEMPO_ANALYSIS_ORACLE").unwrap())
        .args(["8000", "0", path.to_str().unwrap(), "all"])
        .output()
        .unwrap();
    std::fs::remove_file(path).unwrap();
    assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
    let mut a = Buffers::new(10);
    let mut b = Buffers::new(10);
    let pool = ResultPool::new(
        source(320000),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut bins = vec![analysis::OnsetBin::default(); analysis::BIN_CAPACITY];
    let mut flux = vec![0.0; bins.len()];
    let mut global_bins = vec![analysis::OnsetBin::default(); global_analysis::BIN_CAPACITY];
    let mut global_flux = vec![0.0; global_bins.len()];
    let mut beats = vec![Beat::default(); 3072];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); 10];
    let mut s = PublishedSession::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: 320000,
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
        &pool,
    )
    .unwrap();
    s.enable_tempo(&mut bins, &mut flux).unwrap();
    s.enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats)
        .unwrap();
    s.enable_key().unwrap();
    s.enable_meter().unwrap();
    s.enable_calibrated_quality().unwrap();
    let token = CancellationToken::new();
    for block in pcm.chunks(4096) {
        s.push_pcm(PcmView::F32Interleaved(block)).unwrap();
        s.process(WorkBudget::default(), &token).unwrap();
    }
    s.finish_input().unwrap();
    s.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(s.session().state(), SessionState::Complete);
    let owned = pool.acquire().unwrap();
    let native = serialize(&owned);
    assert_eq!(native, c.stdout);
}

#[test]
fn accepted_revision_survives_failed_publication_and_retry() {
    let pcm: Vec<f32> = (0..640000)
        .map(|i| {
            let period = if i < 320000 { 3840 } else { 6000 };
            let phase = i % period;
            if phase < 64 {
                (64 - phase) as f32 / 64.0 * 0.75
            } else {
                0.0
            }
        })
        .collect();
    let mut a = Buffers::new(20);
    let mut b = Buffers::new(20);
    let pool = ResultPool::new(
        source(640000),
        [a.storage(), b.storage()],
        NativeLimits::default(),
    )
    .unwrap();
    let mut bins = vec![analysis::OnsetBin::default(); analysis::BIN_CAPACITY];
    let mut flux = vec![0.0; bins.len()];
    let mut global_bins = vec![analysis::OnsetBin::default(); global_analysis::BIN_CAPACITY];
    let mut global_flux = vec![0.0; global_bins.len()];
    let mut beats = vec![Beat::default(); global_analysis::MAX_BEATS];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); 20];
    let mut s = PublishedSession::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: 640000,
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
        &pool,
    )
    .unwrap();
    s.enable_tempo(&mut bins, &mut flux).unwrap();
    s.enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats)
        .unwrap();
    let token = CancellationToken::new();
    let mut first = 0;
    let mut locked = false;
    while first < pcm.len() {
        let end = (first + 4096).min(pcm.len());
        s.push_pcm(PcmView::F32Interleaved(&pcm[first..end]))
            .unwrap();
        first = end;
        s.process(WorkBudget::default(), &token).unwrap();
        if !locked && first >= 320000 {
            s.lock_grid_range(FrameRange {
                first_frame: 0,
                end_frame: 311808,
            })
            .unwrap();
            locked = true;
        }
        if s.session()
            .grid_revision()
            .is_some_and(|r| r.state == RevisionState::Pending)
        {
            break;
        }
    }
    assert!(first < pcm.len(), "a running pending proposal is required");
    let old = pool.acquire().unwrap();
    let generation = pool.generation();
    while pool.generation() == generation {
        let end = (first + 4096).min(pcm.len());
        assert!(end > first);
        s.push_pcm(PcmView::F32Interleaved(&pcm[first..end]))
            .unwrap();
        first = end;
        match s.process(WorkBudget::default(), &token) {
            Ok(_) | Err(Error::ResultSlotsExhausted) => (),
            Err(e) => panic!("{e:?}"),
        }
    }
    let newer = pool.acquire().unwrap();
    let preserved = serialize(&newer);
    let id = s.session().grid_revision().unwrap().revision_id;
    assert_eq!(s.apply_grid_revision(id), Err(Error::ResultSlotsExhausted));
    assert_eq!(
        s.session().grid_revision().unwrap().state,
        RevisionState::Applied
    );
    assert_eq!(s.apply_grid_revision(id), Err(Error::InvalidState));
    assert_eq!(serialize(&newer), preserved);
    drop(old);
    s.process(
        WorkBudget {
            maximum_steps: 1,
            maximum_input_frames: 0,
        },
        &token,
    )
    .unwrap();
    assert_eq!(
        pool.acquire().unwrap().revision().unwrap().state,
        RevisionState::Applied
    );
    assert_eq!(serialize(&newer), preserved);
}

#[test]
fn musical_attachment_preflights_both_slots_without_mutating_working_storage() {
    for mode in 0..8 {
        let mut a = Buffers::new(1);
        let mut b = Buffers::new(1);
        match mode {
            0 => b.tempo.truncate(2),
            1 => b.coverage.clear(),
            2 => b.global.truncate(7),
            3 => b.beats.truncate(3071),
            4 => b.key.truncate(2),
            5 => b.meter.clear(),
            6 => b.quality.clear(),
            _ => (),
        }
        let limits = NativeLimits {
            maximum_key_candidates: if mode == 7 { 2 } else { 24 },
            ..NativeLimits::default()
        };
        let pool = ResultPool::new(source(128), [a.storage(), b.storage()], limits).unwrap();
        let mut bins = vec![analysis::OnsetBin::default(); analysis::BIN_CAPACITY];
        let mut flux = vec![-7.0; bins.len()];
        let mut global_bins = vec![analysis::OnsetBin::default(); global_analysis::BIN_CAPACITY];
        let mut global_flux = vec![-7.0; global_bins.len()];
        let mut beats = vec![Beat::default(); global_analysis::MAX_BEATS];
        let mut queue = [NormalizedSample::default(); 128];
        let mut columns = [WaveformColumn::default(); 1];
        {
            let mut s = PublishedSession::new(
                SessionConfig {
                    sample_rate: 8000,
                    channel_count: 1,
                    total_frames: 128,
                    frames_per_column: 32768,
                },
                &mut queue,
                &mut columns,
                &pool,
            )
            .unwrap();
            let result = match mode {
                0 | 1 => s.enable_tempo(&mut bins, &mut flux),
                2 | 3 => s.enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats),
                4 | 7 => s.enable_key(),
                5 | 6 => {
                    s.enable_tempo(&mut bins, &mut flux).unwrap();
                    if mode == 5 {
                        s.enable_meter()
                    } else {
                        s.enable_calibrated_quality()
                    }
                }
                _ => unreachable!(),
            };
            assert_eq!(
                result,
                Err(if mode == 7 {
                    Error::LimitExceeded
                } else {
                    Error::BufferTooSmall
                })
            );
            assert_eq!(s.session().state(), SessionState::Created);
            assert_eq!(pool.generation(), 1);
            s.push_pcm(PcmView::F32Interleaved(&[0.25; 128])).unwrap();
            s.finish_input().unwrap();
            s.process(WorkBudget::default(), &CancellationToken::new())
                .unwrap();
            assert_eq!(s.session().state(), SessionState::Complete);
            assert_eq!(pool.acquire().unwrap().overview().unwrap().columns.len(), 1);
        }
        if matches!(mode, 0 | 1) {
            assert!(flux.iter().all(|v| *v == -7.0));
        }
        if matches!(mode, 2 | 3) {
            assert!(global_flux.iter().all(|v| *v == -7.0));
        }
    }
}
