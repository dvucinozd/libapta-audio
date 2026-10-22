// SPDX-License-Identifier: Apache-2.0
//! Isolated synthetic policy probe against the actual unchanged M1 adapter.
//! Build outside the workspace, patching only that probe's libapta dependency.
use libapta::*;
use pajoniiir_apta_adapter as adapter;
use pajoniiir_track_analysis::Beat as ProductBeat;

const TOTAL: u64 = 480000;
const CANDIDATES: [TempoCandidate; 1] = [TempoCandidate {
    tempo_millibpm: 120000,
    score: 1,
    confidence: 0,
    relation_to_selected: 0,
    flags: 0,
}];

fn input<'a>(
    beats: &'a [Beat],
    coverage: &'a [FrameRange],
    meters: &'a [MeterSegment],
) -> NativeResultInput<'a> {
    let range = FrameRange {
        first_frame: 0,
        end_frame: TOTAL,
    };
    NativeResultInput {
        source: SourceInfo {
            sample_rate: 48000,
            channel_count: 2,
            channel_layout: 2,
            total_frames: Some(TOTAL),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        info: NativeResultInfo::default(),
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "synthetic policy audit",
            source_version: "1",
        },
        overview: None,
        detail: None,
        metadata: None,
        tempo: Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Final,
                confidence: 0,
                flags: 0,
                tempo_millibpm: 120000,
                candidate_set_id: 0,
                evidence_range: range,
                applicability_range: range,
            },
            candidates: &CANDIDATES,
        }),
        local_grid: None,
        global_grid: Some(NativeGrid {
            state: FeatureState::Final,
            confidence: 0,
            flags: 0,
            representation: GridRepresentation::Explicit,
            requested_range: range,
            evidence_range: range,
            applicability_range: range,
            coverage_ranges: coverage,
            segments: &[],
            beats,
        }),
        revision: Some(GridRevision {
            state: RevisionState::Applied,
            confidence: 0,
            flags: 0,
            revision_id: 1,
            previous_revision_id: 0,
            proposed_representation: GridRepresentation::Explicit,
            proposed_segment_count: 0,
            proposed_beat_count: beats.len() as u32,
            affected_range: range,
        }),
        key: None,
        meter: Some(Meter {
            state: FeatureState::Final,
            confidence: 0,
            numerator: meters[0].numerator,
            denominator: 4,
            downbeat_frame: meters[0].downbeat_frame,
            downbeat_ordinal: meters[0].downbeat_ordinal,
            segments: meters,
        }),
        quality: &[],
    }
}

fn main() {
    let points: Vec<_> = (0..20)
        .map(|i| Beat {
            position: FractionalFrame {
                whole_frame: i * 24000,
                fraction_q32: 0,
            },
            ordinal: i as i64,
            revision: 1,
            flags: 0,
            confidence: 0,
        })
        .collect();
    let full = [FrameRange {
        first_frame: 0,
        end_frame: TOTAL,
    }];
    let meters = [MeterSegment {
        first_frame: 0,
        end_frame: TOTAL,
        downbeat_frame: 0,
        downbeat_ordinal: 0,
        numerator: 4,
        denominator: 4,
        state: FeatureState::Final,
        confidence: 0,
        segment_id: 1,
    }];
    let sentinel = ProductBeat {
        time_ms: 1234,
        phase: 99,
        bpm_x100: 1,
    };
    let valid = input(&points, &full, &meters);
    assert!(native_validation::validate(&valid, NativeLimits::default()).is_ok());
    let mut bank = [sentinel; 32];
    let converted = adapter::convert(&valid, &mut bank).unwrap();
    assert!(converted.track().beat_grid().is_some());
    assert_ne!(bank, [sentinel; 32]);

    let mut provisional = valid;
    provisional.info.session_state = ResultSessionState::AcceptingInput;
    provisional.global_grid.as_mut().unwrap().state = FeatureState::Provisional;
    provisional.global_grid.as_mut().unwrap().flags = 1;
    let mut untouched = [sentinel; 32];
    let converted = adapter::convert(&provisional, &mut untouched).unwrap();
    assert!(converted.track().beat_grid().is_none());
    assert_eq!(untouched, [sentinel; 32]);

    let gaps = [
        FrameRange {
            first_frame: 0,
            end_frame: 120000,
        },
        FrameRange {
            first_frame: 240000,
            end_frame: TOTAL,
        },
    ];
    let gapped_points: Vec<_> = points
        .iter()
        .copied()
        .filter(|b| b.position.whole_frame < 120000 || b.position.whole_frame >= 240000)
        .collect();
    let gapped = input(&gapped_points, &gaps, &meters);
    assert!(native_validation::validate(&gapped, NativeLimits::default()).is_ok());
    assert!(matches!(
        adapter::convert(&gapped, &mut untouched),
        Err(adapter::Error::UnsupportedGrid)
    ));
    assert_eq!(untouched, [sentinel; 32]);

    let mut multi = [meters[0], meters[0]];
    multi[0].end_frame = 240000;
    multi[1].first_frame = 240000;
    multi[1].downbeat_frame = 240000;
    multi[1].downbeat_ordinal = 10;
    multi[1].numerator = 3;
    multi[1].segment_id = 2;
    let multiple = input(&points, &full, &multi);
    assert!(native_validation::validate(&multiple, NativeLimits::default()).is_ok());
    assert!(matches!(
        adapter::convert(&multiple, &mut untouched),
        Err(adapter::Error::UnsupportedGrid)
    ));
    assert_eq!(untouched, [sentinel; 32]);

    let mut short = [sentinel; 1];
    assert!(matches!(
        adapter::convert(&valid, &mut short),
        Err(adapter::Error::Capacity)
    ));
    assert_eq!(short, [sentinel; 1]);
    let mut pending = valid;
    pending.revision.as_mut().unwrap().state = RevisionState::Pending;
    assert!(matches!(
        adapter::convert(&pending, &mut untouched),
        Err(adapter::Error::UnsupportedGrid)
    ));
    assert_eq!(untouched, [sentinel; 32]);
    let mut duplicate = points.clone();
    duplicate[2].position = duplicate[1].position;
    let invalid = input(&duplicate, &full, &meters);
    assert!(native_validation::validate(&invalid, NativeLimits::default()).is_err());
    assert!(matches!(
        adapter::convert(&invalid, &mut untouched),
        Err(adapter::Error::Coordinate)
    ));
    assert_eq!(untouched, [sentinel; 32]);
    let mut variable = points.clone();
    variable[3].position.whole_frame += 960;
    let dynamic = input(&variable, &full, &meters);
    // The trusted validation gate rejects variable intervals against one TEMP.
    // Do not bypass that rejection by calling the subset converter as adoption.
    assert!(native_validation::validate(&dynamic, NativeLimits::default()).is_err());
    println!("{{\"synthetic_only\":true,\"checks_passed\":8,\"final_constant_control_accepts\":true,\"provisional_tempo_only\":true,\"gapped_native_valid_consumer_rejects\":true,\"multiple_meter_native_valid_consumer_rejects\":true,\"capacity_revision_duplicate_rejections_transactional\":true,\"dynamic_explicit_blocked_before_adoption\":true,\"production_activation\":false}}");
}
