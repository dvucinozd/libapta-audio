// SPDX-License-Identifier: Apache-2.0
//! HS-05 artificial numerical experiment. No trained model or product publication.
use std::sync::atomic::{AtomicBool, Ordering};
pub const MAX_ROWS: u64 = 30000;
pub const MAX_UNITS: u64 = 6000000;
pub const PROFILE_BYTES: usize = 1808;
macro_rules! asset {
    ($name:literal) => {
        include_bytes!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../docs/rust/fixtures/product-host-neural-hs05/assets/",
            $name
        ))
    };
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Profile {
    Zero,
    Asymmetric,
    GateTagged,
    Rounding,
}
impl Profile {
    fn bytes(self) -> &'static [u8; 452] {
        match self {
            Self::Zero => asset!("parameters-zero.f32le"),
            Self::Asymmetric => asset!("parameters-asymmetric.f32le"),
            Self::GateTagged => asset!("parameters-gate-tagged.f32le"),
            Self::Rounding => asset!("parameters-rounding.f32le"),
        }
    }
    fn weight(self, n: usize) -> f32 {
        read32(self.bytes(), n)
    }
}
fn read32(bytes: &[u8], n: usize) -> f32 {
    f32::from_le_bytes(
        bytes[n * 4..n * 4 + 4]
            .try_into()
            .expect("fixed asset index"),
    )
}
fn mac(a: f32, x: f32, w: f32) -> f32 {
    let p = x * w;
    a + p
}
fn sigmoid(x: f32) -> f32 {
    let x = f64::from(x);
    if x >= 0.0 {
        let e = (-x).exp();
        (1.0 / (1.0 + e)) as f32
    } else {
        let e = x.exp();
        (e / (1.0 + e)) as f32
    }
}
fn tanh32(x: f32) -> f32 {
    f64::from(x).tanh() as f32
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    UnsupportedProfile,
    SourceOffset,
    SourceLength,
    InvalidFeatures,
    TruncatedSource,
    ArithmeticOverflow,
    NumericalFailure,
    ZeroBudget,
    BusyReset,
    TerminalReset,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Running,
    Done,
    NoEvidence,
    Cancelled,
    Failed,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    AcceptRow,
    Linear,
    GateInput,
    GateBiasIh,
    GateHidden,
    GateBiasHh,
    GateMerge,
    GateActivate,
    Cell,
    Hidden,
    NextLayer,
    Head,
    SoftmaxMax,
    SoftmaxSubtract,
    SoftmaxExp,
    SoftmaxSum,
    SoftmaxDivide,
    Commit,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flow {
    Budget,
    NeedInput,
    NeedOutput,
    Terminal,
}
#[derive(Clone, Copy)]
pub struct InputBlock<'a> {
    pub first_row: u64,
    pub rows: &'a [[f32; 3]],
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StepReport {
    pub consumed_rows: usize,
    pub produced_rows: usize,
    pub first_output_row: u64,
    pub units: usize,
    pub flow: Flow,
    pub status: Status,
    pub error: Option<Error>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ResetReport {
    pub applied: bool,
    pub units: usize,
    pub status: Status,
    pub error: Option<Error>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Progress {
    pub accepted_rows: u64,
    pub committed_rows: u64,
    pub macs: u64,
    pub step_units: u64,
    pub reset_units: u64,
    pub total_units: u64,
    pub reset_count: u64,
    pub cancellation_phase: Option<Phase>,
}

/// Exactly 64 f32 payload cells. All staged state stays private until Commit.
struct Workspace {
    input: [f32; 3],
    linear: [f32; 2],
    layer_input: [f32; 2],
    ih: [f32; 8],
    hh: [f32; 8],
    z: [f32; 8],
    gates: [f32; 8],
    state: [f32; 8],
    staged: [f32; 8],
    logits: [f32; 3],
    exp: [f32; 3],
    prob: [f32; 3],
    phase: Phase,
    i: usize,
    j: usize,
    layer: usize,
    sum: f32,
    maximum: f32,
    macs: u64,
}
impl Workspace {
    fn new() -> Self {
        Self {
            input: [0.; 3],
            linear: [0.; 2],
            layer_input: [0.; 2],
            ih: [0.; 8],
            hh: [0.; 8],
            z: [0.; 8],
            gates: [0.; 8],
            state: [0.; 8],
            staged: [0.; 8],
            logits: [0.; 3],
            exp: [0.; 3],
            prob: [0.; 3],
            phase: Phase::AcceptRow,
            i: 0,
            j: 0,
            layer: 0,
            sum: 0.,
            maximum: 0.,
            macs: 0,
        }
    }
    fn go(&mut self, p: Phase) {
        self.phase = p;
        self.i = 0;
        self.j = 0;
    }
    fn advance(&mut self, n: usize, p: Phase) {
        self.i += 1;
        if self.i == n {
            self.go(p);
        }
    }
    /// Exactly one bounded scalar unit. AcceptRow and Commit belong to owner.
    fn unit(&mut self, w: impl Fn(usize) -> f32) -> bool {
        let i = self.i;
        let j = self.j;
        let base = 8 + 48 * self.layer;
        let mut finite = true;
        match self.phase {
            Phase::Linear | Phase::Head => {
                let head = self.phase == Phase::Head;
                let width = if head { 2 } else { 3 };
                let count = if head { 3 } else { 2 };
                let a = if head { self.logits[i] } else { self.linear[i] };
                let v = if j < width {
                    let x = if head {
                        self.staged[2 + j]
                    } else {
                        self.input[j]
                    };
                    self.macs += 1;
                    mac(
                        if j == 0 { 0. } else { a },
                        x,
                        w(if head { 104 + i * 2 + j } else { i * 3 + j }),
                    )
                } else {
                    a + w(if head { 110 + i } else { 6 + i })
                };
                finite = v.is_finite();
                if head {
                    self.logits[i] = v;
                } else {
                    self.linear[i] = v;
                }
                self.j += 1;
                if j == width {
                    self.j = 0;
                    self.i += 1;
                    if self.i == count {
                        if head {
                            self.go(Phase::SoftmaxMax);
                        } else {
                            self.layer_input = self.linear;
                            self.layer = 0;
                            self.go(Phase::GateInput);
                        }
                    }
                }
            }
            Phase::GateInput | Phase::GateHidden => {
                let hidden = self.phase == Phase::GateHidden;
                let a = if hidden { self.hh[i] } else { self.ih[i] };
                let x = if hidden {
                    self.state[2 * self.layer + j]
                } else {
                    self.layer_input[j]
                };
                let v = mac(
                    if j == 0 { 0. } else { a },
                    x,
                    w(base + usize::from(hidden) * 16 + 2 * i + j),
                );
                self.macs += 1;
                finite = v.is_finite();
                if hidden {
                    self.hh[i] = v;
                } else {
                    self.ih[i] = v;
                }
                self.j += 1;
                if j == 1 {
                    self.j = 0;
                    self.advance(
                        8,
                        if hidden {
                            Phase::GateBiasHh
                        } else {
                            Phase::GateBiasIh
                        },
                    );
                }
            }
            Phase::GateBiasIh => {
                self.ih[i] += w(base + 32 + i);
                finite = self.ih[i].is_finite();
                self.advance(8, Phase::GateHidden);
            }
            Phase::GateBiasHh => {
                self.hh[i] += w(base + 40 + i);
                finite = self.hh[i].is_finite();
                self.advance(8, Phase::GateMerge);
            }
            Phase::GateMerge => {
                self.z[i] = self.ih[i] + self.hh[i];
                finite = self.z[i].is_finite();
                self.advance(8, Phase::GateActivate);
            }
            Phase::GateActivate => {
                self.gates[i] = if (4..6).contains(&i) {
                    tanh32(self.z[i])
                } else {
                    sigmoid(self.z[i])
                };
                finite = self.gates[i].is_finite();
                self.advance(8, Phase::Cell);
            }
            Phase::Cell => {
                let keep = self.gates[2 + i] * self.state[4 + 2 * self.layer + i];
                let add = self.gates[i] * self.gates[4 + i];
                let c = keep + add;
                self.staged[4 + 2 * self.layer + i] = c;
                finite = c.is_finite();
                self.advance(2, Phase::Hidden);
            }
            Phase::Hidden => {
                let t = tanh32(self.staged[4 + 2 * self.layer + i]);
                let h = self.gates[6 + i] * t;
                self.staged[2 * self.layer + i] = h;
                finite = h.is_finite();
                self.advance(2, Phase::NextLayer);
            }
            Phase::NextLayer => {
                if self.layer == 0 {
                    self.layer_input.copy_from_slice(&self.staged[..2]);
                    self.layer = 1;
                    self.go(Phase::GateInput);
                } else {
                    self.go(Phase::Head);
                }
            }
            Phase::SoftmaxMax => {
                self.maximum = if i == 0 {
                    self.logits[0]
                } else {
                    self.maximum.max(self.logits[i])
                };
                self.advance(3, Phase::SoftmaxSubtract);
            }
            Phase::SoftmaxSubtract => {
                self.logits[i] -= self.maximum;
                finite = self.logits[i].is_finite();
                self.advance(3, Phase::SoftmaxExp);
            }
            Phase::SoftmaxExp => {
                self.exp[i] = f64::from(self.logits[i]).exp() as f32;
                finite = self.exp[i].is_finite();
                self.advance(3, Phase::SoftmaxSum);
            }
            Phase::SoftmaxSum => {
                self.sum = if i == 0 { 0. } else { self.sum };
                self.sum += self.exp[i];
                finite = self.sum.is_finite();
                self.advance(3, Phase::SoftmaxDivide);
                if self.phase == Phase::SoftmaxDivide {
                    finite &= self.sum > 0.;
                }
            }
            Phase::SoftmaxDivide => {
                self.prob[i] = self.exp[i] / self.sum;
                finite = self.prob[i].is_finite() && (0.0..=1.0).contains(&self.prob[i]);
                self.advance(3, Phase::Commit);
                if self.phase == Phase::Commit {
                    finite &=
                        (self.prob.iter().map(|x| f64::from(*x)).sum::<f64>() - 1.0).abs() <= 2e-6;
                }
            }
            Phase::AcceptRow | Phase::Commit | Phase::Terminal => unreachable!("owner action"),
        }
        finite
    }
}
pub struct Session {
    w: Workspace,
    profile: Profile,
    expected: u64,
    accepted: u64,
    committed: u64,
    step_units: u64,
    reset_units: u64,
    status: Status,
    error: Option<Error>,
    cancellation_phase: Option<Phase>,
}
impl Session {
    pub fn new(profile: Profile, expected_rows: u64) -> Result<Self, Error> {
        if expected_rows > MAX_ROWS {
            return Err(Error::UnsupportedProfile);
        }
        let mut s = Self {
            w: Workspace::new(),
            profile,
            expected: expected_rows,
            accepted: 0,
            committed: 0,
            step_units: 0,
            reset_units: 0,
            status: Status::Running,
            error: None,
            cancellation_phase: None,
        };
        if expected_rows == 0 {
            s.status = Status::NoEvidence;
            s.w.phase = Phase::Terminal;
        }
        Ok(s)
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn phase(&self) -> Phase {
        self.w.phase
    }
    pub fn state_snapshot(&self) -> [f32; 8] {
        self.w.state
    }
    pub fn progress(&self) -> Progress {
        Progress {
            accepted_rows: self.accepted,
            committed_rows: self.committed,
            macs: self.w.macs,
            step_units: self.step_units,
            reset_units: self.reset_units,
            total_units: self.step_units + self.reset_units,
            reset_count: self.reset_units,
            cancellation_phase: self.cancellation_phase,
        }
    }
    fn fail(&mut self, e: Error) {
        self.error = Some(e);
        self.status = Status::Failed;
        self.w.phase = Phase::Terminal;
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.cancellation_phase = Some(self.phase());
            self.status = Status::Cancelled;
            self.w.phase = Phase::Terminal;
        }
    }
    pub fn finish_input(&mut self) {
        if self.status == Status::Running && self.accepted < self.expected {
            self.fail(Error::TruncatedSource);
        }
    }
    pub fn reset_state(&mut self, cancel: &AtomicBool) -> ResetReport {
        if self.status == Status::Running && cancel.load(Ordering::Acquire) {
            self.cancel();
        }
        let error = if self.status != Status::Running {
            Some(Error::TerminalReset)
        } else if self.phase() != Phase::AcceptRow || self.accepted != self.committed {
            Some(Error::BusyReset)
        } else if self.step_units + self.reset_units == MAX_UNITS {
            self.fail(Error::ArithmeticOverflow);
            Some(Error::ArithmeticOverflow)
        } else {
            None
        };
        if error.is_none() {
            self.w.state = [0.; 8];
            self.reset_units += 1;
        }
        ResetReport {
            applied: error.is_none(),
            units: usize::from(error.is_none()),
            status: self.status,
            error,
        }
    }
    pub fn step(
        &mut self,
        quota: usize,
        input: InputBlock<'_>,
        output: &mut [[f32; 3]],
        cancel: &AtomicBool,
    ) -> StepReport {
        let mut r = StepReport {
            consumed_rows: 0,
            produced_rows: 0,
            first_output_row: self.committed,
            units: 0,
            flow: Flow::Budget,
            status: self.status,
            error: None,
        };
        if quota == 0 {
            r.error = Some(Error::ZeroBudget);
            return r;
        }
        if self.status == Status::Running && cancel.load(Ordering::Acquire) {
            self.cancel();
        }
        if self.status == Status::Running {
            if input.first_row != self.accepted {
                self.fail(Error::SourceOffset);
            } else if input.rows.len() as u64 > self.expected - self.accepted {
                self.fail(Error::SourceLength);
            }
        }
        while self.status == Status::Running && r.units < quota {
            if cancel.load(Ordering::Acquire) {
                self.cancel();
                break;
            }
            if r.produced_rows == output.len() {
                r.flow = Flow::NeedOutput;
                break;
            }
            if self.step_units + self.reset_units == MAX_UNITS {
                self.fail(Error::ArithmeticOverflow);
                break;
            }
            match self.phase() {
                Phase::AcceptRow => {
                    if r.consumed_rows == input.rows.len() {
                        r.flow = Flow::NeedInput;
                        break;
                    }
                    let x = input.rows[r.consumed_rows];
                    if x.iter().any(|v| !v.is_finite() || v.abs() > 4.) {
                        self.fail(Error::InvalidFeatures);
                        break;
                    }
                    self.w.input = x;
                    self.accepted += 1;
                    r.consumed_rows += 1;
                    self.w.go(Phase::Linear);
                }
                Phase::Commit => {
                    self.w.state = self.w.staged;
                    output[r.produced_rows] = self.w.prob;
                    r.produced_rows += 1;
                    self.committed += 1;
                    if self.committed == self.expected {
                        self.status = Status::Done;
                        self.w.go(Phase::Terminal);
                    } else {
                        self.w.go(Phase::AcceptRow);
                    }
                }
                _ => {
                    let p = self.profile;
                    if !self.w.unit(|n| p.weight(n)) {
                        self.fail(Error::NumericalFailure);
                    }
                }
            }
            r.units += 1;
            self.step_units += 1;
        }
        r.status = self.status;
        r.error = self.error;
        if self.status != Status::Running {
            r.flow = Flow::Terminal;
        }
        r
    }
}
#[derive(Clone, Copy)]
enum ProbeInput {
    Linear {
        x: [f32; 3],
        weights: [f32; 6],
        bias: [f32; 2],
    },
    Activation(f32),
    Softmax([f32; 3]),
    Cell {
        profile: Profile,
        layer: usize,
        x: [f32; 2],
        h: [f32; 2],
        c: [f32; 2],
    },
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ProbeResult {
    pub len: u8,
    pub values: [f32; 36],
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProbeReport {
    pub units: usize,
    pub status: Status,
    pub error: Option<Error>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProbeProgress {
    pub units: u64,
    pub macs: u64,
    pub cancellation_phase: Option<Phase>,
}
pub struct ProbeSession {
    w: Workspace,
    input: ProbeInput,
    output: ProbeResult,
    units: u64,
    status: Status,
    error: Option<Error>,
    cancellation_phase: Option<Phase>,
}
impl ProbeSession {
    pub fn new(index: usize) -> Result<Self, Error> {
        let input = probe_input(index).ok_or(Error::UnsupportedProfile)?;
        let mut w = Workspace::new();
        match input {
            ProbeInput::Linear { x, .. } => {
                w.input = x;
                w.go(Phase::Linear);
            }
            ProbeInput::Activation(x) => {
                w.input[0] = x;
                w.go(Phase::GateActivate);
            }
            ProbeInput::Softmax(x) => {
                w.logits = x;
                w.go(Phase::SoftmaxMax);
            }
            ProbeInput::Cell { layer, x, h, c, .. } => {
                w.layer = layer;
                w.layer_input = x;
                w.state[2 * layer..2 * layer + 2].copy_from_slice(&h);
                w.state[4 + 2 * layer..6 + 2 * layer].copy_from_slice(&c);
                w.go(Phase::GateInput);
            }
        }
        Ok(Self {
            w,
            input,
            output: ProbeResult {
                len: 0,
                values: [0.; 36],
            },
            units: 0,
            status: Status::Running,
            error: None,
            cancellation_phase: None,
        })
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn phase(&self) -> Phase {
        self.w.phase
    }
    pub fn progress(&self) -> ProbeProgress {
        ProbeProgress {
            units: self.units,
            macs: self.w.macs,
            cancellation_phase: self.cancellation_phase,
        }
    }
    pub fn result(&self) -> Option<ProbeResult> {
        if self.status == Status::Done {
            Some(self.output)
        } else {
            None
        }
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.cancellation_phase = Some(self.phase());
            self.status = Status::Cancelled;
            self.w.phase = Phase::Terminal;
        }
    }
    fn publish(&mut self) {
        match self.input {
            ProbeInput::Linear { .. } => {
                self.output.values[..2].copy_from_slice(&self.w.linear);
                self.output.len = 2;
            }
            ProbeInput::Activation(_) => {
                self.output.len = 2;
            }
            ProbeInput::Softmax(_) => {
                self.output.values[..3].copy_from_slice(&self.w.prob);
                self.output.values[3..6].copy_from_slice(&self.w.logits);
                self.output.values[6..9].copy_from_slice(&self.w.exp);
                self.output.values[9] = self.w.sum;
                self.output.len = 10;
            }
            ProbeInput::Cell { layer, .. } => {
                self.output.values[..2].copy_from_slice(&self.w.staged[2 * layer..2 * layer + 2]);
                self.output.values[2..4]
                    .copy_from_slice(&self.w.staged[4 + 2 * layer..6 + 2 * layer]);
                self.output.values[4..12].copy_from_slice(&self.w.ih);
                self.output.values[12..20].copy_from_slice(&self.w.hh);
                self.output.values[20..28].copy_from_slice(&self.w.z);
                self.output.values[28..36].copy_from_slice(&self.w.gates);
                self.output.len = 36;
            }
        }
        self.status = Status::Done;
        self.w.phase = Phase::Terminal;
    }
    pub fn step(&mut self, quota: usize, cancel: &AtomicBool) -> ProbeReport {
        if quota == 0 {
            return ProbeReport {
                units: 0,
                status: self.status,
                error: Some(Error::ZeroBudget),
            };
        }
        let mut used = 0;
        while self.status == Status::Running && used < quota {
            if cancel.load(Ordering::Acquire) {
                self.cancel();
                break;
            }
            if self.units == 256 {
                self.error = Some(Error::ArithmeticOverflow);
                self.status = Status::Failed;
                self.w.phase = Phase::Terminal;
                break;
            }
            let finite = if self.phase() == Phase::Commit {
                self.publish();
                true
            } else {
                match self.input {
                    ProbeInput::Activation(x) => {
                        let i = self.w.i;
                        let v = if i == 0 { sigmoid(x) } else { tanh32(x) };
                        self.output.values[i] = v;
                        self.w.advance(2, Phase::Commit);
                        v.is_finite()
                    }
                    ProbeInput::Linear { weights, bias, .. } => {
                        let f = self
                            .w
                            .unit(|n| if n < 6 { weights[n] } else { bias[n - 6] });
                        if self.phase() == Phase::GateInput {
                            self.w.go(Phase::Commit);
                        }
                        f
                    }
                    ProbeInput::Cell { profile, .. } => {
                        let f = self.w.unit(|n| profile.weight(n));
                        if self.phase() == Phase::NextLayer {
                            self.w.go(Phase::Commit);
                        }
                        f
                    }
                    ProbeInput::Softmax(_) => {
                        self.w.unit(|_| unreachable!("no weights in softmax"))
                    }
                }
            };
            self.units += 1;
            used += 1;
            if !finite {
                self.error = Some(Error::NumericalFailure);
                self.status = Status::Failed;
                self.w.phase = Phase::Terminal;
            }
        }
        ProbeReport {
            units: used,
            status: self.status,
            error: self.error,
        }
    }
}

// Frozen probe INPUTS only; runtime bit decoding preserves Rust1.81 support.
fn probe_input(index: usize) -> Option<ProbeInput> {
    Some(match index {
        0 => ProbeInput::Linear {
            x: [
                f32::from_bits(1065353216),
                f32::from_bits(3221225472),
                f32::from_bits(1077936128),
            ],
            weights: [
                f32::from_bits(0),
                f32::from_bits(0),
                f32::from_bits(0),
                f32::from_bits(0),
                f32::from_bits(0),
                f32::from_bits(0),
            ],
            bias: [f32::from_bits(0), f32::from_bits(0)],
        },
        1 => ProbeInput::Linear {
            x: [
                f32::from_bits(1048576000),
                f32::from_bits(3204448256),
                f32::from_bits(1065353216),
            ],
            weights: [
                f32::from_bits(3187671040),
                f32::from_bits(1035993088),
                f32::from_bits(3197108224),
                f32::from_bits(3179282432),
                f32::from_bits(1042284544),
                f32::from_bits(3193962496),
            ],
            bias: [f32::from_bits(0), f32::from_bits(1046478848)],
        },
        2 => ProbeInput::Linear {
            x: [
                f32::from_bits(1266679808),
                f32::from_bits(1065353216),
                f32::from_bits(3414163456),
            ],
            weights: [
                f32::from_bits(1065353216),
                f32::from_bits(1065353216),
                f32::from_bits(1065353216),
                f32::from_bits(1065353216),
                f32::from_bits(3212836864),
                f32::from_bits(1065353216),
            ],
            bias: [f32::from_bits(0), f32::from_bits(0)],
        },
        3 => ProbeInput::Linear {
            x: [
                f32::from_bits(1266679808),
                f32::from_bits(1065353216),
                f32::from_bits(3414163456),
            ],
            weights: [
                f32::from_bits(1065353216),
                f32::from_bits(1065353216),
                f32::from_bits(1065353216),
                f32::from_bits(1065353216),
                f32::from_bits(3212836864),
                f32::from_bits(1065353216),
            ],
            bias: [f32::from_bits(1065353216), f32::from_bits(3204448256)],
        },
        4 => ProbeInput::Linear {
            x: [
                f32::from_bits(2147483648),
                f32::from_bits(0),
                f32::from_bits(2147483648),
            ],
            weights: [
                f32::from_bits(3212836864),
                f32::from_bits(1065353216),
                f32::from_bits(3212836864),
                f32::from_bits(1065353216),
                f32::from_bits(3212836864),
                f32::from_bits(1065353216),
            ],
            bias: [f32::from_bits(2147483648), f32::from_bits(0)],
        },
        5 => ProbeInput::Activation(f32::from_bits(3271557120)),
        6 => ProbeInput::Activation(f32::from_bits(3266314240)),
        7 => ProbeInput::Activation(f32::from_bits(3248488448)),
        8 => ProbeInput::Activation(f32::from_bits(3221225472)),
        9 => ProbeInput::Activation(f32::from_bits(3212836864)),
        10 => ProbeInput::Activation(f32::from_bits(2147483648)),
        11 => ProbeInput::Activation(f32::from_bits(0)),
        12 => ProbeInput::Activation(f32::from_bits(1065353216)),
        13 => ProbeInput::Activation(f32::from_bits(1073741824)),
        14 => ProbeInput::Activation(f32::from_bits(1101004800)),
        15 => ProbeInput::Activation(f32::from_bits(1118830592)),
        16 => ProbeInput::Activation(f32::from_bits(1124073472)),
        17 => ProbeInput::Activation(f32::from_bits(3017195413)),
        18 => ProbeInput::Activation(f32::from_bits(869711765)),
        19 => ProbeInput::Activation(f32::from_bits(3204448256)),
        20 => ProbeInput::Activation(f32::from_bits(1056964608)),
        21 => ProbeInput::Softmax([f32::from_bits(0), f32::from_bits(0), f32::from_bits(0)]),
        22 => ProbeInput::Softmax([
            f32::from_bits(1065353216),
            f32::from_bits(0),
            f32::from_bits(3212836864),
        ]),
        23 => ProbeInput::Softmax([
            f32::from_bits(3248488448),
            f32::from_bits(1101004800),
            f32::from_bits(0),
        ]),
        24 => ProbeInput::Softmax([
            f32::from_bits(3248488448),
            f32::from_bits(3248488448),
            f32::from_bits(3248488448),
        ]),
        25 => ProbeInput::Softmax([
            f32::from_bits(0),
            f32::from_bits(3271557120),
            f32::from_bits(1124073472),
        ]),
        26 => ProbeInput::Softmax([
            f32::from_bits(3271557120),
            f32::from_bits(3271557120),
            f32::from_bits(3271557120),
        ]),
        27 => ProbeInput::Softmax([
            f32::from_bits(3212836864),
            f32::from_bits(0),
            f32::from_bits(0),
        ]),
        28 => ProbeInput::Softmax([
            f32::from_bits(1099431936),
            f32::from_bits(1098907648),
            f32::from_bits(1097859072),
        ]),
        29 => ProbeInput::Cell {
            profile: Profile::Zero,
            layer: 0,
            x: [f32::from_bits(0), f32::from_bits(0)],
            h: [f32::from_bits(0), f32::from_bits(0)],
            c: [f32::from_bits(0), f32::from_bits(0)],
        },
        30 => ProbeInput::Cell {
            profile: Profile::Zero,
            layer: 1,
            x: [f32::from_bits(1050253722), f32::from_bits(1036831949)],
            h: [f32::from_bits(1056964608), f32::from_bits(3204448256)],
            c: [f32::from_bits(1073741824), f32::from_bits(3229614080)],
        },
        31 => ProbeInput::Cell {
            profile: Profile::GateTagged,
            layer: 0,
            x: [f32::from_bits(1065353216), f32::from_bits(3212836864)],
            h: [f32::from_bits(1056964608), f32::from_bits(3196059648)],
            c: [f32::from_bits(1069547520), f32::from_bits(3221225472)],
        },
        32 => ProbeInput::Cell {
            profile: Profile::Asymmetric,
            layer: 1,
            x: [f32::from_bits(3204448256), f32::from_bits(1061158912)],
            h: [f32::from_bits(3196059648), f32::from_bits(1056964608)],
            c: [f32::from_bits(1048576000), f32::from_bits(3208642560)],
        },
        33 => ProbeInput::Cell {
            profile: Profile::Rounding,
            layer: 0,
            x: [f32::from_bits(1036831949), f32::from_bits(3207803699)],
            h: [f32::from_bits(1050253722), f32::from_bits(3184315597)],
            c: [f32::from_bits(1045220557), f32::from_bits(3201092813)],
        },
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn partial_second_layer_cap_before_write_and_pending_reset_cancel() {
        let flag = AtomicBool::new(false);
        let mut s = Session::new(Profile::GateTagged, 8).unwrap();
        while !(s.w.layer == 1 && s.phase() == Phase::GateHidden && s.w.j == 1) {
            let r = s.step(
                1,
                InputBlock {
                    first_row: s.accepted,
                    rows: if s.accepted < 8 {
                        &[[1., -1., 0.5]]
                    } else {
                        &[]
                    },
                },
                &mut [[123.; 3]],
                &flag,
            );
            assert!(r.error.is_none());
            assert_eq!(s.state_snapshot(), [0.; 8]);
        }
        let w = s.w.hh;
        let state = s.state_snapshot();
        let accepted = s.accepted;
        s.step_units = MAX_UNITS;
        let r = s.step(
            1,
            InputBlock {
                first_row: accepted,
                rows: &[],
            },
            &mut [[123.; 3]],
            &flag,
        );
        assert_eq!(r.error, Some(Error::ArithmeticOverflow));
        assert_eq!(r.units, 0);
        assert_eq!(s.w.hh, w);
        assert_eq!(s.state_snapshot(), state);
        let mut s = Session::new(Profile::Asymmetric, 1).unwrap();
        flag.store(true, Ordering::Release);
        let p = s.progress();
        assert_eq!(
            s.step(
                0,
                InputBlock {
                    first_row: 999,
                    rows: &[]
                },
                &mut [],
                &flag
            )
            .error,
            Some(Error::ZeroBudget)
        );
        assert_eq!(s.progress(), p);
        assert_eq!(s.status(), Status::Running);
        assert_eq!(s.reset_state(&flag).error, Some(Error::TerminalReset));
        assert_eq!(s.status(), Status::Cancelled);
        assert_eq!(s.progress().reset_units, 0);
    }
    #[test]
    fn softmax_leading_zero_terms_and_final_positive_sum() {
        let mut p = ProbeSession::new(25).unwrap();
        let flag = AtomicBool::new(false);
        while p.phase() != Phase::SoftmaxSum {
            p.step(1, &flag);
        }
        assert_eq!(p.w.exp[0], 0.);
        assert_eq!(p.w.exp[1], 0.);
        assert_eq!(p.w.exp[2], 1.);
        assert!(p.step(1, &flag).error.is_none());
        assert_eq!(p.w.sum, 0.);
        while p.status() == Status::Running {
            assert!(p.step(1, &flag).error.is_none());
        }
        assert_eq!(&p.result().unwrap().values[..3], &[0., 0., 1.]);
        let mut s = Session::new(Profile::Zero, 1).unwrap();
        s.step_units = MAX_UNITS;
        assert_eq!(s.reset_state(&flag).error, Some(Error::ArithmeticOverflow));
        assert_eq!(s.progress().reset_units, 0);
    }
}

pub mod synthetic {
    use super::*;
    use sha2::{Digest, Sha256};
    #[derive(Clone, Copy)]
    pub struct Case {
        pub name: &'static str,
        pub profile: Profile,
        pub signal: &'static str,
        pub rows: u64,
        pub resets: &'static [u64],
    }
    impl Case {
        pub fn row(self, r: u64) -> [f32; 3] {
            match self.signal {
                "zero" => [0.; 3],
                "periodic17" => std::array::from_fn(|j| {
                    (((r * 5 + j as u64 * 3 + 1) % 17) as i32 - 8) as f32 / 8.
                }),
                "impulse" => {
                    if r == 0 {
                        [0.75, -0.5, 0.25]
                    } else {
                        [0.; 3]
                    }
                }
                "alternating" => {
                    let sign = if r % 2 == 0 { 1. } else { -1. };
                    [sign, -sign, 0.5 * sign]
                }
                _ => panic!("fixed generator"),
            }
        }
    }
    pub const FIXTURE_COUNT: usize = 12;
    pub const PROBE_COUNT: usize = 34;
    pub fn fixture(i: usize) -> Case {
        match i {
            0 => Case {
                name: "empty",
                profile: Profile::Zero,
                signal: "zero",
                rows: 0,
                resets: &[],
            },
            1 => Case {
                name: "zero-single",
                profile: Profile::Zero,
                signal: "zero",
                rows: 1,
                resets: &[],
            },
            2 => Case {
                name: "zero-seven",
                profile: Profile::Zero,
                signal: "periodic17",
                rows: 7,
                resets: &[],
            },
            3 => Case {
                name: "asym-impulse",
                profile: Profile::Asymmetric,
                signal: "impulse",
                rows: 8,
                resets: &[],
            },
            4 => Case {
                name: "asym-periodic",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 16,
                resets: &[],
            },
            5 => Case {
                name: "asym-reset",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 16,
                resets: &[8],
            },
            6 => Case {
                name: "asym-reset-every",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 8,
                resets: &[0, 1, 2, 3, 4, 5, 6, 7],
            },
            7 => Case {
                name: "asym-reset-last",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 8,
                resets: &[7],
            },
            8 => Case {
                name: "gated-alternating",
                profile: Profile::GateTagged,
                signal: "alternating",
                rows: 8,
                resets: &[],
            },
            9 => Case {
                name: "gated-reset",
                profile: Profile::GateTagged,
                signal: "alternating",
                rows: 8,
                resets: &[4],
            },
            10 => Case {
                name: "rounding-nine",
                profile: Profile::Rounding,
                signal: "periodic17",
                rows: 9,
                resets: &[],
            },
            11 => Case {
                name: "asym-prefix-one",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 1,
                resets: &[],
            },
            _ => panic!("fixed index"),
        }
    }
    pub fn full(name: &str) -> Option<Case> {
        Some(match name {
            "long-zero" => Case {
                name: "long-zero",
                profile: Profile::Zero,
                signal: "periodic17",
                rows: 30000,
                resets: &[],
            },
            "long-carry" => Case {
                name: "long-carry",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 30000,
                resets: &[],
            },
            "long-reset" => Case {
                name: "long-reset",
                profile: Profile::Asymmetric,
                signal: "periodic17",
                rows: 30000,
                resets: &[15000],
            },
            _ => return None,
        })
    }
    pub struct Driver {
        pub session: Session,
        pub case: Case,
        input: [[f32; 3]; 64],
        pub output: [[f32; 3]; 1],
        begin: usize,
        end: usize,
        reset_index: usize,
        input_hash: Sha256,
        trace_hash: Sha256,
        pub last_step_seconds: f64,
        pub reset_seconds: f64,
    }
    impl Driver {
        pub fn new(case: Case) -> Self {
            let mut input_hash = Sha256::new();
            input_hash.update(case.rows.to_le_bytes());
            Self {
                session: Session::new(case.profile, case.rows).unwrap(),
                case,
                input: [[0.; 3]; 64],
                output: [[0.; 3]; 1],
                begin: 0,
                end: 0,
                reset_index: 0,
                input_hash,
                trace_hash: Sha256::new(),
                last_step_seconds: 0.,
                reset_seconds: 0.,
            }
        }
        fn prepare(&mut self, flag: &AtomicBool) {
            if self.session.status() != Status::Running {
                return;
            }
            let p = self.session.progress();
            if self.case.resets.get(self.reset_index) == Some(&p.committed_rows)
                && p.accepted_rows == p.committed_rows
            {
                let t = std::time::Instant::now();
                let r = self.session.reset_state(flag);
                self.reset_seconds += t.elapsed().as_secs_f64();
                assert!(
                    r.applied || r.status == Status::Cancelled,
                    "legal driver reset refused"
                );
                if r.applied {
                    self.reset_index += 1;
                }
            }
            if self.begin == self.end {
                self.begin = 0;
                let at = self.session.progress().accepted_rows;
                let bound = self
                    .case
                    .resets
                    .get(self.reset_index)
                    .copied()
                    .unwrap_or(self.case.rows);
                self.end = (bound - at).min(64) as usize;
                for j in 0..self.end {
                    self.input[j] = self.case.row(at + j as u64);
                }
            }
        }
        pub fn step(&mut self, flag: &AtomicBool) -> StepReport {
            self.prepare(flag);
            let start = std::time::Instant::now();
            let r = self.session.step(
                256,
                InputBlock {
                    first_row: self.session.progress().accepted_rows,
                    rows: &self.input[self.begin..self.end],
                },
                &mut self.output,
                flag,
            );
            self.last_step_seconds = start.elapsed().as_secs_f64();
            for row in &self.input[self.begin..self.begin + r.consumed_rows] {
                for x in row {
                    self.input_hash.update(x.to_le_bytes());
                }
            }
            self.begin += r.consumed_rows;
            if r.produced_rows > 0 {
                for x in self.trace() {
                    self.trace_hash.update(x.to_le_bytes());
                }
            }
            r
        }
        pub fn trace(&self) -> [f32; 11] {
            let mut t = [0.; 11];
            t[..3].copy_from_slice(&self.output[0]);
            t[3..].copy_from_slice(&self.session.state_snapshot());
            t
        }
        pub fn hashes(&self) -> (String, String) {
            (
                format!("{:x}", self.input_hash.clone().finalize()),
                format!("{:x}", self.trace_hash.clone().finalize()),
            )
        }
    }
}
