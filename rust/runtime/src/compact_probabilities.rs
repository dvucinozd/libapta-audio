// SPDX-License-Identifier: Apache-2.0
//! HS06 named probability bridge. Caller storage, no allocation or publication in Session.
use std::sync::atomic::{AtomicBool, Ordering};

pub const MAX_ROWS: usize = 30000;
pub const MAX_UNITS: u64 = 150001;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    UnsupportedProfile,
    Capacity,
    SourceOffset,
    SourceLength,
    InvalidProbability,
    TruncatedSource,
    NumericalFailure,
    ArithmeticOverflow,
    ZeroBudget,
    InvalidScore,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Geometry {
    pub sample_rate: u32,
    pub total_frames: u64,
}
impl Geometry {
    pub fn rows(self) -> Result<usize, Error> {
        if ![44100, 48000].contains(&self.sample_rate)
            || self.total_frames > 600 * u64::from(self.sample_rate)
        {
            return Err(Error::UnsupportedProfile);
        }
        let n = self
            .total_frames
            .checked_mul(50)
            .ok_or(Error::ArithmeticOverflow)?;
        usize::try_from(n.div_ceil(u64::from(self.sample_rate)))
            .map_err(|_| Error::ArithmeticOverflow)
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Support {
    pub first_row: u32,
    pub end_row: u32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Running,
    Ready,
    NoEvidence,
    Cancelled,
    Failed,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    AcceptRow,
    NonBeatLog,
    BeatLog,
    DownbeatLog,
    CommitRow,
    Finalize,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flow {
    Budget,
    NeedInput,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Progress {
    pub accepted_rows: u64,
    pub committed_rows: u64,
    pub units: u64,
    pub cancellation_phase: Option<Phase>,
}
#[derive(Clone, Copy)]
pub struct InputBlock<'a> {
    pub first_row: u64,
    pub rows: &'a [[f32; 3]],
}
#[derive(Clone, Copy, Debug)]
pub struct StepReport {
    pub consumed_rows: usize,
    pub committed_rows: usize,
    pub first_committed_row: u64,
    pub units: usize,
    pub flow: Flow,
    pub status: Status,
    pub error: Option<Error>,
}
pub struct PreparedView<'a> {
    pub geometry: Geometry,
    pub probabilities: &'a [[f32; 3]],
    pub densities: &'a [[f64; 3]],
    pub support: Option<Support>,
}
impl PreparedView<'_> {
    pub fn decoder_densities(&self) -> Option<&[[f64; 3]]> {
        self.support
            .map(|s| &self.densities[s.first_row as usize..s.end_row as usize])
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MeterChoice {
    Three,
    Four,
    NoPath,
}
pub fn choose_meter(score3: f64, score4: f64) -> Result<MeterChoice, Error> {
    if [score3, score4]
        .iter()
        .any(|&s| s.is_nan() || s == f64::INFINITY || s > 0.)
    {
        return Err(Error::InvalidScore);
    }
    Ok(
        if score3 == f64::NEG_INFINITY && score4 == f64::NEG_INFINITY {
            MeterChoice::NoPath
        } else if score4 > score3 {
            MeterChoice::Four
        } else {
            MeterChoice::Three
        },
    )
}
/// The result view pins the Session and both caller buffers.
/// ```compile_fail
/// use libapta_runtime::compact_probabilities::*;
/// let mut p = [[0.;3];1]; let mut d = [[0.;3];1];
/// let view;
/// { let s = Session::new(Geometry{sample_rate:44100,total_frames:0}, &mut p, &mut d).unwrap();
///   view = s.result().unwrap(); }
/// println!("{}", view.probabilities.len());
/// ```
/// ```compile_fail
/// use libapta_runtime::compact_probabilities::*;
/// let mut p = [[0.;3];1]; let mut d = [[0.;3];1];
/// let mut s = Session::new(Geometry{sample_rate:44100,total_frames:0}, &mut p, &mut d).unwrap();
/// let view = s.result().unwrap(); s.cancel();
/// println!("{}", view.densities.len());
/// ```
pub struct Session<'a> {
    geometry: Geometry,
    rows: usize,
    probabilities: &'a mut [[f32; 3]],
    densities: &'a mut [[f64; 3]],
    pending_p: [f32; 3],
    pending_d: [f64; 3],
    support: Option<Support>,
    progress: Progress,
    phase: Phase,
    status: Status,
    error: Option<Error>,
}
impl<'a> Session<'a> {
    pub fn new(
        geometry: Geometry,
        probabilities: &'a mut [[f32; 3]],
        densities: &'a mut [[f64; 3]],
    ) -> Result<Self, Error> {
        let rows = geometry.rows()?;
        if probabilities.len() < rows || densities.len() < rows {
            return Err(Error::Capacity);
        }
        Ok(Self {
            geometry,
            rows,
            probabilities,
            densities,
            pending_p: [0.; 3],
            pending_d: [0.; 3],
            support: None,
            progress: Progress {
                accepted_rows: 0,
                committed_rows: 0,
                units: 0,
                cancellation_phase: None,
            },
            phase: if rows == 0 {
                Phase::Terminal
            } else {
                Phase::AcceptRow
            },
            status: if rows == 0 {
                Status::NoEvidence
            } else {
                Status::Running
            },
            error: None,
        })
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn phase(&self) -> Phase {
        self.phase
    }
    pub fn progress(&self) -> Progress {
        self.progress
    }
    pub fn result(&self) -> Option<PreparedView<'_>> {
        matches!(self.status, Status::Ready | Status::NoEvidence).then(|| PreparedView {
            geometry: self.geometry,
            probabilities: &self.probabilities[..self.rows],
            densities: &self.densities[..self.rows],
            support: self.support,
        })
    }
    fn fail(&mut self, error: Error) {
        self.error = Some(error);
        self.status = Status::Failed;
        self.phase = Phase::Terminal;
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.progress.cancellation_phase = Some(self.phase);
            self.status = Status::Cancelled;
            self.phase = Phase::Terminal;
        }
    }
    pub fn finish_input(&mut self) {
        if self.status == Status::Running && self.progress.accepted_rows < self.rows as u64 {
            self.fail(Error::TruncatedSource);
        }
    }
    pub fn step(&mut self, quota: usize, input: InputBlock<'_>, cancel: &AtomicBool) -> StepReport {
        let before = self.progress;
        let mut flow = Flow::Budget;
        let mut report_error = None;
        if quota == 0 {
            report_error = Some(Error::ZeroBudget);
        } else if self.status != Status::Running {
            flow = Flow::Terminal;
        } else if cancel.load(Ordering::Acquire) {
            self.cancel();
            flow = Flow::Terminal;
        } else if input.first_row != self.progress.accepted_rows {
            self.fail(Error::SourceOffset);
        } else if input.rows.len() as u64 > self.rows as u64 - self.progress.accepted_rows {
            self.fail(Error::SourceLength);
        } else {
            while self.status == Status::Running
                && self.progress.units - before.units < quota as u64
            {
                if cancel.load(Ordering::Acquire) {
                    self.cancel();
                    break;
                }
                let consumed = (self.progress.accepted_rows - before.accepted_rows) as usize;
                if self.phase == Phase::AcceptRow && consumed == input.rows.len() {
                    flow = Flow::NeedInput;
                    break;
                }
                if self.progress.units == MAX_UNITS {
                    self.fail(Error::ArithmeticOverflow);
                    break;
                }
                self.progress.units += 1;
                if let Err(e) = self.tick(input.rows.get(consumed)) {
                    self.fail(e);
                }
            }
        }
        if self.status != Status::Running {
            flow = Flow::Terminal;
        }
        // Zero budget always wins, even on an already failed terminal session.
        if quota != 0 {
            report_error = self.error;
        }
        StepReport {
            consumed_rows: (self.progress.accepted_rows - before.accepted_rows) as usize,
            committed_rows: (self.progress.committed_rows - before.committed_rows) as usize,
            first_committed_row: before.committed_rows,
            units: (self.progress.units - before.units) as usize,
            flow,
            status: self.status,
            error: report_error,
        }
    }
    fn tick(&mut self, row: Option<&[f32; 3]>) -> Result<(), Error> {
        match self.phase {
            Phase::AcceptRow => {
                let p = *row.ok_or(Error::SourceLength)?;
                if p.iter().any(|x| !x.is_finite() || !(0.0..=1.0).contains(x))
                    || (((f64::from(p[0]) + f64::from(p[1])) + f64::from(p[2])) - 1.).abs()
                        > 1. / (1u32 << 21) as f64
                {
                    return Err(Error::InvalidProbability);
                }
                self.pending_p = p;
                self.progress.accepted_rows += 1;
                self.phase = Phase::NonBeatLog;
            }
            Phase::NonBeatLog | Phase::BeatLog | Phase::DownbeatLog => {
                let (slot, source, next) = match self.phase {
                    Phase::NonBeatLog => (0, 2, Phase::BeatLog),
                    Phase::BeatLog => (1, 0, Phase::DownbeatLog),
                    _ => (2, 1, Phase::CommitRow),
                };
                let p = f64::from(self.pending_p[source]);
                let v = if p == 0. {
                    f64::NEG_INFINITY
                } else if slot == 0 {
                    (p / 15.).ln()
                } else {
                    p.ln()
                };
                if v != f64::NEG_INFINITY && (!v.is_finite() || !(-1024.0..=0.).contains(&v)) {
                    return Err(Error::NumericalFailure);
                }
                self.pending_d[slot] = v;
                self.phase = next;
            }
            Phase::CommitRow => {
                let r = self.progress.committed_rows as usize;
                self.probabilities[r] = self.pending_p;
                self.densities[r] = self.pending_d;
                if f64::from(self.pending_p[0]) >= 0.05 || f64::from(self.pending_p[1]) >= 0.05 {
                    let s = self.support.get_or_insert(Support {
                        first_row: r as u32,
                        end_row: r as u32 + 1,
                    });
                    s.end_row = r as u32 + 1;
                }
                self.progress.committed_rows += 1;
                self.phase = if self.progress.committed_rows == self.rows as u64 {
                    Phase::Finalize
                } else {
                    Phase::AcceptRow
                };
            }
            Phase::Finalize => {
                self.status = if self.support.is_some() {
                    Status::Ready
                } else {
                    Status::NoEvidence
                };
                self.phase = Phase::Terminal;
            }
            Phase::Terminal => unreachable!("terminal fence"),
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unit_cap_refuses_before_log_or_storage_write() {
        let mut p = [[7.; 3]; 1];
        let mut d = [[8.; 3]; 1];
        let mut s = Session::new(
            Geometry {
                sample_rate: 44100,
                total_frames: 882,
            },
            &mut p,
            &mut d,
        )
        .unwrap();
        let flag = AtomicBool::new(false);
        s.step(
            1,
            InputBlock {
                first_row: 0,
                rows: &[[0., 1., 0.]],
            },
            &flag,
        );
        assert_eq!(s.phase, Phase::NonBeatLog);
        let pending = s.pending_d;
        s.progress.units = MAX_UNITS;
        let r = s.step(
            1,
            InputBlock {
                first_row: 1,
                rows: &[],
            },
            &flag,
        );
        assert_eq!(r.error, Some(Error::ArithmeticOverflow));
        assert_eq!(r.units, 0);
        assert_eq!(s.pending_d, pending);
        assert_eq!(s.progress.units, MAX_UNITS);
        assert_eq!(s.progress.committed_rows, 0);
        assert!(s.result().is_none());
        assert_eq!(p, [[7.; 3]; 1]);
        assert_eq!(d, [[8.; 3]; 1]);
    }
}

/// Fixed public input recipes and host composition. Outside the allocation-free Session.
pub mod synthetic {
    use super::*;
    use crate::{compact_grid as grid, compact_hmm as hmm};
    use sha2::{Digest, Sha256};
    use std::time::Instant;
    #[derive(Clone, Copy)]
    pub struct Case {
        pub name: &'static str,
        pub geometry: Geometry,
        pub rows: usize,
        pub composition: bool,
        raw: Option<&'static [u8]>,
    }
    impl Case {
        pub fn raw_is_fixture(self) -> bool {
            self.raw.is_some()
        }
        pub fn row(self, r: usize) -> [f32; 3] {
            assert!(r < self.rows);
            if let Some(raw) = self.raw {
                return std::array::from_fn(|j| {
                    f32::from_le_bytes(
                        raw[r * 12 + j * 4..r * 12 + j * 4 + 4]
                            .try_into()
                            .expect("f32 bytes"),
                    )
                });
            }
            if self.name == "full-no-support" {
                return [0.01, 0.01, 0.98];
            }
            let palette = [
                [0., 0., 1.],
                [1., 0., 0.],
                [0., 1., 0.],
                [1f32 / 3.; 3],
                [f32::from_bits(1), 0., 1.],
                [0., f32::from_bits(1), 1.],
                [0.5, 0.5, f32::from_bits(1)],
                [f32::from_bits(0x3d4ccccc), 0., 0.95],
                [0.05, 0., 0.95],
                [0.25, 0.25, f32::from_bits(0x3f000008)],
            ];
            palette[(r * 7 + 3) % 10]
        }
    }
    pub fn full(name: &str) -> Option<Case> {
        let name = match name {
            "full-mixed" => "full-mixed",
            "full-no-support" => "full-no-support",
            _ => return None,
        };
        Some(Case {
            name,
            geometry: Geometry {
                sample_rate: 48000,
                total_frames: 28800000,
            },
            rows: 30000,
            composition: false,
            raw: None,
        })
    }
    pub const FIXTURE_COUNT: usize = 35;
    pub fn fixture(index: usize) -> Case {
        match index {
            0 => Case{name:"empty",geometry:Geometry{sample_rate:44100,total_frames:0},rows:0,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/empty.f32le"))) },
            1 => Case{name:"nonbeat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/nonbeat.f32le"))) },
            2 => Case{name:"beat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/beat.f32le"))) },
            3 => Case{name:"downbeat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/downbeat.f32le"))) },
            4 => Case{name:"uniform",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/uniform.f32le"))) },
            5 => Case{name:"signed-zero",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/signed-zero.f32le"))) },
            6 => Case{name:"subnormal-beat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/subnormal-beat.f32le"))) },
            7 => Case{name:"subnormal-down",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/subnormal-down.f32le"))) },
            8 => Case{name:"subnormal-non",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/subnormal-non.f32le"))) },
            9 => Case{name:"threshold-below-beat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/threshold-below-beat.f32le"))) },
            10 => Case{name:"threshold-at-beat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/threshold-at-beat.f32le"))) },
            11 => Case{name:"threshold-above-beat",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/threshold-above-beat.f32le"))) },
            12 => Case{name:"threshold-below-down",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/threshold-below-down.f32le"))) },
            13 => Case{name:"threshold-at-down",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/threshold-at-down.f32le"))) },
            14 => Case{name:"threshold-above-down",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/threshold-above-down.f32le"))) },
            15 => Case{name:"support-row-zero",geometry:Geometry{sample_rate:44100,total_frames:2646},rows:3,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/support-row-zero.f32le"))) },
            16 => Case{name:"support-exclusive-tail",geometry:Geometry{sample_rate:44100,total_frames:2647},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/support-exclusive-tail.f32le"))) },
            17 => Case{name:"support-gap",geometry:Geometry{sample_rate:44100,total_frames:4410},rows:5,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/support-gap.f32le"))) },
            18 => Case{name:"sum-inside",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/sum-inside.f32le"))) },
            19 => Case{name:"sum-exact",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/sum-exact.f32le"))) },
            20 => Case{name:"sum-between-old-bounds",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/sum-between-old-bounds.f32le"))) },
            21 => Case{name:"all-zero",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/all-zero.f32le"))) },
            22 => Case{name:"nan-beat",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/nan-beat.f32le"))) },
            23 => Case{name:"inf-down",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/inf-down.f32le"))) },
            24 => Case{name:"negative-inf-non",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/negative-inf-non.f32le"))) },
            25 => Case{name:"negative-beat",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/negative-beat.f32le"))) },
            26 => Case{name:"above-one-down",geometry:Geometry{sample_rate:44100,total_frames:3528},rows:4,composition:false,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/above-one-down.f32le"))) },
            27 => Case{name:"composition-3-0",geometry:Geometry{sample_rate:44100,total_frames:154350},rows:175,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-3-0.f32le"))) },
            28 => Case{name:"composition-3-2",geometry:Geometry{sample_rate:44100,total_frames:156114},rows:177,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-3-2.f32le"))) },
            29 => Case{name:"composition-4-0",geometry:Geometry{sample_rate:48000,total_frames:216000},rows:225,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-4-0.f32le"))) },
            30 => Case{name:"composition-4-2",geometry:Geometry{sample_rate:48000,total_frames:217920},rows:227,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-4-2.f32le"))) },
            31 => Case{name:"composition-no-path",geometry:Geometry{sample_rate:44100,total_frames:7056},rows:8,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-no-path.f32le"))) },
            32 => Case{name:"composition-insufficient",geometry:Geometry{sample_rate:44100,total_frames:882},rows:1,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-insufficient.f32le"))) },
            33 => Case{name:"composition-no-support",geometry:Geometry{sample_rate:44100,total_frames:7938},rows:9,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-no-support.f32le"))) },
            34 => Case{name:"composition-empty",geometry:Geometry{sample_rate:44100,total_frames:0},rows:0,composition:true,raw:Some(include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"),"/../../docs/rust/fixtures/product-host-probability-hs06/assets/composition-empty.f32le"))) },
            _=>panic!("fixed fixture index"),
        }
    }
    pub struct Driver {
        pub case: Case,
        input: [[f32; 3]; 64],
        pub step_seconds: f64,
        pub max_step_seconds: f64,
        pub input_hash: Sha256,
    }
    impl Driver {
        pub fn new(case: Case) -> Self {
            let mut h = Sha256::new();
            h.update((case.rows as u64).to_le_bytes());
            Self {
                case,
                input: [[0.; 3]; 64],
                step_seconds: 0.,
                max_step_seconds: 0.,
                input_hash: h,
            }
        }
        pub fn step(
            &mut self,
            s: &mut Session<'_>,
            cap: usize,
            quota: usize,
            flag: &AtomicBool,
        ) -> StepReport {
            assert!((1..=64).contains(&cap));
            let first = s.progress().accepted_rows as usize;
            let n = (self.case.rows - first).min(cap);
            for i in 0..n {
                self.input[i] = self.case.row(first + i);
            }
            let t = Instant::now();
            let report = s.step(
                quota,
                InputBlock {
                    first_row: first as u64,
                    rows: &self.input[..n],
                },
                flag,
            );
            let seconds = t.elapsed().as_secs_f64();
            self.step_seconds += seconds;
            self.max_step_seconds = self.max_step_seconds.max(seconds);
            for row in &self.input[..report.consumed_rows] {
                for v in row {
                    self.input_hash.update(v.to_le_bytes());
                }
            }
            report
        }
    }
    pub fn evidence_hash(view: &PreparedView<'_>) -> String {
        let mut h = Sha256::new();
        for row in view.probabilities {
            for v in row {
                h.update(v.to_le_bytes());
            }
        }
        for row in view.densities {
            for v in row {
                h.update(v.to_le_bytes());
            }
        }
        format!("{:x}", h.finalize())
    }
    pub struct Composition {
        pub status: &'static str,
        pub reason: Option<grid::Reason>,
        pub scores: [f64; 2],
        pub meter: Option<u8>,
        pub path_hash: Option<String>,
        pub events: Vec<grid::Event>,
        pub summary: Option<grid::Summary>,
        pub wire: Option<Vec<u8>>,
        pub maximum_workspace_payload: usize,
        pub maximum_graph_payload: usize,
        pub event_scratch_payload: usize,
    }
    pub fn compose(
        view: &PreparedView<'_>,
        quota: usize,
    ) -> Result<Composition, Box<dyn std::error::Error>> {
        let mut c = Composition {
            status: "NoEvidence",
            reason: None,
            scores: [f64::NEG_INFINITY; 2],
            meter: None,
            path_hash: None,
            events: Vec::new(),
            summary: None,
            wire: None,
            maximum_workspace_payload: 0,
            maximum_graph_payload: 0,
            event_scratch_payload: 0,
        };
        let Some(densities) = view.decoder_densities() else {
            return Ok(c);
        };
        let before = evidence_hash(view);
        let flag = AtomicBool::new(false);
        for meter in [3, 4] {
            let g = hmm::synthetic::load(meter)?;
            // The loader exposes slices with exact vector capacities established by frozen code.
            let graph = g.graph();
            let payload = graph.pointers.len() * 4
                + graph.previous.len() * 2
                + graph.log_probabilities.len() * 8
                + graph.classes.len();
            c.maximum_graph_payload = c.maximum_graph_payload.max(payload);
            let mut w = hmm::Workspace::allocate(hmm::StoragePlan::new(
                graph.classes.len(),
                densities.len(),
                256,
                hmm::Mode::Score,
            )?)?;
            c.maximum_workspace_payload =
                c.maximum_workspace_payload.max(w.allocated_payload_bytes());
            let mut d = hmm::Decoder::new(graph, densities, 256, hmm::Mode::Score, &mut w)?;
            while d.status() == hmm::Status::Running {
                d.step(quota, &flag)?;
            }
            c.scores[(meter - 3) as usize] = match d.status() {
                hmm::Status::Scored => d.result().ok_or("missing score")?.score,
                hmm::Status::NoPath => f64::NEG_INFINITY,
                _ => return Err("HMM score failed".into()),
            };
            if evidence_hash(view) != before {
                return Err("Score changed evidence".into());
            }
        }
        let meter = match choose_meter(c.scores[0], c.scores[1])? {
            MeterChoice::Three => 3,
            MeterChoice::Four => 4,
            MeterChoice::NoPath => {
                c.status = "NoPath";
                return Ok(c);
            }
        };
        c.meter = Some(meter);
        let g = hmm::synthetic::load(meter)?;
        let graph = g.graph();
        let mut w = hmm::Workspace::allocate(hmm::StoragePlan::new(
            graph.classes.len(),
            densities.len(),
            256,
            hmm::Mode::Trace,
        )?)?;
        c.maximum_workspace_payload = c.maximum_workspace_payload.max(w.allocated_payload_bytes());
        let mut d = hmm::Decoder::new(graph, densities, 256, hmm::Mode::Trace, &mut w)?;
        while d.status() == hmm::Status::Running {
            d.step(quota, &flag)?;
        }
        let trace = d.result().ok_or("missing trace")?;
        if trace.score.to_bits() != c.scores[(meter - 3) as usize].to_bits() {
            return Err("Score/trace mismatch".into());
        }
        if evidence_hash(view) != before {
            return Err("Trace changed evidence".into());
        }
        let path = trace.path.ok_or("missing borrowed path")?;
        let mut h = Sha256::new();
        for s in path {
            h.update(s.to_le_bytes());
        }
        c.path_hash = Some(format!("{:x}", h.finalize()));
        let support = view.support.ok_or("missing support")?;
        let mut scratch = vec![grid::Event::default(); grid::MAX_EVENTS];
        c.event_scratch_payload = scratch.capacity() * std::mem::size_of::<grid::Event>();
        let mut s = grid::Session::from_path(
            grid::PathInput {
                geometry: grid::Geometry {
                    sample_rate: view.geometry.sample_rate,
                    total_frames: view.geometry.total_frames,
                },
                meter,
                probabilities: view.probabilities,
                decoded_first_row: support.first_row,
                decoded_end_row: support.end_row,
                states: path,
            },
            grid::Mode::Grid,
            &mut scratch,
        )?;
        while s.status() == grid::Status::Running {
            s.step(quota, &flag)?;
        }
        c.reason = s.reason();
        c.status = match s.status() {
            grid::Status::GridReady => "GridReady",
            grid::Status::UnsupportedGrid => "UnsupportedGrid",
            grid::Status::NoEvidence => "NoEvidence",
            _ => return Err("HS02 failed".into()),
        };
        if let Some(grid::ResultView::Grid { summary, events }) = s.result() {
            c.wire = Some(grid::synthetic::validate_wire(summary)?);
            c.summary = Some(summary);
            c.events = events.to_vec();
        }
        Ok(c)
    }
}
