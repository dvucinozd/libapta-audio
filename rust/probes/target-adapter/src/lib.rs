// SPDX-License-Identifier: Apache-2.0
//! Cooperative diagnostic adapter and bounded telemetry producer. No device acceptance.
#![no_std]
#![forbid(unsafe_code)]

use core::fmt::{self, Write};
use core::sync::atomic::{AtomicBool, Ordering};
use libapta_resource_probe_core as probe;

pub const MAX_STEP_UNITS: usize = 32;
pub const PHASES: usize = 12;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Input,
    ZeroBudget,
    Quota,
    Clock,
    Timeout,
    Core,
    Capacity,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Running,
    Complete,
    Cancelled,
    Failed,
}
#[derive(Clone, Copy, Debug)]
pub struct Step {
    pub units: usize,
    pub status: Status,
    pub error: Option<Error>,
}
#[derive(Clone, Copy, Debug)]
pub struct Stats {
    pub start: u64,
    pub end: u64,
    pub hz: u64,
    pub phase_ticks: [u64; PHASES],
    pub core_units: u64,
}
pub trait Clock {
    fn hz(&self) -> u64;
    fn ticks(&mut self) -> u64;
}

mod sealed {
    pub trait Sealed {}
    impl Sealed for libapta_resource_probe_core::CopyProbe<'_> {}
    impl<T: libapta_resource_probe_core::Scalar> Sealed
        for libapta_resource_probe_core::Projection<'_, T>
    {
    }
    impl Sealed for libapta_resource_probe_core::NonlinearProbe<'_> {}
}
pub trait Job: sealed::Sealed {
    fn status(&self) -> probe::Status;
    fn phase(&self) -> probe::Phase;
    fn progress(&self) -> probe::Progress;
    fn cancel(&mut self);
    fn step(&mut self, quota: usize, flag: &AtomicBool) -> probe::Report;
    fn phase_units_left(&self) -> usize;
}
macro_rules! job_methods {
    () => {
        fn status(&self) -> probe::Status {
            self.status()
        }
        fn phase(&self) -> probe::Phase {
            self.phase()
        }
        fn progress(&self) -> probe::Progress {
            self.progress()
        }
        fn cancel(&mut self) {
            self.cancel();
        }
        fn step(&mut self, n: usize, f: &AtomicBool) -> probe::Report {
            self.step(n, f)
        }
    };
}
impl Job for probe::CopyProbe<'_> {
    job_methods!();
    fn phase_units_left(&self) -> usize {
        if self.phase() == probe::Phase::Finalize {
            1
        } else {
            probe::COPY_WORDS - self.progress().units as usize % probe::COPY_WORDS
        }
    }
}
impl<T: probe::Scalar> Job for probe::Projection<'_, T> {
    job_methods!();
    fn phase_units_left(&self) -> usize {
        let units = self.progress().units as usize;
        match self.phase() {
            probe::Phase::ValidateInput => probe::INPUTS - units,
            probe::Phase::ValidateWeights => probe::INPUTS + probe::WEIGHTS - units,
            probe::Phase::Dot => {
                150 - (units - probe::INPUTS - probe::WEIGHTS - self.progress().outputs * 151)
            }
            _ => 1,
        }
    }
}
impl Job for probe::NonlinearProbe<'_> {
    job_methods!();
    fn phase_units_left(&self) -> usize {
        1
    }
}
fn phase_slot(p: probe::Phase) -> usize {
    match p {
        probe::Phase::Initialize => 0,
        probe::Phase::Prepare => 1,
        probe::Phase::Copy => 2,
        probe::Phase::Verify => 3,
        probe::Phase::ValidateInput => 4,
        probe::Phase::ValidateWeights => 5,
        probe::Phase::Dot => 6,
        probe::Phase::Evaluate => 7,
        probe::Phase::Commit => 8,
        probe::Phase::Finalize => 9,
        probe::Phase::Terminal => 10,
    }
}
pub struct Trial<'a, J: Job, C: Clock> {
    job: J,
    clock: &'a mut C,
    limit: u64,
    stats: Stats,
    status: Status,
    error: Option<Error>,
}
impl<'a, J: Job, C: Clock> Trial<'a, J, C> {
    pub fn new(job: J, clock: &'a mut C, limit: u64) -> Result<Self, Error> {
        let hz = clock.hz();
        if job.status() != probe::Status::Running
            || job.progress().units != 0
            || !(1000..=1000000000).contains(&hz)
            || limit == 0
            || limit > hz * 10
        {
            return Err(Error::Input);
        }
        let start = clock.ticks();
        Ok(Self {
            job,
            clock,
            limit,
            stats: Stats {
                start,
                end: start,
                hz,
                phase_ticks: [0; PHASES],
                core_units: 0,
            },
            status: Status::Running,
            error: None,
        })
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn stats(&self) -> Stats {
        self.stats
    }
    /// A numerically-ready core stays hidden if adapter timing/clock failed.
    pub fn complete_job(&self) -> Option<&J> {
        (self.status == Status::Complete).then_some(&self.job)
    }
    fn fail(&mut self, error: Error) {
        self.job.cancel();
        self.status = Status::Failed;
        self.error = Some(error);
    }
    fn report(&self, units: usize, error: Option<Error>) -> Step {
        Step {
            units,
            status: self.status,
            error: error.or(self.error),
        }
    }
    pub fn step(&mut self, quota: usize, flag: &AtomicBool) -> Step {
        if quota == 0 {
            return self.report(0, Some(Error::ZeroBudget));
        }
        if self.status != Status::Running {
            return self.report(0, None);
        }
        if quota > MAX_STEP_UNITS {
            return self.report(0, Some(Error::Quota));
        }
        if flag.load(Ordering::Acquire) {
            self.job.cancel();
            self.status = Status::Cancelled;
            return self.report(0, None);
        }
        let start = self.clock.ticks();
        if start < self.stats.end {
            self.fail(Error::Clock);
            return self.report(0, None);
        }
        self.stats.end = start;
        if start - self.stats.start >= self.limit {
            self.fail(Error::Timeout);
            return self.report(0, None);
        }
        let phase = self.job.phase();
        let budget = quota.min(self.job.phase_units_left());
        let report = self.job.step(budget, flag);
        let end = self.clock.ticks();
        self.stats.core_units = self.job.progress().units;
        if end < start {
            self.fail(Error::Clock);
            return self.report(report.units, None);
        }
        self.stats.end = end;
        if end - self.stats.start > self.limit {
            self.fail(Error::Timeout);
            return self.report(report.units, None);
        }
        self.stats.phase_ticks[phase_slot(phase)] += end - start;
        match report.status {
            probe::Status::Running => (),
            probe::Status::Ready => self.status = Status::Complete,
            probe::Status::Cancelled => self.status = Status::Cancelled,
            probe::Status::Failed => self.fail(Error::Core),
        }
        self.report(report.units, None)
    }
}

#[cfg(all(feature = "esp32p4", target_arch = "riscv32"))]
pub struct EspClock;
#[cfg(all(feature = "esp32p4", target_arch = "riscv32"))]
impl Clock for EspClock {
    fn hz(&self) -> u64 {
        1000000
    }
    fn ticks(&mut self) -> u64 {
        esp_hal::time::Instant::now()
            .duration_since_epoch()
            .as_micros()
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum EvidenceKind {
    Synthetic,
    DeviceLog,
}
pub struct Identity<'a> {
    pub run_id: &'a str,
    pub unit_id: &'a str,
    pub image_id: &'a str,
    pub elf_sha256: &'a str,
    pub source_revision: &'a str,
    pub protocol_sha256: &'a str,
    pub chip_revision: u16,
    pub kind: EvidenceKind,
}
fn token(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 80
        && s.bytes()
            .all(|b| b.is_ascii_alphanumeric() || b"_.:-".contains(&b))
}
fn hex(s: &str, len: usize) -> bool {
    s.len() == len
        && s.bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}
impl Identity<'_> {
    pub fn validate(&self) -> Result<(), Error> {
        if ![self.run_id, self.unit_id, self.image_id]
            .into_iter()
            .all(token)
            || !hex(self.elf_sha256, 64)
            || !hex(self.source_revision, 40)
            || !hex(self.protocol_sha256, 64)
            || self.chip_revision < 302
        {
            return Err(Error::Input);
        }
        Ok(())
    }
}
pub enum ObservationStatus {
    Measured,
    Failed,
    Cancelled,
    NotSupported,
}
pub struct Observation<'a> {
    pub id: &'a str,
    pub case: &'a str,
    pub variant: &'a str,
    pub phase: &'a str,
    pub trial: u16,
    pub sequence: u16,
    pub status: ObservationStatus,
    pub start: u64,
    pub end: u64,
    pub elapsed: u64,
    pub hz: u64,
    pub result_sha256: Option<&'a str>,
}
struct Sink<'a> {
    out: &'a mut [u8],
    len: usize,
}
impl Write for Sink<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let end = self.len.checked_add(s.len()).ok_or(fmt::Error)?;
        let target = self.out.get_mut(self.len..end).ok_or(fmt::Error)?;
        target.copy_from_slice(s.as_bytes());
        self.len = end;
        Ok(())
    }
}
fn identity(w: &mut Sink<'_>, id: &Identity<'_>) -> fmt::Result {
    write!(w,"\"identity\":{{\"run_id\":\"{}\",\"unit_id\":\"{}\",\"board\":\"JC1060P470C_I_W_Y\",\"image_id\":\"{}\",\"elf_sha256\":\"{}\",\"source_revision\":\"{}\",\"protocol_sha256\":\"{}\",\"chip_revision\":{},\"evidence_kind\":\"{}\"}}",id.run_id,id.unit_id,id.image_id,id.elf_sha256,id.source_revision,id.protocol_sha256,id.chip_revision,if id.kind==EvidenceKind::Synthetic {"synthetic"} else {"device_log"})
}
/// Successful length is the publication fence; failed output prefixes are uncertified.
pub fn encode_observation(
    id: &Identity<'_>,
    obs: &Observation<'_>,
    output: &mut [u8],
) -> Result<usize, Error> {
    id.validate()?;
    if ![obs.id, obs.variant, obs.phase].into_iter().all(token)
        || ![
            "TP-00", "TP-01", "TP-02", "TP-03", "TP-04", "TP-05", "TP-06",
        ]
        .contains(&obs.case)
        || obs.trial > 1024
        || obs.sequence >= 1024
        || !(1000..=1000000000).contains(&obs.hz)
        || obs.start.checked_add(obs.elapsed) != Some(obs.end)
        || obs.result_sha256.is_some_and(|s| !hex(s, 64))
    {
        return Err(Error::Input);
    }
    let mut w = Sink {
        out: output,
        len: 0,
    };
    let result = (|| -> fmt::Result {
        write!(
            w,
            "M1_RESOURCE_PROBE_V1 {{\"schema\":1,\"kind\":\"observation\",\"sequence\":{},",
            obs.sequence
        )?;
        identity(&mut w, id)?;
        write!(w,",\"observation_id\":\"{}\",\"case\":\"{}\",\"variant\":\"{}\",\"trial\":{},\"phase\":\"{}\",\"status\":\"{}\",\"ticks_start\":{},\"ticks_end\":{},\"elapsed_ticks\":{},\"clock_hz\":{},\"memory\":{{",obs.id,obs.case,obs.variant,obs.trial,obs.phase,match obs.status {ObservationStatus::Measured=>"MEASURED",ObservationStatus::Failed=>"FAILED",ObservationStatus::Cancelled=>"CANCELLED",ObservationStatus::NotSupported=>"NOT_SUPPORTED"},obs.start,obs.end,obs.elapsed,obs.hz)?;
        for (i, name) in ["internal", "psram", "dma"].into_iter().enumerate() {
            write!(w,"{}\"{}\":{{\"capacity_bytes\":null,\"used_bytes\":null,\"free_bytes\":null,\"minimum_free_bytes\":null,\"largest_free_block_bytes\":null}}",if i==0 {""} else {","},name)?;
        }
        write!(w,"}},\"stack\":{{\"capacity_bytes\":null,\"high_water_used_bytes\":null}},\"counters\":{{\"audio_underruns\":null,\"usb_errors\":null}},\"result_sha256\":")?;
        match obs.result_sha256 {
            Some(s) => write!(w, "\"{}\"", s)?,
            None => w.write_str("null")?,
        };
        w.write_str("}\n")
    })();
    result.map_err(|_| Error::Capacity)?;
    if w.len > 8192 {
        return Err(Error::Capacity);
    }
    Ok(w.len)
}
pub fn encode_end(id: &Identity<'_>, count: u16, output: &mut [u8]) -> Result<usize, Error> {
    id.validate()?;
    if !(1..=1024).contains(&count) {
        return Err(Error::Input);
    }
    let mut w = Sink {
        out: output,
        len: 0,
    };
    write!(
        w,
        "M1_RESOURCE_PROBE_V1 {{\"schema\":1,\"kind\":\"end\",\"sequence\":{},",
        count
    )
    .map_err(|_| Error::Capacity)?;
    identity(&mut w, id).map_err(|_| Error::Capacity)?;
    writeln!(w, ",\"observations\":{}}}", count).map_err(|_| Error::Capacity)?;
    if w.len > 8192 {
        return Err(Error::Capacity);
    }
    Ok(w.len)
}
