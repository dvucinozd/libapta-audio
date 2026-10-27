// SPDX-License-Identifier: Apache-2.0
use libapta_resource_probe_core::*;
use std::sync::atomic::{AtomicBool, Ordering};

const COPY: &[u8] =
    include_bytes!("../../../../docs/rust/fixtures/resource-probe-core-rc04a2/copy.u32le");
const PROJECTION: &[u8] =
    include_bytes!("../../../../docs/rust/fixtures/resource-probe-core-rc04a2/projection.i32le");
const NONLINEAR: &[u8] =
    include_bytes!("../../../../docs/rust/fixtures/resource-probe-core-rc04a2/nonlinear.f64le");
const QUOTAS: [usize; 4] = [1, 31, 4096, usize::MAX];
fn ref_dot(index: usize) -> f64 {
    i32::from_le_bytes(PROJECTION[4 * index..4 * index + 4].try_into().unwrap()) as f64 / 2048.
}
fn ref_nonlinear(index: usize) -> f64 {
    f64::from_le_bytes(NONLINEAR[index * 8..index * 8 + 8].try_into().unwrap())
}
fn p32() -> (Vec<f32>, Vec<f32>, Vec<f32>) {
    (
        (0..INPUTS).map(input_value).collect(),
        (0..WEIGHTS).map(weight_value).collect(),
        vec![7.; OUTPUTS + 1],
    )
}

#[test]
fn copy_all_words_32_passes_four_quotas_exact_work_and_tails() {
    let flag = AtomicBool::new(false);
    for quota in QUOTAS {
        let mut a = vec![7; COPY_WORDS + 1];
        let mut b = vec![9; COPY_WORDS + 1];
        {
            let mut c = CopyProbe::new(&mut a, &mut b).unwrap();
            while c.status() == Status::Running {
                assert!(c.result().is_none());
                let r = c.step(quota, &flag);
                assert!(r.units > 0 && r.units <= quota);
                assert!(r.error.is_none());
            }
            assert_eq!(c.status(), Status::Ready);
            assert_eq!(c.progress().units, COPY_UNITS);
            assert_eq!(c.progress().passes, 32);
            assert_eq!(c.progress().outputs, COPY_WORDS);
            let v = c.result().unwrap();
            assert_eq!(v.source, v.destination);
            for (i, word) in v.destination.iter().enumerate() {
                assert_eq!(
                    *word,
                    u32::from_le_bytes(COPY[4 * i..4 * i + 4].try_into().unwrap())
                );
            }
            let before = c.progress();
            flag.store(true, Ordering::Release);
            assert_eq!(c.step(1, &flag).units, 0);
            assert_eq!(c.status(), Status::Ready);
            assert_eq!(c.progress(), before);
            flag.store(false, Ordering::Release);
        }
        assert_eq!(a[COPY_WORDS], 7);
        assert_eq!(b[COPY_WORDS], 9);
    }
}

#[test]
fn projection_both_dtypes_all_outputs_four_quotas_exact_bits_and_macs() {
    let flag = AtomicBool::new(false);
    let mut baseline32 = None;
    let mut baseline64 = None;
    for quota in QUOTAS {
        let (x, w, mut out) = p32();
        {
            let mut p = Projection::new(&x, &w, &mut out).unwrap();
            while p.status() == Status::Running {
                assert!(p.result().is_none());
                let r = p.step(quota, &flag);
                assert!(r.units > 0 && r.units <= quota);
                assert!(r.error.is_none());
            }
            assert_eq!(p.status(), Status::Ready);
            assert_eq!(p.progress().units, PROJECTION_UNITS);
            assert_eq!(p.progress().macs, 360000);
            assert_eq!(p.progress().outputs, 2400);
            for (i, v) in p.result().unwrap().iter().enumerate() {
                assert_eq!(v.to_bits(), (ref_dot(i) as f32).to_bits());
            }
        }
        assert_eq!(out[OUTPUTS], 7.);
        let bits = out.iter().map(|x| x.to_bits()).collect::<Vec<_>>();
        if let Some(ref previous) = baseline32 {
            assert_eq!(&bits, previous);
        } else {
            baseline32 = Some(bits);
        }
        let x = (0..INPUTS).map(input_value::<f64>).collect::<Vec<_>>();
        let w = (0..WEIGHTS).map(weight_value::<f64>).collect::<Vec<_>>();
        let mut out = vec![8f64; OUTPUTS + 1];
        {
            let mut p = Projection::new(&x, &w, &mut out).unwrap();
            while p.status() == Status::Running {
                p.step(quota, &flag);
            }
            assert_eq!(p.status(), Status::Ready);
            assert_eq!(p.progress().units, PROJECTION_UNITS);
            assert_eq!(p.progress().macs, 360000);
            for (i, v) in p.result().unwrap().iter().enumerate() {
                assert_eq!(v.to_bits(), ref_dot(i).to_bits());
            }
        }
        assert_eq!(out[OUTPUTS], 8.);
        let bits = out.iter().map(|x| x.to_bits()).collect::<Vec<_>>();
        if let Some(ref previous) = baseline64 {
            assert_eq!(&bits, previous);
        } else {
            baseline64 = Some(bits);
        }
    }
}

