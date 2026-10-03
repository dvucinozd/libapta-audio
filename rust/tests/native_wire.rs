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
fn representable() -> NativeResultInput<'static> {
    let mut input = base();
    input.info.generation = 7;
    input.info.lineage_id_high = 9;
    input.overview = Some(NativeOverview {
        frames_per_column: 96000,
        origin_frame: 0,
        state: FeatureState::Final,
        confidence: 80,
        spans: &[WaveformSpan {
            first_frame: 0,
            end_frame: 96000,
            first_column_index: 0,
            column_count: 1,
            data_column_offset: 0,
        }],
        columns: &[COLUMN],
    });
    input.detail = Some(NativeDetail {
        tiles: &[TILE],
        columns: &[COLUMN],
    });
    input.metadata = Some(Metadata {
        producer_name: Some("native oracle"),
        comments: Some("Čuvaj izvor"),
        ..Default::default()
    });
    input.tempo = Some(TempoView {
        selected: TempoValue {
            state: FeatureState::Final,
            confidence: 80,
            flags: 0,
            tempo_millibpm: 120000,
            candidate_set_id: 1,
            evidence_range: RANGE,
            applicability_range: RANGE,
        },
        candidates: &[TempoCandidate {
            tempo_millibpm: 120000,
            score: 62000,
            confidence: 80,
            relation_to_selected: 0,
            flags: 0,
        }],
    });
    let grid = NativeGrid {
        state: FeatureState::Final,
        confidence: 80,
        flags: 0,
        representation: GridRepresentation::Segments,
        requested_range: RANGE,
        evidence_range: RANGE,
        applicability_range: RANGE,
        coverage_ranges: &[RANGE],
        segments: &[GridSegment {
            confidence: 80,
            ..SEGMENT
        }],
        beats: &[],
    };
    input.local_grid = Some(grid);
    input.global_grid = Some(grid);
    input.revision = Some(GridRevision {
        state: RevisionState::Applied,
        confidence: 80,
        flags: 0,
        revision_id: 1,
        previous_revision_id: 0,
        proposed_representation: GridRepresentation::Segments,
        proposed_segment_count: 1,
        proposed_beat_count: 0,
        affected_range: RANGE,
    });
    input.key = Some(Key {
        state: FeatureState::Final,
        confidence: 80,
        tonic: 9,
        mode: 2,
        tuning_offset_cents: -7,
        first_frame: 0,
        end_frame: 96000,
        candidates: &[KeyCandidate {
            tonic: 9,
            mode: 2,
            tuning_offset_cents: -7,
            score: 62000,
            confidence: 80,
        }],
    });
    input.meter = Some(Meter {
        state: FeatureState::Final,
        confidence: 80,
        numerator: 4,
        denominator: 4,
        downbeat_frame: 0,
        downbeat_ordinal: 0,
        segments: &[MeterSegment {
            confidence: 80,
            ..METER
        }],
    });
    input.quality = &[QualityRecord {
        confidence: 80,
        ..QUALITY
    }];
    input
}
fn tile_descriptor() -> WaveformTile<'static> {
    WaveformTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 256,
        first_column_index: 0,
        state: FeatureState::Final,
        confidence: 255,
        columns: &[],
    }
}
struct Sink {
    bytes: Vec<u8>,
    position: usize,
    flushed: bool,
}
impl libapta::stream::Output for Sink {
    fn write(&mut self, b: &[u8]) -> Result<usize, Error> {
        assert!(b.len() <= 144);
        let n = b.len().min(5);
        self.bytes
            .resize(self.bytes.len().max(self.position + n), 0);
        self.bytes[self.position..self.position + n].copy_from_slice(&b[..n]);
        self.position += n;
        Ok(n)
    }
    fn seek(&mut self, p: u64) -> Result<(), Error> {
        self.position = p as usize;
        Ok(())
    }
    fn flush(&mut self) -> Result<(), Error> {
        self.flushed = true;
        Ok(())
    }
}
fn owned_wire() -> Vec<u8> {
    let input = representable();
    let req = requirements(&input, NativeLimits::default()).unwrap();
    let mut buffers = Buffers::new(req);
    let owned = copy(&input, buffers.storage(), NativeLimits::default()).unwrap();
    let mut tiles = [tile_descriptor()];
    let native = owned.view();
    let wire = from_native(&native, &mut tiles, NativeLimits::default()).unwrap();
    let mut bytes = vec![0; serialized_size(&wire).unwrap()];
    write(&wire, &mut bytes, Limits::default()).unwrap();
    let parsed = parse(&bytes, Limits::default()).unwrap();
    assert_eq!(
        parsed.tempo.unwrap().selected(),
        owned.tempo().unwrap().selected
    );
    assert_eq!(
        parsed.local_grid.unwrap().segment,
        owned.local_grid().unwrap().segments[0]
    );
    assert_eq!(parsed.waveform.overview.column(0, 0), Some(COLUMN));
    assert!(
        parsed.global_grid.is_some()
            && parsed.key.is_some()
            && parsed.meter.is_some()
            && parsed.quality.is_some()
    );
    let mut sink = Sink {
        bytes: vec![],
        position: 0,
        flushed: false,
    };
    assert_eq!(
        libapta::stream_write::write(&wire, &mut sink, Limits::default()),
        Ok(bytes.len())
    );
    assert!(sink.flushed);
    assert_eq!(sink.bytes, bytes);
    bytes
}
#[test]
fn all_native_features_owned_to_buffer_and_stream() {
    owned_wire();
}
#[test]
fn native_only_representations_are_explicitly_rejected() {
    let mut tiles = [tile_descriptor()];
    assert_eq!(
        from_native(&base(), &mut tiles, NativeLimits::default()).unwrap_err(),
        Error::NotAvailable
    );
    let mut input = representable();
    input.tempo.as_mut().unwrap().candidates = &[];
    let mut tiles = [tile_descriptor()];
    assert_eq!(
        from_native(&input, &mut tiles, NativeLimits::default()).unwrap_err(),
        Error::Unsupported
    );
    let mut input = representable();
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
    input.local_grid.as_mut().unwrap().coverage_ranges = &coverage;
    let mut tiles = [tile_descriptor()];
    assert_eq!(
        from_native(&input, &mut tiles, NativeLimits::default()).unwrap_err(),
        Error::Unsupported
    );
    let mut input = representable();
    input.global_grid.as_mut().unwrap().coverage_ranges = &coverage;
    let mut tiles = [tile_descriptor()];
    assert_eq!(
        from_native(&input, &mut tiles, NativeLimits::default()).unwrap_err(),
        Error::Unsupported
    );
    let mut input = representable();
    let beats = [Beat {
        position: FractionalFrame::default(),
        ordinal: 0,
        revision: 1,
        flags: 0,
        confidence: 80,
    }];
    let grid = input.local_grid.as_mut().unwrap();
    grid.representation = GridRepresentation::Explicit;
    grid.segments = &[];
    grid.beats = &beats;
    let mut tiles = [tile_descriptor()];
    assert_eq!(
        from_native(&input, &mut tiles, NativeLimits::default()).unwrap_err(),
        Error::Unsupported
    );
    let input = representable();
    assert_eq!(
        from_native(&input, &mut [], NativeLimits::default()).unwrap_err(),
        Error::BufferTooSmall
    );
}
#[test]
fn revision_unknown_confidence_cannot_enter_wire_subset() {
    let mut input = representable();
    input.revision.as_mut().unwrap().confidence = 255;
    let mut tiles = [tile_descriptor()];
    assert!(libapta::native_validation::validate(&input, NativeLimits::default()).is_ok());
    assert_eq!(
        from_native(&input, &mut tiles, NativeLimits::default()).unwrap_err(),
        Error::Unsupported
    );
}
#[test]
fn quality_cannot_target_native_only_overview_confidence() {
    let mut input = representable();
    input.tempo = None;
    input.local_grid = None;
    input.global_grid = None;
    input.revision = None;
    input.key = None;
    input.meter = None;
    input.detail = None;
    input.quality = &[QualityRecord {
        feature: CONFIDENCE,
        ..QUALITY
    }];
    assert!(libapta::native_validation::validate(&input, NativeLimits::default()).is_ok());
    assert_eq!(
        from_native(&input, &mut [], NativeLimits::default()).unwrap_err(),
        Error::Unsupported
    );
}
#[test]
#[ignore = "requires APTA_C_CONTAINER_ORACLE"]
fn owned_native_buffer_and_stream_bytes_match_c() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    let bytes = owned_wire();
    let oracle = std::env::var_os("APTA_C_CONTAINER_ORACLE").expect("set APTA_C_CONTAINER_ORACLE");
    let mut child = Command::new(oracle)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    child.stdin.take().unwrap().write_all(&bytes).unwrap();
    let out = child.wait_with_output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(out.stdout, bytes);
}
