// SPDX-License-Identifier: Apache-2.0
//! HS-02 opt-in host postprocessor. No model, I/O, allocator or publication in Session.
use std::sync::atomic::{AtomicBool, Ordering};

pub const MAX_EVENTS: usize = 3072;
pub const MAX_ROWS: usize = 30000;
pub const MAX_UNITS: u64 = 4194304;
const Q: i128 = 1 << 32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidInput,
    UnsupportedProfile,
    Capacity,
    ArithmeticOverflow,
    ZeroBudget,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}
type Checked = Result<i128, Error>;
fn add(a: i128, b: i128) -> Checked {
    a.checked_add(b).ok_or(Error::ArithmeticOverflow)
}
fn sub(a: i128, b: i128) -> Checked {
    a.checked_sub(b).ok_or(Error::ArithmeticOverflow)
}
fn mul(a: i128, b: i128) -> Checked {
    a.checked_mul(b).ok_or(Error::ArithmeticOverflow)
}
fn rhu(n: i128, d: i128) -> Checked {
    if d <= 0 {
        return Err(Error::ArithmeticOverflow);
    }
    Ok(add(mul(2, n)?, d)?.div_euclid(mul(2, d)?))
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Geometry {
    pub sample_rate: u32,
    pub total_frames: u64,
}
impl Geometry {
    fn rows(self, meter: u8) -> Result<usize, Error> {
        if ![44100, 48000].contains(&self.sample_rate)
            || ![3, 4].contains(&meter)
            || self.total_frames > 600 * u64::from(self.sample_rate)
        {
            return Err(Error::UnsupportedProfile);
        }
        if self.total_frames == 0 {
            return Err(Error::InvalidInput);
        }
        Ok((50 * self.total_frames).div_ceil(u64::from(self.sample_rate)) as usize)
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Event {
    pub global_row: u32,
    pub beat_number: u8,
    pub winning_class: u8,
    pub reserved: [u8; 2],
}
#[derive(Clone, Copy)]
pub struct PathInput<'a> {
    pub geometry: Geometry,
    pub meter: u8,
    pub probabilities: &'a [[f32; 3]],
    pub decoded_first_row: u32,
    pub decoded_end_row: u32,
    pub states: &'a [u16],
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    EventsOnly,
    Grid,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    ValidateProbabilitiesAndSupport,
    ValidatePath,
    ExtractEvents,
    ValidateEvents,
    AccumulateFit,
    ComputeModel,
    CheckCoverage,
    CheckResiduals,
    BuildSummary,
    Terminal,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Running,
    EventsReady,
    GridReady,
    NoEvidence,
    UnsupportedGrid,
    Cancelled,
    Failed,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reason {
    NoSupport,
    NoRegions,
    EmptyEvents,
    InsufficientEvents,
    PhaseDiscontinuity,
    TempoOutOfProfile,
    AnchorOutOfRange,
    IncompleteCoverage,
    ResidualExceeded,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Summary {
    pub geometry: Geometry,
    pub meter: u8,
    pub tempo_millibpm: u32,
    pub anchor_q32: u128,
    pub period_q32: u128,
    pub beat_count: u32,
    pub downbeat_ordinal: u32,
    pub downbeat_whole_frame: u64,
    pub evidence_frames: [u64; 2],
}
pub enum ResultView<'a> {
    Events(&'a [Event]),
    Grid {
        summary: Summary,
        events: &'a [Event],
    },
}
#[derive(Debug)]
pub struct StepReport {
    pub units: usize,
    pub status: Status,
}
enum Input<'a> {
    Path(PathInput<'a>),
    Events(&'a [Event]),
}
#[derive(Clone, Copy)]
struct GraphBytes {
    map: &'static [u8],
    pointers: &'static [u8],
    previous: &'static [u8],
}
macro_rules! asset {
    ($name:literal) => {
        include_bytes!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../docs/rust/fixtures/product-compact-cs02/assets/",
            $name
        ))
    };
}
fn graph(meter: u8) -> GraphBytes {
    if meter == 3 {
        GraphBytes {
            map: asset!("hmm3-state-map.u8"),
            pointers: asset!("hmm3-pointers.u32le"),
            previous: asset!("hmm3-previous.u16le"),
        }
    } else {
        GraphBytes {
            map: asset!("hmm4-state-map.u8"),
            pointers: asset!("hmm4-pointers.u32le"),
            previous: asset!("hmm4-previous.u16le"),
        }
    }
}
impl GraphBytes {
    fn tuple(self, id: u16) -> Result<(u8, u8, u8), Error> {
        let k = usize::from(id) * 3;
        if k + 2 >= self.map.len() {
            return Err(Error::InvalidInput);
        }
        Ok((self.map[k], self.map[k + 1], self.map[k + 2]))
    }
    fn pointer(self, id: usize) -> usize {
        let k = id * 4;
        u32::from_le_bytes(self.pointers[k..k + 4].try_into().expect("fixed pointer")) as usize
    }
    fn previous(self, k: usize) -> u16 {
        u16::from_le_bytes([self.previous[k * 2], self.previous[k * 2 + 1]])
    }
}

pub struct Session<'a, 's> {
    input: Input<'a>,
    scratch: Option<&'s mut [Event]>,
    geometry: Geometry,
    meter: u8,
    mode: Mode,
    phase: Phase,
    status: Status,
    reason: Option<Reason>,
    error: Option<Error>,
    rows: usize,
    row: usize,
    count: usize,
    support_first: Option<usize>,
    support_end: usize,
    searching: bool,
    low: usize,
    high: usize,
    region: bool,
    peak: f32,
    peak_event: Event,
    phase_bad: bool,
    downbeat: Option<usize>,
    sj: i128,
    sjj: i128,
    sr: i128,
    sjr: i128,
    tempo: u32,
    period: i128,
    anchor: i128,
    summary: Option<Summary>,
    total_units: u64,
}
impl<'a, 's> Session<'a, 's> {
    fn initial(input: Input<'a>, geometry: Geometry, meter: u8, rows: usize) -> Self {
        Self {
            input,
            scratch: None,
            geometry,
            meter,
            mode: Mode::Grid,
            phase: Phase::ValidateEvents,
            status: Status::Running,
            reason: None,
            error: None,
            rows,
            row: 0,
            count: 0,
            support_first: None,
            support_end: 0,
            searching: false,
            low: 0,
            high: 0,
            region: false,
            peak: -1.0,
            peak_event: Event::default(),
            phase_bad: false,
            downbeat: None,
            sj: 0,
            sjj: 0,
            sr: 0,
            sjr: 0,
            tempo: 0,
            period: 0,
            anchor: 0,
            summary: None,
            total_units: 0,
        }
    }
    pub fn from_events(geometry: Geometry, meter: u8, events: &'a [Event]) -> Result<Self, Error> {
        let rows = geometry.rows(meter)?;
        if events.len() > MAX_EVENTS {
            return Err(Error::Capacity);
        }
        let mut s = Self::initial(Input::Events(events), geometry, meter, rows);
        s.count = events.len();
        Ok(s)
    }
    pub fn from_path(
        input: PathInput<'a>,
        mode: Mode,
        scratch: &'s mut [Event],
    ) -> Result<Self, Error> {
        let rows = input.geometry.rows(input.meter)?;
        if input.probabilities.len() > MAX_ROWS
            || input.states.len() > MAX_ROWS
            || scratch.len() < MAX_EVENTS
        {
            return Err(Error::Capacity);
        }
        let (first, end) = (
            input.decoded_first_row as usize,
            input.decoded_end_row as usize,
        );
        if input.probabilities.len() != rows
            || first > end
            || end > rows
            || input.states.len() != end - first
        {
            return Err(Error::InvalidInput);
        }
        let mut s = Self::initial(Input::Path(input), input.geometry, input.meter, rows);
        s.scratch = Some(scratch);
        s.mode = mode;
        s.phase = Phase::ValidateProbabilitiesAndSupport;
        Ok(s)
    }
    pub fn phase(&self) -> Phase {
        self.phase
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn reason(&self) -> Option<Reason> {
        self.reason
    }
    pub fn error(&self) -> Option<Error> {
        self.error
    }
    pub fn total_units(&self) -> u64 {
        self.total_units
    }
    fn events(&self) -> &[Event] {
        match self.input {
            Input::Events(e) => e,
            Input::Path(_) => &self.scratch.as_deref().expect("path scratch")[..self.count],
        }
    }
    pub fn result(&self) -> Option<ResultView<'_>> {
        match self.status {
            Status::EventsReady => Some(ResultView::Events(self.events())),
            Status::GridReady => Some(ResultView::Grid {
                summary: self.summary.expect("complete summary"),
                events: self.events(),
            }),
            _ => None,
        }
    }
    fn stop(&mut self, status: Status, reason: Option<Reason>) {
        self.status = status;
        self.reason = reason;
        self.phase = Phase::Terminal;
    }
    fn unsupported(&mut self, r: Reason) {
        self.stop(Status::UnsupportedGrid, Some(r));
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.stop(Status::Cancelled, None);
        }
    }
    pub fn step(&mut self, quota: usize, cancel: &AtomicBool) -> Result<StepReport, Error> {
        if quota == 0 {
            return Err(Error::ZeroBudget);
        }
        let mut units = 0;
        while units < quota && self.status == Status::Running {
            if cancel.load(Ordering::Acquire) {
                self.cancel();
                break;
            }
            let result = if self.total_units == MAX_UNITS {
                Err(Error::Capacity)
            } else {
                self.tick()
            };
            if let Err(e) = result {
                self.error = Some(e);
                self.stop(Status::Failed, None);
                return Err(e);
            }
            self.total_units += 1;
            units += 1;
        }
        Ok(StepReport {
            units,
            status: self.status,
        })
    }
    fn path(&self) -> PathInput<'a> {
        match self.input {
            Input::Path(p) => p,
            _ => unreachable!("path phase"),
        }
    }
    fn push_peak(&mut self) -> Result<(), Error> {
        if self.count == MAX_EVENTS {
            return Err(Error::Capacity);
        }
        self.scratch.as_deref_mut().expect("path scratch")[self.count] = self.peak_event;
        self.count += 1;
        self.region = false;
        self.peak = -1.0;
        Ok(())
    }
    fn tick(&mut self) -> Result<(), Error> {
        match self.phase {
            Phase::ValidateProbabilitiesAndSupport => {
                let p = self.path();
                if self.row == self.rows {
                    if let Some(first) = self.support_first {
                        if p.decoded_first_row as usize != first
                            || p.decoded_end_row as usize != self.support_end
                        {
                            return Err(Error::InvalidInput);
                        }
                        self.row = 0;
                        self.phase = Phase::ValidatePath;
                    } else if p.states.is_empty()
                        && p.decoded_first_row == 0
                        && p.decoded_end_row == 0
                    {
                        self.stop(Status::NoEvidence, Some(Reason::NoSupport));
                    } else {
                        return Err(Error::InvalidInput);
                    }
                } else {
                    let v = p.probabilities[self.row];
                    if v.iter().any(|x| !x.is_finite() || !(0.0..=1.0).contains(x))
                        || (((f64::from(v[0]) + f64::from(v[1])) + f64::from(v[2])) - 1.0).abs()
                            > 1.0 / (1u32 << 21) as f64
                    {
                        return Err(Error::InvalidInput);
                    }
                    if f64::from(v[0]) >= 0.05 || f64::from(v[1]) >= 0.05 {
                        if self.support_first.is_none() {
                            self.support_first = Some(self.row);
                        }
                        self.support_end = self.row + 1;
                    }
                    self.row += 1;
                }
            }
            Phase::ValidatePath => {
                let p = self.path();
                let g = graph(self.meter);
                if self.row == p.states.len() {
                    self.row = 0;
                    self.phase = Phase::ExtractEvents;
                } else if !self.searching {
                    g.tuple(p.states[self.row])?;
                    if self.row == 0 {
                        self.row += 1;
                    } else {
                        let id = usize::from(p.states[self.row]);
                        self.low = g.pointer(id);
                        self.high = g.pointer(id + 1);
                        self.searching = true;
                    }
                } else if self.low == self.high {
                    return Err(Error::InvalidInput);
                } else {
                    let mid = self.low + (self.high - self.low) / 2;
                    let prev = g.previous(mid);
                    let want = p.states[self.row - 1];
                    if prev == want {
                        self.row += 1;
                        self.searching = false;
                    } else if prev < want {
                        self.low = mid + 1;
                    } else {
                        self.high = mid;
                    }
                }
            }
            Phase::ExtractEvents => {
                let p = self.path();
                if self.row == p.states.len() {
                    if self.region {
                        self.push_peak()?;
                    } else if self.count == 0 {
                        self.stop(Status::NoEvidence, Some(Reason::NoRegions));
                    } else if self.mode == Mode::EventsOnly {
                        self.stop(Status::EventsReady, None);
                    } else {
                        self.row = 0;
                        self.phase = Phase::ValidateEvents;
                    }
                } else {
                    let (b, i, q) = graph(self.meter).tuple(p.states[self.row])?;
                    if 16 * u16::from(q) < u16::from(i) {
                        let r = p.decoded_first_row as usize + self.row;
                        let v = p.probabilities[r];
                        if !self.region {
                            self.region = true;
                            self.peak = -1.0;
                        }
                        for class in [0, 1] {
                            if v[class] > self.peak {
                                self.peak = v[class];
                                self.peak_event = Event {
                                    global_row: r as u32,
                                    beat_number: b + 1,
                                    winning_class: class as u8,
                                    reserved: [0; 2],
                                };
                            }
                        }
                    } else if self.region {
                        self.push_peak()?;
                    }
                    self.row += 1;
                }
            }
            Phase::ValidateEvents => {
                if self.row == self.count {
                    if self.count == 0 {
                        self.stop(Status::NoEvidence, Some(Reason::EmptyEvents));
                    } else if self.count < 2 * usize::from(self.meter) + 1 {
                        self.unsupported(Reason::InsufficientEvents);
                    } else {
                        self.row = 0;
                        self.phase = Phase::AccumulateFit;
                    }
                } else {
                    let e = self.events()[self.row];
                    if e.global_row as usize >= self.rows
                        || !(1..=self.meter).contains(&e.beat_number)
                        || e.winning_class > 1
                        || e.reserved != [0; 2]
                        || (self.row > 0 && self.events()[self.row - 1].global_row >= e.global_row)
                    {
                        return Err(Error::InvalidInput);
                    }
                    self.row += 1;
                }
            }
            Phase::AccumulateFit => {
                if self.row == self.count {
                    if self.phase_bad {
                        self.unsupported(Reason::PhaseDiscontinuity);
                    } else {
                        self.phase = Phase::ComputeModel;
                    }
                } else {
                    let e = self.events()[self.row];
                    let j = self.row as i128;
                    let r = i128::from(e.global_row);
                    if self.row > 0
                        && e.beat_number != self.events()[self.row - 1].beat_number % self.meter + 1
                    {
                        self.phase_bad = true;
                    }
                    if e.beat_number == 1 && self.downbeat.is_none() {
                        self.downbeat = Some(self.row);
                    }
                    self.sj = add(self.sj, j)?;
                    self.sjj = add(self.sjj, mul(j, j)?)?;
                    self.sr = add(self.sr, r)?;
                    self.sjr = add(self.sjr, mul(j, r)?)?;
                    self.row += 1;
                }
            }
            Phase::ComputeModel => {
                let l = self.count as i128;
                let d = sub(mul(l, self.sjj)?, mul(self.sj, self.sj)?)?;
                let u = sub(mul(l, self.sjr)?, mul(self.sj, self.sr)?)?;
                if d <= 0 || u <= 0 {
                    return Err(Error::ArithmeticOverflow);
                }
                let t = rhu(mul(3000000, d)?, u)?;
                if !(40000..=300000).contains(&t) {
                    self.unsupported(Reason::TempoOutOfProfile);
                } else {
                    self.tempo = t as u32;
                    let fs = i128::from(self.geometry.sample_rate);
                    self.period = mul(mul(fs, 60000)?, Q)? / t;
                    let n = sub(
                        mul(mul(mul(2, fs)?, Q)?, self.sr)?,
                        mul(mul(mul(50, l)?, l - 1)?, self.period)?,
                    )?;
                    self.anchor = rhu(n, mul(100, l)?)?;
                    if self.anchor < 0 || self.anchor >= self.period {
                        self.unsupported(Reason::AnchorOutOfRange);
                    } else {
                        self.phase = Phase::CheckCoverage;
                    }
                }
            }
            Phase::CheckCoverage => {
                let l = self.count as i128;
                let end = mul(i128::from(self.geometry.total_frames), Q)?;
                if add(self.anchor, mul(l - 1, self.period)?)? >= end
                    || add(self.anchor, mul(l, self.period)?)? < end
                {
                    self.unsupported(Reason::IncompleteCoverage);
                } else {
                    self.row = 0;
                    self.phase = Phase::CheckResiduals;
                }
            }
            Phase::CheckResiduals => {
                if self.row == self.count {
                    self.phase = Phase::BuildSummary;
                } else {
                    let e = self.events()[self.row];
                    let fs = i128::from(self.geometry.sample_rate);
                    let raw = mul(mul(i128::from(e.global_row), fs)?, Q)?;
                    let fitted = mul(50, add(self.anchor, mul(self.row as i128, self.period)?)?)?;
                    let delta = sub(raw, fitted)?
                        .checked_abs()
                        .ok_or(Error::ArithmeticOverflow)?;
                    if mul(10, delta)? > mul(50, self.period)? {
                        self.unsupported(Reason::ResidualExceeded);
                    } else {
                        self.row += 1;
                    }
                }
            }
            Phase::BuildSummary => {
                let d = self.downbeat.ok_or(Error::InvalidInput)?;
                let (first, end) = match self.input {
                    Input::Path(p) => {
                        (u64::from(p.decoded_first_row), u64::from(p.decoded_end_row))
                    }
                    Input::Events(e) => (
                        u64::from(e[0].global_row),
                        u64::from(e[e.len() - 1].global_row) + 1,
                    ),
                };
                let fs = u64::from(self.geometry.sample_rate);
                self.summary = Some(Summary {
                    geometry: self.geometry,
                    meter: self.meter,
                    tempo_millibpm: self.tempo,
                    anchor_q32: self.anchor as u128,
                    period_q32: self.period as u128,
                    beat_count: self.count as u32,
                    downbeat_ordinal: d as u32,
                    downbeat_whole_frame: (add(self.anchor, mul(d as i128, self.period)?)? / Q)
                        as u64,
                    evidence_frames: [
                        first * fs / 50,
                        (end * fs).div_ceil(50).min(self.geometry.total_frames),
                    ],
                });
                self.stop(Status::GridReady, None);
            }
            Phase::Terminal => {}
        }
        Ok(())
    }
}

#[cfg(test)]
mod arithmetic_tests {
    use super::*;
    #[test]
    fn signed_rounding_and_checked_overflow() {
        for (n, d, e) in [(3, 2, 2), (5, 2, 3), (-1, 2, 0), (-3, 2, -1)] {
            assert_eq!(rhu(n, d), Ok(e));
        }
        assert_eq!(rhu(i128::MAX, 1), Err(Error::ArithmeticOverflow));
        assert_eq!(rhu(1, 0), Err(Error::ArithmeticOverflow));
        assert_eq!(mul(i128::MAX, 2), Err(Error::ArithmeticOverflow));
    }
}

/// Fixed synthetic fixtures only. Allocating helpers are outside the Session boundary.
pub mod synthetic {
    use super::*;
    use sha2::{Digest, Sha256};
    pub const FIXTURE_COUNT: usize = 50;
    pub fn protocol_hash() -> String {
        format!(
            "{:x}",
            Sha256::digest(include_bytes!(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/../../docs/rust/PRODUCT-HOST-GRID-PROTOCOL.json"
            )))
        )
    }
    pub fn fixture_hash() -> String {
        format!(
            "{:x}",
            Sha256::digest(include_bytes!(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/../../docs/rust/fixtures/product-host-grid-hs02/cases.json"
            )))
        )
    }
    pub struct Case {
        pub name: &'static str,
        pub geometry: Geometry,
        pub meter: u8,
        pub probabilities: Vec<[f32; 3]>,
        pub states: Vec<u16>,
        pub events: Vec<Event>,
        pub span: [u32; 2],
        pub scratch_entries: usize,
        pub is_path: bool,
        pub mode: Mode,
        pub expected: &'static str,
        pub expected_event_hash: Option<&'static str>,
        pub expected_summary_hash: Option<&'static str>,
    }
    impl Case {
        pub fn session<'a, 's>(
            &'a self,
            scratch: &'s mut [Event],
        ) -> Result<Session<'a, 's>, Error> {
            if self.is_path {
                Session::from_path(
                    PathInput {
                        geometry: self.geometry,
                        meter: self.meter,
                        probabilities: &self.probabilities,
                        decoded_first_row: self.span[0],
                        decoded_end_row: self.span[1],
                        states: &self.states,
                    },
                    self.mode,
                    scratch,
                )
            } else {
                Session::from_events(self.geometry, self.meter, &self.events)
            }
        }
    }
    pub fn states(i: u32, b: u32, q: u32, len: usize, m: u8) -> Vec<u16> {
        (0..len)
            .map(|k| {
                let v = q + k as u32;
                (((b + v / i) % u32::from(m)) * 2805 + (i - 10) * (i + 9) / 2 + v % i) as u16
            })
            .collect()
    }
    pub fn prob_train(
        rows: usize,
        first: usize,
        i: usize,
        count: usize,
        start: u8,
        m: u8,
    ) -> Vec<[f32; 3]> {
        let mut p = vec![[0.01, 0.01, 0.98]; rows];
        for j in 0..count {
            p[first + j * i] = if (usize::from(start) - 1 + j) % usize::from(m) == 0 {
                [0.02, 0.96, 0.02]
            } else {
                [0.96, 0.02, 0.02]
            };
        }
        p
    }
    fn event_vec(
        rows: Vec<u32>,
        numbers: Vec<u8>,
        classes: Option<Vec<u8>>,
        reserved: [u8; 2],
    ) -> Vec<Event> {
        rows.into_iter()
            .zip(numbers)
            .enumerate()
            .map(|(j, (r, n))| Event {
                global_row: r,
                beat_number: n,
                winning_class: classes.as_ref().map_or(u8::from(n == 1), |c| c[j]),
                reserved,
            })
            .collect()
    }
    pub fn fixture(index: usize) -> Case {
        match index {
            0 => Case {
                name: "regular4-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 198450,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"18940805775360","anchor_whole":4410,"applicability_frames":[0,198450],"beat_count":9,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":4410,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"94704028876800","period_whole":22050,"segment_count":1,"status":"GridReady","tempo_millibpm":120000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "e364e580735f277e0beaa88ec50e890f78e55ec254442012a3a628c0661b7b08",
                ),
            },
            1 => Case {
                name: "regular3-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 154350,
                },
                meter: 3,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155],
                    vec![2, 3, 1, 2, 3, 1, 2],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"18940805775360","anchor_whole":4410,"applicability_frames":[0,154350],"beat_count":7,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":2,"downbeat_whole_frame":48510,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":3,"period_fraction_q32":0,"period_q32":"94704028876800","period_whole":22050,"segment_count":1,"status":"GridReady","tempo_millibpm":120000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "3cf6cc2ac9cd7fbf8fb647f45d438be6317327f7d2f8d552c59e0b2253dd9ba2",
                ),
            },
            2 => Case {
                name: "fractional-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 132300,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![3, 20, 37, 54, 71, 88, 105, 122, 139],
                    vec![3, 4, 1, 2, 3, 4, 1, 2, 3],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":601053504,"anchor_q32":"11365084518720","anchor_whole":2646,"applicability_frames":[0,132300],"beat_count":9,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":2,"downbeat_whole_frame":32634,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":4144703920,"period_q32":"64398589372848","period_whole":14993,"segment_count":1,"status":"GridReady","tempo_millibpm":176471}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "d63287d245286d7c3fda35c8215a28c38bba32a0383284350d6a831c59c159ca",
                ),
            },
            3 => Case {
                name: "min40-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 595350,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 80, 155, 230, 305, 380, 455, 530, 605],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"18940805775360","anchor_whole":4410,"applicability_frames":[0,595350],"beat_count":9,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":4410,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"284112086630400","period_whole":66150,"segment_count":1,"status":"GridReady","tempo_millibpm":40000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "4a3e6968254eda7b15cf6ae06f9f832832783849543b2348c3764cee14b4a3f4",
                ),
            },
            4 => Case {
                name: "max300-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 26460000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    (0..30000).step_by(10).collect(),
                    (0..3000).map(|j| ((j) % 4 + 1) as u8).collect(),
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"0","anchor_whole":0,"applicability_frames":[0,26460000],"beat_count":3000,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":0,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"37881611550720","period_whole":8820,"segment_count":1,"status":"GridReady","tempo_millibpm":300000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "e3b5acffbd1e7298cb061c5039b93cbc6acc8016a65acc9d371c02920aca3aee",
                ),
            },
            5 => Case {
                name: "regular4-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"20615843020800","anchor_whole":4800,"applicability_frames":[0,216000],"beat_count":9,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":4800,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"103079215104000","period_whole":24000,"segment_count":1,"status":"GridReady","tempo_millibpm":120000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "13920249cf43371b505f54550484937d6f0e0e63e4da3d0c9c41828233906bea",
                ),
            },
            6 => Case {
                name: "regular3-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 168000,
                },
                meter: 3,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155],
                    vec![2, 3, 1, 2, 3, 1, 2],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"20615843020800","anchor_whole":4800,"applicability_frames":[0,168000],"beat_count":7,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":2,"downbeat_whole_frame":52800,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":3,"period_fraction_q32":0,"period_q32":"103079215104000","period_whole":24000,"segment_count":1,"status":"GridReady","tempo_millibpm":120000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "c9b131f292b19e2326c6d2930b052eee774cfb84152989a5ec770891b56f42ac",
                ),
            },
            7 => Case {
                name: "fractional-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 144000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![3, 20, 37, 54, 71, 88, 105, 122, 139],
                    vec![3, 4, 1, 2, 3, 4, 1, 2, 3],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":654207896,"anchor_q32":"12370160020376","anchor_whole":2880,"applicability_frames":[0,144000],"beat_count":9,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":2,"downbeat_whole_frame":35520,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":4131415322,"period_q32":"70093702718746","period_whole":16319,"segment_count":1,"status":"GridReady","tempo_millibpm":176471}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "f01f2eeb9ad7ef1b52dc9c7d4d46e9199fd9bfb2ef1830d0fb8edcaa9de6468c",
                ),
            },
            8 => Case {
                name: "min40-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 648000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 80, 155, 230, 305, 380, 455, 530, 605],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"20615843020800","anchor_whole":4800,"applicability_frames":[0,648000],"beat_count":9,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":4800,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"309237645312000","period_whole":72000,"segment_count":1,"status":"GridReady","tempo_millibpm":40000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "4fc1d46ad693122338ed0a05a6964438a0a497e753b6d4b35c7c0777d7d0435a",
                ),
            },
            9 => Case {
                name: "max300-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 28800000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    (0..30000).step_by(10).collect(),
                    (0..3000).map(|j| ((j) % 4 + 1) as u8).collect(),
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"0","anchor_whole":0,"applicability_frames":[0,28800000],"beat_count":3000,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":0,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"41231686041600","period_whole":9600,"segment_count":1,"status":"GridReady","tempo_millibpm":300000}"##,
                expected_event_hash: None,
                expected_summary_hash: Some(
                    "70c8ec7b0a8a08f56abda085d2fd6b9bd27e69950d596f8a8a348c08ade975a2",
                ),
            },
            10 => Case {
                name: "phase-break",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 2, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"PhaseDiscontinuity","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            11 => Case {
                name: "timing-break",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 115, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"ResidualExceeded","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            12 => Case {
                name: "uncovered-tail",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 480000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"IncompleteCoverage","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            13 => Case {
                name: "late-anchor",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 312000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![105, 130, 155, 180, 205, 230, 255, 280, 305],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"AnchorOutOfRange","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            14 => Case {
                name: "insufficient-events",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 192000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180],
                    vec![1, 2, 3, 4, 1, 2, 3, 4],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InsufficientEvents","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            15 => Case {
                name: "duplicate-row",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 30, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            16 => Case {
                name: "unordered-row",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 80, 55, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            17 => Case {
                name: "event-at-EOF",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 225],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            18 => Case {
                name: "missing-event",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 312000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 105, 130, 155, 180, 205, 230, 255, 280, 305],
                    vec![1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"PhaseDiscontinuity","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            19 => Case {
                name: "extra-event",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 312000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![
                        5, 30, 40, 55, 80, 105, 130, 155, 180, 205, 230, 255, 280, 305,
                    ],
                    vec![1, 2, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"PhaseDiscontinuity","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            20 => Case {
                name: "invalid-beat-number",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![0, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            21 => Case {
                name: "invalid-event-class",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    Some(vec![2, 0, 0, 0, 0, 0, 0, 0, 0]),
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            22 => Case {
                name: "reserved-event-byte",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    vec![5, 30, 55, 80, 105, 130, 155, 180, 205],
                    vec![1, 2, 3, 4, 1, 2, 3, 4, 1],
                    None,
                    [1, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            23 => Case {
                name: "empty-events",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 48000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(vec![], vec![], None, [0, 0]),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"result_exposed":false,"status":"NoEvidence"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            24 => Case {
                name: "event-count-3072",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 28800000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    (0..3072).step_by(1).collect(),
                    (0..3072).map(|j| ((j) % 4 + 1) as u8).collect(),
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"TempoOutOfProfile","result_exposed":false,"status":"UnsupportedGrid"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            25 => Case {
                name: "event-count-3073",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 28800000,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: event_vec(
                    (0..3073).step_by(1).collect(),
                    (0..3073).map(|j| ((j) % 4 + 1) as u8).collect(),
                    None,
                    [0, 0],
                ),
                span: [0, 0],
                scratch_entries: 3072,
                is_path: false,
                mode: Mode::Grid,
                expected: r##"{"reason":"Capacity","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            26 => Case {
                name: "class-tie-path-number-not-column",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 4800,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                ],
                states: states(75, 0, 0, 5, 4),
                events: vec![],
                span: [0, 5],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"events":[{"beat_number":1,"global_row":0,"reserved":[0,0],"winning_class":0}],"status":"EventsReady"}"##,
                expected_event_hash: Some(
                    "01acecb507abfe1a354aa8064f4af5d3f1acd019e37db3c11c97523b71c76e9d",
                ),
                expected_summary_hash: None,
            },
            27 => Case {
                name: "earlier-row-beats-later-class",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 1920,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3dcccccd),
                        f32::from_bits(0x3f19999a),
                        f32::from_bits(0x3e99999a),
                    ],
                    [
                        f32::from_bits(0x3f19999a),
                        f32::from_bits(0x3dcccccd),
                        f32::from_bits(0x3e99999a),
                    ],
                ],
                states: states(75, 0, 0, 2, 4),
                events: vec![],
                span: [0, 2],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"events":[{"beat_number":1,"global_row":0,"reserved":[0,0],"winning_class":1}],"status":"EventsReady"}"##,
                expected_event_hash: Some(
                    "ee0293715378037132836eadb89121e2f25ce8d89e57e8621bf3a2c1a17908d7",
                ),
                expected_summary_hash: None,
            },
            28 => Case {
                name: "both-partial-regions",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 72000,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3dcccccd),
                        f32::from_bits(0x3dcccccd),
                        f32::from_bits(0x3f4ccccd)
                    ];
                    75
                ],
                states: states(75, 0, 3, 75, 4),
                events: vec![],
                span: [0, 75],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"events":[{"beat_number":1,"global_row":0,"reserved":[0,0],"winning_class":0},{"beat_number":2,"global_row":72,"reserved":[0,0],"winning_class":0}],"status":"EventsReady"}"##,
                expected_event_hash: Some(
                    "a21317f82b9d03c6c3dab76783f3cd5bb8fcd8eeb52a9bcdb5cd4eabc93f0aea",
                ),
                expected_summary_hash: None,
            },
            29 => Case {
                name: "row0-threshold-at",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3d4ccccd),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x3f733333),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"events":[{"beat_number":1,"global_row":0,"reserved":[0,0],"winning_class":0}],"status":"EventsReady"}"##,
                expected_event_hash: Some(
                    "01acecb507abfe1a354aa8064f4af5d3f1acd019e37db3c11c97523b71c76e9d",
                ),
                expected_summary_hash: None,
            },
            30 => Case {
                name: "threshold-predecessor",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3d4ccccc),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x3f733333),
                ]],
                states: vec![],
                events: vec![],
                span: [0, 0],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"result_exposed":false,"status":"NoEvidence"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            31 => Case {
                name: "all-nonbeat",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x3f800000),
                ]],
                states: vec![],
                events: vec![],
                span: [0, 0],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"result_exposed":false,"status":"NoEvidence"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            32 => Case {
                name: "no-beat-region",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3f19999a),
                    f32::from_bits(0x3dcccccd),
                    f32::from_bits(0x3e99999a),
                ]],
                states: states(75, 0, 5, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"result_exposed":false,"status":"NoEvidence"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            33 => Case {
                name: "probability-zero-triple",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x00000000),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            34 => Case {
                name: "probability-nan",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x7fc00000),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x3f800000),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            35 => Case {
                name: "probability-infinity",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x7f800000),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x3f800000),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            36 => Case {
                name: "probability-negative",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0xbf000000),
                    f32::from_bits(0x3f000000),
                    f32::from_bits(0x3f800000),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            37 => Case {
                name: "probability-over-unit",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3f8ccccd),
                    f32::from_bits(0x00000000),
                    f32::from_bits(0x00000000),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            38 => Case {
                name: "probability-sum-mismatch",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3f000000),
                    f32::from_bits(0x3f000000),
                    f32::from_bits(0x3f000000),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            39 => Case {
                name: "invalid-after-trim",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 2880,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3f19999a),
                        f32::from_bits(0x3dcccccd),
                        f32::from_bits(0x3e99999a),
                    ],
                    [
                        f32::from_bits(0x3c23d70a),
                        f32::from_bits(0x3c23d70a),
                        f32::from_bits(0x3f7ae148),
                    ],
                    [
                        f32::from_bits(0x7fc00000),
                        f32::from_bits(0x00000000),
                        f32::from_bits(0x3f800000),
                    ],
                ],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            40 => Case {
                name: "sentinel-state",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3f19999a),
                    f32::from_bits(0x3dcccccd),
                    f32::from_bits(0x3e99999a),
                ]],
                states: vec![65535],
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            41 => Case {
                name: "out-of-range-state",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 3,
                probabilities: vec![[
                    f32::from_bits(0x3f19999a),
                    f32::from_bits(0x3dcccccd),
                    f32::from_bits(0x3e99999a),
                ]],
                states: vec![8415],
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            42 => Case {
                name: "illegal-path-edge",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 1920,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                ],
                states: vec![0, 3],
                events: vec![],
                span: [0, 2],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            43 => Case {
                name: "decoded-span-drift",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 2880,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3c23d70a),
                        f32::from_bits(0x3c23d70a),
                        f32::from_bits(0x3f7ae148),
                    ],
                    [
                        f32::from_bits(0x3f19999a),
                        f32::from_bits(0x3dcccccd),
                        f32::from_bits(0x3e99999a),
                    ],
                    [
                        f32::from_bits(0x3c23d70a),
                        f32::from_bits(0x3c23d70a),
                        f32::from_bits(0x3f7ae148),
                    ],
                ],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            44 => Case {
                name: "small-scratch",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 960,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3f19999a),
                    f32::from_bits(0x3dcccccd),
                    f32::from_bits(0x3e99999a),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3071,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"Capacity","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            45 => Case {
                name: "unsupported-rate",
                geometry: Geometry {
                    sample_rate: 44101,
                    total_frames: 882,
                },
                meter: 4,
                probabilities: vec![[
                    f32::from_bits(0x3f19999a),
                    f32::from_bits(0x3dcccccd),
                    f32::from_bits(0x3e99999a),
                ]],
                states: states(10, 0, 0, 1, 4),
                events: vec![],
                span: [0, 1],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"UnsupportedProfile","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            46 => Case {
                name: "zero-source",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 0,
                },
                meter: 4,
                probabilities: vec![],
                states: vec![],
                events: vec![],
                span: [0, 0],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            47 => Case {
                name: "row-capacity",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 28800000,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x00000000),
                        f32::from_bits(0x00000000),
                        f32::from_bits(0x3f800000)
                    ];
                    30001
                ],
                states: vec![],
                events: vec![],
                span: [0, 0],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"Capacity","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            48 => Case {
                name: "path-length-mismatch",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 1920,
                },
                meter: 4,
                probabilities: vec![
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                    [
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3ecccccd),
                        f32::from_bits(0x3e4ccccd),
                    ],
                ],
                states: vec![0],
                events: vec![],
                span: [0, 2],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"reason":"InvalidInput","result_exposed":false,"status":"Failed"}"##,
                expected_event_hash: None,
                expected_summary_hash: None,
            },
            49 => Case {
                name: "offset-restoration",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 216000,
                },
                meter: 4,
                probabilities: prob_train(225, 5, 25, 9, 1, 4),
                states: states(25, 0, 0, 201, 4),
                events: vec![],
                span: [5, 206],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::EventsOnly,
                expected: r##"{"events":[{"beat_number":1,"global_row":5,"reserved":[0,0],"winning_class":1},{"beat_number":2,"global_row":30,"reserved":[0,0],"winning_class":0},{"beat_number":3,"global_row":55,"reserved":[0,0],"winning_class":0},{"beat_number":4,"global_row":80,"reserved":[0,0],"winning_class":0},{"beat_number":1,"global_row":105,"reserved":[0,0],"winning_class":1},{"beat_number":2,"global_row":130,"reserved":[0,0],"winning_class":0},{"beat_number":3,"global_row":155,"reserved":[0,0],"winning_class":0},{"beat_number":4,"global_row":180,"reserved":[0,0],"winning_class":0},{"beat_number":1,"global_row":205,"reserved":[0,0],"winning_class":1}],"status":"EventsReady"}"##,
                expected_event_hash: Some(
                    "aeb5854d298864cbebdaeea7a5f3f952a1ec68fe3a35f4383934c2e3c356b1c9",
                ),
                expected_summary_hash: None,
            },
            _ => panic!("fixed fixture index"),
        }
    }
    pub fn full(name: &str) -> Option<Case> {
        match name {
            "full3-44100" => Some(Case {
                name: "full3-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 26460000,
                },
                meter: 3,
                probabilities: prob_train(30000, 5, 25, 1200, 2, 3),
                states: states(25, 1, 0, 29976, 3),
                events: vec![],
                span: [5, 29981],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"18940805775360","anchor_whole":4410,"applicability_frames":[0,26460000],"beat_count":1200,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":2,"downbeat_whole_frame":48510,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":3,"period_fraction_q32":0,"period_q32":"94704028876800","period_whole":22050,"segment_count":1,"status":"GridReady","tempo_millibpm":120000}"##,
                expected_event_hash: Some(
                    "0001d8b2777b1f8f411aa7188a05179c405f717cf28b175051d5d23714580df8",
                ),
                expected_summary_hash: Some(
                    "787d4dd777c5540c6433e0378af038a3caee618456b9a5e6ab3f6149af67ccc9",
                ),
            }),
            "full4-44100" => Some(Case {
                name: "full4-44100",
                geometry: Geometry {
                    sample_rate: 44100,
                    total_frames: 26460000,
                },
                meter: 4,
                probabilities: prob_train(30000, 0, 10, 3000, 1, 4),
                states: states(10, 0, 0, 29991, 4),
                events: vec![],
                span: [0, 29991],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"0","anchor_whole":0,"applicability_frames":[0,26460000],"beat_count":3000,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":0,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"37881611550720","period_whole":8820,"segment_count":1,"status":"GridReady","tempo_millibpm":300000}"##,
                expected_event_hash: Some(
                    "38100492fed06a59c2da6b9bb104a2ece6472248268394a5f93eac1daaccdf50",
                ),
                expected_summary_hash: Some(
                    "e3b5acffbd1e7298cb061c5039b93cbc6acc8016a65acc9d371c02920aca3aee",
                ),
            }),
            "full3-48000" => Some(Case {
                name: "full3-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 28800000,
                },
                meter: 3,
                probabilities: prob_train(30000, 5, 25, 1200, 2, 3),
                states: states(25, 1, 0, 29976, 3),
                events: vec![],
                span: [5, 29981],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"20615843020800","anchor_whole":4800,"applicability_frames":[0,28800000],"beat_count":1200,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":2,"downbeat_whole_frame":52800,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":3,"period_fraction_q32":0,"period_q32":"103079215104000","period_whole":24000,"segment_count":1,"status":"GridReady","tempo_millibpm":120000}"##,
                expected_event_hash: Some(
                    "0001d8b2777b1f8f411aa7188a05179c405f717cf28b175051d5d23714580df8",
                ),
                expected_summary_hash: Some(
                    "4e197a4cd3bbe3a35c3ff4844ae25486d729313ca5b91e059050bbb66688102f",
                ),
            }),
            "full4-48000" => Some(Case {
                name: "full4-48000",
                geometry: Geometry {
                    sample_rate: 48000,
                    total_frames: 28800000,
                },
                meter: 4,
                probabilities: prob_train(30000, 0, 10, 3000, 1, 4),
                states: states(10, 0, 0, 29991, 4),
                events: vec![],
                span: [0, 29991],
                scratch_entries: 3072,
                is_path: true,
                mode: Mode::Grid,
                expected: r##"{"anchor_fraction_q32":0,"anchor_q32":"0","anchor_whole":0,"applicability_frames":[0,28800000],"beat_count":3000,"calibrated_quality_present":false,"confidence":0,"downbeat_ordinal":0,"downbeat_whole_frame":0,"explicit_beat_count":0,"key_present":false,"meter_denominator":4,"meter_numerator":4,"period_fraction_q32":0,"period_q32":"41231686041600","period_whole":9600,"segment_count":1,"status":"GridReady","tempo_millibpm":300000}"##,
                expected_event_hash: Some(
                    "38100492fed06a59c2da6b9bb104a2ece6472248268394a5f93eac1daaccdf50",
                ),
                expected_summary_hash: Some(
                    "70c8ec7b0a8a08f56abda085d2fd6b9bd27e69950d596f8a8a348c08ade975a2",
                ),
            }),
            _ => None,
        }
    }

    pub fn event_hash(events: &[Event]) -> String {
        let mut h = Sha256::new();
        for e in events {
            h.update(e.global_row.to_le_bytes());
            h.update([e.beat_number, e.winning_class, e.reserved[0], e.reserved[1]]);
        }
        format!("{:x}", h.finalize())
    }
    pub fn summary_json(s: Summary) -> String {
        format!("{{\"anchor_fraction_q32\":{},\"anchor_q32\":\"{}\",\"anchor_whole\":{},\"applicability_frames\":[0,{}],\"beat_count\":{},\"calibrated_quality_present\":false,\"confidence\":0,\"downbeat_ordinal\":{},\"downbeat_whole_frame\":{},\"explicit_beat_count\":0,\"key_present\":false,\"meter_denominator\":4,\"meter_numerator\":{},\"period_fraction_q32\":{},\"period_q32\":\"{}\",\"period_whole\":{},\"segment_count\":1,\"status\":\"GridReady\",\"tempo_millibpm\":{}}}",s.anchor_q32 as u32,s.anchor_q32,s.anchor_q32>>32,s.geometry.total_frames,s.beat_count,s.downbeat_ordinal,s.downbeat_whole_frame,s.meter,s.period_q32 as u32,s.period_q32,s.period_q32>>32,s.tempo_millibpm)
    }
    pub fn input_hash(c: &Case) -> String {
        let mut h = Sha256::new();
        h.update(c.geometry.sample_rate.to_le_bytes());
        h.update(c.geometry.total_frames.to_le_bytes());
        h.update([c.meter]);
        for r in c.span {
            h.update(r.to_le_bytes());
        }
        for row in &c.probabilities {
            for v in row {
                h.update(v.to_bits().to_le_bytes());
            }
        }
        for s in &c.states {
            h.update(s.to_le_bytes());
        }
        for e in &c.events {
            h.update(e.global_row.to_le_bytes());
            h.update([e.beat_number, e.winning_class, e.reserved[0], e.reserved[1]]);
        }
        format!("{:x}", h.finalize())
    }
    pub fn summary_hash(s: Summary) -> String {
        format!("{:x}", Sha256::digest((summary_json(s) + "\n").as_bytes()))
    }
    pub fn observation(s: &Session<'_, '_>) -> String {
        match s.result() {
        Some(ResultView::Grid{summary,..})=>summary_json(summary),
        Some(ResultView::Events(e))=>{
            let list:Vec<_>=e.iter().map(|x|format!("{{\"beat_number\":{},\"global_row\":{},\"reserved\":[0,0],\"winning_class\":{}}}",x.beat_number,x.global_row,x.winning_class)).collect();
            format!("{{\"events\":[{}],\"status\":\"EventsReady\"}}",list.join(","))
        }
        None=>match s.status() {
            Status::NoEvidence=>"{\"result_exposed\":false,\"status\":\"NoEvidence\"}".into(),
            Status::UnsupportedGrid=>format!("{{\"reason\":\"{:?}\",\"result_exposed\":false,\"status\":\"UnsupportedGrid\"}}",s.reason().expect("unsupported reason")),
            Status::Failed=>error_json(s.error().expect("failed error")),
            other=>format!("{{\"result_exposed\":false,\"status\":\"{other:?}\"}}"),
        }
    }
    }
    pub fn error_json(e: Error) -> String {
        format!("{{\"reason\":\"{e:?}\",\"result_exposed\":false,\"status\":\"Failed\"}}")
    }
    /// Synthetic serialization scaffolding only; uses the unchanged public validators.
    pub fn validate_wire(s: Summary) -> Result<Vec<u8>, libapta::Error> {
        use libapta::*;
        let range = FrameRange {
            first_frame: 0,
            end_frame: s.geometry.total_frames,
        };
        let evidence = FrameRange {
            first_frame: s.evidence_frames[0],
            end_frame: s.evidence_frames[1],
        };
        let source = SourceInfo {
            sample_rate: s.geometry.sample_rate,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(range.end_frame),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        };
        let segments = [GridSegment {
            applicability_range: range,
            anchor_position: FractionalFrame {
                whole_frame: (s.anchor_q32 >> 32) as u64,
                fraction_q32: s.anchor_q32 as u32,
            },
            anchor_ordinal: 0,
            frames_per_beat: FramePeriod {
                whole_frames: (s.period_q32 >> 32) as u64,
                fraction_q32: s.period_q32 as u32,
            },
            beat_count: s.beat_count,
            nominal_tempo_millibpm: s.tempo_millibpm,
            confidence: 0,
            state: FeatureState::Final,
            flags: 0,
            segment_id: 1,
            revision: 1,
        }];
        let candidates = [TempoCandidate {
            tempo_millibpm: s.tempo_millibpm,
            ..TempoCandidate::default()
        }];
        let tempo = TempoView {
            selected: TempoValue {
                state: FeatureState::Final,
                confidence: 0,
                flags: 0,
                tempo_millibpm: s.tempo_millibpm,
                candidate_set_id: 0,
                evidence_range: evidence,
                applicability_range: range,
            },
            candidates: &candidates,
        };
        let meter_segments = [MeterSegment {
            first_frame: 0,
            end_frame: range.end_frame,
            downbeat_frame: s.downbeat_whole_frame,
            downbeat_ordinal: i64::from(s.downbeat_ordinal),
            numerator: u16::from(s.meter),
            denominator: 4,
            state: FeatureState::Final,
            confidence: 0,
            segment_id: 1,
        }];
        let meter = Meter {
            state: FeatureState::Final,
            confidence: 0,
            numerator: u16::from(s.meter),
            denominator: 4,
            downbeat_frame: s.downbeat_whole_frame,
            downbeat_ordinal: i64::from(s.downbeat_ordinal),
            segments: &meter_segments,
        };
        let revision = GridRevision {
            state: RevisionState::Applied,
            confidence: 0,
            flags: 0,
            revision_id: 1,
            previous_revision_id: 0,
            proposed_representation: GridRepresentation::Segments,
            proposed_segment_count: 1,
            proposed_beat_count: 0,
            affected_range: range,
        };
        let global = GlobalGrid {
            state: FeatureState::Final,
            confidence: 0,
            flags: 0,
            representation: GridRepresentation::Segments,
            requested_range: range,
            evidence_range: evidence,
            applicability_range: range,
            coverage_range: range,
            segments: &segments,
            beats: &[],
        };
        grid::validate_external(&global, Some(range.end_frame), false)?;
        let coverage = [range];
        let native = NativeResultInput {
            source,
            info: NativeResultInfo::default(),
            provenance: Provenance {
                origin: ProvenanceOrigin::ExternalImport,
                source_name: "HS02-synthetic",
                source_version: "1",
            },
            overview: None,
            detail: None,
            metadata: None,
            tempo: Some(tempo),
            local_grid: None,
            global_grid: Some(NativeGrid {
                state: global.state,
                confidence: 0,
                flags: 0,
                representation: global.representation,
                requested_range: range,
                evidence_range: evidence,
                applicability_range: range,
                coverage_ranges: &coverage,
                segments: &segments,
                beats: &[],
            }),
            revision: Some(revision),
            key: None,
            meter: Some(meter),
            quality: &[],
        };
        native_validation::validate(&native, NativeLimits::default())?;
        let mut columns = [WaveformColumn {
            flags: 1,
            ..WaveformColumn::default()
        }];
        let spans = [WaveformSpan {
            first_frame: 0,
            end_frame: range.end_frame,
            first_column_index: 0,
            column_count: 1,
            data_column_offset: 0,
        }];
        let mut bytes = vec![0u8; 4096];
        let size = {
            let input = result::ResultInput {
                source,
                overview: WaveformOverview {
                    level_id: 0,
                    frames_per_column: range.end_frame as u32,
                    origin_frame: 0,
                    logical_column_count: 1,
                    state: FeatureState::Final,
                    spans: &spans,
                    columns: &columns,
                },
                tiles: &[],
                metadata: None,
                tempo: Some(tempo),
                local_grid: None,
                global_grid: Some(global),
                revision: Some(revision),
                key: None,
                meter: Some(meter),
                quality: &[],
            };
            let size = builder::validate(&input, result::Limits::default())?;
            builder::finalize(&input, &mut bytes, result::Limits::default())?;
            size
        };
        columns[0].maximum = 123;
        bytes.truncate(size);
        let view = result::parse(&bytes, result::Limits::default())?;
        let g = view.global_grid.ok_or(libapta::Error::InvalidArgument)?;
        let t = view.tempo.ok_or(libapta::Error::InvalidArgument)?;
        let m = view
            .meter
            .ok_or(libapta::Error::InvalidArgument)?
            .segment(0)
            .ok_or(libapta::Error::InvalidArgument)?;
        if columns[0].maximum != 123
            || view.source != source
            || g.state() != FeatureState::Final
            || g.confidence() != 0
            || g.flags() != 0
            || g.representation() != GridRepresentation::Segments
            || g.requested_range() != range
            || g.evidence_range() != evidence
            || g.applicability_range() != range
            || g.coverage_range() != range
            || g.segment_count() != 1
            || g.beat_count() != 0
            || t.candidate_count() != 1
            || t.candidate(0) != Some(candidates[0])
            || view.meter.expect("present").segment_count() != 1
            || g.segment(0) != Some(segments[0])
            || t.selected() != tempo.selected
            || view.revision != Some(revision)
            || m != meter_segments[0]
            || view.key.is_some()
            || view.quality.is_some()
            || view
                .waveform
                .overview
                .column(0, 0)
                .ok_or(libapta::Error::InvalidArgument)?
                .maximum
                != 0
        {
            return Err(libapta::Error::InvalidArgument);
        }
        Ok(bytes)
    }
}
