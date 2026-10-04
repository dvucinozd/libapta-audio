// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::{self, GridStorage, Storage},
    publication::{PublishedSession, ResultPool},
    session::{CancellationToken, SessionConfig, WorkBudget},
    waveform::{NormalizedSample, PcmView},
    *,
};
use libapta_runtime::HeapResult;
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
                0.05 * (i as f32 * 0.205).sin()
            }
        })
        .collect()
}

#[test]
fn complete_pcm_musical_graph_survives_heap_owner_boundary() {
    let (result, expected_tempo) = {
        let mut a = Buffers::new(10);
        let mut b = Buffers::new(10);
        let features = result::ALL_FEATURES & !(result::WAVEFORM_DETAIL | result::WAVEFORM_3BAND);
        let pool = ResultPool::new_with_requested_features(
            source(320000),
            [a.storage(), b.storage()],
            NativeLimits::default(),
            features,
        )
        .unwrap();
        let mut queue = vec![NormalizedSample::default(); 4096];
        let mut columns = vec![WaveformColumn::default(); 10];
        let mut bins = vec![analysis::OnsetBin::default(); 4096];
        let mut flux = vec![0.0; 4096];
        let mut global_bins = vec![analysis::OnsetBin::default(); 16384];
        let mut global_flux = vec![0.0; 16384];
        let mut beats = vec![Beat::default(); 3072];
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
        for block in samples().chunks(4096) {
            s.push_pcm(PcmView::F32Interleaved(block)).unwrap();
            s.process(WorkBudget::default(), &CancellationToken::new())
                .unwrap();
        }
        s.finish_input().unwrap();
        s.process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        let owned = pool.acquire().unwrap();
        let heap = HeapResult::copy_from(&owned, NativeLimits::default()).unwrap();
        let a = owned.view();
        let b = heap.view();
        assert_eq!(a.source, b.source);
        assert_eq!(a.info, b.info);
        assert_eq!(a.provenance, b.provenance);
        assert_eq!(a.overview, b.overview);
        assert_eq!(a.detail, b.detail);
        assert_eq!(a.metadata, b.metadata);
        assert_eq!(a.tempo, b.tempo);
        assert_eq!(a.local_grid, b.local_grid);
        assert_eq!(a.global_grid, b.global_grid);
        assert_eq!(a.revision, b.revision);
        assert_eq!(a.key, b.key);
        assert_eq!(a.meter, b.meter);
        assert_eq!(a.quality, b.quality);
        assert_eq!(heap.available_features(), owned.available_features());
        assert_eq!(heap.changed_features(), owned.changed_features());
        assert!(
            b.tempo.is_some()
                && b.global_grid.is_some()
                && b.key.is_some()
                && b.meter.is_some()
                && !b.quality.is_empty()
        );
        let expected_tempo = a.tempo.unwrap().selected.tempo_millibpm;
        (heap, expected_tempo)
    }; // Every core lease, array, pool and session was destroyed.
    let result = std::thread::spawn(move || {
        let v = result.view();
        assert_eq!(v.tempo.unwrap().selected.tempo_millibpm, expected_tempo);
        assert_eq!(v.quality[0].calibration_model_id, 1867860160);
        assert_eq!(v.overview.unwrap().columns.len(), 10);
        result
    })
    .join()
    .unwrap();
    assert_eq!(result.source().total_frames, Some(320000));
}

#[test]
fn metadata_opaque_identity_and_detail_are_fully_heap_owned() {
    for opaque in [false, true] {
        let heap = {
            let name = String::from("producer ž");
            let comments = String::from("immutable metadata");
            let bytes = vec![0xff, 0, 0xfe];
            let source_name = String::from("import");
            let version = String::from("1");
            let spans = [WaveformSpan {
                first_frame: 0,
                end_frame: 512,
                first_column_index: 0,
                column_count: 2,
                data_column_offset: 0,
            }];
            let columns = [WaveformColumn {
                flags: 1,
                ..WaveformColumn::default()
            }; 2];
            let tiles = [NativeTile {
                level_id: 1,
                tile_index: 0,
                first_frame: 0,
                end_frame: 512,
                first_column_index: 0,
                state: FeatureState::Final,
                confidence: 255,
                data_column_offset: 0,
                column_count: 2,
            }];
            let input = NativeResultInput {
                source: source(512),
                info: NativeResultInfo::default(),
                provenance: Provenance {
                    origin: ProvenanceOrigin::ExternalImport,
                    source_name: &source_name,
                    source_version: &version,
                },
                metadata: Some(Metadata {
                    producer_name: Some(&name),
                    producer_version_string: Some(""),
                    backend_name: None,
                    backend_version: Some("2"),
                    creation_unix_time: Some(123),
                    application_source_id: Some(if opaque {
                        SourceId::Bytes(&bytes)
                    } else {
                        SourceId::Text(&name)
                    }),
                    comments: Some(&comments),
                }),
                overview: Some(NativeOverview {
                    frames_per_column: 256,
                    origin_frame: 0,
                    state: FeatureState::Final,
                    confidence: 255,
                    spans: &spans,
                    columns: &columns,
                }),
                detail: Some(NativeDetail {
                    tiles: &tiles,
                    columns: &columns,
                }),
                tempo: None,
                local_grid: None,
                global_grid: None,
                revision: None,
                key: None,
                meter: None,
                quality: &[],
            };
            let mut copied_spans = [WaveformSpan::default(); 1];
            let mut copied_columns = columns;
            let mut copied_detail = columns;
            let mut copied_tiles = tiles;
            let mut text = [0; 256];
            let owned = owned_result::copy(
                &input,
                Storage {
                    overview_spans: &mut copied_spans,
                    overview_columns: &mut copied_columns,
                    detail_tiles: &mut copied_tiles,
                    detail_columns: &mut copied_detail,
                    text_bytes: &mut text,
                    ..Storage::default()
                },
                NativeLimits::default(),
            )
            .unwrap();
            let heap = HeapResult::copy_from(&owned, NativeLimits::default()).unwrap();
            assert_eq!(heap.view().metadata, input.metadata);
            heap
        };
        let v = heap.view();
        let m = v.metadata.unwrap();
        assert_eq!(m.producer_name, Some("producer ž"));
        assert_eq!(m.producer_version_string, Some(""));
        assert_eq!(m.backend_name, None);
        assert_eq!(m.comments, Some("immutable metadata"));
        assert_eq!(
            m.application_source_id,
            Some(if opaque {
                SourceId::Bytes(&[0xff, 0, 0xfe])
            } else {
                SourceId::Text("producer ž")
            })
        );
        assert_eq!(v.provenance.source_name, "import");
        assert_eq!(v.provenance.source_version, "1");
        assert_eq!(v.detail.unwrap().tiles[0].end_frame, 512);
        let mut tiles = [WaveformTile {
            level_id: 0,
            tile_index: 0,
            first_frame: 0,
            end_frame: 0,
            first_column_index: 0,
            state: FeatureState::Final,
            columns: &[],
            confidence: 255,
        }];
        let wire = result::from_native(&v, &mut tiles, NativeLimits::default()).unwrap();
        let mut data = vec![0; result::serialized_size(&wire).unwrap()];
        result::write(&wire, &mut data, result::Limits::default()).unwrap();
        result::parse(&data, result::Limits::default()).unwrap();
    }
}
