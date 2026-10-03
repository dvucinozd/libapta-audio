// SPDX-License-Identifier: Apache-2.0
use libapta::{native_validation::validate, result::*, *};
fn r() -> FrameRange {
    FrameRange {
        first_frame: 0,
        end_frame: 1000,
    }
}
fn base() -> NativeResultInput<'static> {
    NativeResultInput {
        source: SourceInfo {
            sample_rate: 1000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(1000),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        info: NativeResultInfo::default(),
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "external",
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
                evidence_range: r(),
                applicability_range: r(),
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
fn segment() -> GridSegment {
    GridSegment {
        applicability_range: r(),
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod {
            whole_frames: 500,
            fraction_q32: 0,
        },
        beat_count: 2,
        nominal_tempo_millibpm: 120000,
        confidence: 255,
        state: FeatureState::Partial,
        flags: 0,
        segment_id: 1,
        revision: 1,
    }
}
fn grid<'a>(
    coverage: &'a [FrameRange],
    segments: &'a [GridSegment],
    beats: &'a [Beat],
) -> NativeGrid<'a> {
    NativeGrid {
        state: FeatureState::Final,
        confidence: 255,
        flags: 0,
        representation: if beats.is_empty() {
            GridRepresentation::Segments
        } else if segments.is_empty() {
            GridRepresentation::Explicit
        } else {
            GridRepresentation::Hybrid
        },
        requested_range: r(),
        evidence_range: r(),
        applicability_range: r(),
        coverage_ranges: coverage,
        segments,
        beats,
    }
}
#[test]
fn selected_only_tempo_and_unknown_confidence_are_native() {
    let i = base();
    let v = validate(&i, NativeLimits::default()).unwrap();
    assert_eq!(v.available_features, BPM | CONFIDENCE);
    assert_eq!(v.changed_features, v.available_features);
    assert!(libapta::tempo::tempo_payload_size(&i.tempo.unwrap()).is_err());
}
#[test]
fn provenance_state_feature_and_limits() {
    let mut i = base();
    i.provenance.origin = ProvenanceOrigin::NativeAnalysis;
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::Unsupported)
    );
    i.provenance.origin = ProvenanceOrigin::ExternalImport;
    i.provenance.source_name = "";
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    i = base();
    i.info.session_state = ResultSessionState::Created;
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    i = base();
    i.tempo = None;
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    i = base();
    i.tempo.as_mut().unwrap().selected.state = FeatureState::Partial;
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    i.info.session_state = ResultSessionState::AcceptingInput;
    assert!(validate(&i, NativeLimits::default()).is_ok());
    let limits = NativeLimits {
        maximum_tempo_candidates: 0,
        ..NativeLimits::default()
    };
    assert!(validate(&i, limits).is_ok());
}
#[test]
fn partial_key_only_without_waveform() {
    let mut i = base();
    i.tempo = None;
    i.info.session_state = ResultSessionState::Draining;
    i.key = Some(Key {
        state: FeatureState::Partial,
        confidence: 255,
        tonic: 0,
        mode: 1,
        tuning_offset_cents: 0,
        first_frame: 0,
        end_frame: 1000,
        candidates: &[],
    });
    assert_eq!(
        validate(&i, NativeLimits::default())
            .unwrap()
            .available_features,
        MUSICAL_KEY
    );
}
#[test]
fn multicoverage_and_segment_finality_follow_native_contract() {
    let coverage = [
        FrameRange {
            first_frame: 0,
            end_frame: 100,
        },
        FrameRange {
            first_frame: 500,
            end_frame: 600,
        },
    ];
    let segments = [segment()];
    let mut i = base();
    i.local_grid = Some(grid(&coverage, &segments, &[]));
    assert!(validate(&i, NativeLimits::default()).is_ok());
    let limits = NativeLimits {
        maximum_grid_coverage_ranges: 1,
        ..NativeLimits::default()
    };
    assert_eq!(validate(&i, limits), Err(Error::LimitExceeded));
    let coverage = [
        coverage[0],
        FrameRange {
            first_frame: 99,
            end_frame: 600,
        },
    ];
    i.local_grid = Some(grid(&coverage, &segments, &[]));
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
}
#[test]
fn fractional_only_period_is_supported() {
    let coverage = [r()];
    let s = [GridSegment {
        frames_per_beat: FramePeriod {
            whole_frames: 0,
            fraction_q32: 0x80000000,
        },
        ..segment()
    }];
    let mut i = base();
    i.source.sample_rate = 1;
    i.local_grid = Some(grid(&coverage, &s, &[]));
    assert!(validate(&i, NativeLimits::default()).is_ok());
}
#[test]
fn explicit_ordinal_period_and_hybrid_overflow() {
    let coverage = [r()];
    let beats = [
        Beat {
            position: FractionalFrame::default(),
            ordinal: 0,
            revision: 1,
            confidence: 255,
            ..Beat::default()
        },
        Beat {
            position: FractionalFrame {
                whole_frame: 500,
                fraction_q32: 0,
            },
            ordinal: 1,
            revision: 1,
            confidence: 255,
            ..Beat::default()
        },
    ];
    let mut i = base();
    i.local_grid = Some(grid(&coverage, &[], &beats));
    assert!(validate(&i, NativeLimits::default()).is_ok());
    let bad = [
        beats[0],
        Beat {
            ordinal: 2,
            ..beats[1]
        },
    ];
    i.local_grid = Some(grid(&coverage, &[], &bad));
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    let s = [GridSegment {
        anchor_ordinal: i64::MIN,
        frames_per_beat: FramePeriod {
            whole_frames: u64::MAX,
            fraction_q32: 0,
        },
        ..segment()
    }];
    let beats = [Beat {
        ordinal: i64::MAX,
        ..beats[0]
    }];
    i.local_grid = Some(grid(&coverage, &s, &beats));
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::LimitExceeded)
    );
}
#[test]
fn backing_aliases_and_unused_columns_follow_pointer_semantics() {
    let columns = [
        WaveformColumn {
            flags: 1,
            ..WaveformColumn::default()
        },
        WaveformColumn::default(),
    ];
    let spans = [
        WaveformSpan {
            first_frame: 0,
            end_frame: 500,
            first_column_index: 0,
            column_count: 1,
            data_column_offset: 0,
        },
        WaveformSpan {
            first_frame: 500,
            end_frame: 1000,
            first_column_index: 1,
            column_count: 1,
            data_column_offset: 0,
        },
    ];
    let mut i = base();
    i.tempo = None;
    i.overview = Some(NativeOverview {
        frames_per_column: 500,
        origin_frame: 0,
        state: FeatureState::Final,
        confidence: 255,
        spans: &spans,
        columns: &columns,
    });
    assert_eq!(
        validate(&i, NativeLimits::default())
            .unwrap()
            .available_features,
        WAVEFORM_OVERVIEW
    );
    let limits = NativeLimits {
        maximum_waveform_columns: 1,
        ..NativeLimits::default()
    };
    assert_eq!(validate(&i, limits), Err(Error::LimitExceeded));
}
#[test]
fn native_quality_preserves_input_order_but_requires_features() {
    let quality = [
        QualityRecord {
            feature: CONFIDENCE,
            calibration_model_id: 1,
            evidence_coverage_permille: 65535,
            confidence: 255,
            state: FeatureState::Partial,
            flags: 0,
        },
        QualityRecord {
            feature: BPM,
            calibration_model_id: 2,
            evidence_coverage_permille: 0,
            confidence: 0,
            state: FeatureState::Partial,
            flags: 0,
        },
    ];
    let mut i = base();
    i.info.session_state = ResultSessionState::AcceptingInput;
    i.quality = &quality;
    assert!(validate(&i, NativeLimits::default()).is_ok());
    i.tempo = None;
    assert_eq!(
        validate(&i, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
}

#[test]
fn unknown_duration_has_one_native_representation() {
    let mut input = base();
    input.source.total_frames = Some(u64::MAX);
    assert_eq!(
        validate(&input, NativeLimits::default()),
        Err(Error::InvalidArgument)
    );
    input.source.total_frames = None;
    assert!(validate(&input, NativeLimits::default()).is_ok());
}
