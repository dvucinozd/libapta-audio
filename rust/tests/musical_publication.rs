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
#[allow(clippy::drop_non_drop)] // Exercise lifetime after session/pool destruction.
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
    let mut independent = Buffers::new(10);
    let copied = owned.copy_to(independent.storage()).unwrap();
    assert_eq!(copied.info(), owned.info());
    assert_eq!(copied.available_features(), owned.available_features());
    assert_eq!(copied.changed_features(), owned.changed_features());
    drop(owned);
    drop(s);
    drop(pool);
    assert_eq!(serialize(&copied), native);
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

fn lifecycle_row(pool: &ResultPool<'_>, clock_calls: u64) -> Vec<u64> {
    let r = pool.acquire().unwrap();
    vec![
        r.info().generation,
        r.info().session_state as u64,
        r.available_features(),
        r.changed_features(),
        r.tempo()
            .map_or(0, |t| u64::from(t.selected.tempo_millibpm)),
        r.tempo().map_or(0, |t| t.selected.state as u64),
        clock_calls,
        r.meter().map_or(0, |m| m.downbeat_frame),
        r.meter().map_or(0, |m| m.downbeat_ordinal as u64),
        r.meter().map_or(0, |m| m.segments[0].first_frame),
        r.meter().map_or(0, |m| m.segments[0].end_frame),
        r.global_grid().map_or(0, |g| u64::from(g.flags)),
        r.global_grid().map_or(0, |g| g.segments.len() as u64),
        r.global_grid().map_or(0, |g| g.beats.len() as u64),
        0,
        0,
        0,
        0,
    ]
}
#[test]
#[ignore = "requires APTA_C_MUSICAL_LIFECYCLE_ORACLE"]
fn exact_intermediate_musical_generations_and_capability_masks_match_c() {
    let oracle = std::env::var_os("APTA_C_MUSICAL_LIFECYCLE_ORACLE").unwrap();
    for profile in (0..12)
        .chain(16..20)
        .chain(24..28)
        .chain(32..36)
        .chain(96..100)
        .chain(512..516)
        .chain(520..524)
        .chain(256..260)
        .chain(288..292)
        .chain([1024, 1026, 2048, 2050, 4096, 4098, 8194, 16386, 32770])
    {
        let count: usize = if profile & (8192 | 16384) != 0 {
            256 * 16400 + 17
        } else if profile & 32768 != 0 {
            256 * 4800 + 17
        } else {
            320000
        };
        let rate = if profile & 16384 != 0 { 2000 } else { 8000 };
        let mut features = result::WAVEFORM_OVERVIEW | result::BPM | result::LOCAL_BEATGRID;
        if profile & 1 != 0 {
            features |= result::CONFIDENCE | result::GRID_LOCKING;
        }
        if profile & 2 != 0 {
            features |= result::GLOBAL_BEATGRID
                | result::DYNAMIC_TEMPO
                | result::MUSICAL_KEY
                | result::METER_DOWNBEAT
                | result::CALIBRATED_QUALITY;
        }
        let mut a = Buffers::new(count.div_ceil(32768));
        let mut b = Buffers::new(count.div_ceil(32768));
        let mut input_source = source(count as u64);
        input_source.sample_rate = rate;
        if profile & 512 != 0 {
            input_source.total_frames = None;
        }
        let pool = ResultPool::new_with_requested_features(
            input_source,
            [a.storage(), b.storage()],
            NativeLimits::default(),
            features,
        )
        .unwrap();
        let mut rows = vec![];
        if profile & 32 != 0 {
            rows = scheduled_music_rows(profile, &pool);
        } else {
            let mut bins = vec![analysis::OnsetBin::default(); 4096];
            let mut flux = vec![0.0; 4096];
            let mut gb = vec![analysis::OnsetBin::default(); 16384];
            let mut gf = vec![0.0; 16384];
            let mut beats = vec![Beat::default(); 3072];
            let mut queue = vec![NormalizedSample::default(); 4096];
            let mut columns = vec![WaveformColumn::default(); count.div_ceil(32768)];
            let mut s = PublishedSession::new(
                SessionConfig {
                    sample_rate: rate,
                    channel_count: 1,
                    total_frames: if profile & 512 != 0 {
                        session::TOTAL_FRAMES_UNKNOWN
                    } else {
                        count as u64
                    },
                    frames_per_column: 32768,
                },
                &mut queue,
                &mut columns,
                &pool,
            )
            .unwrap();
            s.enable_tempo(&mut bins, &mut flux).unwrap();
            if profile & 2 != 0 {
                s.enable_global_grid(true, &mut gb, &mut gf, &mut beats)
                    .unwrap();
                s.enable_key().unwrap();
                s.enable_meter().unwrap();
                s.enable_calibrated_quality().unwrap();
            }
            assert_lock_request_preflight(&mut s, profile);
            let token = CancellationToken::new();
            let pcm: Vec<f32> = (0..count)
                .map(|frame| {
                    if profile & 1024 != 0 {
                        return 0.0;
                    }
                    if profile & 4096 != 0 {
                        return 0.25;
                    }
                    let tempo = if profile & 32768 != 0 {
                        [80, 160, 120, 200][frame / 65536 % 4]
                    } else {
                        120
                    };
                    let period = rate as usize * 60 / tempo;
                    let phase = frame % period;
                    let value = if phase < 64 {
                        (64 - phase) as f32 / 64.0
                            * if frame / period % 4 == 0 { 0.75 } else { 0.2 }
                    } else {
                        0.0
                    };
                    if profile & 2048 != 0 {
                        value * 1e-8
                    } else {
                        value
                    }
                })
                .collect();
            let mut clock_calls = 0;
            rows.push(lifecycle_row(&pool, clock_calls));
            let mut retained = if profile & 16 != 0 {
                Some(pool.acquire().unwrap())
            } else {
                None
            };
            for (index, block) in pcm.chunks(4096).enumerate() {
                s.push_pcm(PcmView::F32Interleaved(block)).unwrap();
                rows.push(lifecycle_row(&pool, clock_calls));
                if profile & 8 != 0 && (retained.is_some() || index * 4096 >= 163840) {
                    token.cancel();
                    let error = s.process(WorkBudget::default(), &token).unwrap_err();
                    rows.push(lifecycle_row(&pool, clock_calls));
                    if let Some(old) = retained.take() {
                        assert_eq!(error, Error::ResultSlotsExhausted);
                        assert_eq!(old.info().generation, 1);
                        assert_eq!(old.available_features(), 0);
                        drop(old);
                        assert_eq!(
                            s.process(WorkBudget::default(), &token),
                            Err(Error::Cancelled)
                        );
                        rows.push(lifecycle_row(&pool, clock_calls));
                    } else {
                        assert_eq!(error, Error::Cancelled);
                    }
                    break;
                }
                let work = if profile & (4 | 256) != 0 {
                    s.process_with_clock(
                        WorkBudget::default(),
                        if profile & 256 != 0 { 20 } else { 1000000 },
                        &mut || {
                            clock_calls += 1;
                            clock_calls * 1000
                        },
                        &token,
                    )
                } else {
                    s.process(WorkBudget::default(), &token)
                };
                rows.push(lifecycle_row(&pool, clock_calls));
                if work == Err(Error::ResultSlotsExhausted) && retained.is_some() {
                    let old = retained.take().unwrap();
                    assert_eq!(old.info().generation, 1);
                    assert_eq!(old.available_features(), 0);
                    drop(old);
                    s.process(WorkBudget::default(), &token).unwrap();
                    rows.push(lifecycle_row(&pool, clock_calls));
                } else {
                    work.unwrap();
                }
            }
            if profile & 8 == 0 {
                s.finish_input().unwrap();
                rows.push(lifecycle_row(&pool, clock_calls));
                for _ in 0..1000 {
                    if profile & (4 | 256) != 0 {
                        s.process_with_clock(
                            WorkBudget::default(),
                            if profile & 256 != 0 { 20 } else { 1000000 },
                            &mut || {
                                clock_calls += 1;
                                clock_calls * 1000
                            },
                            &token,
                        )
                        .unwrap();
                    } else {
                        s.process(WorkBudget::default(), &token).unwrap_or_else(|e| panic!("profile {profile} final {e:?} tempo {:?} grid {:?} global {:?} meter {:?}",s.session().tempo(),s.session().local_grid(),s.session().global_grid(),s.session().meter()));
                    }
                    rows.push(lifecycle_row(&pool, clock_calls));
                    if s.session().state() == SessionState::Complete {
                        break;
                    }
                }
                assert_eq!(s.session().state(), SessionState::Complete);
            }
        }
        let out = std::process::Command::new(&oracle)
            .arg(profile.to_string())
            .output()
            .unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        let expected: Vec<Vec<u64>> = std::str::from_utf8(&out.stdout)
            .unwrap()
            .lines()
            .map(|l| l.split_whitespace().map(|v| v.parse().unwrap()).collect())
            .collect();
        for (index, (actual, expected)) in rows.iter().zip(&expected).enumerate() {
            assert_eq!(actual, expected, "profile {profile} row {index}");
        }
        assert_eq!(rows.len(), expected.len());
    }
}

