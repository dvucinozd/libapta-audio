// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-compact-probabilities")]
use libapta_runtime::compact_probabilities::{synthetic as fx, *};
use sha2::{Digest, Sha256};
use std::sync::atomic::{AtomicBool, Ordering};
fn geometry(rows: u64) -> Geometry {
    Geometry {
        sample_rate: 44100,
        total_frames: rows * 882,
    }
}
fn near(x: f64, y: f64) {
    assert!(
        x == y || (x.is_finite() && y.is_finite() && (x - y).abs() <= 4e-13),
        "{x:?} vs {y:?}"
    );
}
fn bits_p(p: &[[f32; 3]]) -> Vec<[u32; 3]> {
    p.iter().map(|v| v.map(f32::to_bits)).collect()
}
fn bits_d(d: &[[f64; 3]]) -> Vec<[u64; 3]> {
    d.iter().map(|v| v.map(f64::to_bits)).collect()
}
#[test]
fn all_35_frozen_cases_700_schedules_numeric_bits_support_and_canaries() {
    let flag = AtomicBool::new(false);
    assert_eq!(
        format!(
            "{:x}",
            Sha256::digest(include_bytes!(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/../../docs/rust/fixtures/product-host-probability-hs06/cases.json"
            )))
        ),
        CASE_BOOK_SHA
    );
    for (index, e) in EXPECTED.iter().enumerate() {
        let case = fx::fixture(index);
        let mut base = None;
        for cap in [1, 2, 7, 64] {
            for quota in [1, 2, 5, 31, usize::MAX] {
                let sentinel_p = [f32::from_bits(0x7fc00123); 3];
                let sentinel_d = [f64::from_bits(0x7ff8000000000123); 3];
                let mut p = vec![sentinel_p; case.rows + 2];
                let mut d = vec![sentinel_d; case.rows + 2];
                let progress;
                {
                    let mut s = Session::new(case.geometry, &mut p, &mut d).unwrap();
                    let mut driver = fx::Driver::new(case);
                    while s.status() == Status::Running {
                        assert!(s.result().is_none());
                        let before = s.progress();
                        let r = driver.step(&mut s, cap, quota, &flag);
                        assert!(r.units > 0 && r.units <= quota);
                        assert_eq!(r.first_committed_row, before.committed_rows);
                        assert!(r.consumed_rows <= cap);
                    }
                    progress = s.progress();
                    assert_eq!(progress.units, e.units);
                    assert_eq!(progress.accepted_rows, e.accepted);
                    assert_eq!(progress.committed_rows, e.accepted);
                    assert_eq!(s.status(), e.status);
                    let r = s.step(
                        1,
                        InputBlock {
                            first_row: u64::MAX,
                            rows: &[[f32::NAN; 3]],
                        },
                        &flag,
                    );
                    assert_eq!(
                        r.error,
                        if e.status == Status::Failed {
                            Some(Error::InvalidProbability)
                        } else {
                            None
                        }
                    );
                    assert_eq!(r.units, 0);
                    assert_eq!(s.progress(), progress);
                    if let Some(v) = s.result() {
                        assert_eq!(v.support.map(|s| [s.first_row, s.end_row]), e.support);
                        assert_eq!(v.probabilities.len(), case.rows);
                        assert_eq!(
                            v.decoder_densities().map(<[_]>::len),
                            e.support.map(|b| (b[1] - b[0]) as usize)
                        );
                        for (r, row) in v.densities.iter().enumerate() {
                            for j in 0..3 {
                                near(row[j], DENSITY[e.density[r] as usize][j]);
                            }
                        }
                    } else {
                        assert_eq!(e.status, Status::Failed);
                    }
                }
                for (r, row) in p.iter().take(progress.committed_rows as usize).enumerate() {
                    assert_eq!(row.map(f32::to_bits), case.row(r).map(f32::to_bits));
                }
                if e.status == Status::Failed {
                    // Every frozen invalid case has the independent nonbeat,
                    // beat prefix. Verify private committed storage after drop.
                    for r in 0..2 {
                        for j in 0..3 {
                            near(d[r][j], DENSITY[r][j]);
                        }
                    }
                }
                assert!(p[progress.committed_rows as usize..]
                    .iter()
                    .all(|v| v.map(f32::to_bits) == sentinel_p.map(f32::to_bits)));
                assert!(d[progress.committed_rows as usize..]
                    .iter()
                    .all(|v| v.map(f64::to_bits) == sentinel_d.map(f64::to_bits)));
                let current = (bits_p(&p), bits_d(&d));
                if let Some(ref b) = base {
                    assert_eq!(&current, b);
                } else {
                    base = Some(current);
                }
            }
        }
    }
    assert!(std::mem::size_of::<Session<'static>>() <= 512);
    assert!(std::mem::size_of::<fx::Driver>() <= 4096);
}
#[test]
fn geometry_capacity_both_buffers_and_exclusive_tail() {
    for fs in [44100, 48000] {
        assert_eq!(
            Geometry {
                sample_rate: fs,
                total_frames: 600 * u64::from(fs)
            }
            .rows(),
            Ok(30000)
        );
        assert_eq!(
            Geometry {
                sample_rate: fs,
                total_frames: 600 * u64::from(fs) + 1
            }
            .rows(),
            Err(Error::UnsupportedProfile)
        );
        assert_eq!(
            Geometry {
                sample_rate: fs,
                total_frames: 1
            }
            .rows(),
            Ok(1)
        );
    }
    for fs in [0, 22050, u32::MAX] {
        assert_eq!(
            Geometry {
                sample_rate: fs,
                total_frames: 1
            }
            .rows(),
            Err(Error::UnsupportedProfile)
        );
    }
    assert_eq!(
        Geometry {
            sample_rate: 44100,
            total_frames: u64::MAX
        }
        .rows(),
        Err(Error::UnsupportedProfile)
    );
    for (pn, dn) in [(1, 2), (2, 1)] {
        let mut p = vec![[7.; 3]; pn];
        let mut d = vec![[8.; 3]; dn];
        assert!(matches!(
            Session::new(geometry(2), &mut p, &mut d),
            Err(Error::Capacity)
        ));
        assert!(p.iter().all(|&v| v == [7.; 3]));
        assert!(d.iter().all(|&v| v == [8.; 3]));
    }
    let mut p = [[7.; 3]; 2];
    let mut d = [[8.; 3]; 2];
    let s = Session::new(geometry(0), &mut p, &mut d).unwrap();
    let v = s.result().unwrap();
    assert_eq!(s.status(), Status::NoEvidence);
    assert_eq!(s.progress().units, 0);
    assert!(
        v.probabilities.is_empty() && v.densities.is_empty() && v.decoder_densities().is_none()
    );
}
#[test]
fn each_phase_cancel_and_eof_priority_staging_and_terminal_fences() {
    let phases = [
        Phase::AcceptRow,
        Phase::NonBeatLog,
        Phase::BeatLog,
        Phase::DownbeatLog,
        Phase::CommitRow,
        Phase::Finalize,
    ];
    for (units, phase) in phases.into_iter().enumerate() {
        for cancel in [false, true] {
            let mut p = [[7.; 3]; 2];
            let mut d = [[8.; 3]; 2];
            let flag = AtomicBool::new(false);
            {
                let mut s = Session::new(geometry(1), &mut p, &mut d).unwrap();
                if units > 0 {
                    let r = s.step(
                        units,
                        InputBlock {
                            first_row: 0,
                            rows: &[[1., 0., 0.]],
                        },
                        &flag,
                    );
                    assert_eq!(r.units, units);
                }
                assert_eq!(s.phase(), phase);
                assert!(s.result().is_none());
                let before = s.progress();
                flag.store(true, Ordering::Release);
                let r = s.step(
                    0,
                    InputBlock {
                        first_row: u64::MAX,
                        rows: &[[f32::NAN; 3]],
                    },
                    &flag,
                );
                assert_eq!(r.error, Some(Error::ZeroBudget));
                assert_eq!(s.progress(), before);
                assert_eq!(s.status(), Status::Running);
                if cancel {
                    let r = s.step(
                        1,
                        InputBlock {
                            first_row: u64::MAX,
                            rows: &[[f32::NAN; 3]],
                        },
                        &flag,
                    );
                    assert_eq!(r.units, 0);
                    assert_eq!(r.error, None);
                    assert_eq!(s.status(), Status::Cancelled);
                    assert_eq!(s.progress().cancellation_phase, Some(phase));
                    flag.store(false, Ordering::Release);
                    s.finish_input();
                    s.cancel();
                    s.step(
                        1,
                        InputBlock {
                            first_row: u64::MAX,
                            rows: &[],
                        },
                        &flag,
                    );
                    assert_eq!(s.status(), Status::Cancelled);
                    assert!(s.result().is_none());
                } else {
                    s.finish_input();
                    if units == 0 {
                        assert_eq!(s.status(), Status::Failed);
                    } else {
                        assert_eq!(s.status(), Status::Running);
                        flag.store(false, Ordering::Release);
                        while s.status() == Status::Running {
                            s.step(
                                1,
                                InputBlock {
                                    first_row: 1,
                                    rows: &[],
                                },
                                &flag,
                            );
                        }
                        assert_eq!(s.status(), Status::Ready);
                        flag.store(true, Ordering::Release);
                        s.step(
                            99,
                            InputBlock {
                                first_row: u64::MAX,
                                rows: &[],
                            },
                            &flag,
                        );
                        s.cancel();
                        s.finish_input();
                        assert_eq!(s.status(), Status::Ready);
                        assert!(s.result().is_some());
                    }
                }
            }
            if cancel && units < 5 || !cancel && units == 0 {
                assert_eq!(p, [[7.; 3]; 2]);
                assert_eq!(d, [[8.; 3]; 2]);
            }
        }
    }
    // EOF while a row is staged still refuses a missing later row.
    for units in 0..=5 {
        let mut p = [[7.; 3]; 2];
        let mut d = [[8.; 3]; 2];
        let mut s = Session::new(geometry(2), &mut p, &mut d).unwrap();
        if units > 0 {
            s.step(
                units,
                InputBlock {
                    first_row: 0,
                    rows: &[[0., 1., 0.]],
                },
                &AtomicBool::new(false),
            );
        }
        s.finish_input();
        assert_eq!(s.status(), Status::Failed);
        assert!(s.result().is_none());
        assert_eq!(
            s.step(
                1,
                InputBlock {
                    first_row: 0,
                    rows: &[]
                },
                &AtomicBool::new(false)
            )
            .error,
            Some(Error::TruncatedSource)
        );
    }
}
#[test]
fn invalid_columns_source_offsets_oversupply_starvation_and_input_reuse() {
    let flag = AtomicBool::new(false);
    for col in 0..3 {
        for bad in [
            f32::NAN,
            f32::INFINITY,
            f32::NEG_INFINITY,
            -f32::from_bits(1),
            f32::from_bits(0x3f800001),
        ] {
            let mut input = [[0., 0., 1.]; 2];
            input[1][col] = bad;
            let mut p = [[9.; 3]; 2];
            let mut d = [[9.; 3]; 2];
            let mut s = Session::new(geometry(2), &mut p, &mut d).unwrap();
            let r = s.step(
                99,
                InputBlock {
                    first_row: 0,
                    rows: &input,
                },
                &flag,
            );
            assert_eq!(r.error, Some(Error::InvalidProbability));
            assert_eq!(r.consumed_rows, 1);
            assert_eq!(r.committed_rows, 1);
            assert_eq!(r.units, 6);
            assert!(s.result().is_none());
        }
    }
    for (offset, count, error) in [(1, 0, Error::SourceOffset), (0, 3, Error::SourceLength)] {
        let mut p = [[0.; 3]; 2];
        let mut d = [[0.; 3]; 2];
        let mut s = Session::new(geometry(2), &mut p, &mut d).unwrap();
        let r = s.step(
            99,
            InputBlock {
                first_row: offset,
                rows: &[[0., 0., 1.]; 3][..count],
            },
            &flag,
        );
        assert_eq!(r.error, Some(error));
        assert_eq!(r.units, 0);
    }
    let mut p = [[0.; 3]; 2];
    let mut d = [[0.; 3]; 2];
    let mut s = Session::new(geometry(2), &mut p, &mut d).unwrap();
    let mut input = [[1., 0., 0.]];
    assert_eq!(
        s.step(
            9,
            InputBlock {
                first_row: 0,
                rows: &[]
            },
            &flag
        )
        .flow,
        Flow::NeedInput
    );
    s.step(
        1,
        InputBlock {
            first_row: 0,
            rows: &input,
        },
        &flag,
    );
    input[0] = [f32::NAN; 3];
    assert!(input[0][0].is_nan());
    let r = s.step(
        99,
        InputBlock {
            first_row: 1,
            rows: &[],
        },
        &flag,
    );
    assert_eq!(r.units, 4);
    assert_eq!(r.flow, Flow::NeedInput);
    assert_eq!(s.progress().committed_rows, 1);
    let r = s.step(
        1,
        InputBlock {
            first_row: 0,
            rows: &[],
        },
        &flag,
    );
    assert_eq!(r.error, Some(Error::SourceOffset));
    assert_eq!(p[0], [1., 0., 0.]);
}
#[test]
fn selector_all_11_registered_cases() {
    for (a, b, e) in [
        (-10., -10., Ok(MeterChoice::Three)),
        (-10., -9., Ok(MeterChoice::Four)),
        (-9., -10., Ok(MeterChoice::Three)),
        (f64::NEG_INFINITY, -9., Ok(MeterChoice::Four)),
        (-9., f64::NEG_INFINITY, Ok(MeterChoice::Three)),
        (
            f64::NEG_INFINITY,
            f64::NEG_INFINITY,
            Ok(MeterChoice::NoPath),
        ),
        (-0., 0., Ok(MeterChoice::Three)),
        (f64::NAN, -9., Err(Error::InvalidScore)),
        (-9., f64::INFINITY, Err(Error::InvalidScore)),
        (1., -9., Err(Error::InvalidScore)),
        (-9., f64::NAN, Err(Error::InvalidScore)),
    ] {
        assert_eq!(choose_meter(a, b), e);
    }
}
#[test]
fn eight_compositions_two_hmm_quotas_shifted_origin_and_owned_wire_reuse() {
    let flag = AtomicBool::new(false);
    let mut baseline = std::collections::BTreeMap::new();
    for index in 27..35 {
        let case = fx::fixture(index);
        assert!(case.composition);
        for quota in [4096, usize::MAX] {
            let mut p = vec![[0.; 3]; case.rows];
            let mut d = vec![[0.; 3]; case.rows];
            let c = {
                let mut s = Session::new(case.geometry, &mut p, &mut d).unwrap();
                let mut driver = fx::Driver::new(case);
                while s.status() == Status::Running {
                    assert!(driver.step(&mut s, 7, 31, &flag).error.is_none());
                }
                let v = s.result().unwrap();
                let before = fx::evidence_hash(&v);
                let c = fx::compose(&v, quota).unwrap();
                assert_eq!(fx::evidence_hash(&v), before);
                c
            };
            // All preparation, graph, trace workspace and HS02 scratch borrows ended.
            p.fill([f32::NAN; 3]);
            d.fill([f64::NAN; 3]);
            match index {
                27..=30 => {
                    let meter = if index < 29 { 3 } else { 4 };
                    let shift = if index % 2 == 0 { 2 } else { 0 };
                    assert_eq!(c.status, "GridReady");
                    assert_eq!(c.meter, Some(meter));
                    let summary = c.summary.unwrap();
                    assert_eq!(summary.tempo_millibpm, 120000);
                    assert_eq!(
                        summary.anchor_q32,
                        (shift as u128 * u128::from(case.geometry.sample_rate) / 50) << 32
                    );
                    assert_eq!(
                        summary.period_q32,
                        (u128::from(case.geometry.sample_rate) / 2) << 32
                    );
                    assert_eq!(summary.downbeat_ordinal, 0);
                    assert_eq!(c.events.len(), 2 * meter as usize + 1);
                    for (i, e) in c.events.iter().enumerate() {
                        assert_eq!(e.global_row, shift + 25 * i as u32);
                        assert_eq!(e.beat_number, (i % meter as usize + 1) as u8);
                        assert_eq!(e.winning_class, u8::from(i % meter as usize == 0));
                        assert_eq!(e.reserved, [0; 2]);
                    }
                    let key = (meter, quota);
                    let pair = (c.scores.map(f64::to_bits), c.path_hash.clone());
                    if shift == 0 {
                        baseline.insert(key, pair);
                    } else {
                        assert_eq!(baseline[&key], pair);
                    }
                    let parsed = libapta::result::parse(
                        c.wire.as_ref().unwrap(),
                        libapta::result::Limits::default(),
                    )
                    .unwrap();
                    assert_eq!(
                        parsed
                            .global_grid
                            .unwrap()
                            .segment(0)
                            .unwrap()
                            .anchor_position
                            .whole_frame,
                        (summary.anchor_q32 >> 32) as u64
                    );
                }
                31 => {
                    assert_eq!(c.status, "NoPath");
                    assert_eq!(c.scores, [f64::NEG_INFINITY; 2]);
                    assert!(c.meter.is_none() && c.wire.is_none());
                }
                32 => {
                    assert_eq!(c.status, "UnsupportedGrid");
                    assert_eq!(
                        c.reason,
                        Some(libapta_runtime::compact_grid::Reason::InsufficientEvents)
                    );
                    assert_eq!(c.meter, Some(3));
                    assert!(c.wire.is_none());
                }
                _ => {
                    assert_eq!(c.status, "NoEvidence");
                    assert_eq!(c.maximum_workspace_payload, 0);
                    assert!(c.meter.is_none() && c.wire.is_none());
                }
            }
        }
    }
}
#[test]
fn maximum_geometry_pending_cancel_at_registered_unit_without_full_run() {
    let case = fx::full("full-mixed").unwrap();
    let mut p = vec![[-7.; 3]; case.rows];
    let mut d = vec![[-8.; 3]; case.rows];
    {
        let mut s = Session::new(case.geometry, &mut p, &mut d).unwrap();
        let mut driver = fx::Driver::new(case);
        let flag = AtomicBool::new(false);
        while s.progress().units < 686 {
            let remaining = 686 - s.progress().units;
            let r = driver.step(&mut s, 64, remaining.min(31) as usize, &flag);
            assert!(r.error.is_none());
        }
        assert_eq!(s.phase(), Phase::NonBeatLog);
        assert_eq!(s.progress().accepted_rows, 138);
        assert_eq!(s.progress().committed_rows, 137);
        assert!(s.result().is_none());
        flag.store(true, Ordering::Release);
        let r = driver.step(&mut s, 64, 31, &flag);
        assert_eq!(r.units, 0);
        assert_eq!(s.status(), Status::Cancelled);
        assert_eq!(s.progress().cancellation_phase, Some(Phase::NonBeatLog));
        flag.store(false, Ordering::Release);
        let r = driver.step(&mut s, 64, 31, &flag);
        assert_eq!(r.units, 0);
        assert_eq!(s.status(), Status::Cancelled);
        assert!(s.result().is_none());
    }
    for (i, row) in p.iter().take(137).enumerate() {
        assert_eq!(row.map(f32::to_bits), case.row(i).map(f32::to_bits));
    }
    assert!(p[137..].iter().all(|&v| v == [-7.; 3]));
    assert!(d[137..].iter().all(|&v| v == [-8.; 3]));
}
#[test]
fn fixed_cli_refuses_arbitrary_arguments_before_output() {
    for args in [
        vec![],
        vec!["--case", "other"],
        vec!["--file", "arbitrary"],
        vec!["--case", "fixtures", "--model", "x"],
    ] {
        let output =
            std::process::Command::new(env!("CARGO_BIN_EXE_apta-compact-probabilities-synthetic"))
                .args(args)
                .output()
                .unwrap();
        assert!(!output.status.success());
        assert!(output.stdout.is_empty());
    }
}