#[test]
fn nonlinear_every_value_four_schedules_scalar_gates_and_exact_zero_logs() {
    let flag = AtomicBool::new(false);
    for kind in [Activation::Sigmoid, Activation::Tanh] {
        let mut baseline = None;
        for quota in QUOTAS {
            let mut out = vec![7f32; NONLINEAR_OUTPUTS + 1];
            {
                let mut p = NonlinearProbe::new_activation(kind, &mut out).unwrap();
                while p.status() == Status::Running {
                    let r = p.step(quota, &flag);
                    assert!(r.units > 0 && r.units <= quota);
                }
                assert_eq!(p.status(), Status::Ready);
                assert_eq!(p.progress().units, NONLINEAR_UNITS);
                assert_eq!(p.progress().outputs, NONLINEAR_OUTPUTS);
                let Some(NonlinearView::F32(values)) = p.result() else {
                    panic!("f32 view")
                };
                for (i, v) in values.iter().enumerate() {
                    let reference =
                        ref_nonlinear((if kind == Activation::Sigmoid { 0 } else { 7 }) + i % 7);
                    assert!((f64::from(*v) - reference).abs() <= 2e-6 + 2e-5 * reference.abs());
                }
            }
            assert_eq!(out[NONLINEAR_OUTPUTS], 7.);
            let bits = out.iter().map(|v| v.to_bits()).collect::<Vec<_>>();
            if let Some(ref old) = baseline {
                assert_eq!(&bits, old);
            } else {
                baseline = Some(bits);
            }
        }
    }
    for kind in [LogKind::Log, LogKind::LogDiv15] {
        let mut baseline = None;
        for quota in QUOTAS {
            let mut out = vec![7f64; NONLINEAR_OUTPUTS + 1];
            {
                let mut p = NonlinearProbe::new_logs(kind, &mut out).unwrap();
                while p.status() == Status::Running {
                    p.step(quota, &flag);
                }
                assert_eq!(p.status(), Status::Ready);
                assert_eq!(p.progress().units, NONLINEAR_UNITS);
                let Some(NonlinearView::F64(values)) = p.result() else {
                    panic!("f64 view")
                };
                for (i, v) in values.iter().enumerate() {
                    let reference =
                        ref_nonlinear((if kind == LogKind::Log { 14 } else { 21 }) + i % 7);
                    if reference == f64::NEG_INFINITY {
                        assert_eq!(*v, reference);
                    } else {
                        assert!(v.is_finite() && (*v - reference).abs() <= 4e-13);
                    }
                }
            }
            assert_eq!(out[NONLINEAR_OUTPUTS], 7.);
            let bits = out.iter().map(|v| v.to_bits()).collect::<Vec<_>>();
            if let Some(ref old) = baseline {
                assert_eq!(&bits, old);
            } else {
                baseline = Some(bits);
            }
        }
    }
}

