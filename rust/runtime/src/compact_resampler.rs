// SPDX-License-Identifier: Apache-2.0
//! HS-03 safe host FIR stream. Session has no allocation, I/O or retained caller slices.
use std::sync::atomic::{AtomicBool, Ordering};
pub const RING_SAMPLES: usize = 65;
pub const OUTPUT_RATE: u32 = 22050;
const FIR44: &[u8; 520] = include_bytes!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../docs/rust/fixtures/product-compact-cs02/assets/fir-44100.f64le"
));
const FIR48: &[u8; 76440] = include_bytes!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../docs/rust/fixtures/product-compact-cs02/assets/fir-48000.f64le"
));
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    UnsupportedProfile,
    InvalidInput,
    InvalidPcm,
    SourceOffset,
    SourceLength,
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
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Config {
    pub source_rate: u32,
    pub channels: u8,
    pub frames: u64,
}
#[derive(Clone, Copy)]
pub struct InputBlock<'a> {
    pub first_frame: u64,
    pub interleaved: &'a [f32],
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
    Convolve,
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
pub struct Position {
    pub center: u64,
    pub phase: u16,
    pub phase_count: u16,
    pub required_frames: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Progress {
    pub source_frames: u64,
    pub output_samples: u64,
    pub macs: u64,
    pub units: u64,
    pub source_frames_before_first_output: Option<u64>,
    pub cancellation_phase: Option<Phase>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StepReport {
    pub consumed_frames: usize,
    pub produced: usize,
    pub first_output: u64,
    pub units: usize,
    pub flow: Flow,
    pub status: Status,
    pub error: Option<Error>,
}
pub struct Session {
    config: Config,
    ring: [f32; 65],
    read: u64,
    written: u64,
    count: u64,
    units: u64,
    macs: u64,
    phase: Phase,
    status: Status,
    error: Option<Error>,
    tap: usize,
    accumulator: f64,
    center: u64,
    fraction: usize,
    first_read: Option<u64>,
    cancel_phase: Option<Phase>,
}
impl Session {
    pub fn new(config: Config) -> Result<Self, Error> {
        if ![44100, 48000].contains(&config.source_rate)
            || ![1, 2].contains(&config.channels)
            || config.frames > 600 * u64::from(config.source_rate)
        {
            return Err(Error::UnsupportedProfile);
        }
        let count = (config.frames * 22050).div_ceil(u64::from(config.source_rate));
        let empty = config.frames == 0;
        Ok(Self {
            config,
            ring: [0.0; 65],
            read: 0,
            written: 0,
            count,
            units: 0,
            macs: 0,
            phase: if empty { Phase::Terminal } else { Phase::Fill },
            status: if empty {
                Status::NoEvidence
            } else {
                Status::Running
            },
            error: None,
            tap: 0,
            accumulator: 0.0,
            center: 0,
            fraction: 0,
            first_read: None,
            cancel_phase: None,
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
    pub fn output_count(&self) -> u64 {
        self.count
    }
    pub fn progress(&self) -> Progress {
        Progress {
            source_frames: self.read,
            output_samples: self.written,
            macs: self.macs,
            units: self.units,
            source_frames_before_first_output: self.first_read,
            cancellation_phase: self.cancel_phase,
        }
    }
    pub fn source_position(&self, k: u64) -> Option<Position> {
        if k >= self.count {
            return None;
        }
        let (center, phase, phase_count) = if self.config.source_rate == 44100 {
            (k * 2, 0, 1)
        } else {
            (k * 320 / 147, (k * 320 % 147) as u16, 147)
        };
        Some(Position {
            center,
            phase,
            phase_count,
            required_frames: (center + 33).min(self.config.frames),
        })
    }
    fn stop(&mut self, status: Status) {
        self.status = status;
        self.phase = Phase::Terminal;
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.cancel_phase = Some(self.phase);
            self.stop(Status::Cancelled);
        }
    }
    fn fail(&mut self, e: Error) {
        self.error = Some(e);
        self.stop(Status::Failed);
    }
    pub fn finish_input(&mut self) -> Result<(), Error> {
        if self.status == Status::Running && self.read < self.config.frames {
            self.fail(Error::TruncatedSource);
            return Err(Error::TruncatedSource);
        }
        Ok(())
    }
    pub fn step(
        &mut self,
        quota: usize,
        input: InputBlock<'_>,
        output: &mut [f32],
        cancel: &AtomicBool,
    ) -> StepReport {
        let mut report = StepReport {
            consumed_frames: 0,
            produced: 0,
            first_output: self.written,
            units: 0,
            flow: Flow::Budget,
            status: self.status,
            error: None,
        };
        if quota == 0 {
            report.error = Some(Error::ZeroBudget);
            return report;
        }
        if self.status != Status::Running {
            report.flow = Flow::Terminal;
            return report;
        }
        if cancel.load(Ordering::Acquire) {
            self.cancel();
            report.status = self.status;
            report.flow = Flow::Terminal;
            return report;
        }
        let channels = usize::from(self.config.channels);
        let pre_error = if input.first_frame != self.read {
            Some(Error::SourceOffset)
        } else if input.interleaved.len() % channels != 0 {
            Some(Error::InvalidInput)
        } else if (input.interleaved.len() / channels) as u64 > self.config.frames - self.read {
            Some(Error::SourceLength)
        } else {
            None
        };
        if let Some(e) = pre_error {
            self.fail(e);
            report.error = Some(e);
            report.status = self.status;
            report.flow = Flow::Terminal;
            return report;
        }
        while report.units < quota && self.status == Status::Running {
            if cancel.load(Ordering::Acquire) {
                self.cancel();
                break;
            }
            if report.produced == output.len() {
                report.flow = Flow::NeedOutput;
                break;
            }
            match self.phase {
                Phase::Fill => {
                    let needed = (self.center + 33).min(self.config.frames);
                    if self.read < needed {
                        if report.consumed_frames == input.interleaved.len() / channels {
                            report.flow = Flow::NeedInput;
                            break;
                        }
                        let start = report.consumed_frames * channels;
                        let values = &input.interleaved[start..start + channels];
                        if values
                            .iter()
                            .any(|v| !v.is_finite() || !(-1.0..=1.0).contains(v))
                        {
                            self.fail(Error::InvalidPcm);
                            break;
                        }
                        let mono = if channels == 1 {
                            values[0]
                        } else {
                            let sum = values[0] + values[1];
                            sum * 0.5
                        };
                        self.ring[(self.read % 65) as usize] = mono;
                        self.read += 1;
                        report.consumed_frames += 1;
                    } else {
                        self.tap = 0;
                        self.accumulator = 0.0;
                        self.phase = Phase::Convolve;
                    }
                }
                Phase::Convolve => {
                    let index = self.center as i64 + self.tap as i64 - 32;
                    let sample = if index < 0 || index >= self.config.frames as i64 {
                        0.0
                    } else {
                        let j = index as u64;
                        if j >= self.read || self.read - j > 65 {
                            self.fail(Error::ArithmeticOverflow);
                            break;
                        }
                        self.ring[(j % 65) as usize]
                    };
                    let table: &[u8] = if self.config.source_rate == 44100 {
                        FIR44
                    } else {
                        FIR48
                    };
                    let at = (self.fraction * 65 + self.tap) * 8;
                    let coefficient = f64::from_le_bytes(
                        table[at..at + 8].try_into().expect("fixed coefficient"),
                    );
                    let product = f64::from(sample) * coefficient;
                    self.accumulator += product;
                    self.macs += 1;
                    self.tap += 1;
                    if self.tap == 65 {
                        self.phase = Phase::Emit;
                    }
                }
                Phase::Emit => {
                    let value = self.accumulator as f32;
                    if !value.is_finite() {
                        self.fail(Error::ArithmeticOverflow);
                        break;
                    }
                    output[report.produced] = value;
                    report.produced += 1;
                    self.written += 1;
                    if self.first_read.is_none() {
                        self.first_read = Some(self.read);
                    }
                    if self.written == self.count {
                        if self.read != self.config.frames {
                            self.fail(Error::TruncatedSource);
                            break;
                        }
                        self.stop(Status::Done);
                    } else {
                        let p = self
                            .source_position(self.written)
                            .expect("owned next output");
                        self.center = p.center;
                        self.fraction = usize::from(p.phase);
                        self.phase = Phase::Fill;
                    }
                }
                Phase::Terminal => unreachable!("terminal fence"),
            }
            report.units += 1;
            self.units += 1;
        }
        report.status = self.status;
        if self.status != Status::Running {
            report.flow = Flow::Terminal;
            report.error = self.error;
        }
        report
    }
}

/// Public artificial generators and allocating hash/report helpers, outside Session.
pub mod synthetic {
    use super::*;
    use sha2::{Digest, Sha256};
    #[derive(Clone, Copy, Debug)]
    pub struct Case {
        pub name: &'static str,
        pub config: Config,
        pub signal: &'static str,
        pub impulse: u64,
        pub expected_input: &'static str,
        pub expected_output: &'static str,
    }
    impl Case {
        pub fn sample(self, j: u64, ch: u8) -> f32 {
            match self.signal {
                "zero" => 0.0,
                "negative-zero" => f32::from_bits(0x80000000),
                "dc" => 1.0,
                "impulse" | "left-impulse" | "right-impulse" => {
                    let active =
                        self.signal == "impulse" || ch == u8::from(self.signal == "right-impulse");
                    f32::from(u8::from(active && j == self.impulse))
                }
                "alternating" => {
                    if j % 2 == 0 {
                        1.0
                    } else {
                        -1.0
                    }
                }
                "step" => {
                    if j < self.config.frames / 2 {
                        -1.0
                    } else {
                        1.0
                    }
                }
                "periodic" | "cancel-pair" => {
                    let (factor, offset) = if ch == 0 || self.signal == "cancel-pair" {
                        (17, 3)
                    } else {
                        (29, 11)
                    };
                    let value = ((j * factor + offset) % 257) as i32 - 128;
                    let value = value as f32 / 128.0;
                    if self.signal == "cancel-pair" && ch == 1 {
                        -value
                    } else {
                        value
                    }
                }
                _ => panic!("fixed signal"),
            }
        }
    }
    pub fn protocol_hash() -> String {
        format!(
            "{:x}",
            Sha256::digest(include_bytes!(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/../../docs/rust/PRODUCT-HOST-RESAMPLER-PROTOCOL.json"
            )))
        )
    }
    pub fn fixture_hash() -> String {
        format!(
            "{:x}",
            Sha256::digest(include_bytes!(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/../../docs/rust/fixtures/product-host-resampler-hs03/cases.json"
            )))
        )
    }
    pub fn asset_hash() -> String {
        let mut h = Sha256::new();
        h.update(FIR44);
        h.update(FIR48);
        format!("{:x}", h.finalize())
    }
    /// Bounded artificial source/sink. No whole-source PCM or output is retained.
    pub struct Driver {
        pub session: Session,
        pub case: Case,
        input: [f32; 1024],
        output: [f32; 256],
        filled: usize,
        cursor: usize,
        input_chunk: usize,
        output_chunk: usize,
        quota: usize,
        input_hash: Sha256,
        output_hash: Sha256,
        pub minimum: Option<f32>,
        pub maximum: Option<f32>,
    }
    impl Driver {
        pub fn new(case: Case, input_chunk: usize, output_chunk: usize, quota: usize) -> Self {
            assert!(
                (1..=512).contains(&input_chunk) && (1..=256).contains(&output_chunk) && quota > 0
            );
            let mut h = Sha256::new();
            h.update(case.config.source_rate.to_le_bytes());
            h.update([case.config.channels]);
            h.update(case.config.frames.to_le_bytes());
            Self {
                session: Session::new(case.config).expect("fixed config"),
                case,
                input: [0.0; 1024],
                output: [0.0; 256],
                filled: 0,
                cursor: 0,
                input_chunk,
                output_chunk,
                quota,
                input_hash: h,
                output_hash: Sha256::new(),
                minimum: None,
                maximum: None,
            }
        }
        pub fn step(&mut self, flag: &AtomicBool) -> StepReport {
            let before = self.session.progress();
            let channels = usize::from(self.case.config.channels);
            if self.cursor == self.filled && self.session.status() == Status::Running {
                self.cursor = 0;
                self.filled = self
                    .input_chunk
                    .min((self.case.config.frames - before.source_frames) as usize);
                for j in 0..self.filled {
                    for ch in 0..channels {
                        self.input[j * channels + ch] =
                            self.case.sample(before.source_frames + j as u64, ch as u8);
                    }
                }
            }
            let start = self.cursor * channels;
            let report = self.session.step(
                self.quota,
                InputBlock {
                    first_frame: before.source_frames,
                    interleaved: &self.input[start..self.filled * channels],
                },
                &mut self.output[..self.output_chunk],
                flag,
            );
            assert_eq!(report.first_output, before.output_samples);
            for value in &self.input[start..start + report.consumed_frames * channels] {
                self.input_hash.update(value.to_bits().to_le_bytes());
            }
            self.cursor += report.consumed_frames;
            for value in &self.output[..report.produced] {
                self.output_hash.update(value.to_bits().to_le_bytes());
                self.minimum = Some(self.minimum.map_or(*value, |x| x.min(*value)));
                self.maximum = Some(self.maximum.map_or(*value, |x| x.max(*value)));
            }
            report
        }
        pub fn hashes(&self) -> (String, String) {
            (
                format!("{:x}", self.input_hash.clone().finalize()),
                format!("{:x}", self.output_hash.clone().finalize()),
            )
        }
    }
    pub const FIXTURE_COUNT: usize = 102;
    pub fn fixture(i: usize) -> Case {
        match i {
            0 => Case {
                name: "empty-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 0,
                },
                signal: "zero",
                impulse: 0,
                expected_input: "09901f2bcf9716a208c4381e72c086092eb6a3dabdb6fdd329de001265a72fb2",
                expected_output: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            },
            1 => Case {
                name: "one-dc-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 1,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "9953026a90001a067554fe636744a38515e58799ac0e801e9da73c4647b9cd96",
                expected_output: "a7d65f97024683f7f862fc7abf74c5762d66ff3964403962a7ed234b2852db75",
            },
            2 => Case {
                name: "two-dc-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 2,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "b57631d8b54e593c0dd88f5264b7337c20056a23118ade3f0091df407d6c7a34",
                expected_output: "1a0cac5f99518bbe230b733dea46280b016f20db1432644d8d1773f99c3aec1c",
            },
            3 => Case {
                name: "short31-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 31,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "604cbb4e19c74ef15ef6bb4c72e96a43d30062e14d91d50ac253a0a2c38a696d",
                expected_output: "12b3821da5efc0b2e16278ef35571d227be04402ba5eaa3213af152787cbc85e",
            },
            4 => Case {
                name: "lookahead32-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 32,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "5b6462489238723b83e6b2e9bb86f0d6a8a74c32600eaf4056570f499dd5d648",
                expected_output: "105b022b105cc2b4eeb04ee47b4d0e54d395c225aac00b7596ec148ebecdd770",
            },
            5 => Case {
                name: "lookahead33-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 33,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "dd3f776f5a9b85db13108598a6b9af0896bf666df9bbda13d5f6c4027f651225",
                expected_output: "0d19bd9faa53c9c62d84019fd6b69784ad0102c9c16e81c03fbc92025651e0e6",
            },
            6 => Case {
                name: "dc65-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 65,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "32401c03a21969043272bde609640645431264dae813b5abb177d793a76dec2d",
                expected_output: "f27db564326029b34c1e50a9a9e5704e2899a77f9fc3d10ef537ec85c102768a",
            },
            7 => Case {
                name: "dc321-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "1dbe595262967b03be0530636ea48b51f81e5b28cf83292610307bf20a3e6d7d",
                expected_output: "76fc14128f934dcfabdd98cb72cce591e1ee3dae3fe11d7b1fe2e282bb6ad9a4",
            },
            8 => Case {
                name: "impulse-start-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 0,
                expected_input: "297e77aa9939a5265bf61a474a16060d3716dff995a035179c78abbd5b33c1a0",
                expected_output: "68bdb388d8cb283116b0f6dce0889ea371749ad7f362faec6ea0ec6e5f5daf90",
            },
            9 => Case {
                name: "impulse32-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 32,
                expected_input: "36fd84fdf457b03f2c07ea67d147d535226d2c588bf6cfb256ffe3bf3cc4b419",
                expected_output: "da40e55840eb1580cd3baf1dc9302274325dde7a55d88544ee324358985bdff4",
            },
            10 => Case {
                name: "impulse-middle-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 160,
                expected_input: "6b9521301c73e01dc2f5a5782eb61b2f5aba58bae5dd7b82c4f3f55d6d0d7503",
                expected_output: "abf4ec1e5b51d55febbdc5860e8a5d283200ab3fe8a381205b799d5e5fc31d67",
            },
            11 => Case {
                name: "impulse-end-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 320,
                expected_input: "8ada07eece2c3bc4ee9f6fda550e3d670d2aa097d62f77c24d6314721219c6af",
                expected_output: "3e3f2e9c381b4da3dd9a3e5203beebf232739bcaac3e357835955c89c80ce21b",
            },
            12 => Case {
                name: "alternating-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "alternating",
                impulse: 0,
                expected_input: "ae9473e7a735feb4e6a8ba86828e53fac2943ed0b877c1192b980acc17e813bb",
                expected_output: "cb498bfe8a61ae8193e77a43d6957c6d8e9fc0a20bb993df74b9c702c2d51029",
            },
            13 => Case {
                name: "step-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 321,
                },
                signal: "step",
                impulse: 0,
                expected_input: "a4982ac0827a54c45020700d7d02e42a117ad8600608c4b73bebab71af07e6c3",
                expected_output: "9394af95bb58c057b9948b151b21136599983aeff0654dbdd3cb035b7bb0c4ec",
            },
            14 => Case {
                name: "periodic1025-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 1025,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "82c32d0b5c046391840153b8f27439397e7b8c33bf1ac28928c069613f52e16d",
                expected_output: "b8f06312b1b43c0acb7aadece4c211bd46a1fad116a75e5350b85f256f180b82",
            },
            15 => Case {
                name: "ringwrap4097-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 4097,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "915ccf26f7ef3113f269e0995f4b4bb8e12f18c3c4a93ccbe7d94a4f97beaf6d",
                expected_output: "0556197d74608b5337a6ae4941070788a85f70ba0224e32b2bda0968328a99cd",
            },
            16 => Case {
                name: "negative-zero-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 65,
                },
                signal: "negative-zero",
                impulse: 0,
                expected_input: "326d035a0aa9a8453e60bb3726220f224c778e290e04043319ef43e0a65654b2",
                expected_output: "115bad14f1c9f2c027a84de21b107015722cb76be8d0abf3760ad8e00d6c24a5",
            },
            17 => Case {
                name: "phase146-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 146,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "01934c9b27e32d8ed2590c9f52fe47955110efe68d7cc231ca8248d404117a72",
                expected_output: "431b5ecd578bcf1a2e3ec7ad4e32fd3994804d21d633dd451ba10f840a41cd0b",
            },
            18 => Case {
                name: "phase147-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 147,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "294778c7cfa4b447004c2dd61bb023527ff7cad34dabedc4b6a3b2580c7cbe06",
                expected_output: "f7878dd6fbc9577697900804ac37994ab71f0b2628a1396356e8f1ba7bc7b4d4",
            },
            19 => Case {
                name: "phase148-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 148,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "f80b0bc3bde342c08a1dfafc3705d2794aa9c0d06b05b6d4fc0b065d7ce99d4c",
                expected_output: "8c886421420ec6e796547eedbeeaed0b0d15feb4689b44faf93e4c87ea8dde6e",
            },
            20 => Case {
                name: "boundary319-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 319,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "e838fd42b0d250977edf9575684f8fd32880052b77dbf8ad854be29bc8948827",
                expected_output: "00cd5aa85716ea56c9c9bb0d64e7d9a1218cba0d90ba6c5d47ae719566ee851a",
            },
            21 => Case {
                name: "boundary320-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 320,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "159613f008310cf7ae93b5b306c1727ae9f1cdb0a840bbd2f46e693ca87b9fe1",
                expected_output: "2881af4cbd93295d081c6a630e23f28f965c5f178f3ebd4b507d5132860494ca",
            },
            22 => Case {
                name: "short64-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 64,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "b49cae2349118746cc9bb6d071ed007642b7747b4ce2c0b395f8b2ad9ef75914",
                expected_output: "b003e4bb20c4c612ff28360b0ed3046593c9f09b9755cd9ed37497715824326b",
            },
            23 => Case {
                name: "short66-44100-1",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 66,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "87f82d2cda01eaee3ce31c4d99cb96f59dc8464593beb60061211d5032bae24e",
                expected_output: "6e7e3c8165411476aa0b04073b15d13bce260aea63d978e4c39b07a5ca7e84b9",
            },
            24 => Case {
                name: "empty-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 0,
                },
                signal: "zero",
                impulse: 0,
                expected_input: "237ad00bad4143fb15699504b48f366e3cceb0c6585ee7a8fc151ff9ec819c1f",
                expected_output: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            },
            25 => Case {
                name: "one-dc-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 1,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "a1e71550f9c9e5eea9b10efa0f56a8d561d10824d60d0684b71d192aebf7992c",
                expected_output: "a7d65f97024683f7f862fc7abf74c5762d66ff3964403962a7ed234b2852db75",
            },
            26 => Case {
                name: "two-dc-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 2,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "43e06fa9727e0310c61a71000c8278b316d5ac1f092a207687e615e0067fa70c",
                expected_output: "1a0cac5f99518bbe230b733dea46280b016f20db1432644d8d1773f99c3aec1c",
            },
            27 => Case {
                name: "short31-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 31,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "c30523a80112387cdb2f24ed7d137a2a273fe5cf1fb7a6c0348920c1a7691a82",
                expected_output: "e62acdb9f30d77e78c25957e74eac12c42d9420fe7c799f5110aa20f63577e28",
            },
            28 => Case {
                name: "lookahead32-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 32,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "51401ba284e842debbfe693303327817877c68615a52eac6c2005a4788f72516",
                expected_output: "169e9cc2afd1eeab9ffad43a80de5f88aae79cef75251beccb1fdf4c3e12ca69",
            },
            29 => Case {
                name: "lookahead33-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 33,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "d0be30cf89aa661add2b433cadff5303a691698751a90c6ddc83b1c5371adac7",
                expected_output: "a3f277b3c3ad972879afa470c77fdcf58b672e62573bd4dba55c5f85d250f582",
            },
            30 => Case {
                name: "dc65-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 65,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "a88198f303df803f2f02eb9f9b9c9cba75a5ef1f5b3bedbdbe3fa8a04de18639",
                expected_output: "f27db564326029b34c1e50a9a9e5704e2899a77f9fc3d10ef537ec85c102768a",
            },
            31 => Case {
                name: "dc321-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "65d3e0b2d6f8d0a81d9e89a8ed4d3f1acbf8afd3eb2b42e939ecef2e5b094985",
                expected_output: "76fc14128f934dcfabdd98cb72cce591e1ee3dae3fe11d7b1fe2e282bb6ad9a4",
            },
            32 => Case {
                name: "impulse-start-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 0,
                expected_input: "ffd285ee95b67d5d9414c0400841575a18550c50043f5141b391101e11f9a035",
                expected_output: "68bdb388d8cb283116b0f6dce0889ea371749ad7f362faec6ea0ec6e5f5daf90",
            },
            33 => Case {
                name: "impulse32-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 32,
                expected_input: "4d91638620fd40d27279abebe07f9ac6b51549db489bdfde9fa9651d8ab847f1",
                expected_output: "da40e55840eb1580cd3baf1dc9302274325dde7a55d88544ee324358985bdff4",
            },
            34 => Case {
                name: "impulse-middle-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 160,
                expected_input: "a7b881272f37476399e1e9cb5eb4ba89fcccb8b147ce56f97b7b2d66f3ad98c0",
                expected_output: "abf4ec1e5b51d55febbdc5860e8a5d283200ab3fe8a381205b799d5e5fc31d67",
            },
            35 => Case {
                name: "impulse-end-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 320,
                expected_input: "78429b79625bcab107c585fe9876efd59cad5da1b6ce4edf27d409b307e16745",
                expected_output: "3e3f2e9c381b4da3dd9a3e5203beebf232739bcaac3e357835955c89c80ce21b",
            },
            36 => Case {
                name: "alternating-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "alternating",
                impulse: 0,
                expected_input: "99301864967aa89524f24596c440cce9b6d8ed2fc298dec12e007adf3f808bc6",
                expected_output: "cb498bfe8a61ae8193e77a43d6957c6d8e9fc0a20bb993df74b9c702c2d51029",
            },
            37 => Case {
                name: "step-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "step",
                impulse: 0,
                expected_input: "0aba3c0fc09ddd04f14cc987e35a81b23f3cc72ea9dd2491052fca91d9babb5a",
                expected_output: "9394af95bb58c057b9948b151b21136599983aeff0654dbdd3cb035b7bb0c4ec",
            },
            38 => Case {
                name: "periodic1025-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 1025,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "550007862feb8d2167f08e2ec444b07bc35377d847a9da3e03576aa1aba125c2",
                expected_output: "771dcadb808588832fa440e6462d95e1b13df06d5285b801e5e897a11ffddfef",
            },
            39 => Case {
                name: "ringwrap4097-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 4097,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "276529ebf08e21918ae69a7f5be714670e8afa1dba9e09322806354e87b9ff08",
                expected_output: "6c67370789c0136878242b683886054a66a973dcb3409d222a6ab38e6a7234b3",
            },
            40 => Case {
                name: "negative-zero-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 65,
                },
                signal: "negative-zero",
                impulse: 0,
                expected_input: "4dd873822cdddbecddcaefa239e8f68f98b10c447fb5d14e7f038b56004ba054",
                expected_output: "115bad14f1c9f2c027a84de21b107015722cb76be8d0abf3760ad8e00d6c24a5",
            },
            41 => Case {
                name: "phase146-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 146,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "ea65fa8342b72c1a818be229e2d0aabf86a99f151862fcc5bc535cffec325f58",
                expected_output: "eafd7d00eec32a08f16e6883ccc36394b65be7514f0f67465ecf4e81cbeec4ea",
            },
            42 => Case {
                name: "phase147-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 147,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "7d8d42ef251ae416d5acef303fde994a098446ff45db9bd920f5a74cd4132fbb",
                expected_output: "0a40dc6256c1f999d4e868397c36cc466e6e8e0165b770c695695c12c4bf7680",
            },
            43 => Case {
                name: "phase148-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 148,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "a7a39dd55d142e4702ee359173038d5eff57ca70507255b19053e5aa595ec42e",
                expected_output: "d159ee0a9f741ac77d19cc29a28ac85fd919d5cffb54f0df27a667619b0b60bf",
            },
            44 => Case {
                name: "boundary319-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 319,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "3ac8c42489c42950d9aff3f322c34e560f116fc35b2ebf7187ae6679c2268df3",
                expected_output: "205cee591b63136c1b6c276f19acf315f55dfb0b4759844e5f3974260a184761",
            },
            45 => Case {
                name: "boundary320-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 320,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "614b8a2e53a4277f1c69de2f9fdc09583d8835fb81b6927c4664bb0dde7dba25",
                expected_output: "31e8a776138f719c359dd2826cfa30f7990b24dabb7145699f9c4eb4a296a86d",
            },
            46 => Case {
                name: "short64-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 64,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "f56e013a852404580994d8d4d6cb5bde2c03c304fb907f5f3aa6fd1a3dfeb82d",
                expected_output: "e674312386cc3c1d350cc36a41f19860e6346728e00c8077459297f7ba74756c",
            },
            47 => Case {
                name: "short66-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 66,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "7318fc09ce3281375e2c1aaadb432ae8372809f61521d661d24b6d0e5ad7da6b",
                expected_output: "8675730d9129c5e53253de83b6788a2aaaeeb6286dcc971d0dc9e04a8697d2f6",
            },
            48 => Case {
                name: "cancel-pair-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "cancel-pair",
                impulse: 0,
                expected_input: "cb1a09c6f665d9f11e56ca55aa9f1a256d2ea46853986b6da557acbf602582fa",
                expected_output: "7ffb21772afdf16b75c7e774fcef924f07dc104279aa2cc4f3b55ffda3d3a7bb",
            },
            49 => Case {
                name: "left-impulse-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "left-impulse",
                impulse: 160,
                expected_input: "c9b0fa5b787f6cfa43d35bae8539edfe1e7b42db07a21b97ce8ce995056ff8a9",
                expected_output: "85994eab10524aa9aa188acb34c3f50cc2e0e088624fa585df6cba24c7150fbb",
            },
            50 => Case {
                name: "right-impulse-44100-2",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 321,
                },
                signal: "right-impulse",
                impulse: 160,
                expected_input: "3c61e7f964d97e70b99423edc807f01830881a4eec858e9f286effe56efb13a1",
                expected_output: "85994eab10524aa9aa188acb34c3f50cc2e0e088624fa585df6cba24c7150fbb",
            },
            51 => Case {
                name: "empty-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 0,
                },
                signal: "zero",
                impulse: 0,
                expected_input: "fbecbe9f19533b13ffbd6dbbfa96cfb372510f7be75259e7034ff7e43b95e2e4",
                expected_output: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            },
            52 => Case {
                name: "one-dc-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 1,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "0e8f3b8bfd6d9ed1e5ec7599635d0b095a9f74231b36fae1e7ea8ef652114f99",
                expected_output: "b6b3d0479dd7cdcb8e4eed25bddafc829fbcdd3c46b674dd0be58941ec5b826c",
            },
            53 => Case {
                name: "two-dc-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 2,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "9d0ee4a982adee7fab85dd06cce20fc87d7cdf5a839676cf8e726967cfb4bd59",
                expected_output: "f89cc12e4b9a88f55d59792aebf6f40323f9cc7e22c57cbd35e6ec29dfb697db",
            },
            54 => Case {
                name: "short31-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 31,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "58057a134dd0857d1f4c9def13c87085121c5ee31c182c171a109196185c3efc",
                expected_output: "9b55dc0801b723c9a5f18ff60511b9c62220bc0ef0067cf9316ef3455ec22bc3",
            },
            55 => Case {
                name: "lookahead32-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 32,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "343bf8068c4bd0c2f0bf8435379316e0dac6615c5e36edb8720151d39d05f50c",
                expected_output: "90955a8dc9078154f7b6bbae55ae6e90be4aa3e0aa740e8c3ddef97afeaebb2f",
            },
            56 => Case {
                name: "lookahead33-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 33,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "50fa261b8e2cb37f1573fcf55dfb21a86fd7e82b96f2873beff5ae354095f0cb",
                expected_output: "867bb843f656a444df7460054ad079c7f092bf16a4a5a709ba6c26f7b9fc4706",
            },
            57 => Case {
                name: "dc65-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 65,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "413f9c2331073f1162ba1dc2a19094c3d0f6fb329f95e954fcb8026682d9a4e7",
                expected_output: "258b5ac0debd9091d68dd059a007dae5ba550a16de5d5994fe273023a5d38605",
            },
            58 => Case {
                name: "dc321-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "2c6bb988aa940146894d6b0d314ca8529fbe40381b0c2374de6f35482485442d",
                expected_output: "7df61e2a17dd02af38ed9b5ea0fe6f70f25fd5122458d2b131ed3895f445556c",
            },
            59 => Case {
                name: "impulse-start-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 0,
                expected_input: "fae5421c79ae2eede66ea1654e120012f0616b18ac8b6b97b5e513df2b8483a9",
                expected_output: "b22213bd8e68e34c2ca916ecc4e838e67d148aec0b81cb57e749169b6369d66f",
            },
            60 => Case {
                name: "impulse32-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 32,
                expected_input: "dbb913897c8de122d029eec06c97d31f01e6f7f70c42cfb8205ebd3655c21575",
                expected_output: "f63b122ed4a50e95c3bb3b40c977e3ffed7aca29f18d6cc125af01aeeb611182",
            },
            61 => Case {
                name: "impulse-middle-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 160,
                expected_input: "90d8f3d8ed4a56f6deb34eccf744f44168bfea1e268420c1a2c1ee05852ce1f2",
                expected_output: "1b5b61d952800db5c34e524205dc501bc63f9b31107a11476252ce0901cd9ce1",
            },
            62 => Case {
                name: "impulse-end-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 320,
                expected_input: "289b01196a00bd31ef79d60d24dc655f6dd0e90a87d8cf5fb30e85428ac605e1",
                expected_output: "2654dc69ac706e2600eea043a2b51f035ecef69620ef5731515e5d9bf0b5624b",
            },
            63 => Case {
                name: "alternating-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "alternating",
                impulse: 0,
                expected_input: "9c73d6e033cdf9e1b189656d496befe81a23fa86e31d1bf8250b903e0e03fdaa",
                expected_output: "7001e8272d3fdd6accc5c60323d86978a3a1ecc95d90c32575eef992cd2920c8",
            },
            64 => Case {
                name: "step-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 321,
                },
                signal: "step",
                impulse: 0,
                expected_input: "90701661f4ad8a2a4ae577ff71256f3c0a632ba337ff7ecea1029f9379590d43",
                expected_output: "fc2464fd3bedeff8340a4856a1b298366f428f1fe126a0afd7651611f897341d",
            },
            65 => Case {
                name: "periodic1025-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 1025,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "d3b987b4dc890fef9a1fd7a4f3305e57a83cef5f228827853085ce83f96da604",
                expected_output: "33f38c8b47b0bec5f3ec5e45700d53adb741232794077edbbb74d2e238d70776",
            },
            66 => Case {
                name: "ringwrap4097-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 4097,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "fdee147063e2845d39e659d88a1f57820a8dfe750d959b332803d2be594f3cbf",
                expected_output: "b35534deb408cb37727ce75fcb1c5214fd35646c566275f2d3e817b46fb608cd",
            },
            67 => Case {
                name: "negative-zero-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 65,
                },
                signal: "negative-zero",
                impulse: 0,
                expected_input: "ec6a26476a5b0909fb92952eb0e388242bfa857cef87e1a629bc86e29456b773",
                expected_output: "6edd9f6f9cc92cded36e6c4a580933f9c9f1b90562b46903b806f21902a1a54f",
            },
            68 => Case {
                name: "phase146-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 146,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "997a8b021381a9db5d1c70cc8e13e74b6e3cc3d36d52bb91d8a16b22e0d78c8b",
                expected_output: "a631930a7c5f94e96118d61721f79fa96d5adbefe98df63032e082a9e31e7e84",
            },
            69 => Case {
                name: "phase147-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 147,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "40b64fe85065451f2647f95471e15d9348e5b21072cb33f61877b9b99e57e237",
                expected_output: "d88a536d7415a9b87b91ac092a2ebb34fb183941ce2f055d3cd9562e74b0247c",
            },
            70 => Case {
                name: "phase148-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 148,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "f7372289575aa620bfe616437a20e863681bcb3ef3679c3cd142da09c2815698",
                expected_output: "6fb0fbf3d5ad768c9126508919e3c1d2d3e80b9f3950f61b7cb4bab57b9489a1",
            },
            71 => Case {
                name: "boundary319-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 319,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "4855ff25d832285def638c0a3f7326ca2c2ed4e2fe893e79af344f66184b0591",
                expected_output: "9ca7ad560bc8a67f596b74da7a8ade4a8a46a837d84eab4a019203fdfacd5836",
            },
            72 => Case {
                name: "boundary320-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 320,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "3548079097ff220544ef47fa3d4f4230adbbff899036a1138ff3b6400c954270",
                expected_output: "8182e33276ff1ff60c57f6f34fce461615159d7588cf1c750cbd52bd376fb40c",
            },
            73 => Case {
                name: "short64-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 64,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "9f9513a4c0481f95d7b49368b0369b6b46d499092c8326c2021df51018494953",
                expected_output: "030af3592a85b35173a8321d0cc4e3d7cf08d2bc9e824482f860890aa5ba152b",
            },
            74 => Case {
                name: "short66-48000-1",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 66,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "134f427c4dab9eecb66e58dd9ba5368d10ae27dc74d14a31bd18cc4842f1daaa",
                expected_output: "fdfde2725a0d897e7ccec28f8fff0b3ccba399a433a5c8bd11bb5164eb7683c8",
            },
            75 => Case {
                name: "empty-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 0,
                },
                signal: "zero",
                impulse: 0,
                expected_input: "bc63fa0972654b53fbc72be2eb5699493adcdd07a3a447d8187fd0da86d7e4e5",
                expected_output: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            },
            76 => Case {
                name: "one-dc-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 1,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "e8933535662d795eb99f4638dba76a6e79d28b163e883e246844fee7c576e368",
                expected_output: "b6b3d0479dd7cdcb8e4eed25bddafc829fbcdd3c46b674dd0be58941ec5b826c",
            },
            77 => Case {
                name: "two-dc-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 2,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "f2031625e7c15a8822c3bac87c95133aa40fbba8d1c4885b28ead994c2ddc319",
                expected_output: "f89cc12e4b9a88f55d59792aebf6f40323f9cc7e22c57cbd35e6ec29dfb697db",
            },
            78 => Case {
                name: "short31-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 31,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "5c983267fa27128b898fe37b5b8d144117df075d2e9ae059b96b7689bad04ac6",
                expected_output: "3d1e851c6fffc9508dd86ade58e18374a529fa31502bb34c804bfa88bd2a44ce",
            },
            79 => Case {
                name: "lookahead32-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 32,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "7784634f297c9f41b874def063b2ba30699c11b3a4cc4a593877a164fde26357",
                expected_output: "a5b78f2cf6f4d413afba41b5066f315fbae8ea603ffe51cc22bfb17003fc0a59",
            },
            80 => Case {
                name: "lookahead33-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 33,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "5026d77a57dafc9e5b8d50adb16fac3c738eb109b5047a1ede22042cf36b0cc7",
                expected_output: "b0b4cb44a036013c38e6e3b6f577cffba82eb7ef18b0131060174574df292bf0",
            },
            81 => Case {
                name: "dc65-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 65,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "18021aec9bb5d52ea6f5c94a9c3c1f8ba2b6c893787a70d3d8f90dcd1902049d",
                expected_output: "258b5ac0debd9091d68dd059a007dae5ba550a16de5d5994fe273023a5d38605",
            },
            82 => Case {
                name: "dc321-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "dc",
                impulse: 0,
                expected_input: "37f9942167247f151324fe2f3e7640b57586277bce908355e2d5e338496a8c4e",
                expected_output: "7df61e2a17dd02af38ed9b5ea0fe6f70f25fd5122458d2b131ed3895f445556c",
            },
            83 => Case {
                name: "impulse-start-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 0,
                expected_input: "0a59eb8c75c71418eacfdc39960cbed91bad931e63acc3cc53ddccd3fdf6e697",
                expected_output: "b22213bd8e68e34c2ca916ecc4e838e67d148aec0b81cb57e749169b6369d66f",
            },
            84 => Case {
                name: "impulse32-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 32,
                expected_input: "aadd742b9118f569ab4e0e3c9a426a995ce5f79e4ffbad48b5462293211aa2f0",
                expected_output: "f63b122ed4a50e95c3bb3b40c977e3ffed7aca29f18d6cc125af01aeeb611182",
            },
            85 => Case {
                name: "impulse-middle-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 160,
                expected_input: "7d5ce731e71fa0f7ad26e4c4100ae9331c9eb1a0dbfb77738ba095116494288a",
                expected_output: "1b5b61d952800db5c34e524205dc501bc63f9b31107a11476252ce0901cd9ce1",
            },
            86 => Case {
                name: "impulse-end-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "impulse",
                impulse: 320,
                expected_input: "9605ed86322b1cbb349f804d1e891d228c7270c6c4715885e4b4b1babf3e865b",
                expected_output: "2654dc69ac706e2600eea043a2b51f035ecef69620ef5731515e5d9bf0b5624b",
            },
            87 => Case {
                name: "alternating-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "alternating",
                impulse: 0,
                expected_input: "578f4c12cd422bc5b8c87eecc4b2af749bf42d036c1efb06cad607aba9cbaddf",
                expected_output: "7001e8272d3fdd6accc5c60323d86978a3a1ecc95d90c32575eef992cd2920c8",
            },
            88 => Case {
                name: "step-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "step",
                impulse: 0,
                expected_input: "6b651378bbef042cb0eef030557de38e1032fe06dea927eb1a4518ee027e85ed",
                expected_output: "fc2464fd3bedeff8340a4856a1b298366f428f1fe126a0afd7651611f897341d",
            },
            89 => Case {
                name: "periodic1025-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 1025,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "520e674655b8908857f7eb004db77b5d52f428136e3d2bba8774e7a3f137af55",
                expected_output: "1f035b9dd477dc72f03404840f939618af4025abcd799d31f6e75531e9b2cb93",
            },
            90 => Case {
                name: "ringwrap4097-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 4097,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "d5993413e0d23b2917f52ce3d40a61c54e903c332e86d225b298e28f8b98c664",
                expected_output: "0be898b359e79940837df5be99fc2ca57b7ae961519f92f020a8be942d0d326a",
            },
            91 => Case {
                name: "negative-zero-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 65,
                },
                signal: "negative-zero",
                impulse: 0,
                expected_input: "3494d3af9c59b728b92a08d239b289f336cfc6864adc0f6b74c6b2438a9143f6",
                expected_output: "6edd9f6f9cc92cded36e6c4a580933f9c9f1b90562b46903b806f21902a1a54f",
            },
            92 => Case {
                name: "phase146-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 146,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "9c8a055d487e61f26d35dda72ca599c2eb5c00b6f6366834b6763464382e4c5f",
                expected_output: "9e9d3a06a5f7154865256d0a20fde76dceb923181c3699ac3af5e0e4ad37869e",
            },
            93 => Case {
                name: "phase147-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 147,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "1d3955f08aeb38274b5b85ac2f301ae180d1e3a0f18a8a716aeebaea74812370",
                expected_output: "8d5e21d8059f69310ca468d1d485bb7b322d1b4959f9e6f6dd8c1a3dd4593b00",
            },
            94 => Case {
                name: "phase148-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 148,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "b468dbf3f9b1b952a642245bbed447390b8fac1f71baa26f5750dd904ca212ce",
                expected_output: "1ed003edc5e1d89a8d5d08432b4da39e274647c22eb4432d0fcedea881b9d916",
            },
            95 => Case {
                name: "boundary319-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 319,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "1d8694bd2171cdc854a0552152464abea28677d6b574b69aa794422849f150e8",
                expected_output: "526e12daa8b14944189834f98f229c45c94639d2076ebe2e601f01b1106f13db",
            },
            96 => Case {
                name: "boundary320-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 320,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "ab0454a09dfb0cf18bf23cc4750355d788a71166c23ad6a98efdb1b49c4c4fc5",
                expected_output: "65ef5e1d0a9a82aa539015e791add5687b1154b5e0497bc0eb89339d10d0414d",
            },
            97 => Case {
                name: "short64-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 64,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "b20aa3c86ddec1ab70b6db6b83ffe455607e17ac04ca74fd93e97104ef5d3484",
                expected_output: "be10e4966b245523ebf6430f2c5826a129f30bcafa1e5d1073b0869662dd9277",
            },
            98 => Case {
                name: "short66-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 66,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "22734b22f0b529fd718fe691573bbd6201d06cbdea6ee97fb657b820627d79d2",
                expected_output: "355a5a82d0c96c6935b877b0f05214917e21a04a9109dc678fcd232c41808d02",
            },
            99 => Case {
                name: "cancel-pair-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "cancel-pair",
                impulse: 0,
                expected_input: "af59d9b43b1ca4331752e7e5d4878e91ccdc4025244c29367460499a8360a671",
                expected_output: "2b73f6b3cd6cd9f97ccbbedc2fa1904331b916b2ace865882f72ddd083c79b04",
            },
            100 => Case {
                name: "left-impulse-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "left-impulse",
                impulse: 160,
                expected_input: "88b476479edd426c7bbdb3710dc3c95aba538cc0a4d23f734563aa8247ffbaaa",
                expected_output: "c6f252945783a2590000899fa52f286b08fe6f59832c6c564ed4dbb9929f9bd6",
            },
            101 => Case {
                name: "right-impulse-48000-2",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 321,
                },
                signal: "right-impulse",
                impulse: 160,
                expected_input: "9c3da7c42bd453fb9318f73b6cff998c6d0efb54552724eca81ab416bd35a381",
                expected_output: "c6f252945783a2590000899fa52f286b08fe6f59832c6c564ed4dbb9929f9bd6",
            },
            _ => panic!("fixed fixture index"),
        }
    }
    pub fn full(name: &str) -> Option<Case> {
        match name {
            "full44100-mono" => Some(Case {
                name: "full44100-mono",
                config: Config {
                    source_rate: 44100,
                    channels: 1,
                    frames: 26460000,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "73dc1476731e9b814bdbec142814bc8ff5127d749426fce5b052e2e2632d28aa",
                expected_output: "e3b07d8b5794c8dd38781a4d7a3d59364897dcb2b494784811dc2efffca8cafe",
            }),
            "full44100-stereo" => Some(Case {
                name: "full44100-stereo",
                config: Config {
                    source_rate: 44100,
                    channels: 2,
                    frames: 26460000,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "61b9b2802000dfd35f91cb2890e1a7c7eee20345bec958f3d7ad5fe41336413a",
                expected_output: "ffc7dcaef7d90009f1a14a62be05155a26cb1e7f9e84bebf8a297a0259e0de75",
            }),
            "full48000-mono" => Some(Case {
                name: "full48000-mono",
                config: Config {
                    source_rate: 48000,
                    channels: 1,
                    frames: 28800000,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "6e0c172eb3090fd6b946da6864a87fb3a2d46b5b75d729a70f6c4dc30c11118f",
                expected_output: "2b2cd0fae09548fd34b3bafcba8e4a0487cf9ceb9b0052ad805344d13f141969",
            }),
            "full48000-stereo" => Some(Case {
                name: "full48000-stereo",
                config: Config {
                    source_rate: 48000,
                    channels: 2,
                    frames: 28800000,
                },
                signal: "periodic",
                impulse: 0,
                expected_input: "7f87fc2f505eb85365284a1c8b754c25312068742cfd0e0cb7f1912f086cca9c",
                expected_output: "eb57635ee154a825831e6f4954806291f178b5604788db3ba7677150b1f09871",
            }),
            _ => None,
        }
    }
}
