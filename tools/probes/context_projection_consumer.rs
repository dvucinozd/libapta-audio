// SPDX-License-Identifier: Apache-2.0
//! Source-exact Ruzina snapshot policy probe in an external host workspace.
#![forbid(unsafe_code)]
use libapta::{result, *};
use ruzina_apta_adapter as adapter;
use ruzina_track_analysis::Beat as ProductBeat;
use std::error::Error;

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 3 {
        return Err("APTA file and policy mode required".into());
    }
    let mode = args[2].as_str();
    if ![
        "normal",
        "missing_meter",
        "provisional_meter",
        "provisional_grid",
        "capacity",
    ]
    .contains(&mode)
    {
        return Err("unknown policy mode".into());
    }
    let bytes = std::fs::read(&args[1])?;
    let view = result::parse(&bytes, Default::default())?;
    let g = view.global_grid.ok_or("missing grid")?;
    let mut segments: Vec<_> = (0..g.segment_count())
        .map(|i| g.segment(i).unwrap())
        .collect();
    let mut beats: Vec<_> = (0..g.beat_count()).map(|i| g.beat(i).unwrap()).collect();
    let meters: Vec<_> = view
        .meter
        .map(|m| {
            (0..m.segment_count())
                .map(|i| m.segment(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let coverage = [g.coverage_range()];
    let t = view.tempo.ok_or("missing tempo")?;
    let candidates: Vec<_> = (0..t.candidate_count())
        .map(|i| t.candidate(i).unwrap())
        .collect();
    let provisional_grid = mode == "provisional_grid";
    if provisional_grid {
        for s in &mut segments {
            s.state = FeatureState::Provisional;
            s.flags |= 1;
        }
        for b in &mut beats {
            b.flags |= 1;
        }
    }
    let meter = view.meter.map(|m| {
        let s = meters[0];
        Meter {
            state: if mode == "provisional_meter" {
                FeatureState::Provisional
            } else {
                m.state()
            },
            confidence: m.confidence(),
            numerator: s.numerator,
            denominator: s.denominator,
            downbeat_frame: s.downbeat_frame,
            downbeat_ordinal: s.downbeat_ordinal,
            segments: &meters,
        }
    });
    let input = NativeResultInput {
        source: view.source,
        info: NativeResultInfo {
            session_state: if mode.starts_with("provisional") {
                ResultSessionState::AcceptingInput
            } else {
                ResultSessionState::Completed
            },
            ..Default::default()
        },
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "P2 public generated policy probe",
            source_version: "1",
        },
        overview: None,
        detail: None,
        metadata: None,
        tempo: Some(TempoView {
            selected: t.selected(),
            candidates: &candidates,
        }),
        local_grid: None,
        global_grid: Some(NativeGrid {
            state: if provisional_grid {
                FeatureState::Provisional
            } else {
                g.state()
            },
            confidence: g.confidence(),
            flags: g.flags() | u32::from(provisional_grid),
            representation: g.representation(),
            requested_range: g.requested_range(),
            evidence_range: g.evidence_range(),
            applicability_range: g.applicability_range(),
            coverage_ranges: &coverage,
            segments: &segments,
            beats: &beats,
        }),
        revision: view.revision,
        key: None,
        meter: if mode == "missing_meter" { None } else { meter },
        quality: &[],
    };
    // This validation precedes conversion. No invalid external graph bypasses it.
    native_validation::validate(&input, NativeLimits::default())?;
    let sentinel = ProductBeat {
        time_ms: 1234,
        phase: 99,
        bpm_x100: 1,
    };
    let mut bank = vec![sentinel; if mode == "capacity" { 1 } else { 3072 }];
    match adapter::convert(&input, &mut bank) {
        Ok(analysis) => {
            if let Some(grid) = analysis.track().beat_grid() {
                print!("{{\"native_valid\":true,\"adapter\":\"grid\",\"beats\":[");
                for (i, b) in grid.beats().iter().enumerate() {
                    if i != 0 {
                        print!(",");
                    }
                    print!("[{},{},{}]", b.time_ms, b.phase, b.bpm_x100);
                }
                println!("]}}");
            } else {
                assert!(bank.iter().all(|b| *b == sentinel));
                println!("{{\"native_valid\":true,\"adapter\":\"tempo_only\",\"destination_unchanged\":true}}");
            }
        }
        Err(error) => {
            assert!(bank.iter().all(|b| *b == sentinel));
            println!("{{\"native_valid\":true,\"adapter\":\"{error:?}\",\"destination_unchanged\":true}}");
        }
    }
    Ok(())
}