#[test]
fn capacity_failures_do_not_touch_any_storage() {
    let mut a = vec![7; COPY_WORDS];
    let mut b = vec![8; COPY_WORDS];
    assert!(matches!(
        CopyProbe::new(&mut a[..COPY_WORDS - 1], &mut b),
        Err(Error::Capacity)
    ));
    assert!(matches!(
        CopyProbe::new(&mut a, &mut b[..COPY_WORDS - 1]),
        Err(Error::Capacity)
    ));
    assert!(a.iter().all(|v| *v == 7));
    assert!(b.iter().all(|v| *v == 8));
    let (x, w, mut out) = p32();
    assert!(matches!(
        Projection::new(&x[..INPUTS - 1], &w, &mut out),
        Err(Error::Capacity)
    ));
    assert!(matches!(
        Projection::new(&x, &w[..WEIGHTS - 1], &mut out),
        Err(Error::Capacity)
    ));
    assert!(matches!(
        Projection::new(&x, &w, &mut out[..OUTPUTS - 1]),
        Err(Error::Capacity)
    ));
    assert!(out.iter().all(|v| *v == 7.));
    let mut f32s = vec![7.; NONLINEAR_OUTPUTS - 1];
    let mut f64s = vec![8.; NONLINEAR_OUTPUTS - 1];
    assert!(matches!(
        NonlinearProbe::new_activation(Activation::Sigmoid, &mut f32s),
        Err(Error::Capacity)
    ));
    assert!(matches!(
        NonlinearProbe::new_logs(LogKind::Log, &mut f64s),
        Err(Error::Capacity)
    ));
    assert!(f32s.iter().all(|v| *v == 7.));
    assert!(f64s.iter().all(|v| *v == 8.));
}

#[test]
fn projection_malformed_fixed_inputs_fail_before_first_output_write() {
    let flag = AtomicBool::new(false);
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 0.5] {
        for weights in [false, true] {
            let (mut x, mut w, mut out) = p32();
            if weights {
                w[WEIGHTS - 1] = bad;
            } else {
                x[INPUTS - 1] = bad;
            }
            {
                let mut p = Projection::new(&x, &w, &mut out).unwrap();
                let r = p.step(usize::MAX, &flag);
                assert_eq!(r.error, Some(Error::InvalidInput));
                assert_eq!(p.status(), Status::Failed);
                assert_eq!(p.progress().outputs, 0);
                assert!(p.result().is_none());
                assert_eq!(p.step(1, &flag).units, 0);
            }
            assert!(out.iter().all(|v| *v == 7.));
        }
    }
}

#[test]
fn cancellation_each_copy_phase_is_terminal_and_zero_budget_is_inert() {
    let flag = AtomicBool::new(false);
    for phase in [
        Phase::Initialize,
        Phase::Prepare,
        Phase::Copy,
        Phase::Verify,
        Phase::Finalize,
    ] {
        let mut a = vec![7; COPY_WORDS];
        let mut b = vec![8; COPY_WORDS];
        let mut c = CopyProbe::new(&mut a, &mut b).unwrap();
        while c.phase() != phase {
            c.step(1, &flag);
        }
        let before = c.progress();
        flag.store(true, Ordering::Release);
        let r = c.step(0, &flag);
        assert_eq!(r.error, Some(Error::ZeroBudget));
        assert_eq!(r.units, 0);
        assert_eq!(c.progress(), before);
        assert_eq!(c.phase(), phase);
        let r = c.step(31, &flag);
        assert_eq!(r.units, 0);
        assert_eq!(c.status(), Status::Cancelled);
        assert_eq!(c.progress().cancellation_phase, Some(phase));
        assert!(c.result().is_none());
        flag.store(false, Ordering::Release);
        assert_eq!(c.step(1, &flag).units, 0);
        assert_eq!(c.status(), Status::Cancelled);
    }
}

