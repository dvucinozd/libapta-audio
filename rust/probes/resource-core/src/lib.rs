// SPDX-License-Identifier: Apache-2.0
//! Fixed artificial resource probes, with caller storage and no HAL or allocation.
#![no_std]
#![forbid(unsafe_code)]

use core::hint::black_box;
use core::sync::atomic::{AtomicBool, Ordering};

pub const COPY_WORDS: usize = 16384;
pub const COPY_PASSES: u32 = 32;
pub const COPY_UNITS: u64 = 1589249;
pub const INPUTS: usize = 2400;
pub const WEIGHTS: usize = 22500;
pub const OUTPUTS: usize = 2400;
pub const PROJECTION_UNITS: u64 = 387301;
pub const NONLINEAR_OUTPUTS: usize = 28672;
pub const NONLINEAR_UNITS: u64 = 57345;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Capacity,
    InvalidInput,
    Mismatch,
    Numerical,
    WorkLimit,
    ZeroBudget,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Running,
    Ready,
    Cancelled,
    Failed,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Initialize,
    Prepare,
    Copy,
    Verify,
    ValidateInput,
    ValidateWeights,
    Dot,
    Evaluate,
    Commit,
    Finalize,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Progress {
    pub units: u64,
    pub outputs: usize,
    pub macs: u64,
    pub passes: u32,
    pub cancellation_phase: Option<Phase>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Report {
    pub units: usize,
    pub status: Status,
    pub phase: Phase,
    pub error: Option<Error>,
}
struct Control {
    status: Status,
    phase: Phase,
    error: Option<Error>,
    progress: Progress,
}
impl Control {
    fn new(phase: Phase) -> Self {
        Self {
            status: Status::Running,
            phase,
            error: None,
            progress: Progress {
                units: 0,
                outputs: 0,
                macs: 0,
                passes: 0,
                cancellation_phase: None,
            },
        }
    }
    fn fail(&mut self, error: Error) {
        self.error = Some(error);
        self.status = Status::Failed;
        self.phase = Phase::Terminal;
    }
    fn cancel(&mut self) {
        if self.status == Status::Running {
            self.progress.cancellation_phase = Some(self.phase);
            self.status = Status::Cancelled;
            self.phase = Phase::Terminal;
        }
    }
    fn charge(&mut self, flag: &AtomicBool, cap: u64) -> bool {
        if self.status != Status::Running {
            return false;
        }
        if flag.load(Ordering::Acquire) {
            self.cancel();
            return false;
        }
        if self.progress.units >= cap {
            self.fail(Error::WorkLimit);
            return false;
        }
        self.progress.units += 1;
        true
    }
    fn finish(&mut self) {
        self.status = Status::Ready;
        self.phase = Phase::Terminal;
    }
    fn report(&self, before: u64, quota: usize) -> Report {
        Report {
            units: (self.progress.units - before) as usize,
            status: self.status,
            phase: self.phase,
            error: if quota == 0 {
                Some(Error::ZeroBudget)
            } else {
                self.error
            },
        }
    }
}
macro_rules! accessors {
    () => {
        pub fn status(&self) -> Status {
            self.control.status
        }
        pub fn phase(&self) -> Phase {
            self.control.phase
        }
        pub fn progress(&self) -> Progress {
            self.control.progress
        }
        pub fn cancel(&mut self) {
            self.control.cancel();
        }
    };
}

pub struct CopyView<'a> {
    pub source: &'a [u32],
    pub destination: &'a [u32],
}
pub struct CopyProbe<'a> {
    source: &'a mut [u32],
    destination: &'a mut [u32],
    control: Control,
    index: usize,
}
impl<'a> CopyProbe<'a> {
    pub fn new(source: &'a mut [u32], destination: &'a mut [u32]) -> Result<Self, Error> {
        if source.len() < COPY_WORDS || destination.len() < COPY_WORDS {
            return Err(Error::Capacity);
        }
        Ok(Self {
            source,
            destination,
            control: Control::new(Phase::Initialize),
            index: 0,
        })
    }
    accessors!();
    pub fn result(&self) -> Option<CopyView<'_>> {
        (self.status() == Status::Ready).then(|| CopyView {
            source: &self.source[..COPY_WORDS],
            destination: &self.destination[..COPY_WORDS],
        })
    }
    pub fn step(&mut self, quota: usize, flag: &AtomicBool) -> Report {
        let before = self.control.progress.units;
        while self.control.progress.units - before < quota as u64
            && self.control.charge(flag, COPY_UNITS)
        {
            match self.control.phase {
                Phase::Initialize => {
                    self.source[self.index] = (self.index as u32).wrapping_mul(2654435761)
                }
                Phase::Prepare => {
                    self.destination[self.index] = !black_box(self.source[self.index])
                }
                Phase::Copy => self.destination[self.index] = black_box(self.source[self.index]),
                Phase::Verify => {
                    if black_box(self.destination[self.index]) != black_box(self.source[self.index])
                    {
                        self.control.fail(Error::Mismatch);
                        continue;
                    }
                }
                Phase::Finalize => {
                    self.control.finish();
                    continue;
                }
                _ => unreachable!("copy phase"),
            }
            self.index += 1;
            if self.index == COPY_WORDS {
                self.index = 0;
                self.control.phase = match self.control.phase {
                    Phase::Initialize => Phase::Prepare,
                    Phase::Prepare => Phase::Copy,
                    Phase::Copy => Phase::Verify,
                    Phase::Verify => {
                        self.control.progress.passes += 1;
                        if self.control.progress.passes == COPY_PASSES {
                            self.control.progress.outputs = COPY_WORDS;
                            Phase::Finalize
                        } else {
                            Phase::Prepare
                        }
                    }
                    _ => unreachable!("copy transition"),
                };
            }
        }
        self.control.report(before, quota)
    }
}

