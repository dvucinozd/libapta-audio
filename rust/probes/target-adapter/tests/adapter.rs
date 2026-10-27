// SPDX-License-Identifier: Apache-2.0
use libapta_resource_probe_core as p;
use libapta_target_probe_adapter::*;
use std::sync::atomic::{AtomicBool, Ordering};

struct Fake {
    now: u64,
    delta: u64,
    reads: usize,
    hz: u64,
}
impl Clock for Fake {
    fn hz(&self) -> u64 {
        self.hz
    }
    fn ticks(&mut self) -> u64 {
        self.reads += 1;
        let v = self.now;
        self.now = self.now.wrapping_add(self.delta);
        v
    }
}
fn clock() -> Fake {
    Fake {
        now: 0,
        delta: 1,
        reads: 0,
        hz: 1000000,
    }
}
fn input() -> (Vec<f32>, Vec<f32>, Vec<f32>) {
    (
        (0..p::INPUTS).map(p::input_value).collect(),
        (0..p::WEIGHTS).map(p::weight_value).collect(),
        vec![7.; p::OUTPUTS],
    )
}
fn identity() -> Identity<'static> {
    Identity {
        run_id: "synthetic-run",
        unit_id: "synthetic-unit",
        image_id: "synthetic-image",
        elf_sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        source_revision: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        protocol_sha256: "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        chip_revision: 302,
        kind: EvidenceKind::Synthetic,
    }
}
fn observation() -> Observation<'static> {
    Observation {
        id: "trial-0",
        case: "TP-03",
        variant: "projection32",
        phase: "whole_trial",
        trial: 0,
        sequence: 0,
        status: ObservationStatus::Measured,
        start: 1,
        end: 3,
        elapsed: 2,
        hz: 1000000,
        result_sha256: None,
    }
}

#[test]
fn full_projection_clocked_steps_stay_in_phase_and_counts_match() {
    let (x, w, mut out) = input();
    let job = p::Projection::new(&x, &w, &mut out).unwrap();
    let mut c = clock();
    let flag = AtomicBool::new(false);
    let mut trial = Trial::new(job, &mut c, 10000000).unwrap();
    let mut calls = 0;
    while trial.status() == Status::Running {
        let r = trial.step(32, &flag);
        assert!(r.units > 0 && r.units <= 32);
        assert!(r.error.is_none());
        calls += 1;
    }
    assert_eq!(trial.status(), Status::Complete);
    assert_eq!(trial.stats().core_units, p::PROJECTION_UNITS);
    assert_eq!(trial.complete_job().unwrap().progress().macs, 360000);
    assert_eq!(trial.complete_job().unwrap().result().unwrap().len(), 2400);
    assert_eq!(trial.stats().phase_ticks.iter().sum::<u64>(), calls);
    let stats = trial.stats();
    flag.store(true, Ordering::Release);
    assert_eq!(trial.step(32, &flag).units, 0);
    assert_eq!(trial.status(), Status::Complete);
    assert_eq!(trial.stats().end, stats.end);
}

#[test]
fn full_copy_and_nonlinear_boundary_budgets_match_core() {
    let flag = AtomicBool::new(false);
    let mut a = vec![0; p::COPY_WORDS];
    let mut b = vec![0; p::COPY_WORDS];
    let mut c = clock();
    let mut t = Trial::new(p::CopyProbe::new(&mut a, &mut b).unwrap(), &mut c, 10000000).unwrap();
    while t.status() == Status::Running {
        t.step(31, &flag);
    }
    assert_eq!(t.status(), Status::Complete);
    assert_eq!(t.stats().core_units, p::COPY_UNITS);
    assert!(
        t.stats().phase_ticks[0] > 0
            && t.stats().phase_ticks[1] > 0
            && t.stats().phase_ticks[2] > 0
            && t.stats().phase_ticks[3] > 0
    );
    let mut out = vec![7.; p::NONLINEAR_OUTPUTS];
    let mut c = clock();
    let mut t = Trial::new(
        p::NonlinearProbe::new_logs(p::LogKind::LogDiv15, &mut out).unwrap(),
        &mut c,
        10000000,
    )
    .unwrap();
    while t.status() == Status::Running {
        assert!(t.step(32, &flag).units <= 1);
    }
    assert_eq!(t.status(), Status::Complete);
    assert_eq!(t.stats().core_units, p::NONLINEAR_UNITS);
}