// Frozen registration expectations follow. Only tests read these values.
struct Expected {
    status: Status,
    accepted: u64,
    units: u64,
    support: Option<[u32; 2]>,
    density: &'static [u8],
}

const CASE_BOOK_SHA: &str = "6d77d2ef1a2138cb24f3bf383bb9c6639326e6e8a0eac35ac2e484c47bb9c573";
const DENSITY: [[f64; 3]; 18] = [
    [-2.70805020110221, f64::NEG_INFINITY, f64::NEG_INFINITY],
    [f64::NEG_INFINITY, 0.0, f64::NEG_INFINITY],
    [f64::NEG_INFINITY, f64::NEG_INFINITY, 0.0],
    [
        -3.8066624599679977,
        -1.0986122588657878,
        -1.0986122588657878,
    ],
    [-2.70805020110221, -103.27892990343184, f64::NEG_INFINITY],
    [-2.70805020110221, f64::NEG_INFINITY, -103.27892990343184],
    [
        -105.98698010453406,
        -std::f64::consts::LN_2,
        -std::f64::consts::LN_2,
    ],
    [-2.759343508038107, -2.9957323331586374, f64::NEG_INFINITY],
    [-2.759343508038107, -2.99573225865283, f64::NEG_INFINITY],
    [-2.759343508038107, -2.9957321841470277, f64::NEG_INFINITY],
    [-2.759343508038107, f64::NEG_INFINITY, -2.9957323331586374],
    [-2.759343508038107, f64::NEG_INFINITY, -2.99573225865283],
    [-2.759343508038107, f64::NEG_INFINITY, -2.9957321841470277],
    [-3.401196904825111, -1.3862943611198906, -1.3862943611198906],
    [-3.401196427988294, -1.3862943611198906, -1.3862943611198906],
    [-6.620073228882098, -3.912023027779888, -0.04082201687199717],
    [-2.728252888956989, -4.605170208339834, -4.605170208339834],
    [-6.620073228882098, -0.04082201687199717, -3.912023027779888],
];
const EXPECTED: [Expected; 35] = [
    Expected {
        status: Status::NoEvidence,
        accepted: 0,
        units: 0,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 1,
        units: 6,
        support: None,
        density: &[0],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[1],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[2],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[3],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 1,
        units: 6,
        support: None,
        density: &[0],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 1,
        units: 6,
        support: None,
        density: &[4],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 1,
        units: 6,
        support: None,
        density: &[5],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[6],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 1,
        units: 6,
        support: None,
        density: &[7],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[8],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[9],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 1,
        units: 6,
        support: None,
        density: &[10],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[11],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[12],
    },
    Expected {
        status: Status::Ready,
        accepted: 3,
        units: 16,
        support: Some([0, 1]),
        density: &[1, 0, 0],
    },
    Expected {
        status: Status::Ready,
        accepted: 4,
        units: 21,
        support: Some([3, 4]),
        density: &[0, 0, 0, 2],
    },
    Expected {
        status: Status::Ready,
        accepted: 5,
        units: 26,
        support: Some([1, 4]),
        density: &[0, 1, 0, 2, 0],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[13],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[14],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Failed,
        accepted: 2,
        units: 11,
        support: None,
        density: &[],
    },
    Expected {
        status: Status::Ready,
        accepted: 175,
        units: 876,
        support: Some([0, 151]),
        density: &[
            15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        ],
    },
    Expected {
        status: Status::Ready,
        accepted: 177,
        units: 886,
        support: Some([2, 153]),
        density: &[
            16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16,
        ],
    },
    Expected {
        status: Status::Ready,
        accepted: 225,
        units: 1126,
        support: Some([0, 201]),
        density: &[
            15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16,
        ],
    },
    Expected {
        status: Status::Ready,
        accepted: 227,
        units: 1136,
        support: Some([2, 203]),
        density: &[
            16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16,
            16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
            16, 16, 16, 16, 16, 16, 16,
        ],
    },
    Expected {
        status: Status::Ready,
        accepted: 8,
        units: 41,
        support: Some([0, 8]),
        density: &[2, 2, 2, 2, 2, 2, 2, 2],
    },
    Expected {
        status: Status::Ready,
        accepted: 1,
        units: 6,
        support: Some([0, 1]),
        density: &[2],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 9,
        units: 46,
        support: None,
        density: &[16, 16, 16, 16, 16, 16, 16, 16, 16],
    },
    Expected {
        status: Status::NoEvidence,
        accepted: 0,
        units: 0,
        support: None,
        density: &[],
    },
];
