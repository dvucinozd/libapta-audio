// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-compact-neural")]
use libapta_runtime::compact_neural::{synthetic as fx, *};
use std::sync::atomic::{AtomicBool, Ordering};
fn raw(name: &str, suffix: &str) -> Vec<f32> {
    std::fs::read(format!(
        "{}/../../docs/rust/fixtures/product-host-neural-hs05/assets/{name}-{suffix}.f32le",
        env!("CARGO_MANIFEST_DIR")
    ))
    .unwrap()
    .chunks_exact(4)
    .map(|b| f32::from_le_bytes(b.try_into().unwrap()))
    .collect()
}
fn near(x: f32, y: f64) {
    assert!(
        (f64::from(x) - y).abs() <= 2e-6 + 2e-5 * y.abs(),
        "{x} vs {y}"
    );
}
fn winner(p: &[f32]) -> usize {
    let mut w = 0;
    for i in 1..3 {
        if p[i] > p[w] {
            w = i;
        }
    }
    w
}
#[test]
fn all_34_probes_five_quotas_exact_affine_and_numeric_nonlinear() {
    let flag = AtomicBool::new(false);
    for i in 0..34 {
        let e = probe_expected(i);
        let mut base = None;
        for quota in [1, 3, 17, 256, usize::MAX] {
            let mut p = ProbeSession::new(i).unwrap();
            while p.status() == Status::Running {
                assert!(p.result().is_none());
                let r = p.step(quota, &flag);
                assert!(r.units <= quota && r.units > 0);
                assert!(r.error.is_none());
            }
            let r = p.result().unwrap();
            assert_eq!(r.len as usize, e.bits.len());
            for (j, &b) in e.bits.iter().enumerate() {
                near(r.values[j], f64::from(f32::from_bits(b)));
            }
            for &j in e.exact {
                assert_eq!(r.values[j].to_bits(), e.bits[j], "probe{i} scalar{j}");
            }
            for (j, &ideal) in e.ideal.iter().enumerate() {
                near(r.values[j], ideal);
            }
            if let Some(w) = e.argmax {
                assert_eq!(winner(&r.values[..3]), w);
            }
            let bits: Vec<_> = r.values.iter().map(|x| x.to_bits()).collect();
            if let Some(ref base) = base {
                assert_eq!(&bits, base);
            } else {
                base = Some(bits);
            }
            assert!(r.values[r.len as usize..].iter().all(|x| x.to_bits() == 0));
            assert!(p.progress().units <= 256);
            let before = p.progress();
            p.cancel();
            p.step(1, &flag);
            assert_eq!(p.progress(), before);
        }
    }
    assert!(matches!(
        ProbeSession::new(34),
        Err(Error::UnsupportedProfile)
    ));
    assert!(std::mem::size_of::<ProbeSession>() <= 4096);
}
#[test]
fn all_12_sequences_480_schedules_state_reset_and_bit_invariance() {
    let flag = AtomicBool::new(false);
    for i in 0..12 {
        let c = fx::fixture(i);
        let flat = raw(c.name, "input");
        let input: Vec<[f32; 3]> = flat
            .chunks_exact(3)
            .map(|v| v.try_into().unwrap())
            .collect();
        let expected = raw(c.name, "trace");
        let mut baseline = None;
        for chunk in [1, 2, 7, 64] {
            for capacity in [1, 7] {
                for quota in [1, 3, 17, 256, usize::MAX] {
                    let mut s = Session::new(c.profile, c.rows).unwrap();
                    let mut at = 0;
                    let mut reset = 0;
                    let mut rows = Vec::new();
                    let mut out = [[123.; 3]; 9];
                    let mut end = 0;
                    while s.status() == Status::Running {
                        let p = s.progress();
                        if c.resets.get(reset) == Some(&p.committed_rows)
                            && p.accepted_rows == p.committed_rows
                        {
                            assert!(s.reset_state(&flag).applied);
                            reset += 1;
                        }
                        if at == end {
                            end = (at + chunk)
                                .min(input.len())
                                .min(c.resets.get(reset).copied().unwrap_or(c.rows) as usize);
                        }
                        let old = s.state_snapshot();
                        let p = s.progress();
                        let r = s.step(
                            quota,
                            InputBlock {
                                first_row: at as u64,
                                rows: &input[at..end],
                            },
                            &mut out[1..1 + capacity],
                            &flag,
                        );
                        assert!(r.error.is_none(), "{} {:?}", c.name, r);
                        assert!(r.units <= quota);
                        assert_eq!(r.first_output_row, rows.len() as u64);
                        assert_eq!(out[0], [123.; 3]);
                        assert_eq!(out[1 + capacity], [123.; 3]);
                        at += r.consumed_rows;
                        if r.produced_rows == 0 {
                            assert_eq!(s.state_snapshot(), old);
                        } else {
                            for row in &out[1..1 + r.produced_rows] {
                                let ordinal = rows.len();
                                for j in 0..3 {
                                    near(row[j], f64::from(expected[11 * ordinal + j]));
                                }
                                assert_eq!(
                                    winner(row),
                                    winner(&expected[11 * ordinal..11 * ordinal + 3])
                                );
                                rows.push(*row);
                            }
                            let last = rows.len() - 1;
                            for (j, x) in s.state_snapshot().iter().enumerate() {
                                near(*x, f64::from(expected[11 * last + 3 + j]));
                            }
                        }
                        assert_eq!(s.progress().step_units - p.step_units, r.units as u64);
                        if at == input.len() {
                            s.finish_input();
                        }
                        assert!(r.units > 0 || r.flow == Flow::NeedInput);
                    }
                    assert_eq!(at, input.len());
                    assert_eq!(rows.len() as u64, c.rows);
                    assert_eq!(s.progress().step_units, 172 * c.rows);
                    assert_eq!(s.progress().macs, 76 * c.rows);
                    assert_eq!(s.progress().reset_count, c.resets.len() as u64);
                    let bits: Vec<_> = rows
                        .iter()
                        .flatten()
                        .chain(s.state_snapshot().iter())
                        .map(|x| x.to_bits())
                        .collect();
                    if let Some(ref base) = baseline {
                        assert_eq!(&bits, base, "{} {chunk}/{capacity}/{quota}", c.name);
                    } else {
                        baseline = Some(bits);
                    }
                    assert_eq!(
                        s.status(),
                        if c.rows == 0 {
                            Status::NoEvidence
                        } else {
                            Status::Done
                        }
                    );
                }
            }
        }
    }
}
fn one(s: &mut Session, flag: &AtomicBool) -> StepReport {
    let p = s.progress();
    s.step(
        1,
        InputBlock {
            first_row: p.accepted_rows,
            rows: if p.accepted_rows < 8 {
                &[[1., -1., 0.5]]
            } else {
                &[]
            },
        },
        &mut [[123.; 3]],
        flag,
    )
}
#[test]
fn every_phase_cancel_privacy_reset_busy_and_terminal_fences() {
    let phases = [
        Phase::AcceptRow,
        Phase::Linear,
        Phase::GateInput,
        Phase::GateBiasIh,
        Phase::GateHidden,
        Phase::GateBiasHh,
        Phase::GateMerge,
        Phase::GateActivate,
        Phase::Cell,
        Phase::Hidden,
        Phase::NextLayer,
        Phase::Head,
        Phase::SoftmaxMax,
        Phase::SoftmaxSubtract,
        Phase::SoftmaxExp,
        Phase::SoftmaxSum,
        Phase::SoftmaxDivide,
        Phase::Commit,
    ];
    for phase in phases {
        for layer in [0, 1] {
            let flag = AtomicBool::new(false);
            let mut s = Session::new(Profile::GateTagged, 8).unwrap();
            // Start after a committed prefix; no reset may expose a partial new row.
            while s.progress().committed_rows == 0 {
                one(&mut s, &flag);
            }
            let mut hits = 0;
            while s.phase() != phase || hits < layer + 1 {
                let before = s.phase();
                one(&mut s, &flag);
                if s.phase() == phase && before != phase {
                    hits += 1;
                }
            }
            let before = s.progress();
            let state = s.state_snapshot();
            if phase != Phase::AcceptRow {
                assert_eq!(s.reset_state(&flag).error, Some(Error::BusyReset));
                assert_eq!(s.state_snapshot(), state);
            }
            let z = s.step(
                0,
                InputBlock {
                    first_row: u64::MAX,
                    rows: &[[f32::NAN; 3]],
                },
                &mut [],
                &flag,
            );
            assert_eq!(z.error, Some(Error::ZeroBudget));
            assert_eq!(s.progress(), before);
            flag.store(true, Ordering::Release);
            let mut out = [[123.; 3]];
            let r = s.step(
                99,
                InputBlock {
                    first_row: u64::MAX,
                    rows: &[[f32::NAN; 3]],
                },
                &mut out,
                &flag,
            );
            assert_eq!(r.status, Status::Cancelled);
            assert_eq!(r.units, 0);
            assert_eq!(out, [[123.; 3]]);
            assert_eq!(s.state_snapshot(), state);
            assert_eq!(s.progress().cancellation_phase, Some(phase));
            flag.store(false, Ordering::Release);
            let p = s.progress();
            one(&mut s, &flag);
            s.finish_input();
            assert_eq!(s.reset_state(&flag).error, Some(Error::TerminalReset));
            assert_eq!(s.progress(), p);
            assert_eq!(s.state_snapshot(), state);
        }
    }
    for i in 0..34 {
        let flag = AtomicBool::new(false);
        let mut p = ProbeSession::new(i).unwrap();
        p.step(1, &flag);
        flag.store(true, Ordering::Release);
        assert_eq!(p.step(999, &flag).status, Status::Cancelled);
        assert!(p.result().is_none());
        let before = p.progress();
        flag.store(false, Ordering::Release);
        p.step(999, &flag);
        assert_eq!(p.progress(), before);
    }
}
#[test]
fn invalid_input_each_column_prefix_progress_offsets_eof_backpressure_and_reuse() {
    for bad in [
        f32::NAN,
        f32::INFINITY,
        f32::NEG_INFINITY,
        4.000001,
        -4.000001,
    ] {
        for column in 0..3 {
            let flag = AtomicBool::new(false);
            let mut s = Session::new(Profile::Asymmetric, 8).unwrap();
            while s.progress().committed_rows == 0 {
                one(&mut s, &flag);
            }
            let p = s.progress();
            let state = s.state_snapshot();
            let mut invalid = [0.; 3];
            invalid[column] = bad;
            let r = s.step(
                usize::MAX,
                InputBlock {
                    first_row: p.accepted_rows,
                    rows: &[[0.; 3], invalid],
                },
                &mut [[123.; 3]; 3],
                &flag,
            );
            assert_eq!(r.error, Some(Error::InvalidFeatures));
            assert_eq!(r.consumed_rows, 1);
            assert_eq!(r.produced_rows, 1);
            assert_eq!(s.progress().committed_rows, 2);
            assert_ne!(s.state_snapshot(), state);
            let p = s.progress();
            let state = s.state_snapshot();
            one(&mut s, &flag);
            assert_eq!(s.progress(), p);
            assert_eq!(s.state_snapshot(), state);
        }
    }
    let flag = AtomicBool::new(false);
    let mut s = Session::new(Profile::Zero, 1).unwrap();
    let p = s.progress();
    assert_eq!(
        s.step(
            1,
            InputBlock {
                first_row: 0,
                rows: &[[f32::NAN; 3]]
            },
            &mut [],
            &flag
        )
        .flow,
        Flow::NeedOutput
    );
    assert_eq!(s.progress(), p);
    assert_eq!(
        s.step(
            1,
            InputBlock {
                first_row: 0,
                rows: &[]
            },
            &mut [[0.; 3]],
            &flag
        )
        .flow,
        Flow::NeedInput
    );
    assert!(s.reset_state(&flag).applied);
    assert!(s.reset_state(&flag).applied);
    assert_eq!(s.progress().reset_count, 2);
    s.finish_input();
    assert_eq!(s.status(), Status::Failed);
    for (offset, rows, error) in [
        (1, &[][..], Error::SourceOffset),
        (0, &[[0.; 3]; 2][..], Error::SourceLength),
    ] {
        let mut s = Session::new(Profile::Zero, 1).unwrap();
        assert_eq!(
            s.step(
                1,
                InputBlock {
                    first_row: offset,
                    rows
                },
                &mut [[0.; 3]],
                &flag
            )
            .error,
            Some(error)
        );
    }
    assert!(matches!(
        Session::new(Profile::Zero, 30001),
        Err(Error::UnsupportedProfile)
    ));
    assert!(matches!(
        Session::new(Profile::Zero, u64::MAX),
        Err(Error::UnsupportedProfile)
    ));
    for _ in 0..2 {
        let mut s = Session::new(Profile::Zero, 1).unwrap();
        let r = s.step(
            usize::MAX,
            InputBlock {
                first_row: 0,
                rows: &[[4., -4., -0.]],
            },
            &mut [[0.; 3]],
            &flag,
        );
        assert_eq!(r.status, Status::Done);
        assert_eq!(s.state_snapshot(), [0.; 8]);
        let p = s.progress();
        s.cancel();
        s.finish_input();
        s.step(
            1,
            InputBlock {
                first_row: u64::MAX,
                rows: &[[f32::NAN; 3]],
            },
            &mut [],
            &flag,
        );
        assert_eq!(s.progress(), p);
    }
    assert!(std::mem::size_of::<Session>() <= 4096);
}
#[test]
fn executable_refuses_arbitrary_sources_before_header() {
    let exe = env!("CARGO_BIN_EXE_apta-compact-neural-synthetic");
    for args in [
        vec![],
        vec!["--case", "unknown"],
        vec!["--model", "model.pt"],
        vec!["--input-fd", "0"],
        vec!["--case", "fixtures", "extra"],
    ] {
        let p = std::process::Command::new(exe).args(args).output().unwrap();
        assert!(!p.status.success());
        assert!(p.stdout.is_empty());
    }
}