#[test]
fn integrated_feature_plan_counts_and_aggregate_attachment_are_atomic() {
    use libapta::publication::plan_features;
    let config = SessionConfig {
        sample_rate: 8000,
        channel_count: 1,
        total_frames: 128,
        frames_per_column: 32768,
    };
    let all = result::ALL_FEATURES;
    let p = plan_features(config, all, NativeLimits::default()).unwrap();
    assert_eq!(
        (p.band_sums, p.detail_cache_tiles, p.detail_output_columns),
        (1, 4, 256)
    );
    assert_eq!(
        (p.tempo_bins, p.global_bins, p.global_beats),
        (4096, 16384, 3072)
    );
    assert_eq!(
        (
            p.result_slot.tempo_candidates,
            p.result_slot.key_candidates,
            p.result_slot.meter_segments,
            p.result_slot.quality
        ),
        (3, 3, 1, 1)
    );
    assert_eq!(
        p.result_slot.retained_bytes,
        owned_result::retained_size(&p.result_slot).unwrap()
    );
    assert_eq!(p.result_pool_bytes, p.result_slot.retained_bytes * 2);
    assert!(p.optional_working_bytes > 4096 * core::mem::size_of::<analysis::OnsetBin>());
    assert_eq!(
        plan_features(
            config,
            all,
            NativeLimits {
                maximum_storage_bytes: p.result_slot.retained_bytes - 1,
                ..NativeLimits::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    assert!(plan_features(
        config,
        all,
        NativeLimits {
            maximum_storage_bytes: p.result_slot.retained_bytes,
            ..NativeLimits::default()
        }
    )
    .is_ok());
    assert_eq!(
        plan_features(
            config,
            all,
            NativeLimits {
                maximum_waveform_columns: 256,
                ..NativeLimits::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    for features in 0..=all {
        let coherent = libapta::publication::validate_requested_features(features);
        assert_eq!(
            plan_features(config, features, NativeLimits::default()).is_ok(),
            coherent.is_ok(),
            "features {features}"
        );
    }
    let features =
        result::WAVEFORM_OVERVIEW | result::BPM | result::LOCAL_BEATGRID | result::MUSICAL_KEY;
    let bytes = plan_features(config, features, NativeLimits::default())
        .unwrap()
        .result_slot
        .retained_bytes;
    for key_first in [false, true] {
        for fits in [false, true] {
            let limits = NativeLimits {
                maximum_storage_bytes: bytes - usize::from(!fits),
                ..NativeLimits::default()
            };
            let mut a = Buffers::new(1);
            let mut b = Buffers::new(1);
            let pool = ResultPool::new(source(128), [a.storage(), b.storage()], limits).unwrap();
            let mut queue = [NormalizedSample::default(); 128];
            let mut columns = [WaveformColumn::default(); 1];
            let mut bins = vec![analysis::OnsetBin::default(); 4096];
            let mut flux = vec![-7.0; 4096];
            {
                let mut s = PublishedSession::new(config, &mut queue, &mut columns, &pool).unwrap();
                let last = if key_first {
                    s.enable_key().unwrap();
                    s.enable_tempo(&mut bins, &mut flux)
                } else {
                    s.enable_tempo(&mut bins, &mut flux).unwrap();
                    s.enable_key()
                };
                assert_eq!(
                    last,
                    if fits {
                        Ok(())
                    } else {
                        Err(Error::LimitExceeded)
                    }
                );
                assert_eq!(s.session().state(), SessionState::Created);
                assert_eq!(pool.generation(), 1);
                s.push_pcm(PcmView::F32Interleaved(&[0.25; 128])).unwrap();
                s.finish_input().unwrap();
                s.process(WorkBudget::default(), &CancellationToken::new())
                    .unwrap();
                assert_eq!(s.session().state(), SessionState::Complete);
            }
            if key_first && !fits {
                assert!(flux.iter().all(|v| *v == -7.0));
            }
        }
    }
}

struct MusicalSource {
    pcm: Vec<f32>,
    reads: u64,
    first: u64,
    maximum: u32,
    releases: std::rc::Rc<std::cell::Cell<u64>>,
    release: Box<dyn FnMut()>,
}
impl pull::PullSource for MusicalSource {
    fn total_frames(&mut self) -> Option<u64> {
        Some(320000)
    }
    fn read_frames(&mut self, first: u64, maximum: u32) -> Result<pull::PullRead<'_>, Error> {
        self.reads += 1;
        self.first = first;
        self.maximum = maximum;
        Ok(pull::PullRead::Data(pull::PullBlock::new(
            first,
            PcmView::F32Interleaved(&self.pcm[first as usize..first as usize + maximum as usize]),
            &mut self.release,
        )))
    }
}
fn scheduled_music_rows(profile: u32, pool: &ResultPool<'_>) -> Vec<Vec<u64>> {
    let releases = std::rc::Rc::new(std::cell::Cell::new(0));
    let released = releases.clone();
    let pcm = (0..320000)
        .map(|frame| {
            let phase = frame % 4000;
            if phase < 64 {
                (64 - phase) as f32 / 64.0 * if frame / 4000 % 4 == 0 { 0.75 } else { 0.2 }
            } else {
                0.0
            }
        })
        .collect();
    let source = MusicalSource {
        pcm,
        reads: 0,
        first: 0,
        maximum: 0,
        releases,
        release: Box::new(move || released.set(released.get() + 1)),
    };
    let mut acc = vec![sparse::SparseAccumulator::default(); 10];
    let mut ranges = vec![FrameRange::default(); 100];
    let mut nodes = vec![sparse::QueuedBlock::default(); 2];
    let mut pcm = vec![NormalizedSample::default(); 8192];
    let mut spans = vec![WaveformSpan::default(); 10];
    let mut columns = vec![WaveformColumn::default(); 10];
    let mut requests = [scheduler::RequestSlot::default(); 16];
    let mut bins = vec![analysis::OnsetBin::default(); 4096];
    let mut flux = vec![0.0; 4096];
    let mut gb = vec![analysis::OnsetBin::default(); 16384];
    let mut gf = vec![0.0; 16384];
    let mut beats = vec![Beat::default(); 3072];
    let mut s = PublishedSparseSession::new_scheduled(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: 320000,
            frames_per_column: 32768,
        },
        sparse::Workspace {
            accumulators: &mut acc,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut columns,
        },
        pool,
        &mut requests,
    )
    .unwrap();
    s.enable_tempo(&mut bins, &mut flux).unwrap();
    if profile & 2 != 0 {
        s.enable_global_grid(true, &mut gb, &mut gf, &mut beats)
            .unwrap();
        s.enable_key().unwrap();
        s.enable_meter().unwrap();
        s.enable_calibrated_quality().unwrap();
    }
    assert_eq!(
        s.lock_grid_range(FrameRange::default()),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        s.lock_grid_range(FrameRange {
            first_frame: 0,
            end_frame: 1
        }),
        Err(if profile & 1 != 0 {
            Error::InvalidState
        } else {
            Error::Unsupported
        })
    );
    let mut s = sparse_pull::ScheduledPullSession::new(s, source).unwrap();
    if profile & 64 != 0 {
        s.set_focus(Focus {
            playhead_frame: 163840,
            lookahead_frames: 32768,
            feature_mask: result::BPM | result::LOCAL_BEATGRID,
            priority: 240,
            ..Focus::default()
        })
        .unwrap();
        let id = s
            .request_region(RegionRequest {
                range: FrameRange {
                    first_frame: 163840,
                    end_frame: 196608,
                },
                feature_mask: result::BPM | result::LOCAL_BEATGRID,
                soft_deadline_monotonic_ns: 0,
                priority: 96,
                request_id: 0,
            })
            .unwrap();
        let d = s.next_pcm_request().unwrap();
        assert_eq!((d.range.first_frame, d.request_token), (163840, id));
    }
    let mut clock_calls = 0;
    let mut rows = vec![lifecycle_row(pool, 0)];
    let token = CancellationToken::new();
    for step in 0..200 {
        let work = if profile & (4 | 256) != 0 {
            s.process_with_clock(
                WorkBudget::default(),
                if profile & 256 != 0 { 20 } else { 1000000 },
                &mut || {
                    clock_calls += 1;
                    clock_calls * 1000
                },
                &token,
            )
        } else {
            s.process(WorkBudget::default(), &token)
        };
        work.unwrap_or_else(|e| {
            panic!(
                "profile {profile} step {step}: {e:?} tempo {:?} grid {:?}",
                s.session().session().tempo(),
                s.session().session().local_grid()
            )
        });
        let mut row = lifecycle_row(pool, clock_calls);
        let src = s.source();
        row[14..].copy_from_slice(&[
            src.reads,
            src.releases.get(),
            src.first,
            u64::from(src.maximum),
        ]);
        rows.push(row);
        if s.session().session().state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(s.session().session().state(), SessionState::Complete);
    rows
}

fn assert_lock_request_preflight(s: &mut PublishedSession<'_, '_, '_>, profile: u32) {
    assert_eq!(
        s.lock_grid_range(FrameRange::default()),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        s.lock_grid_range(FrameRange {
            first_frame: 0,
            end_frame: 1
        }),
        Err(if profile & 1 != 0 {
            Error::InvalidState
        } else {
            Error::Unsupported
        })
    );
}