#[test]
fn cancellation_projection_each_phase_and_private_staged_output() {
    let flag = AtomicBool::new(false);
    for phase in [
        Phase::ValidateInput,
        Phase::ValidateWeights,
        Phase::Dot,
        Phase::Commit,
        Phase::Finalize,
    ] {
        let (x, w, mut out) = p32();
        let outputs;
        {
            let mut p = Projection::new(&x, &w, &mut out).unwrap();
            while p.phase() != phase {
                p.step(1, &flag);
            }
            let before = p.progress();
            assert_eq!(p.step(0, &flag).error, Some(Error::ZeroBudget));
            flag.store(true, Ordering::Release);
            assert_eq!(p.step(1, &flag).units, 0);
            assert_eq!(p.status(), Status::Cancelled);
            assert_eq!(p.progress().cancellation_phase, Some(phase));
            assert!(p.result().is_none());
            assert_eq!(p.progress().outputs, before.outputs);
            outputs = before.outputs;
            flag.store(false, Ordering::Release);
            assert_eq!(p.step(1, &flag).units, 0);
        }
        assert!(out[outputs..].iter().all(|v| *v == 7.));
    }
}

#[test]
fn cancellation_nonlinear_eval_commit_finalize_and_ready_stability() {
    let flag = AtomicBool::new(false);
    for phase in [Phase::Evaluate, Phase::Commit, Phase::Finalize] {
        let mut out = vec![7f64; NONLINEAR_OUTPUTS + 1];
        let outputs;
        {
            let mut p = NonlinearProbe::new_logs(LogKind::LogDiv15, &mut out).unwrap();
            while p.phase() != phase {
                p.step(1, &flag);
            }
            outputs = p.progress().outputs;
            flag.store(true, Ordering::Release);
            assert_eq!(p.step(0, &flag).error, Some(Error::ZeroBudget));
            assert_eq!(p.step(1, &flag).units, 0);
            assert_eq!(p.status(), Status::Cancelled);
            assert_eq!(p.progress().cancellation_phase, Some(phase));
            assert!(p.result().is_none());
            flag.store(false, Ordering::Release);
            assert_eq!(p.step(1, &flag).units, 0);
        }
        assert!(out[outputs..].iter().all(|v| *v == 7.));
    }
    let mut out = vec![7f32; NONLINEAR_OUTPUTS];
    let mut p = NonlinearProbe::new_activation(Activation::Tanh, &mut out).unwrap();
    p.step(usize::MAX, &flag);
    assert_eq!(p.status(), Status::Ready);
    flag.store(true, Ordering::Release);
    p.cancel();
    assert_eq!(p.step(1, &flag).units, 0);
    assert_eq!(p.status(), Status::Ready);
    assert!(p.result().is_some());
}

#[test]
fn typed_staging_and_failure_terminal_priority() {
    let flag = AtomicBool::new(false);
    let x = (0..INPUTS).map(input_value::<f64>).collect::<Vec<_>>();
    let mut w = (0..WEIGHTS).map(weight_value::<f64>).collect::<Vec<_>>();
    w[17] = f64::NAN;
    let mut out = vec![7.; OUTPUTS];
    {
        let mut p = Projection::new(&x, &w, &mut out).unwrap();
        assert_eq!(p.step(usize::MAX, &flag).error, Some(Error::InvalidInput));
        let before = p.progress();
        flag.store(true, Ordering::Release);
        assert_eq!(p.step(0, &flag).error, Some(Error::ZeroBudget));
        assert_eq!(p.step(1, &flag).error, Some(Error::InvalidInput));
        assert_eq!(p.status(), Status::Failed);
        assert_eq!(p.progress(), before);
    }
    assert!(out.iter().all(|v| *v == 7.));
    flag.store(false, Ordering::Release);
    let mut out = vec![7.; NONLINEAR_OUTPUTS];
    {
        let mut p = NonlinearProbe::new_activation(Activation::Sigmoid, &mut out).unwrap();
        p.step(1, &flag);
        assert_eq!(p.phase(), Phase::Commit);
        flag.store(true, Ordering::Release);
        assert_eq!(p.step(1, &flag).units, 0);
        assert_eq!(p.status(), Status::Cancelled);
        assert_eq!(p.progress().outputs, 0);
        assert!(p.result().is_none());
    }
    assert!(out.iter().all(|v| *v == 7.));
}