mod sealed {
    pub trait Sealed {}
    impl Sealed for f32 {}
    impl Sealed for f64 {}
}
/// Only f32/f64 implement this fixed-probe arithmetic boundary.
pub trait Scalar: sealed::Sealed + Copy {
    fn from_dyadic(numerator: i32, denominator: u32) -> Self;
    fn same_bits(self, other: Self) -> bool;
    fn multiply(self, other: Self) -> Self;
    fn add(self, other: Self) -> Self;
}
macro_rules! scalar {
    ($t:ty) => {
        impl Scalar for $t {
            fn from_dyadic(numerator: i32, denominator: u32) -> Self {
                numerator as Self / denominator as Self
            }
            fn same_bits(self, other: Self) -> bool {
                self.to_bits() == other.to_bits()
            }
            fn multiply(self, other: Self) -> Self {
                self * other
            }
            fn add(self, other: Self) -> Self {
                self + other
            }
        }
    };
}
scalar!(f32);
scalar!(f64);

pub fn input_value<T: Scalar>(index: usize) -> T {
    // Modulo limits keep these helpers total even for diagnostic oversized indices.
    let i = index % INPUTS;
    T::from_dyadic((((i / 150 + 3 * (i % 150)) % 17) as i32) - 8, 32)
}
pub fn weight_value<T: Scalar>(index: usize) -> T {
    let i = index % WEIGHTS;
    T::from_dyadic((((5 * (i / 150) + 7 * (i % 150)) % 19) as i32) - 9, 64)
}

