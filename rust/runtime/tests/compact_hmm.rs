// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-compact-hmm")]
use libapta_runtime::compact_hmm::*;
use std::sync::atomic::{AtomicBool, Ordering};

fn tiny(logs: &[f64; 4]) -> Graph<'_> {
    Graph {
        pointers: &[0, 2, 4],
        previous: &[0, 1, 0, 1],
        log_probabilities: logs,
        classes: &[0, 1],
        initial_log_probability: 0.5_f64.ln(),
    }
}
// Exhaustive path enumeration: no DP, checkpoints or production implementation imports.
fn brute(g: Graph<'_>, d: &[[f64; 3]]) -> (f64, Vec<u16>) {
    let states = g.classes.len();
    let mut best = f64::NEG_INFINITY;
    let mut best_path = Vec::new();
    let mut best_rank = Vec::new();
    for mut code in 0..states.pow((d.len() + 1) as u32) {
        let mut sequence = Vec::new();
        for _ in 0..=d.len() {
            sequence.push((code % states) as u16);
            code /= states;
        }
        let mut score = g.initial_log_probability;
        let mut valid = true;
        for row in 0..d.len() {
            let state = sequence[row + 1] as usize;
            let lo = g.pointers[state] as usize;
            let hi = g.pointers[state + 1] as usize;
            if let Ok(k) = g.previous[lo..hi].binary_search(&sequence[row]) {
                score = (score + g.log_probabilities[lo + k]) + d[row][g.classes[state] as usize];
            } else {
                valid = false;
                break;
            }
        }
        if !valid {
            continue;
        }
        let rank: Vec<_> = sequence[1..]
            .iter()
            .rev()
            .copied()
            .chain([sequence[0]])
            .collect();
        if score > best || (score == best && (best_rank.is_empty() || rank < best_rank)) {
            best = score;
            best_path = sequence[1..].to_vec();
            best_rank = rank;
        }
    }
    (best, best_path)
}
// Independent dense oracle, intentionally bounded to <=513 generated rows.
fn monolithic(g: Graph<'_>, d: &[[f64; 3]]) -> (f64, Vec<u16>) {
    assert!(d.len() <= 513);
    let s = g.classes.len();
    let mut scores = vec![g.initial_log_probability; s];
    let mut parents = vec![u16::MAX; d.len() * s];
    for (row, obs) in d.iter().enumerate() {
        let mut next = vec![f64::NEG_INFINITY; s];
        for state in 0..s {
            for k in g.pointers[state] as usize..g.pointers[state + 1] as usize {
                let p = g.previous[k] as usize;
                let candidate =
                    (scores[p] + g.log_probabilities[k]) + obs[g.classes[state] as usize];
                if candidate > next[state] {
                    next[state] = candidate;
                    parents[row * s + state] = p as u16;
                }
            }
        }
        scores = next;
    }
    let mut state = 0;
    for i in 1..s {
        if scores[i] > scores[state] {
            state = i;
        }
    }
    let score = scores[state];
    let mut path = vec![0; d.len()];
    for row in (0..d.len()).rev() {
        path[row] = state as u16;
        if row > 0 {
            state = parents[row * s + state] as usize;
        }
    }
    (score, path)
}
fn decode(g: Graph<'_>, density: &[[f64; 3]], block: usize, quota: usize) -> (f64, Vec<u16>) {
    let mut work = Workspace::allocate(
        StoragePlan::new(g.classes.len(), density.len(), block, Mode::Trace).unwrap(),
    )
    .unwrap();
    let mut d = Decoder::new(g, density, block, Mode::Trace, &mut work).unwrap();
    let cancel = AtomicBool::new(false);
    while d.status() == Status::Running {
        assert!(d.result().is_none());
        let report = d.step(quota, &cancel).unwrap();
        assert!(report.units <= quota);
    }
    assert_eq!(d.status(), Status::Decoded);
    let result = d.result().unwrap();
    (result.score, result.path.unwrap().to_vec())
}
#[test]
fn exhaustive_oracle_ties_blocks_and_work_quota_invariance() {
    let half = 0.5_f64.ln();
    let logs = [half; 4];
    let g = tiny(&logs);
    for rows in [1, 3, 8] {
        let density = vec![[half, half, f64::NEG_INFINITY]; rows];
        let oracle = brute(g, &density);
        assert_eq!(oracle.1, vec![0; rows]);
        for block in [1, 2, 3, 256] {
            for quota in [1, 7, 256, 4096, usize::MAX] {
                let actual = decode(g, &density, block, quota);
                assert_eq!(actual.0.to_bits(), oracle.0.to_bits());
                assert_eq!(actual.1, oracle.1);
            }
        }
    }
}
#[test]
fn exhaustive_nontrivial_oracle_matches_exact_score_and_path() {
    let logs = [0.8_f64.ln(), 0.2_f64.ln(), 0.2_f64.ln(), 0.8_f64.ln()];
    let g = tiny(&logs);
    let a = 0.9_f64.ln();
    let b = 0.1_f64.ln();
    let density = [
        [a, b, f64::NEG_INFINITY],
        [b, a, f64::NEG_INFINITY],
        [a, b, f64::NEG_INFINITY],
    ];
    let oracle = brute(g, &density);
    assert_eq!(oracle.1, [0, 0, 0]);
    let actual = decode(g, &density, 2, 7);
    assert_eq!(actual.0.to_bits(), oracle.0.to_bits());
    assert_eq!(actual.1, oracle.1);
}
#[test]
fn full_graph_partial_blocks_match_monolithic_oracle() {
    for (meter, rows) in [(3, 257), (4, 513)] {
        let graph = synthetic::load(meter).unwrap();
        let density = synthetic::densities(rows, meter).unwrap();
        let expected = monolithic(graph.graph(), &density);
        let actual = decode(graph.graph(), &density, 256, 4096);
        assert_eq!(actual.0.to_bits(), expected.0.to_bits());
        assert_eq!(actual.1, expected.1);
    }
}
#[test]
fn cancellation_at_every_phase_fences_partial_path_and_cannot_resume() {
    let logs = [0.5_f64.ln(); 4];
    let g = tiny(&logs);
    let density = [[-0.5; 3]; 7];
    for phase in [
        Phase::Initialize,
        Phase::SaveCheckpoint,
        Phase::Forward,
        Phase::SelectFinal,
        Phase::Restore,
        Phase::Replay,
        Phase::VerifyReplay,
        Phase::Traceback,
    ] {
        let mut work =
            Workspace::allocate(StoragePlan::new(2, 7, 2, Mode::Trace).unwrap()).unwrap();
        let flag = AtomicBool::new(false);
        {
            let mut d = Decoder::new(g, &density, 2, Mode::Trace, &mut work).unwrap();
            let mut steps = 0;
            while d.phase() != phase {
                d.step(1, &flag).unwrap();
                steps += 1;
                assert!(steps < 10000);
            }
            assert!(d.result().is_none());
            flag.store(true, Ordering::Release);
            d.step(usize::MAX, &flag).unwrap();
            assert_eq!(d.status(), Status::Cancelled);
            assert!(d.result().is_none());
            flag.store(false, Ordering::Release);
            d.step(100, &flag).unwrap();
            assert_eq!(d.status(), Status::Cancelled);
            assert!(d.result().is_none());
        }
        let mut next = Decoder::new(g, &density, 2, Mode::Trace, &mut work).unwrap();
        while next.status() == Status::Running {
            next.step(256, &flag).unwrap();
        }
        assert_eq!(next.status(), Status::Decoded);
    }
}
#[test]
fn no_path_and_empty_input_do_not_expose_results() {
    let logs = [0.5_f64.ln(); 4];
    let g = tiny(&logs);
    for density in [vec![], vec![[f64::NEG_INFINITY; 3]; 3]] {
        let mut work =
            Workspace::allocate(StoragePlan::new(2, density.len(), 2, Mode::Trace).unwrap())
                .unwrap();
        let mut d = Decoder::new(g, &density, 2, Mode::Trace, &mut work).unwrap();
        d.step(usize::MAX, &AtomicBool::new(false)).unwrap();
        assert_eq!(
            d.status(),
            if density.is_empty() {
                Status::NoEvidence
            } else {
                Status::NoPath
            }
        );
        assert!(d.result().is_none());
    }
}
#[test]
fn zero_budget_is_explicit_and_does_not_change_progress() {
    let logs = [0.5_f64.ln(); 4];
    let g = tiny(&logs);
    let density = [[-1.0; 3]; 3];
    let mut work = Workspace::allocate(StoragePlan::new(2, 3, 2, Mode::Trace).unwrap()).unwrap();
    let mut d = Decoder::new(g, &density, 2, Mode::Trace, &mut work).unwrap();
    assert!(matches!(
        d.step(0, &AtomicBool::new(false)),
        Err(Error::ZeroBudget)
    ));
    assert_eq!(d.total_units(), 0);
    assert_eq!(d.phase(), Phase::Initialize);
    assert!(d.result().is_none());
}
#[test]
fn malformed_graphs_and_nonfinite_transitions_are_rejected() {
    let logs = [-1.0; 4];
    let good = tiny(&logs);
    for g in [
        Graph {
            pointers: &[0, 3, 2],
            ..good
        },
        Graph {
            pointers: &[1, 2, 4],
            ..good
        },
        Graph {
            previous: &[0, 0, 0, 1],
            ..good
        },
        Graph {
            previous: &[1, 0, 0, 1],
            ..good
        },
        Graph {
            previous: &[0, 65535, 0, 1],
            ..good
        },
        Graph {
            classes: &[0, 3],
            ..good
        },
        Graph {
            log_probabilities: &[f64::NAN, -1.0, -1.0, -1.0],
            ..good
        },
        Graph {
            initial_log_probability: f64::INFINITY,
            ..good
        },
        Graph {
            log_probabilities: &[f64::NEG_INFINITY; 4],
            ..good
        },
    ] {
        assert_eq!(g.validate(), Err(Error::InvalidGraph));
    }
}
#[test]
fn capacity_and_workspace_refusals_precede_processing() {
    assert_eq!(
        StoragePlan::new(2, 30001, 256, Mode::Trace),
        Err(Error::Capacity)
    );
    assert_eq!(
        StoragePlan::new(11221, 3, 256, Mode::Trace),
        Err(Error::Capacity)
    );
    for block in [0, 257] {
        assert_eq!(
            StoragePlan::new(2, 3, block, Mode::Trace),
            Err(Error::Capacity)
        );
    }
    let logs = [-1.0; 4];
    let graph = tiny(&logs);
    let mut work = Workspace::allocate(StoragePlan::new(2, 3, 256, Mode::Score).unwrap()).unwrap();
    assert!(matches!(
        Decoder::new(graph, &[[-1.0; 3]; 3], 256, Mode::Trace, &mut work),
        Err(Error::Capacity)
    ));
}
#[test]
fn invalid_density_values_are_not_clamped() {
    let logs = [-1.0; 4];
    let graph = tiny(&logs);
    let mut work = Workspace::allocate(StoragePlan::new(2, 1, 1, Mode::Trace).unwrap()).unwrap();
    for value in [f64::NAN, f64::INFINITY, 0.1, -1025.0] {
        assert!(matches!(
            Decoder::new(graph, &[[value; 3]], 1, Mode::Trace, &mut work),
            Err(Error::InvalidDensity)
        ));
    }
}
#[test]
fn score_mode_needs_no_trace_and_completed_result_is_stable() {
    let logs = [0.5_f64.ln(); 4];
    let graph = tiny(&logs);
    let density = [[-0.5; 3]; 7];
    let plan = StoragePlan::new(2, 7, 256, Mode::Score).unwrap();
    assert_eq!(plan.payload_bytes(), 32);
    let mut work = Workspace::allocate(plan).unwrap();
    let mut d = Decoder::new(graph, &density, 256, Mode::Score, &mut work).unwrap();
    d.step(usize::MAX, &AtomicBool::new(false)).unwrap();
    let score = d.result().unwrap().score.to_bits();
    assert_eq!(d.status(), Status::Scored);
    assert!(d.result().unwrap().path.is_none());
    d.cancel();
    d.step(1, &AtomicBool::new(true)).unwrap();
    assert_eq!(d.result().unwrap().score.to_bits(), score);
}
#[test]
fn maximum_trace_payload_matches_registered_formula() {
    assert_eq!(
        StoragePlan::new(11220, 30000, 256, Mode::Trace)
            .unwrap()
            .payload_bytes(),
        16575840
    );
}
#[test]
fn executable_refuses_arbitrary_inputs_and_case_names() {
    let binary = env!("CARGO_BIN_EXE_apta-compact-hmm-synthetic");
    for args in [
        vec!["--input", "music.wav"],
        vec!["--case", "real"],
        vec!["--case", "small3", "extra"],
    ] {
        let output = std::process::Command::new(binary)
            .args(args)
            .output()
            .unwrap();
        assert!(!output.status.success());
        assert!(output.stdout.is_empty());
    }
}
