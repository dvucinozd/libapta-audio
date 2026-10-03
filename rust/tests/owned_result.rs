// SPDX-License-Identifier: Apache-2.0
use libapta::{owned_result::*, result::*, *};
const RANGE: FrameRange = FrameRange {
    first_frame: 0,
    end_frame: 96000,
};
const COLUMN: WaveformColumn = WaveformColumn {
    minimum: -5,
    maximum: 10,
    rms: 3,
    low: 1,
    mid: 2,
    high: 3,
    flags: 9,
};
const SEGMENT: GridSegment = GridSegment {
    applicability_range: RANGE,
    anchor_position: FractionalFrame {
        whole_frame: 0,
        fraction_q32: 0,
    },
    anchor_ordinal: 0,
    frames_per_beat: FramePeriod {
        whole_frames: 24000,
        fraction_q32: 0,
    },
    beat_count: 4,
    nominal_tempo_millibpm: 120000,
    confidence: 255,
    state: FeatureState::Final,
    flags: 0,
    segment_id: 1,
    revision: 1,
};
const TILE: NativeTile = NativeTile {
    level_id: 1,
    tile_index: 0,
    first_frame: 0,
    end_frame: 256,
    first_column_index: 0,
    state: FeatureState::Final,
    confidence: 255,
    data_column_offset: 0,
    column_count: 1,
};
const METER: MeterSegment = MeterSegment {
    first_frame: 0,
    end_frame: 96000,
    downbeat_frame: 0,
    downbeat_ordinal: 0,
    numerator: 4,
    denominator: 4,
    state: FeatureState::Final,
    confidence: 255,
    segment_id: 1,
};
const QUALITY: QualityRecord = QualityRecord {
    feature: MUSICAL_KEY,
    calibration_model_id: 7,
    evidence_coverage_permille: 65535,
    confidence: 255,
    state: FeatureState::Final,
    flags: 0,
};
fn base() -> NativeResultInput<'static> {
    NativeResultInput {
        source: SourceInfo {
            sample_rate: 48000,
            channel_count: 2,
            channel_layout: 2,
            total_frames: Some(96000),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        info: NativeResultInfo::default(),
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "import",
            source_version: "1",
        },
        overview: None,
        detail: None,
        metadata: None,
        tempo: Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Final,
                confidence: 255,
                flags: 0,
                tempo_millibpm: 120000,
                candidate_set_id: 0,
                evidence_range: RANGE,
                applicability_range: RANGE,
            },
            candidates: &[],
        }),
        local_grid: None,
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    }
}
struct Buffers {
    spans: Vec<WaveformSpan>,
    columns: Vec<WaveformColumn>,
    tiles: Vec<NativeTile>,
    detail: Vec<WaveformColumn>,
    tempo: Vec<TempoCandidate>,
    lc: Vec<FrameRange>,
    ls: Vec<GridSegment>,
    lb: Vec<Beat>,
    gc: Vec<FrameRange>,
    gs: Vec<GridSegment>,
    gb: Vec<Beat>,
    key: Vec<KeyCandidate>,
    meter: Vec<MeterSegment>,
    quality: Vec<QualityRecord>,
    text: Vec<u8>,
}
impl Buffers {
    fn new(r: Requirements) -> Self {
        Self {
            spans: vec![WaveformSpan::default(); r.overview_spans],
            columns: vec![COLUMN; r.overview_columns],
            tiles: vec![TILE; r.detail_tiles],
            detail: vec![COLUMN; r.detail_columns],
            tempo: vec![TempoCandidate::default(); r.tempo_candidates],
            lc: vec![RANGE; r.local_grid.coverage_ranges],
            ls: vec![SEGMENT; r.local_grid.segments],
            lb: vec![Beat::default(); r.local_grid.beats],
            gc: vec![RANGE; r.global_grid.coverage_ranges],
            gs: vec![SEGMENT; r.global_grid.segments],
            gb: vec![Beat::default(); r.global_grid.beats],
            key: vec![KeyCandidate::default(); r.key_candidates],
            meter: vec![METER; r.meter_segments],
            quality: vec![QUALITY; r.quality],
            text: vec![0xa5; r.text_bytes],
        }
    }
    fn storage(&mut self) -> Storage<'_> {
        Storage {
            overview_spans: &mut self.spans,
            overview_columns: &mut self.columns,
            detail_tiles: &mut self.tiles,
            detail_columns: &mut self.detail,
            tempo_candidates: &mut self.tempo,
            local_grid: GridStorage {
                coverage_ranges: &mut self.lc,
                segments: &mut self.ls,
                beats: &mut self.lb,
            },
            global_grid: GridStorage {
                coverage_ranges: &mut self.gc,
                segments: &mut self.gs,
                beats: &mut self.gb,
            },
            key_candidates: &mut self.key,
            meter_segments: &mut self.meter,
            quality: &mut self.quality,
            text_bytes: &mut self.text,
        }
    }
}
#[test]
fn selected_only_tempo_unknown_confidence_and_repeated_copies() {
    let input = base();
    let r = requirements(&input, NativeLimits::default()).unwrap();
    assert_eq!(r.tempo_candidates, 0);
    let mut buffers = Buffers::new(r);
    let owned = copy(&input, buffers.storage(), NativeLimits::default()).unwrap();
    assert!(owned.overview().is_none());
    assert_eq!(owned.tempo().unwrap().selected.confidence, 255);
    assert!(owned.tempo().unwrap().candidates.is_empty());
    assert_eq!(owned.available_features(), BPM | CONFIDENCE);
    assert_eq!(owned.changed_features(), BPM | CONFIDENCE);
    let mut second = Buffers::new(r);
    let copied = copy(&owned.view(), second.storage(), NativeLimits::default()).unwrap();
    assert_eq!(copied.tempo(), owned.tempo());
    assert_eq!(copied.provenance(), owned.provenance());
    assert_eq!(copied.requirements(), owned.requirements());
    let storage = owned.into_storage();
    storage.text_bytes.fill(0);
    assert_eq!(copied.provenance().source_name, "import");
    assert_eq!(copied.info(), input.info);
}
#[test]
fn all_typed_arrays_and_text_survive_source_drop() {
    let spans = [WaveformSpan {
        first_frame: 0,
        end_frame: 96000,
        first_column_index: 0,
        column_count: 1,
        data_column_offset: 0,
    }];
    let columns = [COLUMN];
    let tiles = [TILE];
    let coverage = [
        FrameRange {
            first_frame: 0,
            end_frame: 48000,
        },
        FrameRange {
            first_frame: 48000,
            end_frame: 96000,
        },
    ];
    let segments = [SEGMENT];
    let beats = [
        Beat {
            position: FractionalFrame {
                whole_frame: 0,
                fraction_q32: 0,
            },
            ordinal: 0,
            revision: 1,
            flags: 0,
            confidence: 255,
        },
        Beat {
            position: FractionalFrame {
                whole_frame: 24000,
                fraction_q32: 0,
            },
            ordinal: 1,
            revision: 1,
            flags: 0,
            confidence: 255,
        },
    ];
    let key = [KeyCandidate {
        tonic: 9,
        mode: 2,
        tuning_offset_cents: -7,
        score: 62000,
        confidence: 255,
    }];
    let meter = [METER];
    let quality = [
        QUALITY,
        QualityRecord {
            feature: WAVEFORM_OVERVIEW,
            ..QUALITY
        },
    ];
    let grid = NativeGrid {
        state: FeatureState::Final,
        confidence: 255,
        flags: 0,
        representation: GridRepresentation::Hybrid,
        requested_range: RANGE,
        evidence_range: RANGE,
        applicability_range: RANGE,
        coverage_ranges: &coverage,
        segments: &segments,
        beats: &beats,
    };
    let mut input = base();
    input.overview = Some(NativeOverview {
        frames_per_column: 96000,
        origin_frame: 0,
        state: FeatureState::Final,
        confidence: 255,
        spans: &spans,
        columns: &columns,
    });
    input.detail = Some(NativeDetail {
        tiles: &tiles,
        columns: &columns,
    });
    input.local_grid = Some(grid);
    input.global_grid = Some(grid);
    input.revision = Some(GridRevision {
        state: RevisionState::Applied,
        confidence: 255,
        flags: 0,
        revision_id: 1,
        previous_revision_id: 0,
        proposed_representation: GridRepresentation::Hybrid,
        proposed_segment_count: 1,
        proposed_beat_count: 2,
        affected_range: RANGE,
    });
    input.key = Some(Key {
        state: FeatureState::Final,
        confidence: 255,
        tonic: 9,
        mode: 2,
        tuning_offset_cents: -7,
        first_frame: 0,
        end_frame: 96000,
        candidates: &key,
    });
    input.meter = Some(Meter {
        state: FeatureState::Final,
        confidence: 255,
        numerator: 4,
        denominator: 4,
        downbeat_frame: 0,
        downbeat_ordinal: 0,
        segments: &meter,
    });
    input.quality = &quality;
    let producer = String::from("čćž 😀");
    let source_id = vec![0, 255, 0];
    let provenance = String::from("External native import");
    input.metadata = Some(Metadata {
        producer_name: Some(&producer),
        producer_version_string: Some(""),
        backend_name: None,
        backend_version: Some("x"),
        creation_unix_time: Some(u64::MAX),
        application_source_id: Some(SourceId::Bytes(&source_id)),
        comments: Some("comment"),
    });
    input.provenance.source_name = &provenance;
    let r = requirements(&input, NativeLimits::default()).unwrap();
    let mut buffers = Buffers::new(r);
    let owned = copy(&input, buffers.storage(), NativeLimits::default()).unwrap();
    drop(producer);
    drop(source_id);
    drop(provenance);
    std::thread::scope(|scope| {
        let first = scope.spawn(|| owned.metadata().unwrap().producer_name.unwrap());
        let second = scope.spawn(|| owned.provenance().source_name);
        assert_eq!(first.join().unwrap(), "čćž 😀");
        assert_eq!(second.join().unwrap(), "External native import");
    });
    assert_eq!(owned.overview().unwrap().columns, &columns);
    assert_eq!(owned.detail().unwrap().tiles, &tiles);
    assert_eq!(owned.local_grid().unwrap().coverage_ranges, &coverage);
    assert_eq!(owned.global_grid().unwrap().beats, &beats);
    assert_eq!(owned.key().unwrap().candidates, &key);
    assert_eq!(owned.meter().unwrap().segments, &meter);
    assert_eq!(owned.quality(), &quality);
    assert_eq!(owned.metadata().unwrap().producer_name, Some("čćž 😀"));
    assert_eq!(owned.metadata().unwrap().producer_version_string, Some(""));
    assert_eq!(owned.metadata().unwrap().backend_name, None);
    assert_eq!(
        owned.metadata().unwrap().application_source_id,
        Some(SourceId::Bytes(&[0, 255, 0]))
    );
    assert_eq!(owned.provenance().source_name, "External native import");
    assert_eq!(owned.info().generation, 1);
    assert_eq!(owned.changed_features(), owned.available_features());
    let mut other = Buffers::new(r);
    let second = copy(&owned.view(), other.storage(), NativeLimits::default()).unwrap();
    assert_eq!(second.requirements(), r);
    assert_eq!(second.local_grid(), owned.local_grid());
    assert_eq!(second.metadata(), owned.metadata());
}
#[test]
fn capacity_validation_is_transactional_and_accounting_excludes_spare_capacity() {
    let input = base();
    let r = requirements(&input, NativeLimits::default()).unwrap();
    assert_eq!(
        requirements(
            &input,
            NativeLimits {
                maximum_storage_bytes: r.retained_bytes - 1,
                ..Default::default()
            }
        )
        .unwrap_err(),
        Error::LimitExceeded
    );
    assert!(requirements(
        &input,
        NativeLimits {
            maximum_storage_bytes: r.retained_bytes,
            ..Default::default()
        }
    )
    .is_ok());
    let mut bytes = vec![0xa5; r.text_bytes - 1];
    assert_eq!(
        copy(
            &input,
            Storage {
                text_bytes: &mut bytes,
                ..Default::default()
            },
            NativeLimits::default()
        )
        .unwrap_err(),
        Error::BufferTooSmall
    );
    assert!(bytes.iter().all(|b| *b == 0xa5));
    let mut buffers = Buffers::new(r);
    buffers.text.resize(r.text_bytes + 10, 0xa5);
    let owned = copy(
        &input,
        buffers.storage(),
        NativeLimits {
            maximum_storage_bytes: r.retained_bytes,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(owned.requirements(), r);
    let storage = owned.into_storage();
    assert_eq!(storage.text_bytes.len(), r.text_bytes + 10);
    assert_eq!(&storage.text_bytes[r.text_bytes..], &[0xa5; 10]);
    let mut bad = input;
    bad.source.sample_rate = 0;
    assert_eq!(
        copy(
            &bad,
            Storage {
                text_bytes: storage.text_bytes,
                ..Default::default()
            },
            NativeLimits::default()
        )
        .unwrap_err(),
        Error::InvalidArgument
    );
}
#[test]
fn metadata_absence_empty_values_and_text_source_id_are_distinct() {
    for metadata in [
        None,
        Some(Metadata::default()),
        Some(Metadata {
            application_source_id: Some(SourceId::Text("")),
            ..Default::default()
        }),
        Some(Metadata {
            application_source_id: Some(SourceId::Bytes(&[])),
            ..Default::default()
        }),
    ] {
        let input = NativeResultInput { metadata, ..base() };
        let mut buffers = Buffers::new(requirements(&input, NativeLimits::default()).unwrap());
        let owned = copy(&input, buffers.storage(), NativeLimits::default()).unwrap();
        assert_eq!(owned.metadata(), metadata);
    }
}

#[test]
fn waveform_aliases_are_duplicated_and_unused_backing_is_not_retained() {
    let mut input = base();
    input.tempo = None;
    let mut columns = [COLUMN; 10];
    columns[7].maximum = 123;
    let spans = [
        WaveformSpan {
            first_frame: 0,
            end_frame: 48000,
            first_column_index: 0,
            column_count: 1,
            data_column_offset: 7,
        },
        WaveformSpan {
            first_frame: 48000,
            end_frame: 96000,
            first_column_index: 1,
            column_count: 1,
            data_column_offset: 7,
        },
    ];
    input.overview = Some(NativeOverview {
        frames_per_column: 48000,
        origin_frame: 0,
        state: FeatureState::Final,
        confidence: 255,
        spans: &spans,
        columns: &columns,
    });
    let tiles = [
        NativeTile {
            data_column_offset: 7,
            ..TILE
        },
        NativeTile {
            tile_index: 1,
            first_frame: 16384,
            end_frame: 16640,
            first_column_index: 64,
            data_column_offset: 7,
            ..TILE
        },
    ];
    input.detail = Some(NativeDetail {
        tiles: &tiles,
        columns: &columns,
    });
    let req = requirements(&input, NativeLimits::default()).unwrap();
    assert_eq!((req.overview_columns, req.detail_columns), (2, 2));
    let mut storage = Buffers::new(req);
    let owned = copy(
        &input,
        storage.storage(),
        NativeLimits {
            maximum_storage_bytes: req.retained_bytes,
            ..Default::default()
        },
    )
    .unwrap();
    columns[7].maximum = 3;
    let overview = owned.overview().unwrap();
    assert_eq!(overview.columns.len(), 2);
    assert_eq!(overview.columns[0].maximum, 123);
    assert_eq!(overview.columns[1].maximum, 123);
    assert_eq!(overview.spans[0].data_column_offset, 0);
    assert_eq!(overview.spans[1].data_column_offset, 1);
    let detail = owned.detail().unwrap();
    assert_eq!(detail.columns.len(), 2);
    assert_eq!(detail.columns[0].maximum, 123);
    assert_eq!(detail.tiles[0].data_column_offset, 0);
    assert_eq!(detail.tiles[1].data_column_offset, 1);
    assert_eq!(columns[7].maximum, 3);
    assert_eq!(
        requirements(&owned.view(), NativeLimits::default()).unwrap(),
        req
    );
}

#[test]
fn every_capacity_is_checked_before_any_copy_and_shared_reads_are_safe() {
    fn send_sync<T: Send + Sync>() {}
    send_sync::<OwnedResult<'_>>();
    let candidate = [KeyCandidate {
        tonic: 9,
        mode: 2,
        tuning_offset_cents: -7,
        score: 30000,
        confidence: 255,
    }];
    let quality = [QUALITY];
    let mut input = base();
    input.key = Some(Key {
        state: FeatureState::Final,
        confidence: 255,
        tonic: 9,
        mode: 2,
        tuning_offset_cents: -7,
        first_frame: 0,
        end_frame: 96000,
        candidates: &candidate,
    });
    input.quality = &quality;
    let mut keys = [KeyCandidate::default()];
    let mut text = [0xa5; 100];
    assert_eq!(
        copy(
            &input,
            Storage {
                key_candidates: &mut keys,
                text_bytes: &mut text,
                ..Default::default()
            },
            NativeLimits::default()
        )
        .unwrap_err(),
        Error::BufferTooSmall
    );
    assert_eq!(keys, [KeyCandidate::default()]);
    assert_eq!(text, [0xa5; 100]);
    let mut buffers = Buffers::new(requirements(&input, NativeLimits::default()).unwrap());
    let owned = copy(&input, buffers.storage(), NativeLimits::default()).unwrap();
    std::thread::scope(|scope| {
        let first = scope.spawn(|| owned.key().unwrap().candidates[0]);
        let second = scope.spawn(|| owned.provenance().source_name);
        assert_eq!(first.join().unwrap(), candidate[0]);
        assert_eq!(second.join().unwrap(), "import");
    });
}

#[test]
fn replacement_is_transactional_and_reuses_original_capacity() {
    let input = base();
    let req = requirements(&input, NativeLimits::default()).unwrap();
    let mut buffers = Buffers::new(req);
    buffers.text.resize(80, 0xa5);
    let mut owned = copy(&input, buffers.storage(), NativeLimits::default()).unwrap();
    let before_tempo = owned.tempo().unwrap().selected;
    let before_info = owned.info();
    let before_bytes = owned.provenance().source_name.as_bytes().to_vec();
    let mut bad = base();
    bad.source.sample_rate = 0;
    assert_eq!(
        owned.replace(&bad, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    let candidates = [TempoCandidate {
        tempo_millibpm: 120000,
        score: 1,
        confidence: 100,
        relation_to_selected: 0,
        flags: 0,
    }];
    let mut too_large = base();
    too_large.tempo.as_mut().unwrap().candidates = &candidates;
    assert_eq!(
        owned.replace(&too_large, NativeLimits::default()),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(
        owned.replace(
            &input,
            NativeLimits {
                maximum_storage_bytes: req.retained_bytes - 1,
                ..Default::default()
            }
        ),
        Err(Error::LimitExceeded)
    );
    assert_eq!(owned.tempo().unwrap().selected, before_tempo);
    assert_eq!(owned.info(), before_info);
    assert_eq!(owned.provenance().source_name.as_bytes(), before_bytes);
    let recovered = owned.into_storage();
    let mut expected = [0xa5; 80];
    expected[..7].copy_from_slice(b"import1");
    assert_eq!(recovered.text_bytes, &expected);
    let mut owned = copy(&input, recovered, NativeLimits::default()).unwrap();
    let mut updated = base();
    updated.info.generation = 2;
    updated.provenance.source_name = "Updated import";
    updated.tempo.as_mut().unwrap().selected.confidence = 80;
    owned.replace(&updated, NativeLimits::default()).unwrap();
    assert_eq!(owned.info().generation, 2);
    assert_eq!(owned.tempo().unwrap().selected.confidence, 80);
    assert_eq!(owned.provenance().source_name, "Updated import");
    assert_eq!(owned.into_storage().text_bytes.len(), 80);
}
