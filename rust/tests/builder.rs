// SPDX-License-Identifier: Apache-2.0
use libapta::{
    builder,
    result::{Limits, ResultInput},
    *,
};
fn input<'a>(spans: &'a [WaveformSpan], columns: &'a [WaveformColumn]) -> ResultInput<'a> {
    ResultInput {
        source: SourceInfo {
            sample_rate: 1000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(1000),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        overview: WaveformOverview {
            level_id: 0,
            frames_per_column: 1000,
            origin_frame: 0,
            logical_column_count: 1,
            state: FeatureState::Final,
            spans,
            columns,
        },
        tiles: &[],
        metadata: None,
        tempo: None,
        local_grid: None,
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    }
}
fn span() -> WaveformSpan {
    WaveformSpan {
        first_frame: 0,
        end_frame: 1000,
        first_column_index: 0,
        column_count: 1,
        data_column_offset: 0,
    }
}
fn column() -> WaveformColumn {
    WaveformColumn {
        minimum: -1,
        maximum: 1,
        flags: 1,
        ..WaveformColumn::default()
    }
}
fn tempo() -> TempoView<'static> {
    TempoView {
        selected: TempoValue {
            state: FeatureState::Final,
            confidence: 80,
            flags: 0,
            tempo_millibpm: 120000,
            candidate_set_id: 0,
            evidence_range: r(),
            applicability_range: r(),
        },
        candidates: &[TempoCandidate {
            tempo_millibpm: 120000,
            score: 100,
            confidence: 80,
            relation_to_selected: 0,
            flags: 0,
        }],
    }
}
fn r() -> FrameRange {
    FrameRange {
        first_frame: 0,
        end_frame: 1000,
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
        confidence: 80,
        state: FeatureState::Final,
        flags: 0,
        segment_id: 1,
        revision: 1,
    }
}
fn global(s: &[GridSegment]) -> GlobalGrid<'_> {
    GlobalGrid {
        state: FeatureState::Final,
        confidence: 80,
        flags: 0,
        representation: GridRepresentation::Segments,
        requested_range: r(),
        evidence_range: r(),
        applicability_range: r(),
        coverage_range: r(),
        segments: s,
        beats: &[],
    }
}
fn revision() -> GridRevision {
    GridRevision {
        state: RevisionState::Applied,
        confidence: 80,
        flags: 0,
        revision_id: 1,
        previous_revision_id: 0,
        proposed_representation: GridRepresentation::Segments,
        proposed_segment_count: 1,
        proposed_beat_count: 0,
        affected_range: r(),
    }
}
#[test]
fn finalized_storage_is_independent_of_input() {
    let spans = [span()];
    let mut columns = [column()];
    let mut bytes = [0; 256];
    let result =
        builder::finalize(&input(&spans, &columns), &mut bytes, Limits::default()).unwrap();
    columns[0].maximum = 20;
    assert_eq!(result.waveform.overview.column(0, 0).unwrap().maximum, 1);
    assert_eq!(columns[0].maximum, 20);
}
#[test]
fn source_geometry_flags_and_limits() {
    let spans = [span()];
    let columns = [column()];
    let mut i = input(&spans, &columns);
    i.source.channel_layout = 2;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    i.source.channel_layout = 1;
    i.source.sample_rate = 768001;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    i.source.sample_rate = 1000;
    i.overview.level_id = 1;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    i.overview.level_id = 0;
    let mut limits = Limits::default();
    limits.container.maximum_file_bytes = 1;
    assert_eq!(builder::validate(&i, limits), Err(Error::LimitExceeded));
    let bad = [WaveformColumn {
        flags: 0x81,
        ..column()
    }];
    i.overview.columns = &bad;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::Unsupported)
    );
}
#[test]
fn grid_tempo_modifier_and_revision_coherence() {
    let spans = [span()];
    let columns = [column()];
    let s = [segment()];
    let mut i = input(&spans, &columns);
    i.tempo = Some(tempo());
    i.global_grid = Some(global(&s));
    i.revision = Some(revision());
    assert!(builder::validate(&i, Limits::default()).is_ok());
    let mut bytes = [0; 1024];
    assert!(builder::finalize(&i, &mut bytes, Limits::default()).is_ok());
    let s = [GridSegment {
        frames_per_beat: FramePeriod {
            whole_frames: 499,
            fraction_q32: 0,
        },
        ..segment()
    }];
    i.global_grid = Some(global(&s));
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    let s = [GridSegment {
        flags: 2,
        ..segment()
    }];
    i.global_grid = Some(global(&s));
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    let s = [GridSegment {
        flags: 256,
        ..segment()
    }];
    let mut g = global(&s);
    g.flags = 256;
    i.global_grid = Some(g);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::Unsupported)
    );
}
#[test]
fn external_tempo_source_bounds_and_duplicate_candidates() {
    let spans = [span()];
    let columns = [column()];
    let mut i = input(&spans, &columns);
    let mut t = tempo();
    t.selected.evidence_range.end_frame = 1001;
    i.tempo = Some(t);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    t = tempo();
    t.selected.flags = 256;
    i.tempo = Some(t);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::Unsupported)
    );
    let candidates = [
        TempoCandidate {
            tempo_millibpm: 120000,
            score: 100,
            confidence: 80,
            ..TempoCandidate::default()
        },
        TempoCandidate {
            tempo_millibpm: 120000,
            score: 90,
            confidence: 80,
            ..TempoCandidate::default()
        },
    ];
    t = tempo();
    t.candidates = &candidates;
    i.tempo = Some(t);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
}
#[test]
fn exhaustion_does_not_publish_or_write() {
    let spans = [span()];
    let columns = [column()];
    let i = input(&spans, &columns);
    let mut bytes = [0xaa; 1];
    assert_eq!(
        builder::finalize(&i, &mut bytes, Limits::default()).unwrap_err(),
        Error::BufferTooSmall
    );
    assert_eq!(bytes, [0xaa]);
}
#[test]
fn tempo_selection_and_strict_score_contract() {
    let spans = [span()];
    let columns = [column()];
    let mut i = input(&spans, &columns);
    let candidates = [
        TempoCandidate {
            tempo_millibpm: 120000,
            score: 100,
            confidence: 80,
            ..TempoCandidate::default()
        },
        TempoCandidate {
            tempo_millibpm: 60000,
            score: 100,
            confidence: 80,
            ..TempoCandidate::default()
        },
    ];
    let mut t = tempo();
    t.candidates = &candidates;
    i.tempo = Some(t);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    let candidates = [candidates[1]];
    t.candidates = &candidates;
    i.tempo = Some(t);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
}
#[test]
fn absent_quality_feature_is_native_argument_error() {
    let spans = [span()];
    let columns = [column()];
    let mut i = input(&spans, &columns);
    let quality = [QualityRecord {
        feature: libapta::result::MUSICAL_KEY,
        calibration_model_id: 1,
        evidence_coverage_permille: 1000,
        confidence: 80,
        state: FeatureState::Final,
        flags: 0,
    }];
    i.quality = &quality;
    let mut bytes = [0; 1024];
    assert_eq!(
        builder::finalize(&i, &mut bytes, Limits::default()).unwrap_err(),
        Error::InvalidArgument
    );
}
#[test]
fn preflight_checks_quality_meter_and_waveform_before_output() {
    let spans = [span()];
    let columns = [column()];
    let mut i = input(&spans, &columns);
    let q = [QualityRecord {
        feature: libapta::result::MUSICAL_KEY,
        calibration_model_id: 1,
        evidence_coverage_permille: 1000,
        confidence: 80,
        state: FeatureState::Final,
        flags: 0,
    }];
    i.quality = &q;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    i.quality = &[];
    i.overview.logical_column_count = 2;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    i.overview.logical_column_count = 1;
    let segments = [segment()];
    i.tempo = Some(tempo());
    i.global_grid = Some(global(&segments));
    i.revision = Some(revision());
    let meters = [MeterSegment {
        first_frame: 0,
        end_frame: 1000,
        downbeat_frame: 1,
        downbeat_ordinal: 0,
        numerator: 4,
        denominator: 4,
        state: FeatureState::Final,
        confidence: 80,
        segment_id: 1,
    }];
    i.meter = Some(Meter {
        state: FeatureState::Final,
        confidence: 80,
        numerator: 4,
        denominator: 4,
        downbeat_frame: 1,
        downbeat_ordinal: 0,
        segments: &meters,
    });
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
}
#[test]
fn preflight_rejects_overlapping_storage_and_aggregate_limits() {
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
    let columns = [column(), column()];
    let mut i = input(&spans, &columns);
    i.overview.frames_per_column = 500;
    i.overview.logical_column_count = 2;
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
    let spans = [
        spans[0],
        WaveformSpan {
            data_column_offset: 1,
            ..spans[1]
        },
    ];
    i.overview.spans = &spans;
    assert!(builder::validate(&i, Limits::default()).is_ok());
    let mut limits = Limits::default();
    limits.container.maximum_section_count = 0;
    assert_eq!(builder::validate(&i, limits), Err(Error::LimitExceeded));
    limits = Limits::default();
    limits.container.maximum_waveform_columns = 1;
    assert_eq!(builder::validate(&i, limits), Err(Error::LimitExceeded));
}

#[test]
fn preflight_rejects_unreferenced_columns() {
    let spans = [span()];
    let columns = [column(), column()];
    let i = input(&spans, &columns);
    assert_eq!(
        builder::validate(&i, Limits::default()),
        Err(Error::InvalidArgument)
    );
}