/// Views cannot outlive their owner or permit concurrent output reuse.
/// ```compile_fail
/// use libapta_resource_probe_core::*;
/// let x = [0f32; INPUTS]; let w = [0f32; WEIGHTS]; let mut out = [0f32; OUTPUTS];
/// let view;
/// { let job = Projection::new(&x, &w, &mut out).unwrap(); view = job.result(); }
/// println!("{:?}", view);
/// ```
/// ```compile_fail
/// use libapta_resource_probe_core::*;
/// let x = [0f32; INPUTS]; let w = [0f32; WEIGHTS]; let mut out = [0f32; OUTPUTS];
/// let mut job = Projection::new(&x, &w, &mut out).unwrap();
/// let view = job.result(); job.cancel(); println!("{:?}", view);
/// ```
pub struct Projection<'a, T: Scalar> {
    input: &'a [T],
    weights: &'a [T],
    output: &'a mut [T],
    control: Control,
    index: usize,
    term: usize,
    accumulator: T,
}
impl<'a, T: Scalar> Projection<'a, T> {
    pub fn new(input: &'a [T], weights: &'a [T], output: &'a mut [T]) -> Result<Self, Error> {
        if input.len() < INPUTS || weights.len() < WEIGHTS || output.len() < OUTPUTS {
            return Err(Error::Capacity);
        }
        Ok(Self {
            input,
            weights,
            output,
            control: Control::new(Phase::ValidateInput),
            index: 0,
            term: 0,
            accumulator: T::from_dyadic(0, 1),
        })
    }
    accessors!();
    pub fn result(&self) -> Option<&[T]> {
        (self.status() == Status::Ready).then(|| &self.output[..OUTPUTS])
    }
    pub fn step(&mut self, quota: usize, flag: &AtomicBool) -> Report {
        let before = self.control.progress.units;
        while self.control.progress.units - before < quota as u64
            && self.control.charge(flag, PROJECTION_UNITS)
        {
            match self.control.phase {
                Phase::ValidateInput | Phase::ValidateWeights => {
                    let input = self.control.phase == Phase::ValidateInput;
                    let (actual, expected, limit) = if input {
                        (self.input[self.index], input_value(self.index), INPUTS)
                    } else {
                        (self.weights[self.index], weight_value(self.index), WEIGHTS)
                    };
                    if !actual.same_bits(expected) {
                        self.control.fail(Error::InvalidInput);
                        continue;
                    }
                    self.index += 1;
                    if self.index == limit {
                        self.index = 0;
                        self.control.phase = if input {
                            Phase::ValidateWeights
                        } else {
                            Phase::Dot
                        };
                    }
                }
                Phase::Dot => {
                    let row = self.index / 150;
                    let column = self.index % 150;
                    let product = black_box(self.input[row * 150 + self.term])
                        .multiply(black_box(self.weights[self.term * 150 + column]));
                    self.accumulator = self.accumulator.add(product);
                    self.control.progress.macs += 1;
                    self.term += 1;
                    if self.term == 150 {
                        self.control.phase = Phase::Commit;
                    }
                }
                Phase::Commit => {
                    self.output[self.index] = self.accumulator;
                    self.control.progress.outputs += 1;
                    self.index += 1;
                    self.term = 0;
                    self.accumulator = T::from_dyadic(0, 1);
                    self.control.phase = if self.index == OUTPUTS {
                        Phase::Finalize
                    } else {
                        Phase::Dot
                    };
                }
                Phase::Finalize => self.control.finish(),
                _ => unreachable!("projection phase"),
            }
        }
        self.control.report(before, quota)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Activation {
    Sigmoid,
    Tanh,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogKind {
    Log,
    LogDiv15,
}
enum Storage<'a> {
    Activation(Activation, &'a mut [f32]),
    Log(LogKind, &'a mut [f64]),
}
pub enum NonlinearView<'a> {
    F32(&'a [f32]),
    F64(&'a [f64]),
}
pub struct NonlinearProbe<'a> {
    storage: Storage<'a>,
    control: Control,
    index: usize,
    pending32: f32,
    pending64: f64,
}
impl<'a> NonlinearProbe<'a> {
    fn new(storage: Storage<'a>) -> Result<Self, Error> {
        let length = match &storage {
            Storage::Activation(_, s) => s.len(),
            Storage::Log(_, s) => s.len(),
        };
        if length < NONLINEAR_OUTPUTS {
            return Err(Error::Capacity);
        }
        Ok(Self {
            storage,
            control: Control::new(Phase::Evaluate),
            index: 0,
            pending32: 0.,
            pending64: 0.,
        })
    }
    pub fn new_activation(kind: Activation, output: &'a mut [f32]) -> Result<Self, Error> {
        Self::new(Storage::Activation(kind, output))
    }
    pub fn new_logs(kind: LogKind, output: &'a mut [f64]) -> Result<Self, Error> {
        Self::new(Storage::Log(kind, output))
    }
    accessors!();
    pub fn result(&self) -> Option<NonlinearView<'_>> {
        if self.status() != Status::Ready {
            return None;
        }
        Some(match &self.storage {
            Storage::Activation(_, out) => NonlinearView::F32(&out[..NONLINEAR_OUTPUTS]),
            Storage::Log(_, out) => NonlinearView::F64(&out[..NONLINEAR_OUTPUTS]),
        })
    }
    pub fn step(&mut self, quota: usize, flag: &AtomicBool) -> Report {
        let before = self.control.progress.units;
        while self.control.progress.units - before < quota as u64
            && self.control.charge(flag, NONLINEAR_UNITS)
        {
            match self.control.phase {
                Phase::Evaluate => match &self.storage {
                    Storage::Activation(kind, _) => {
                        let x = black_box([-20f32, -2., -1., 0., 1., 2., 20.][self.index % 7]);
                        let value = match kind {
                            Activation::Tanh => libm::tanhf(x),
                            Activation::Sigmoid if x >= 0. => 1. / (1. + libm::expf(-x)),
                            Activation::Sigmoid => {
                                let e = libm::expf(x);
                                e / (1. + e)
                            }
                        };
                        let low = if *kind == Activation::Sigmoid {
                            0.
                        } else {
                            -1.
                        };
                        if !value.is_finite() || !(low..=1.).contains(&value) {
                            self.control.fail(Error::Numerical);
                        } else {
                            self.pending32 = value;
                            self.control.phase = Phase::Commit;
                        }
                    }
                    Storage::Log(kind, _) => {
                        let bits = [
                            0, 0x80000000, 1, 0x3d4ccccd, 0x3eaaaaab, 0x3f000000, 0x3f800000,
                        ][self.index % 7];
                        let p = black_box(f64::from(f32::from_bits(bits)));
                        let value = if p == 0. {
                            f64::NEG_INFINITY
                        } else {
                            let argument = if *kind == LogKind::LogDiv15 {
                                p / 15.
                            } else {
                                p
                            };
                            libm::log(argument)
                        };
                        if (p != 0. && !value.is_finite())
                            || (value.is_finite() && !(-1024.0..=0.).contains(&value))
                        {
                            self.control.fail(Error::Numerical);
                        } else {
                            self.pending64 = value;
                            self.control.phase = Phase::Commit;
                        }
                    }
                },
                Phase::Commit => {
                    match &mut self.storage {
                        Storage::Activation(_, out) => out[self.index] = self.pending32,
                        Storage::Log(_, out) => out[self.index] = self.pending64,
                    }
                    self.index += 1;
                    self.control.progress.outputs += 1;
                    self.control.phase = if self.index == NONLINEAR_OUTPUTS {
                        Phase::Finalize
                    } else {
                        Phase::Evaluate
                    };
                }
                Phase::Finalize => self.control.finish(),
                _ => unreachable!("nonlinear phase"),
            }
        }
        self.control.report(before, quota)
    }
}

#[cfg(test)]
mod internal {
    use super::*;
    #[test]
    fn injected_work_cap_and_copy_mismatch_cannot_publish() {
        let flag = AtomicBool::new(false);
        let mut a = [0; COPY_WORDS];
        let mut b = [7; COPY_WORDS];
        let mut c = CopyProbe::new(&mut a, &mut b).unwrap();
        c.control.progress.units = COPY_UNITS;
        let r = c.step(1, &flag);
        assert_eq!(r.error, Some(Error::WorkLimit));
        assert_eq!(r.units, 0);
        assert!(c.result().is_none());
        assert_eq!(c.destination[0], 7);
        let mut c = CopyProbe::new(&mut a, &mut b).unwrap();
        c.step(COPY_WORDS * 3, &flag);
        assert_eq!(c.phase(), Phase::Verify);
        c.destination[0] ^= 1;
        let r = c.step(1, &flag);
        assert_eq!(r.error, Some(Error::Mismatch));
        assert_eq!(c.status(), Status::Failed);
        assert!(c.result().is_none());
    }
    #[test]
    fn projection_and_nonlinear_caps_refuse_pending_commit_writes() {
        let flag = AtomicBool::new(false);
        let x = core::array::from_fn::<_, INPUTS, _>(input_value::<f32>);
        let w = core::array::from_fn::<_, WEIGHTS, _>(weight_value::<f32>);
        let mut out = [7.; OUTPUTS];
        let mut p = Projection::new(&x, &w, &mut out).unwrap();
        p.step(INPUTS + WEIGHTS + 150, &flag);
        assert_eq!(p.phase(), Phase::Commit);
        p.control.progress.units = PROJECTION_UNITS;
        let r = p.step(1, &flag);
        assert_eq!(r.error, Some(Error::WorkLimit));
        assert_eq!(r.units, 0);
        assert_eq!(p.output[0], 7.);
        assert!(p.result().is_none());
        let mut out = [7.; NONLINEAR_OUTPUTS];
        let mut p = NonlinearProbe::new_activation(Activation::Sigmoid, &mut out).unwrap();
        p.step(1, &flag);
        assert_eq!(p.phase(), Phase::Commit);
        p.control.progress.units = NONLINEAR_UNITS;
        let r = p.step(1, &flag);
        assert_eq!(r.error, Some(Error::WorkLimit));
        assert_eq!(r.units, 0);
        assert!(p.result().is_none());
        match &p.storage {
            Storage::Activation(_, s) => assert_eq!(s[0], 7.),
            _ => unreachable!(),
        }
    }
}