struct Expected {
    bits: &'static [u32],
    exact: &'static [usize],
    argmax: Option<usize>,
    ideal: &'static [f64],
}
fn probe_expected(i: usize) -> Expected {
    match i {
        0 => Expected {
            bits: &[0, 0],
            exact: &[0, 1],
            argmax: None,
            ideal: &[],
        },
        1 => Expected {
            bits: &[3199729664, 3183476736],
            exact: &[0, 1],
            argmax: None,
            ideal: &[],
        },
        2 => Expected {
            bits: &[0, 3212836864],
            exact: &[0, 1],
            argmax: None,
            ideal: &[],
        },
        3 => Expected {
            bits: &[1065353216, 3217031168],
            exact: &[0, 1],
            argmax: None,
            ideal: &[],
        },
        4 => Expected {
            bits: &[0, 0],
            exact: &[0, 1],
            argmax: None,
            ideal: &[],
        },
        5 => Expected {
            bits: &[0, 3212836864],
            exact: &[],
            argmax: None,
            ideal: &[2.572209372642415e-56, -1.0],
        },
        6 => Expected {
            bits: &[4320708, 3212836864],
            exact: &[],
            argmax: None,
            ideal: &[6.054601895401186e-39, -1.0],
        },
        7 => Expected {
            bits: &[822977587, 3212836864],
            exact: &[],
            argmax: None,
            ideal: &[2.0611536181902033e-09, -1.0],
        },
        8 => Expected {
            bits: &[1039409321, 3212233347],
            exact: &[],
            argmax: None,
            ideal: &[0.11920292202211755, -0.9640275800758169],
        },
        9 => Expected {
            bits: &[1049211569, 3208837078],
            exact: &[],
            argmax: None,
            ideal: &[0.2689414213699951, -0.7615941559557649],
        },
        10 => Expected {
            bits: &[1056964608, 2147483648],
            exact: &[],
            argmax: None,
            ideal: &[0.5, -0.0],
        },
        11 => Expected {
            bits: &[1056964608, 0],
            exact: &[],
            argmax: None,
            ideal: &[0.5, 0.0],
        },
        12 => Expected {
            bits: &[1060841128, 1061353430],
            exact: &[],
            argmax: None,
            ideal: &[0.7310585786300049, 0.7615941559557649],
        },
        13 => Expected {
            bits: &[1063353323, 1064749699],
            exact: &[],
            argmax: None,
            ideal: &[0.8807970779778823, 0.9640275800758169],
        },
        14 => Expected {
            bits: &[1065353216, 1065353216],
            exact: &[],
            argmax: None,
            ideal: &[0.9999999979388463, 1.0],
        },
        15 => Expected {
            bits: &[1065353216, 1065353216],
            exact: &[],
            argmax: None,
            ideal: &[1.0, 1.0],
        },
        16 => Expected {
            bits: &[1065353216, 1065353216],
            exact: &[],
            argmax: None,
            ideal: &[1.0, 1.0],
        },
        17 => Expected {
            bits: &[1056964607, 3017195413],
            exact: &[],
            argmax: None,
            ideal: &[0.4999999749999997, -1.0000000116860941e-07],
        },
        18 => Expected {
            bits: &[1056964608, 869711765],
            exact: &[],
            argmax: None,
            ideal: &[0.5000000250000003, 1.0000000116860941e-07],
        },
        19 => Expected {
            bits: &[1052855555, 3203177119],
            exact: &[],
            argmax: None,
            ideal: &[0.37754066879814546, -0.46211715726000974],
        },
        20 => Expected {
            bits: &[1059019135, 1055693471],
            exact: &[],
            argmax: None,
            ideal: &[0.6224593312018546, 0.46211715726000974],
        },
        21 => Expected {
            bits: &[
                1051372203, 1051372203, 1051372203, 0, 0, 0, 1065353216, 1065353216, 1065353216,
                1077936128,
            ],
            exact: &[0, 1, 2, 3, 4, 5],
            argmax: Some(0),
            ideal: &[],
        },
        22 => Expected {
            bits: &[
                1059736891, 1048222234, 1035493875, 0, 3212836864, 3221225472, 1065353216,
                1052531378, 1040880981, 1069574487,
            ],
            exact: &[3, 4, 5],
            argmax: Some(0),
            ideal: &[],
        },
        23 => Expected {
            bits: &[
                580697234, 1065353216, 822977587, 3256877056, 0, 3248488448, 580697234, 1065353216,
                822977587, 1065353216,
            ],
            exact: &[3, 4, 5],
            argmax: Some(1),
            ideal: &[],
        },
        24 => Expected {
            bits: &[
                1051372203, 1051372203, 1051372203, 0, 0, 0, 1065353216, 1065353216, 1065353216,
                1077936128,
            ],
            exact: &[0, 1, 2, 3, 4, 5],
            argmax: Some(0),
            ideal: &[],
        },
        25 => Expected {
            bits: &[
                0, 0, 1065353216, 3271557120, 3279945728, 0, 0, 0, 1065353216, 1065353216,
            ],
            exact: &[3, 4, 5],
            argmax: Some(2),
            ideal: &[],
        },
        26 => Expected {
            bits: &[
                1051372203, 1051372203, 1051372203, 0, 0, 0, 1065353216, 1065353216, 1065353216,
                1077936128,
            ],
            exact: &[0, 1, 2, 3, 4, 5],
            argmax: Some(0),
            ideal: &[],
        },
        27 => Expected {
            bits: &[
                1042224979, 1054358060, 1054358060, 3212836864, 0, 0, 1052531378, 1065353216,
                1065353216, 1075284822,
            ],
            exact: &[3, 4, 5],
            argmax: Some(1),
            ideal: &[],
        },
        28 => Expected {
            bits: &[
                1059736891, 1048222234, 1035493875, 0, 3212836864, 3221225472, 1065353216,
                1052531378, 1040880981, 1069574487,
            ],
            exact: &[3, 4, 5],
            argmax: Some(0),
            ideal: &[],
        },
        29 => Expected {
            bits: &[
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                1056964608, 1056964608, 1056964608, 1056964608, 0, 0, 1056964608, 1056964608,
            ],
            exact: &[
                4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                26, 27,
            ],
            argmax: None,
            ideal: &[],
        },
        30 => Expected {
            bits: &[
                1052964822, 3203844739, 1065353216, 3221225472, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1056964608, 1056964608, 1056964608, 1056964608,
                0, 0, 1056964608, 1056964608,
            ],
            exact: &[
                4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                26, 27,
            ],
            argmax: None,
            ideal: &[],
        },
        31 => Expected {
            bits: &[
                1059524583, 3188276023, 1068435774, 3215249308, 1052770304, 3208642560, 1059061760,
                3187671040, 1065353216, 3212836864, 1059061760, 3213885440, 1031798784, 3191865344,
                3190816768, 1051197440, 3175088128, 1045430272, 1054867456, 3205496832, 1054867456,
                3211788288, 1055391744, 1045430272, 1064566784, 3209428992, 1065877504, 3218604032,
                1058770896, 1049629797, 1058833287, 1057813659, 1061011181, 3207170965, 1061044290,
                1042274806,
            ],
            exact: &[
                4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                26, 27,
            ],
            argmax: None,
            ideal: &[],
        },
        32 => Expected {
            bits: &[
                3185934882, 3189177470, 3190723759, 3199775247, 3183476736, 3204579328, 1039138816,
                1050148864, 3201564672, 1045954560, 1053294592, 3166699520, 3175088128, 3190292480,
                1015021568, 1044905984, 3199991808, 1038090240, 1049886720, 3196846080, 3188719616,
                3207331840, 1040711680, 1056702464, 3209166848, 1050935296, 1059979264, 3197632512,
                1055786900, 1051531378, 1057520847, 1058988303, 3207022277, 1050582201, 1059710517,
                1054492371,
            ],
            exact: &[
                4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                26, 27,
            ],
            argmax: None,
            ideal: &[],
        },
        33 => Expected {
            bits: &[
                3173742969, 1051012685, 3188150485, 1055008309, 3200757269, 1061494456, 3200086180,
                1061830000, 3186999952, 1065688760, 3184315596, 1065856532, 3195388558, 1062668862,
                3200421725, 1060320052, 3204783800, 1057971241, 3207132610, 1044549468, 3206629294,
                1070470267, 3208642560, 1069463634, 3206797066, 1070386380, 3208810332, 1067450368,
                1051847911, 1062558323, 1050952369, 1062267521, 3205537090, 1064039032, 1050879387,
                1061616928,
            ],
            exact: &[
                4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                26, 27,
            ],
            argmax: None,
            ideal: &[],
        },
        _ => panic!("fixed expected index"),
    }
}
