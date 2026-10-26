// SPDX-License-Identifier: Apache-2.0
//! HS-01 synthetic host experiment. No model, file I/O, product publication or core changes.
use std::sync::atomic::{AtomicBool, Ordering};

pub const MAX_STATES: usize = 11220;
pub const MAX_EDGES: usize = 28380;
pub const MAX_ROWS: usize = 30000;
pub const MAX_BLOCK: usize = 256;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidGraph,
    InvalidDensity,
    Capacity,
    Allocation,
    ZeroBudget,
    ReplayMismatch,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}

#[derive(Clone, Copy)]
pub struct Graph<'a> {
    pub pointers: &'a [u32],
    pub previous: &'a [u16],
    pub log_probabilities: &'a [f64],
    pub classes: &'a [u8],
    pub initial_log_probability: f64,
}
fn finite_log(value: f64) -> bool {
    value.is_finite() && (-1024.0..=0.0).contains(&value)
}
impl Graph<'_> {
    pub fn validate(&self) -> Result<(), Error> {
        let s = self.classes.len();
        if s == 0 || s > MAX_STATES || self.previous.len() > MAX_EDGES {
            return Err(Error::Capacity);
        }
        if self.pointers.len() != s + 1
            || self.pointers[0] != 0
            || self.pointers[s] as usize != self.previous.len()
            || self.previous.len() != self.log_probabilities.len()
            || !finite_log(self.initial_log_probability)
            || self.classes.iter().any(|&c| c > 2)
            || self.log_probabilities.iter().any(|&p| !finite_log(p))
        {
            return Err(Error::InvalidGraph);
        }
        for pair in self.pointers.windows(2) {
            let (lo, hi) = (pair[0] as usize, pair[1] as usize);
            if lo >= hi || hi > self.previous.len() {
                return Err(Error::InvalidGraph);
            }
            let incoming = &self.previous[lo..hi];
            if incoming.iter().any(|&p| p as usize >= s)
                || incoming.windows(2).any(|v| v[0] >= v[1])
            {
                return Err(Error::InvalidGraph);
            }
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Score,
    Trace,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StoragePlan {
    states: usize,
    checkpoint_values: usize,
    parent_values: usize,
    path_values: usize,
}
impl StoragePlan {
    pub fn new(states: usize, rows: usize, block: usize, mode: Mode) -> Result<Self, Error> {
        if states == 0
            || states > MAX_STATES
            || rows > MAX_ROWS
            || !(1..=MAX_BLOCK).contains(&block)
        {
            return Err(Error::Capacity);
        }
        let trace = mode == Mode::Trace;
        Ok(Self {
            states,
            checkpoint_values: if trace {
                rows.div_ceil(block)
                    .checked_mul(states)
                    .ok_or(Error::Capacity)?
            } else {
                0
            },
            parent_values: if trace {
                block.min(rows).checked_mul(states).ok_or(Error::Capacity)?
            } else {
                0
            },
            path_values: if trace { rows } else { 0 },
        })
    }
    pub fn payload_bytes(&self) -> usize {
        self.checkpoint_values * 8
            + self.states * 16
            + self.parent_values * 2
            + self.path_values * 2
    }
}

/// Caller-owned host storage, allocated before stepping. Vec controls are separate payload overhead.
pub struct Workspace {
    a: Vec<f64>,
    b: Vec<f64>,
    checkpoints: Vec<f64>,
    parents: Vec<u16>,
    path: Vec<u16>,
}
fn allocate<T: Clone>(n: usize, value: T) -> Result<Vec<T>, Error> {
    let mut v = Vec::new();
    v.try_reserve_exact(n).map_err(|_| Error::Allocation)?;
    v.resize(n, value);
    Ok(v)
}
impl Workspace {
    pub fn allocate(plan: StoragePlan) -> Result<Self, Error> {
        // Recheck allocation dimensions before reserving workspace storage.
        if plan.states == 0
            || plan.states > MAX_STATES
            || plan.checkpoint_values > MAX_STATES * MAX_ROWS
            || plan.parent_values > MAX_STATES * MAX_BLOCK
            || plan.path_values > MAX_ROWS
        {
            return Err(Error::Capacity);
        }
        Ok(Self {
            a: allocate(plan.states, 0.0)?,
            b: allocate(plan.states, 0.0)?,
            checkpoints: allocate(plan.checkpoint_values, 0.0)?,
            parents: allocate(plan.parent_values, u16::MAX)?,
            path: allocate(plan.path_values, u16::MAX)?,
        })
    }
    pub fn allocated_payload_bytes(&self) -> usize {
        (self.a.capacity() + self.b.capacity() + self.checkpoints.capacity()) * 8
            + (self.parents.capacity() + self.path.capacity()) * 2
    }
    fn fits(&self, p: StoragePlan) -> bool {
        self.a.len() >= p.states
            && self.b.len() >= p.states
            && self.checkpoints.len() >= p.checkpoint_values
            && self.parents.len() >= p.parent_values
            && self.path.len() >= p.path_values
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Running,
    Scored,
    Decoded,
    NoEvidence,
    NoPath,
    Cancelled,
    Failed,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Phase {
    Initialize,
    SaveCheckpoint,
    Forward,
    SelectFinal,
    Restore,
    Replay,
    VerifyReplay,
    Traceback,
    Terminal,
}
pub struct ResultView<'a> {
    pub score: f64,
    pub final_state: u16,
    pub path: Option<&'a [u16]>,
}
#[derive(Debug)]
pub struct StepReport {
    pub units: usize,
    pub status: Status,
}

pub struct Decoder<'g, 'd, 'w> {
    graph: Graph<'g>,
    densities: &'d [[f64; 3]],
    workspace: &'w mut Workspace,
    mode: Mode,
    block: usize,
    phase: Phase,
    status: Status,
    a_previous: bool,
    row: usize,
    state: usize,
    edge: usize,
    started: bool,
    best: f64,
    parent: u16,
    copy: usize,
    block_index: usize,
    block_start: usize,
    block_end: usize,
    trace_row: usize,
    trace_state: u16,
    final_score: f64,
    final_state: u16,
    total_units: u64,
}
impl<'g, 'd, 'w> Decoder<'g, 'd, 'w> {
    pub fn new(
        graph: Graph<'g>,
        densities: &'d [[f64; 3]],
        block: usize,
        mode: Mode,
        workspace: &'w mut Workspace,
    ) -> Result<Self, Error> {
        graph.validate()?;
        let plan = StoragePlan::new(graph.classes.len(), densities.len(), block, mode)?;
        if densities
            .iter()
            .flatten()
            .any(|&v| v != f64::NEG_INFINITY && !finite_log(v))
        {
            return Err(Error::InvalidDensity);
        }
        if !workspace.fits(plan) {
            return Err(Error::Capacity);
        }
        let empty = densities.is_empty();
        Ok(Self {
            graph,
            densities,
            workspace,
            mode,
            block,
            phase: if empty {
                Phase::Terminal
            } else {
                Phase::Initialize
            },
            status: if empty {
                Status::NoEvidence
            } else {
                Status::Running
            },
            a_previous: true,
            row: 0,
            state: 0,
            edge: 0,
            started: false,
            best: f64::NEG_INFINITY,
            parent: u16::MAX,
            copy: 0,
            block_index: 0,
            block_start: 0,
            block_end: 0,
            trace_row: 0,
            trace_state: 0,
            final_score: f64::NEG_INFINITY,
            final_state: 0,
            total_units: 0,
        })
    }
    pub fn status(&self) -> Status {
        self.status
    }
    pub fn phase(&self) -> Phase {
        self.phase
    }
    pub fn total_units(&self) -> u64 {
        self.total_units
    }
    pub fn result(&self) -> Option<ResultView<'_>> {
        matches!(self.status, Status::Scored | Status::Decoded).then(|| ResultView {
            score: self.final_score,
            final_state: self.final_state,
            path: (self.status == Status::Decoded)
                .then(|| &self.workspace.path[..self.densities.len()]),
        })
    }
    pub fn cancel(&mut self) {
        if self.status == Status::Running {
            self.stop(Status::Cancelled);
        }
    }
    fn stop(&mut self, status: Status) {
        self.status = status;
        self.phase = Phase::Terminal;
    }
    fn previous(&self, index: usize) -> f64 {
        if self.a_previous {
            self.workspace.a[index]
        } else {
            self.workspace.b[index]
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
            if let Err(error) = self.tick() {
                self.stop(Status::Failed);
                return Err(error);
            }
            units += 1;
            self.total_units += 1;
        }
        Ok(StepReport {
            units,
            status: self.status,
        })
    }
    fn tick(&mut self) -> Result<(), Error> {
        let s = self.graph.classes.len();
        let rows = self.densities.len();
        match self.phase {
            Phase::Initialize => {
                if self.copy < s {
                    self.workspace.a[self.copy] = self.graph.initial_log_probability;
                    self.copy += 1;
                } else {
                    self.copy = 0;
                    self.phase = if self.mode == Mode::Trace {
                        Phase::SaveCheckpoint
                    } else {
                        Phase::Forward
                    };
                }
            }
            Phase::SaveCheckpoint => {
                if self.copy < s {
                    self.workspace.checkpoints[self.block_index * s + self.copy] =
                        self.previous(self.copy);
                    self.copy += 1;
                } else {
                    self.phase = Phase::Forward;
                    self.copy = 0;
                }
            }
            Phase::Forward | Phase::Replay => {
                let replay = self.phase == Phase::Replay;
                if self.state == s {
                    self.a_previous = !self.a_previous;
                    self.row += 1;
                    self.state = 0;
                    if replay && self.row == self.block_end {
                        self.phase = Phase::VerifyReplay;
                    } else if !replay && self.row == rows {
                        self.phase = Phase::SelectFinal;
                    } else if !replay && self.mode == Mode::Trace && self.row % self.block == 0 {
                        self.block_index = self.row / self.block;
                        self.phase = Phase::SaveCheckpoint;
                    }
                } else if !self.started {
                    self.best = f64::NEG_INFINITY;
                    self.parent = u16::MAX;
                    self.edge = self.graph.pointers[self.state] as usize;
                    self.started = true;
                } else if self.edge < self.graph.pointers[self.state + 1] as usize {
                    let p = self.graph.previous[self.edge];
                    let value = (self.previous(p as usize)
                        + self.graph.log_probabilities[self.edge])
                        + self.densities[self.row][self.graph.classes[self.state] as usize];
                    if value > self.best {
                        self.best = value;
                        self.parent = p;
                    }
                    self.edge += 1;
                } else {
                    if self.a_previous {
                        self.workspace.b[self.state] = self.best;
                    } else {
                        self.workspace.a[self.state] = self.best;
                    }
                    if replay {
                        self.workspace.parents[(self.row - self.block_start) * s + self.state] =
                            self.parent;
                    }
                    self.state += 1;
                    self.started = false;
                }
            }
            Phase::SelectFinal => {
                if self.state < s {
                    let value = self.previous(self.state);
                    if value > self.final_score {
                        self.final_score = value;
                        self.final_state = self.state as u16;
                    }
                    self.state += 1;
                } else if !self.final_score.is_finite() {
                    self.stop(Status::NoPath);
                } else if self.mode == Mode::Score {
                    self.stop(Status::Scored);
                } else {
                    self.trace_state = self.final_state;
                    self.block_index = (rows - 1) / self.block;
                    self.copy = 0;
                    self.phase = Phase::Restore;
                }
            }
            Phase::Restore => {
                if self.copy < s {
                    self.workspace.a[self.copy] =
                        self.workspace.checkpoints[self.block_index * s + self.copy];
                    self.copy += 1;
                } else {
                    self.block_start = self.block_index * self.block;
                    self.block_end = (self.block_start + self.block).min(rows);
                    self.row = self.block_start;
                    self.state = 0;
                    self.started = false;
                    self.a_previous = true;
                    self.phase = Phase::Replay;
                }
            }
            Phase::VerifyReplay => {
                let expected = if self.block_end == rows {
                    self.final_score
                } else {
                    self.workspace.checkpoints
                        [(self.block_index + 1) * s + self.trace_state as usize]
                };
                if self.previous(self.trace_state as usize).to_bits() != expected.to_bits() {
                    return Err(Error::ReplayMismatch);
                }
                self.trace_row = self.block_end - 1;
                self.phase = Phase::Traceback;
            }
            Phase::Traceback => {
                self.workspace.path[self.trace_row] = self.trace_state;
                if self.trace_row != 0 {
                    let p = self.workspace.parents
                        [(self.trace_row - self.block_start) * s + self.trace_state as usize];
                    if p as usize >= s {
                        return Err(Error::ReplayMismatch);
                    }
                    self.trace_state = p;
                }
                if self.trace_row == self.block_start {
                    if self.block_start == 0 {
                        self.stop(Status::Decoded);
                    } else {
                        self.block_index -= 1;
                        self.copy = 0;
                        self.phase = Phase::Restore;
                    }
                } else {
                    self.trace_row -= 1;
                }
            }
            Phase::Terminal => {}
        }
        Ok(())
    }
}

/// Frozen public graph fixtures and deterministic artificial densities, host experiment only.
pub mod synthetic {
    use super::*;
    use sha2::{Digest, Sha256};
    // include_bytes paths are fixed at compile time; there is no input-file CLI.
    const P3: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm3-pointers.u32le"
    ));
    const E3: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm3-previous.u16le"
    ));
    const L3: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm3-logp.f64le"
    ));
    const M3: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm3-state-map.u8"
    ));
    const P4: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm4-pointers.u32le"
    ));
    const E4: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm4-previous.u16le"
    ));
    const L4: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm4-logp.f64le"
    ));
    const M4: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-compact-cs02/assets/hmm4-state-map.u8"
    ));
    pub struct FrozenGraph {
        pointers: Vec<u32>,
        previous: Vec<u16>,
        logp: Vec<f64>,
        classes: Vec<u8>,
        prior: f64,
    }
    impl FrozenGraph {
        pub fn graph(&self) -> Graph<'_> {
            Graph {
                pointers: &self.pointers,
                previous: &self.previous,
                log_probabilities: &self.logp,
                classes: &self.classes,
                initial_log_probability: self.prior,
            }
        }
    }
    pub fn load(meter: u8) -> Result<FrozenGraph, Error> {
        let (p, e, l, m, prior) = match meter {
            3 => (P3, E3, L3, M3, 0xc0221356bc07601a),
            4 => (P4, E4, L4, M4, 0xc022a6a1cc9106f7),
            _ => return Err(Error::InvalidGraph),
        };
        if p.len() % 4 != 0 || e.len() % 2 != 0 || l.len() % 8 != 0 || m.len() % 3 != 0 {
            return Err(Error::InvalidGraph);
        }
        let graph = FrozenGraph {
            pointers: p
                .chunks_exact(4)
                .map(|v| u32::from_le_bytes(v.try_into().expect("four bytes")))
                .collect(),
            previous: e
                .chunks_exact(2)
                .map(|v| u16::from_le_bytes(v.try_into().expect("two bytes")))
                .collect(),
            logp: l
                .chunks_exact(8)
                .map(|v| f64::from_le_bytes(v.try_into().expect("eight bytes")))
                .collect(),
            classes: m
                .chunks_exact(3)
                .map(|v| {
                    if 16 * (v[2] as u16) < v[1] as u16 {
                        if v[0] == 0 {
                            2
                        } else {
                            1
                        }
                    } else {
                        0
                    }
                })
                .collect(),
            prior: f64::from_bits(prior),
        };
        graph.graph().validate()?;
        Ok(graph)
    }
    pub fn asset_hash() -> String {
        let mut hash = Sha256::new();
        for data in [L3, P3, E3, M3, L4, P4, E4, M4] {
            hash.update(data);
        }
        format!("{:x}", hash.finalize())
    }
    pub fn densities(rows: usize, meter: u8) -> Result<Vec<[f64; 3]>, Error> {
        if rows > MAX_ROWS || ![3, 4].contains(&meter) {
            return Err(Error::Capacity);
        }
        Ok((0..rows)
            .map(|r| {
                let p: [f32; 3] = if r % (25 * meter as usize) == 0 {
                    [0.02, 0.96, 0.02]
                } else if r % 25 == 0 {
                    [0.96, 0.02, 0.02]
                } else {
                    [0.01, 0.01, 0.98]
                };
                [
                    (f64::from(p[2]) / 15.0).ln(),
                    f64::from(p[0]).ln(),
                    f64::from(p[1]).ln(),
                ]
            })
            .collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn changed_checkpoint_refuses_publication() {
        let graph = Graph {
            pointers: &[0, 1],
            previous: &[0],
            log_probabilities: &[0.0],
            classes: &[0],
            initial_log_probability: 0.0,
        };
        let mut work =
            Workspace::allocate(StoragePlan::new(1, 3, 2, Mode::Trace).unwrap()).unwrap();
        let mut d = Decoder::new(graph, &[[-0.5; 3]; 3], 2, Mode::Trace, &mut work).unwrap();
        let cancel = AtomicBool::new(false);
        while d.phase() != Phase::Restore {
            d.step(1, &cancel).unwrap();
        }
        d.workspace.checkpoints[1] = -2.0;
        let error = d.step(usize::MAX, &cancel).unwrap_err();
        assert_eq!(error, Error::ReplayMismatch);
        assert_eq!(d.status(), Status::Failed);
        assert!(d.result().is_none());
    }
}
