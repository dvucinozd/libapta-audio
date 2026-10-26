// SPDX-License-Identifier: Apache-2.0
//! HS-04 synthetic host frontend. Allocator-free, bounded and opt-in.
//! Streamed rows are provisional until Done; no product publication is performed.
use std::sync::atomic::{AtomicBool, Ordering};

pub const SIZE: usize = 1764;
pub const HOP: usize = 441;
pub const BINS: usize = 882;
pub const BANDS: usize = 144;
pub const FEATURES: usize = 288;
pub const TABLE_BYTES: usize = 50476;
const RADICES: [usize; 6] = [2, 2, 3, 3, 7, 7];
macro_rules! table {
    ($name:literal) => {
        include_bytes!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../docs/rust/fixtures/product-compact-cs02/assets/",
            $name
        ))
    };
}
const HANN: &[u8; 14112] = table!("hann-1764.f64le");
const ROOTS: &[u8; 28224] = table!("dft-roots-1764.f64le");
const STARTS: &[u8; 288] = table!("filter-starts.u16le");
const OFFSETS: &[u8; 580] = table!("filter-offsets.u32le");
const WEIGHTS: &[u8; 6980] = table!("filter-weights.f32le");
pub const EDGE_BINS: &[u8; 292] = table!("filter-edge-bins.u16le");
fn f64_at(bytes: &[u8], i: usize) -> f64 {
    f64::from_le_bytes(
        bytes[8 * i..8 * i + 8]
            .try_into()
            .expect("static table index"),
    )
}
fn f32_at(bytes: &[u8], i: usize) -> f32 {
    f32::from_le_bytes(
        bytes[4 * i..4 * i + 4]
            .try_into()
            .expect("static table index"),
    )
}
fn start(b: usize) -> usize {
    usize::from(u16::from_le_bytes(
        STARTS[2 * b..2 * b + 2].try_into().unwrap(),
    ))
}
fn offset(b: usize) -> usize {
    u32::from_le_bytes(OFFSETS[4 * b..4 * b + 4].try_into().unwrap()) as usize
}
#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct Complex {
    pub re: f64,
    pub im: f64,
}
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
enum FftAction {
    #[default]
    Descend,
    Load,
    Mac,
    Store,
    Return,
}
#[derive(Clone, Copy, Default)]
struct Frame {
    n: usize,
    first: usize,
    stride: usize,
    base: usize,
    child: usize,
    b: usize,
    index: usize,
    c: usize,
    a: usize,
    action: FftAction,
}
/// Fixed explicit-stack transform. A unit is one leaf, stack action, load, store,
/// or complex MAC. No recursion or second full transform buffer.
pub struct Transform {
    values: [Complex; SIZE],
    children: [Complex; 7],
    sums: [Complex; 7],
    stack: [Frame; 7],
    depth: usize,
    done: bool,
    macs: u64,
    units: u64,
}
impl Default for Transform {
    fn default() -> Self {
        Self::new()
    }
}
impl Transform {
    pub fn new() -> Self {
        let mut t = Self {
            values: [Complex::default(); SIZE],
            children: [Complex::default(); 7],
            sums: [Complex::default(); 7],
            stack: [Frame::default(); 7],
            depth: 0,
            done: false,
            macs: 0,
            units: 0,
        };
        t.reset();
        t
    }
    fn reset(&mut self) {
        self.stack[0] = Frame {
            n: SIZE,
            stride: 1,
            ..Frame::default()
        };
        self.depth = 0;
        self.done = false;
        self.macs = 0;
        self.units = 0;
    }
    pub fn done(&self) -> bool {
        self.done
    }
    pub fn macs(&self) -> u64 {
        self.macs
    }
    pub fn units(&self) -> u64 {
        self.units
    }
    pub fn values(&self) -> &[Complex; SIZE] {
        &self.values
    }
    /// Input must remain identical across calls. Primitive probes are real,
    /// unwindowed fixed arrays; Session uses the same engine with a ring/Hann leaf.
    pub fn step(&mut self, quota: usize, input: &[f32; SIZE], cancel: &AtomicBool) -> usize {
        let mut used = 0;
        while used < quota && !self.done && !cancel.load(Ordering::Acquire) {
            self.unit(|j| f64::from(input[j]));
            used += 1;
        }
        used
    }
    fn unit(&mut self, leaf: impl FnOnce(usize) -> f64) {
        let d = self.depth;
        let f = self.stack[d];
        self.units += 1;
        if f.n == 1 {
            self.values[f.base] = Complex {
                re: leaf(f.first),
                im: 0.0,
            };
            self.depth -= 1;
            return;
        }
        let p = RADICES[d];
        let m = f.n / p;
        match f.action {
            FftAction::Descend => {
                if f.child < p {
                    self.stack[d].child += 1;
                    self.stack[d + 1] = Frame {
                        n: m,
                        first: f.first + f.child * f.stride,
                        stride: f.stride * p,
                        base: f.base + f.child * m,
                        ..Frame::default()
                    };
                    self.depth += 1;
                } else {
                    self.stack[d].action = FftAction::Load;
                }
            }
            FftAction::Load => {
                self.children[f.index] = self.values[f.base + f.index * m + f.b];
                self.stack[d].index += 1;
                if f.index + 1 == p {
                    self.sums[0] = Complex::default();
                    self.stack[d].c = 0;
                    self.stack[d].a = 0;
                    self.stack[d].action = FftAction::Mac;
                }
            }
            FftAction::Mac => {
                let k = f.b + m * f.c;
                let root = ((SIZE / f.n) * f.a * k) % SIZE;
                let w = Complex {
                    re: f64_at(ROOTS, 2 * root),
                    im: f64_at(ROOTS, 2 * root + 1),
                };
                let x = self.children[f.a];
                let re = x.re * w.re - x.im * w.im;
                let im = x.re * w.im + x.im * w.re;
                self.sums[f.c].re += re;
                self.sums[f.c].im += im;
                self.macs += 1;
                self.stack[d].a += 1;
                if f.a + 1 == p {
                    self.stack[d].a = 0;
                    self.stack[d].c += 1;
                    if f.c + 1 == p {
                        self.stack[d].index = 0;
                        self.stack[d].action = FftAction::Store;
                    } else {
                        self.sums[f.c + 1] = Complex::default();
                    }
                }
            }
            FftAction::Store => {
                self.values[f.base + f.index * m + f.b] = self.sums[f.index];
                self.stack[d].index += 1;
                if f.index + 1 == p {
                    self.stack[d].b += 1;
                    self.stack[d].index = 0;
                    self.stack[d].action = if f.b + 1 == m {
                        FftAction::Return
                    } else {
                        FftAction::Load
                    };
                }
            }
            FftAction::Return => {
                if d == 0 {
                    self.done = true;
                } else {
                    self.depth -= 1;
                }
            }
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    UnsupportedProfile,
    SourceOffset,
    SourceLength,
    InvalidPcm,
    TruncatedSource,
    ArithmeticOverflow,
    ZeroBudget,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}
#[derive(Clone, Copy, Debug)]
pub struct Config {
    pub source_rate: u32,
    pub source_frames: u64,
}
#[derive(Clone, Copy)]
pub struct InputBlock<'a> {
    pub first_sample: u64,
    pub samples: &'a [f32],
}
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
    Fill,
    Transform,
    Magnitudes,
    Filter,
    Log,
    Difference,
    CommitHistory,
    Emit,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flow {
    Budget,
    NeedInput,
    NeedOutput,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RowTime {
    pub row: u64,
    pub center_sample: u64,
    pub frame_first_sample: i64,
    pub frame_end_sample: u64,
    pub required_input_samples: u64,
    pub source_center_q32: u64,
    pub required_original_source_frames: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Progress {
    pub consumed_samples: u64,
    pub produced_rows: u64,
    pub macs: u64,
    pub units: u64,
    pub cancellation_phase: Option<Phase>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StepReport {
    pub consumed_samples: usize,
    pub produced_rows: usize,
    pub first_row: u64,
    pub units: usize,
    pub flow: Flow,
    pub status: Status,
    pub error: Option<Error>,
}
pub struct Session {
    config: Config,
    ring: [f32; SIZE],
    fft: Transform,
    magnitudes: [f32; BINS],
    previous: [f32; BANDS],
    row: [f32; FEATURES],
    count: u64,
    rows: u64,
    read: u64,
    written: u64,
    units: u64,
    macs: u64,
    phase: Phase,
    status: Status,
    error: Option<Error>,
    cancellation_phase: Option<Phase>,
    index: usize,
    term: usize,
    accumulator: f64,
}
impl Session {
    pub fn new(config: Config) -> Result<Self, Error> {
        if ![44100, 48000].contains(&config.source_rate)
            || config.source_frames > 600 * u64::from(config.source_rate)
        {
            return Err(Error::UnsupportedProfile);
        }
        let count = (config.source_frames * 22050).div_ceil(u64::from(config.source_rate));
        let empty = count == 0;
        Ok(Self {
            config,
            ring: [0.0; SIZE],
            fft: Transform::new(),
            magnitudes: [0.0; BINS],
            previous: [0.0; BANDS],
            row: [0.0; FEATURES],
            count,
            rows: count.div_ceil(HOP as u64),
            read: 0,
            written: 0,
            units: 0,
            macs: 0,
            phase: if empty { Phase::Terminal } else { Phase::Fill },
            status: if empty {
                Status::NoEvidence
            } else {
                Status::Running
            },
            error: None,
            cancellation_phase: None,
            index: 0,
            term: 0,
            accumulator: 0.0,
        })
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn phase(&self) -> Phase {
        self.phase
    }
    pub fn error(&self) -> Option<Error> {
        self.error
    }
    pub fn input_count(&self) -> u64 {
        self.count
    }
    pub fn row_count(&self) -> u64 {
        self.rows
    }
    pub fn progress(&self) -> Progress {
        Progress {
            consumed_samples: self.read,
            produced_rows: self.written,
            macs: self.macs,
            units: self.units,
            cancellation_phase: self.cancellation_phase,
        }
    }
    pub fn row_time(&self, row: u64) -> Option<RowTime> {
        if row >= self.rows {
            return None;
        }
        let c = row * HOP as u64;
        let q = self.count.min(c + BINS as u64);
        let required = if q == 0 {
            0
        } else {
            let center = if self.config.source_rate == 44100 {
                2 * (q - 1)
            } else {
                (q - 1) * 320 / 147
            };
            self.config.source_frames.min(center + 33)
        };
        Some(RowTime {
            row,
            center_sample: c,
            frame_first_sample: c as i64 - BINS as i64,
            frame_end_sample: c + BINS as u64,
            required_input_samples: q,
            source_center_q32: row * u64::from(self.config.source_rate) * (1 << 32) / 50,
            required_original_source_frames: required,
        })
    }
    fn fail(&mut self, e: Error) {
        self.error = Some(e);
        self.status = Status::Failed;
        self.phase = Phase::Terminal;
    }
    pub fn finish_input(&mut self) {
        if self.status == Status::Running && self.read < self.count {
            self.fail(Error::TruncatedSource);
        }
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.cancellation_phase = Some(self.phase);
            self.status = Status::Cancelled;
            self.phase = Phase::Terminal;
        }
    }
    pub fn step(
        &mut self,
        quota: usize,
        input: InputBlock<'_>,
        output: &mut [[f32; FEATURES]],
        cancel: &AtomicBool,
    ) -> StepReport {
        let mut report = StepReport {
            consumed_samples: 0,
            produced_rows: 0,
            first_row: self.written,
            units: 0,
            flow: Flow::Budget,
            status: self.status,
            error: None,
        };
        if quota == 0 {
            report.error = Some(Error::ZeroBudget);
            return report;
        }
        if self.status == Status::Running && cancel.load(Ordering::Acquire) {
            self.cancel();
        }
        if self.status == Status::Running {
            if input.first_sample != self.read {
                self.fail(Error::SourceOffset);
            } else if input.samples.len() as u64 > self.count - self.read {
                self.fail(Error::SourceLength);
            }
        }
        while self.status == Status::Running && report.units < quota {
            if cancel.load(Ordering::Acquire) {
                self.cancel();
                break;
            }
            if report.produced_rows == output.len() {
                report.flow = Flow::NeedOutput;
                break;
            }
            if self.phase == Phase::Fill {
                let required = self.count.min(self.written * HOP as u64 + BINS as u64);
                if self.read < required {
                    if report.consumed_samples == input.samples.len() {
                        report.flow = Flow::NeedInput;
                        break;
                    }
                    let x = input.samples[report.consumed_samples];
                    if !x.is_finite() || x.abs() > 2.0 {
                        self.fail(Error::InvalidPcm);
                        break;
                    }
                    self.ring[self.read as usize % SIZE] = x;
                    self.read += 1;
                    report.consumed_samples += 1;
                } else {
                    self.fft.reset();
                    self.phase = Phase::Transform;
                }
            } else {
                match self.phase {
                    Phase::Transform => {
                        let first = (self.written * HOP as u64) as i64 - BINS as i64;
                        let ring = &self.ring;
                        let count = self.count;
                        let old = self.fft.macs;
                        self.fft.unit(|n| {
                            let j = first + n as i64;
                            let x = if j < 0 || j as u64 >= count {
                                0.0
                            } else {
                                f64::from(ring[j as usize % SIZE])
                            };
                            x * f64_at(HANN, n)
                        });
                        self.macs += self.fft.macs - old;
                        if self.fft.done {
                            self.index = 0;
                            self.phase = Phase::Magnitudes;
                        }
                    }
                    Phase::Magnitudes => {
                        let z = self.fft.values[self.index];
                        self.magnitudes[self.index] =
                            f64::from(z.re as f32).hypot(f64::from(z.im as f32)) as f32;
                        self.index += 1;
                        if self.index == BINS {
                            self.index = 0;
                            self.term = 0;
                            self.accumulator = 0.0;
                            self.phase = Phase::Filter;
                        }
                    }
                    Phase::Filter => {
                        let lo = offset(self.index);
                        let terms = offset(self.index + 1) - lo;
                        if self.term < terms {
                            let product = f64::from(self.magnitudes[start(self.index) + self.term])
                                * f64::from(f32_at(WEIGHTS, lo + self.term));
                            self.accumulator += product;
                            self.term += 1;
                        } else {
                            self.row[self.index] = self.accumulator as f32;
                            self.accumulator = 0.0;
                            self.term = 0;
                            self.index += 1;
                            if self.index == BANDS {
                                self.index = 0;
                                self.phase = Phase::Log;
                            }
                        }
                    }
                    Phase::Log => {
                        self.row[self.index] =
                            f64::from(1.0_f32 + self.row[self.index]).log10() as f32;
                        self.index += 1;
                        if self.index == BANDS {
                            self.index = 0;
                            self.phase = Phase::Difference;
                        }
                    }
                    Phase::Difference => {
                        self.row[BANDS + self.index] = if self.written == 0 {
                            0.0
                        } else {
                            (self.row[self.index] - self.previous[self.index]).max(0.0)
                        };
                        self.index += 1;
                        if self.index == BANDS {
                            self.index = 0;
                            self.phase = Phase::CommitHistory;
                        }
                    }
                    Phase::CommitHistory => {
                        self.previous[self.index] = self.row[self.index];
                        self.index += 1;
                        if self.index == BANDS {
                            self.phase = Phase::Emit;
                        }
                    }
                    Phase::Emit => {
                        output[report.produced_rows] = self.row;
                        report.produced_rows += 1;
                        self.written += 1;
                        if self.written == self.rows {
                            self.status = Status::Done;
                            self.phase = Phase::Terminal;
                        } else {
                            self.phase = Phase::Fill;
                        }
                    }
                    Phase::Fill | Phase::Terminal => unreachable!(),
                }
            }
            report.units += 1;
            self.units += 1;
            if self.units > 4_000_000_000 {
                self.fail(Error::ArithmeticOverflow);
            }
        }
        report.status = self.status;
        report.error = self.error;
        if self.status != Status::Running {
            report.flow = Flow::Terminal;
        }
        report
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn intermediate_window_spectrum_magnitude_and_partial_radix7_filter() {
        let c = synthetic::fixture(11);
        let mut s = Session::new(c.config).unwrap();
        let flag = AtomicBool::new(false);
        let mut saw_radix7_mac = false;
        while s.phase != Phase::Magnitudes {
            let at = s.read;
            let sample = [c.sample(at)];
            s.step(
                1,
                InputBlock {
                    first_sample: at,
                    samples: &sample,
                },
                &mut [[0.0; FEATURES]],
                &flag,
            );
            let f = s.fft.stack[s.fft.depth];
            if s.phase == Phase::Transform
                && s.fft.depth == 5
                && f.action == FftAction::Mac
                && f.a > 0
            {
                saw_radix7_mac = true;
                let p = s.progress();
                let old = s.fft.units;
                assert_eq!(
                    s.step(
                        0,
                        InputBlock {
                            first_sample: at,
                            samples: &[]
                        },
                        &mut [],
                        &flag
                    )
                    .error,
                    Some(Error::ZeroBudget)
                );
                assert_eq!(s.progress(), p);
                assert_eq!(s.fft.units, old);
            }
        }
        assert!(saw_radix7_mac);
        // Direct source-index DFT independently checks window indexing and no
        // circular shift before the f32 STFT projection/magnitude boundary.
        let expected: [f64; BINS] = std::array::from_fn(|k| {
            let mut re = 0.0;
            let mut im = 0.0;
            for n in BINS..SIZE {
                let x = f64::from(c.sample((n - BINS) as u64)) * f64_at(HANN, n);
                re += x * f64_at(ROOTS, 2 * ((n * k) % SIZE));
                im += x * f64_at(ROOTS, 2 * ((n * k) % SIZE) + 1);
            }
            let got = s.fft.values[k];
            assert!((got.re - re).hypot(got.im - im) <= 1e-4 + 2e-5 * re.hypot(im));
            f64::from(re as f32).hypot(f64::from(im as f32))
        });
        while s.phase != Phase::Filter {
            s.step(
                1,
                InputBlock {
                    first_sample: s.read,
                    samples: &[],
                },
                &mut [[0.0; FEATURES]],
                &flag,
            );
        }
        for (got, expected) in s.magnitudes.iter().zip(expected) {
            assert!((f64::from(*got) - expected).abs() <= 2e-5 + 2e-5 * expected);
        }
        s.step(
            1,
            InputBlock {
                first_sample: s.read,
                samples: &[],
            },
            &mut [[123.0; FEATURES]],
            &flag,
        );
        assert_eq!(s.term, 1);
        s.cancel();
        assert_eq!(s.cancellation_phase, Some(Phase::Filter));
    }
}

/// Fixed mathematical workload helpers; not part of Session's input authority.
pub mod synthetic {
    use super::*;
    use sha2::{Digest, Sha256};
    macro_rules! raw {
        ($name:literal) => {
            include_bytes!(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/../../docs/rust/fixtures/product-host-frontend-hs04/",
                $name
            ))
        };
    }
    #[derive(Clone, Copy)]
    pub struct Case {
        pub name: &'static str,
        pub config: Config,
        pub raw: &'static [u8],
        pub signal: &'static str,
    }
    impl Case {
        pub fn sample(self, j: u64) -> f32 {
            if !self.raw.is_empty() {
                return f32_at(self.raw, j as usize);
            }
            match self.signal {
                "zero" => 0.0,
                "dc2" => 2.0,
                "pulse441" => {
                    if j % 441 == 0 {
                        2.0
                    } else {
                        0.0
                    }
                }
                "alternating2" => {
                    if j % 2 == 0 {
                        2.0
                    } else {
                        -2.0
                    }
                }
                _ => panic!("fixed signal"),
            }
        }
    }
    pub const FIXTURE_COUNT: usize = 20;
    pub const PRIMITIVE_COUNT: usize = 10;
    pub fn fixture(i: usize) -> Case {
        match i {
            0 => Case {
                name: "empty",
                config: Config {
                    source_rate: 44100,
                    source_frames: 0,
                },
                raw: raw!("assets/empty-input.f32le"),
                signal: "zero",
            },
            1 => Case {
                name: "one-impulse",
                config: Config {
                    source_rate: 48000,
                    source_frames: 1,
                },
                raw: raw!("assets/one-impulse-input.f32le"),
                signal: "impulse0",
            },
            2 => Case {
                name: "zero440",
                config: Config {
                    source_rate: 44100,
                    source_frames: 879,
                },
                raw: raw!("assets/zero440-input.f32le"),
                signal: "zero",
            },
            3 => Case {
                name: "zero441",
                config: Config {
                    source_rate: 48000,
                    source_frames: 958,
                },
                raw: raw!("assets/zero441-input.f32le"),
                signal: "zero",
            },
            4 => Case {
                name: "zero442",
                config: Config {
                    source_rate: 44100,
                    source_frames: 883,
                },
                raw: raw!("assets/zero442-input.f32le"),
                signal: "zero",
            },
            5 => Case {
                name: "dc1-882",
                config: Config {
                    source_rate: 48000,
                    source_frames: 1918,
                },
                raw: raw!("assets/dc1-882-input.f32le"),
                signal: "dc1",
            },
            6 => Case {
                name: "dc2-1764",
                config: Config {
                    source_rate: 44100,
                    source_frames: 3527,
                },
                raw: raw!("assets/dc2-1764-input.f32le"),
                signal: "dc2",
            },
            7 => Case {
                name: "negative-dc2-1765",
                config: Config {
                    source_rate: 48000,
                    source_frames: 3841,
                },
                raw: raw!("assets/negative-dc2-1765-input.f32le"),
                signal: "dc-minus2",
            },
            8 => Case {
                name: "impulse0-1764",
                config: Config {
                    source_rate: 44100,
                    source_frames: 3527,
                },
                raw: raw!("assets/impulse0-1764-input.f32le"),
                signal: "impulse0",
            },
            9 => Case {
                name: "impulse882-1764",
                config: Config {
                    source_rate: 48000,
                    source_frames: 3838,
                },
                raw: raw!("assets/impulse882-1764-input.f32le"),
                signal: "impulse882",
            },
            10 => Case {
                name: "impulseEOF-1765",
                config: Config {
                    source_rate: 44100,
                    source_frames: 3529,
                },
                raw: raw!("assets/impulseEOF-1765-input.f32le"),
                signal: "impulseEOF",
            },
            11 => Case {
                name: "step2-2205",
                config: Config {
                    source_rate: 48000,
                    source_frames: 4798,
                },
                raw: raw!("assets/step2-2205-input.f32le"),
                signal: "step2",
            },
            12 => Case {
                name: "periodic257-1323",
                config: Config {
                    source_rate: 44100,
                    source_frames: 2645,
                },
                raw: raw!("assets/periodic257-1323-input.f32le"),
                signal: "periodic257",
            },
            13 => Case {
                name: "cos7-1764",
                config: Config {
                    source_rate: 48000,
                    source_frames: 3838,
                },
                raw: raw!("assets/cos7-1764-input.f32le"),
                signal: "cos7",
            },
            14 => Case {
                name: "alt2-1764",
                config: Config {
                    source_rate: 44100,
                    source_frames: 3527,
                },
                raw: raw!("assets/alt2-1764-input.f32le"),
                signal: "alternating2",
            },
            15 => Case {
                name: "pulse441-2205",
                config: Config {
                    source_rate: 48000,
                    source_frames: 4798,
                },
                raw: raw!("assets/pulse441-2205-input.f32le"),
                signal: "pulse441",
            },
            16 => Case {
                name: "composition-44100-1",
                config: Config {
                    source_rate: 44100,
                    source_frames: 321,
                },
                raw: raw!("assets/composition-44100-1-input.f32le"),
                signal: "frozen-resampler-composition",
            },
            17 => Case {
                name: "composition-44100-2",
                config: Config {
                    source_rate: 44100,
                    source_frames: 321,
                },
                raw: raw!("assets/composition-44100-2-input.f32le"),
                signal: "frozen-resampler-composition",
            },
            18 => Case {
                name: "composition-48000-1",
                config: Config {
                    source_rate: 48000,
                    source_frames: 321,
                },
                raw: raw!("assets/composition-48000-1-input.f32le"),
                signal: "frozen-resampler-composition",
            },
            19 => Case {
                name: "composition-48000-2",
                config: Config {
                    source_rate: 48000,
                    source_frames: 321,
                },
                raw: raw!("assets/composition-48000-2-input.f32le"),
                signal: "frozen-resampler-composition",
            },
            _ => panic!("fixed fixture index"),
        }
    }
    pub fn primitive(i: usize) -> (&'static str, [f32; SIZE]) {
        let (name, bytes): (&str, &[u8]) = match i {
            0 => ("delta0-a1", raw!("assets/delta0-a1-input.f32le")),
            1 => ("constant-a1", raw!("assets/constant-a1-input.f32le")),
            2 => ("cos7-a1", raw!("assets/cos7-a1-input.f32le")),
            3 => ("delta3-a1", raw!("assets/delta3-a1-input.f32le")),
            4 => ("nyquist-a1", raw!("assets/nyquist-a1-input.f32le")),
            5 => ("delta0-a2", raw!("assets/delta0-a2-input.f32le")),
            6 => ("constant-a2", raw!("assets/constant-a2-input.f32le")),
            7 => ("cos7-a2", raw!("assets/cos7-a2-input.f32le")),
            8 => ("delta3-a2", raw!("assets/delta3-a2-input.f32le")),
            9 => ("nyquist-a2", raw!("assets/nyquist-a2-input.f32le")),
            _ => panic!("fixed primitive index"),
        };
        (name, std::array::from_fn(|j| f32_at(bytes, j)))
    }
    pub fn full(name: &str) -> Option<Case> {
        Some(match name {
            "fullzero-44100" => Case {
                name: "fullzero-44100",
                config: Config {
                    source_rate: 44100,
                    source_frames: 26460000,
                },
                raw: &[],
                signal: "zero",
            },
            "fulldc2-44100" => Case {
                name: "fulldc2-44100",
                config: Config {
                    source_rate: 44100,
                    source_frames: 26460000,
                },
                raw: &[],
                signal: "dc2",
            },
            "fullpulse441-48000" => Case {
                name: "fullpulse441-48000",
                config: Config {
                    source_rate: 48000,
                    source_frames: 28800000,
                },
                raw: &[],
                signal: "pulse441",
            },
            "fullalternating2-48000" => Case {
                name: "fullalternating2-48000",
                config: Config {
                    source_rate: 48000,
                    source_frames: 28800000,
                },
                raw: &[],
                signal: "alternating2",
            },
            _ => return None,
        })
    }
    pub struct Driver {
        pub session: Session,
        pub case: Case,
        pub input: [f32; 512],
        pub output: [[f32; FEATURES]; 1],
        begin: usize,
        end: usize,
        input_hash: Sha256,
        output_hash: Sha256,
        pub last_step_seconds: f64,
    }
    impl Driver {
        pub fn new(case: Case) -> Self {
            let session = Session::new(case.config).unwrap();
            let mut input_hash = Sha256::new();
            input_hash.update(case.config.source_rate.to_le_bytes());
            input_hash.update(case.config.source_frames.to_le_bytes());
            input_hash.update(session.input_count().to_le_bytes());
            Self {
                session,
                case,
                input: [0.0; 512],
                output: [[0.0; FEATURES]; 1],
                begin: 0,
                end: 0,
                input_hash,
                output_hash: Sha256::new(),
                last_step_seconds: 0.0,
            }
        }
        pub fn prepare(&mut self) {
            if self.begin == self.end {
                self.begin = 0;
                let p = self.session.progress().consumed_samples;
                self.end = (self.session.input_count() - p).min(512) as usize;
                for j in 0..self.end {
                    self.input[j] = self.case.sample(p + j as u64);
                }
            }
        }
        pub fn step(&mut self, flag: &AtomicBool) -> StepReport {
            let p = self.session.progress().consumed_samples;
            let started = std::time::Instant::now();
            let r = self.session.step(
                4096,
                InputBlock {
                    first_sample: p,
                    samples: &self.input[self.begin..self.end],
                },
                &mut self.output,
                flag,
            );
            self.last_step_seconds = started.elapsed().as_secs_f64();
            for x in &self.input[self.begin..self.begin + r.consumed_samples] {
                self.input_hash.update(x.to_le_bytes());
            }
            self.begin += r.consumed_samples;
            if r.produced_rows > 0 {
                for x in self.output[0] {
                    self.output_hash.update(x.to_le_bytes());
                }
            }
            r
        }
        pub fn hashes(&self) -> (String, String) {
            (
                format!("{:x}", self.input_hash.clone().finalize()),
                format!("{:x}", self.output_hash.clone().finalize()),
            )
        }
    }
}
