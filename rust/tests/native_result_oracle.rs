// SPDX-License-Identifier: Apache-2.0
use libapta::*;
fn scenario<T>(id: u32, f: impl FnOnce(NativeResultInput<'_>) -> T) -> T {
    let range = FrameRange {
        first_frame: 0,
        end_frame: 100000,
    };
    let source = SourceInfo {
        sample_rate: if id == 12 { 1 } else { 48000 },
        channel_count: 2,
        channel_layout: 2,
        total_frames: if id == 27 { None } else { Some(100000) },
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let info = NativeResultInfo {
        generation: if id == 16 { 0 } else { 42 },
        container_version: if id == 15 { 0 } else { 1 },
        session_state: match id {
            3 => ResultSessionState::Created,
            5 | 20 => ResultSessionState::AcceptingInput,
            6 => ResultSessionState::Cancelled,
            7 => ResultSessionState::Failed,
            _ => ResultSessionState::Completed,
        },
        lineage_id_high: 123,
        lineage_id_low: 456,
    };
    let provenance = Provenance {
        origin: if id == 2 {
            ProvenanceOrigin::NativeAnalysis
        } else {
            ProvenanceOrigin::ExternalImport
        },
        source_name: if id == 17 { "" } else { "oracle" },
        source_version: "1",
    };
    let candidates = [
        TempoCandidate {
            tempo_millibpm: if id == 23 { 60000 } else { 120000 },
            score: 50000,
            confidence: if id == 1 { 255 } else { 80 },
            relation_to_selected: 0,
            flags: 0,
        },
        TempoCandidate {
            tempo_millibpm: 120000,
            score: 40000,
            confidence: 80,
            relation_to_selected: 0,
            flags: 0,
        },
    ];
    let tempo = TempoView {
        selected: TempoValue {
            state: if (4..=7).contains(&id) {
                FeatureState::Partial
            } else {
                FeatureState::Final
            },
            confidence: 255,
            flags: 0,
            tempo_millibpm: 120000,
            candidate_set_id: 0,
            evidence_range: range,
            applicability_range: range,
        },
        candidates: match id {
            1 | 23 => &candidates[..1],
            24 => &candidates,
            _ => &[],
        },
    };
    let coverage = [
        FrameRange {
            first_frame: 0,
            end_frame: 24000,
        },
        FrameRange {
            first_frame: 48000,
            end_frame: 72000,
        },
    ];
    let state = if id == 20 {
        FeatureState::Stable
    } else {
        FeatureState::Final
    };
    let segments = [GridSegment {
        applicability_range: range,
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod {
            whole_frames: if id == 12 { 0 } else { 24000 },
            fraction_q32: if id == 12 { 0x80000000 } else { 0 },
        },
        beat_count: 0,
        nominal_tempo_millibpm: 120000,
        confidence: 255,
        state: if id == 21 {
            FeatureState::Partial
        } else {
            state
        },
        flags: 0,
        segment_id: 1,
        revision: 7,
    }];
    let beats = [
        Beat {
            position: FractionalFrame::default(),
            ordinal: 0,
            revision: 0,
            flags: 0,
            confidence: 255,
        },
        Beat {
            position: FractionalFrame {
                whole_frame: if id == 10 { 23000 } else { 24000 },
                fraction_q32: 0,
            },
            ordinal: 1,
            revision: 0,
            flags: 0,
            confidence: 255,
        },
    ];
    let explicit = (8..=10).contains(&id);
    let grid = NativeGrid {
        state,
        confidence: 255,
        flags: if id == 22 { 256 } else { 0 },
        representation: if explicit {
            GridRepresentation::Explicit
        } else {
            GridRepresentation::Segments
        },
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_ranges: &coverage,
        segments: if explicit { &[] } else { &segments },
        beats: if explicit {
            &beats[..if id == 8 { 1 } else { 2 }]
        } else {
            &[]
        },
    };
    let global = (18..=20).contains(&id);
    let revision = GridRevision {
        state: if id == 18 {
            RevisionState::Applied
        } else {
            RevisionState::Pending
        },
        confidence: 255,
        flags: 0,
        revision_id: 7,
        previous_revision_id: 6,
        proposed_representation: GridRepresentation::Segments,
        proposed_segment_count: 1,
        proposed_beat_count: 0,
        affected_range: range,
    };
    let key = Key {
        state: FeatureState::Final,
        confidence: if id == 13 { 80 } else { 255 },
        tonic: 0,
        mode: 1,
        tuning_offset_cents: 0,
        first_frame: 0,
        end_frame: 100000,
        candidates: &[],
    };
    let quality = [QualityRecord {
        feature: 1 << 3,
        calibration_model_id: 0,
        evidence_coverage_permille: 65535,
        confidence: 255,
        state: FeatureState::Final,
        flags: 0,
    }];
    f(NativeResultInput {
        source,
        info,
        provenance,
        overview: None,
        detail: None,
        metadata: None,
        tempo: if matches!(id, 13 | 14 | 25 | 26) {
            None
        } else {
            Some(tempo)
        },
        local_grid: if (8..=12).contains(&id) || matches!(id, 21 | 22) {
            Some(grid)
        } else {
            None
        },
        global_grid: if global { Some(grid) } else { None },
        revision: if global { Some(revision) } else { None },
        key: if matches!(id, 13 | 14) {
            Some(key)
        } else {
            None
        },
        meter: None,
        quality: if id == 25 { &quality } else { &[] },
    })
}
#[test]
#[ignore = "requires APTA_C_NATIVE_RESULT_ORACLE"]
fn native_import_acceptance_and_features_match_c() {
    let oracle =
        std::env::var_os("APTA_C_NATIVE_RESULT_ORACLE").expect("set APTA_C_NATIVE_RESULT_ORACLE");
    for id in 0..28 {
        let output = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "C scenario{id}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let values: Vec<i128> = std::str::from_utf8(&output.stdout)
            .unwrap()
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        assert_eq!(values.len(), 7);
        scenario(id, |input| {
            let got = libapta::native_validation::validate(&input, NativeLimits::default());
            assert_eq!(
                got.is_ok(),
                values[0] >= 0,
                "scenario{id}: Rust={got:?}, C={values:?}"
            );
            if let Ok(v) = got {
                assert_eq!(v.available_features as i128, values[1], "scenario{id}");
                assert_eq!(v.changed_features, v.available_features);
                assert_eq!(input.info.generation as i128, values[2]);
                assert_eq!(input.info.session_state as u8 as i128, values[3]);
                assert_eq!(
                    input.tempo.map_or(0, |t| t.candidates.len()) as i128,
                    values[4]
                );
                assert_eq!(
                    input.local_grid.map_or(0, |g| g.coverage_ranges.len()) as i128,
                    values[5]
                );
                assert_eq!(input.info.container_version as i128, values[6]);
            } else if values[0] == -1 {
                assert_eq!(got, Err(Error::InvalidArgument), "scenario{id}");
            } else if values[0] == -3 {
                assert_eq!(got, Err(Error::Unsupported), "scenario{id}");
            }
        });
    }
}
#[test]
#[ignore = "requires APTA_C_NATIVE_RESULT_ORACLE"]
fn native_copy_matches_c_after_input_scope_ends() {
    use libapta::owned_result::{copy, GridStorage, Storage};
    let oracle =
        std::env::var_os("APTA_C_NATIVE_RESULT_ORACLE").expect("set APTA_C_NATIVE_RESULT_ORACLE");
    for id in 0..28 {
        let output = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(output.status.success());
        let values: Vec<i128> = std::str::from_utf8(&output.stdout)
            .unwrap()
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        if values[0] < 0 {
            continue;
        } // Rejected imports have no C result to copy.
        let seed = scenario(11, |input| input.local_grid.unwrap().segments[0]);
        let mut tempo = [TempoCandidate::default(); 2];
        let mut local_coverage = [FrameRange::default(); 2];
        let mut global_coverage = [FrameRange::default(); 2];
        let mut local_segments = [seed];
        let mut global_segments = [seed];
        let mut local_beats = [Beat::default(); 2];
        let mut global_beats = [Beat::default(); 2];
        let mut text = [0xa5; 32];
        let owned = scenario(id, |input| {
            copy(
                &input,
                Storage {
                    tempo_candidates: &mut tempo,
                    local_grid: GridStorage {
                        coverage_ranges: &mut local_coverage,
                        segments: &mut local_segments,
                        beats: &mut local_beats,
                    },
                    global_grid: GridStorage {
                        coverage_ranges: &mut global_coverage,
                        segments: &mut global_segments,
                        beats: &mut global_beats,
                    },
                    text_bytes: &mut text,
                    ..Default::default()
                },
                NativeLimits::default(),
            )
            .unwrap()
        });
        assert_eq!(
            owned.available_features() as i128,
            values[1],
            "scenario{id}"
        );
        assert_eq!(owned.info().generation as i128, values[2]);
        assert_eq!(owned.info().session_state as u8 as i128, values[3]);
        assert_eq!(
            owned.tempo().map_or(0, |t| t.candidates.len()) as i128,
            values[4]
        );
        assert_eq!(
            owned.local_grid().map_or(0, |g| g.coverage_ranges.len()) as i128,
            values[5]
        );
        assert_eq!(owned.info().container_version as i128, values[6]);
        assert_eq!(owned.provenance().source_name, "oracle");
        assert_eq!(owned.provenance().source_version, "1");
        assert_eq!(owned.info().lineage_id_high, 123);
        assert_eq!(owned.info().lineage_id_low, 456);
        assert_eq!(
            libapta::native_validation::validate(&owned.view(), NativeLimits::default())
                .unwrap()
                .available_features,
            owned.available_features()
        );
    }
}