#[test]
fn zero_and_large_quotas_are_inert_and_pending_cancel_cannot_resume() {
    let (x, w, mut out) = input();
    let mut c = clock();
    let flag = AtomicBool::new(true);
    let mut t = Trial::new(
        p::Projection::new(&x, &w, &mut out).unwrap(),
        &mut c,
        10000000,
    )
    .unwrap();
    assert_eq!(t.step(0, &flag).error, Some(Error::ZeroBudget));
    assert_eq!(t.step(33, &flag).error, Some(Error::Quota));
    assert_eq!(t.stats().core_units, 0);
    assert_eq!(t.step(32, &flag).status, Status::Cancelled);
    assert!(t.complete_job().is_none());
    flag.store(false, Ordering::Release);
    assert_eq!(t.step(32, &flag).units, 0);
    assert_eq!(t.status(), Status::Cancelled);
}

#[test]
fn backwards_clock_and_pre_work_timeout_fence_outputs() {
    for reversed in [false, true] {
        let (x, w, mut out) = input();
        let mut c = Fake {
            now: if reversed { u64::MAX } else { 0 },
            delta: 1,
            reads: 0,
            hz: 1000000,
        };
        let flag = AtomicBool::new(false);
        let mut t = Trial::new(p::Projection::new(&x, &w, &mut out).unwrap(), &mut c, 1).unwrap();
        let r = t.step(32, &flag);
        assert_eq!(r.units, 0);
        assert_eq!(t.status(), Status::Failed);
        assert_eq!(
            r.error,
            Some(if reversed {
                Error::Clock
            } else {
                Error::Timeout
            })
        );
        assert!(t.complete_job().is_none());
    }
}

#[test]
fn post_work_timeout_hides_even_numerically_ready_result() {
    let flag = AtomicBool::new(false);
    let mut b = [0.; p::NONLINEAR_OUTPUTS];
    let mut c3 = Fake {
        now: 0,
        delta: 2,
        reads: 0,
        hz: 1000000,
    };
    let job = p::NonlinearProbe::new_activation(p::Activation::Tanh, &mut b).unwrap();
    // Every one-unit nonlinear quantum has two reads with delta2. On the last
    // call pre-work is4*N-2,post-work4*N,limit4*N-1:core Ready is hidden.
    let mut late = Trial::new(job, &mut c3, 4 * p::NONLINEAR_UNITS - 1).unwrap();
    while late.status() == Status::Running {
        late.step(32, &flag);
    }
    assert_eq!(late.stats().core_units, p::NONLINEAR_UNITS);
    assert!(late.complete_job().is_none());
    assert_eq!(late.status(), Status::Failed);
    assert_eq!(late.step(1, &flag).error, Some(Error::Timeout));
}

#[test]
fn fresh_job_and_valid_clock_budget_are_required() {
    let flag = AtomicBool::new(false);
    let (x, w, mut out) = input();
    let mut c = clock();
    let mut job = p::Projection::new(&x, &w, &mut out).unwrap();
    job.step(1, &flag);
    assert!(matches!(
        Trial::new(job, &mut c, 10000000),
        Err(Error::Input)
    ));
    for (hz, limit) in [
        (0, 1),
        (999, 1),
        (1000000001, 1),
        (1000000, 0),
        (1000000, 10000001),
    ] {
        let mut c = Fake {
            now: 0,
            delta: 1,
            reads: 0,
            hz,
        };
        let job = p::Projection::new(&x, &w, &mut out).unwrap();
        assert!(matches!(Trial::new(job, &mut c, limit), Err(Error::Input)));
        assert_eq!(c.reads, 0);
    }
}

#[test]
fn formatter_capacity_tokens_domains_and_terminal() {
    let id = identity();
    let obs = observation();
    let mut out = [0u8; 8192];
    let n = encode_observation(&id, &obs, &mut out).unwrap();
    let text = std::str::from_utf8(&out[..n]).unwrap();
    assert!(text.starts_with("M1_RESOURCE_PROBE_V1 "));
    assert!(text.ends_with("}\n"));
    assert!(text.contains("\"free_bytes\":null"));
    assert!(text.contains("\"evidence_kind\":\"synthetic\""));
    assert!(!text.contains("NOT_QUALIFIED"));
    assert_eq!(
        encode_observation(&id, &obs, &mut [0; 10]),
        Err(Error::Capacity)
    );
    assert!(encode_end(&id, 1, &mut out).is_ok());
    assert_eq!(encode_end(&id, 0, &mut out), Err(Error::Input));
    let mut bad = identity();
    bad.unit_id = "quote\"";
    assert_eq!(encode_observation(&bad, &obs, &mut out), Err(Error::Input));
    let mut bad = observation();
    bad.end = 4;
    assert_eq!(encode_observation(&id, &bad, &mut out), Err(Error::Input));
    bad = observation();
    bad.result_sha256 = Some("invalid");
    assert_eq!(encode_observation(&id, &bad, &mut out), Err(Error::Input));
}
