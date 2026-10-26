// SPDX-License-Identifier: Apache-2.0
//! HS-01 fixed generated cases only. No waveform, model or arbitrary input-file interface.
#![forbid(unsafe_code)]
use libapta_runtime::compact_hmm::{
    synthetic, Decoder, Graph, Mode, Status, StoragePlan, Workspace,
};
use sha2::{Digest, Sha256};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

const PROTOCOL: &[u8] = include_bytes!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../docs/rust/PRODUCT-HOST-HMM-PROTOCOL.json"
));
struct Run {
    score: f64,
    state: u16,
    path_hash: String,
    payload: usize,
    units: u64,
    allocation: f64,
    begin: f64,
    steps: f64,
    max_step: f64,
}

fn path_score(g: Graph<'_>, densities: &[[f64; 3]], path: &[u16]) -> Result<f64, String> {
    if path.len() != densities.len() || path.is_empty() {
        return Err("path coverage".into());
    }
    let first = path[0] as usize;
    let mut score = f64::NEG_INFINITY;
    for k in g.pointers[first] as usize..g.pointers[first + 1] as usize {
        let value = (g.initial_log_probability + g.log_probabilities[k])
            + densities[0][g.classes[first] as usize];
        if value > score {
            score = value;
        }
    }
    for row in 1..path.len() {
        let state = path[row] as usize;
        let lo = g.pointers[state] as usize;
        let hi = g.pointers[state + 1] as usize;
        let offset = g.previous[lo..hi]
            .binary_search(&path[row - 1])
            .map_err(|_| "invalid path edge")?;
        score =
            (score + g.log_probabilities[lo + offset]) + densities[row][g.classes[state] as usize];
    }
    Ok(score)
}
fn run(g: Graph<'_>, density: &[[f64; 3]], mode: Mode) -> Result<Run, Box<dyn std::error::Error>> {
    let plan = StoragePlan::new(g.classes.len(), density.len(), 256, mode)?;
    let start = Instant::now();
    let mut work = Workspace::allocate(plan)?;
    let allocation = start.elapsed().as_secs_f64();
    let payload = work.allocated_payload_bytes();
    let start = Instant::now();
    let mut decoder = Decoder::new(g, density, 256, mode, &mut work)?;
    let begin = start.elapsed().as_secs_f64();
    let cancel = AtomicBool::new(false);
    let start = Instant::now();
    let mut max_step: f64 = 0.0;
    while decoder.status() == Status::Running {
        let step = Instant::now();
        decoder.step(4096, &cancel)?;
        max_step = max_step.max(step.elapsed().as_secs_f64());
    }
    let steps = start.elapsed().as_secs_f64();
    let units = decoder.total_units();
    let result = decoder.result().ok_or("no finite result")?;
    let mut hash = Sha256::new();
    if let Some(path) = result.path {
        if path_score(g, density, path)?.to_bits() != result.score.to_bits() {
            return Err("independent path score differs".into());
        }
        for state in path {
            hash.update(state.to_le_bytes());
        }
    }
    Ok(Run {
        score: result.score,
        state: result.final_state,
        path_hash: format!("{:x}", hash.finalize()),
        payload,
        units,
        allocation,
        begin,
        steps,
        max_step,
    })
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 2 || args[0] != "--case" {
        return Err("only --case NAME is accepted".into());
    }
    let case = &args[1];
    let (rows, target) = match case.as_str() {
        "small3" => (257, 3),
        "small4" => (513, 4),
        "full3" => (30000, 3),
        "full4" | "cancel" => (30000, 4),
        _ => return Err("unknown generated case".into()),
    };
    let density = synthetic::densities(rows, target)?;
    let mut dh = Sha256::new();
    for row in &density {
        for v in row {
            dh.update(v.to_le_bytes());
        }
    }
    let density_hash = format!("{:x}", dh.finalize());
    let protocol_hash = format!("{:x}", Sha256::digest(PROTOCOL));
    let assets_hash = synthetic::asset_hash();
    let g3 = synthetic::load(3)?;
    let g4 = synthetic::load(4)?;
    if case == "cancel" {
        let plan = StoragePlan::new(g4.graph().classes.len(), rows, 256, Mode::Trace)?;
        let mut work = Workspace::allocate(plan)?;
        let payload = work.allocated_payload_bytes();
        let (units, latency) = {
            let mut d = Decoder::new(g4.graph(), &density, 256, Mode::Trace, &mut work)?;
            let flag = Arc::new(AtomicBool::new(false));
            let clock = Arc::new(Mutex::new(None));
            let (f, c) = (Arc::clone(&flag), Arc::clone(&clock));
            let requester = std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(10));
                *c.lock().expect("clock") = Some(Instant::now());
                f.store(true, Ordering::Release);
            });
            d.step(usize::MAX, &flag)?;
            let returned = Instant::now();
            requester.join().map_err(|_| "request thread failed")?;
            let request = clock
                .lock()
                .map_err(|_| "clock poisoned")?
                .ok_or("no request")?;
            let latency = returned
                .checked_duration_since(request)
                .ok_or("decoder finished before cancellation request")?
                .as_secs_f64();
            if d.status() != Status::Cancelled || d.result().is_some() || latency > 0.05 {
                return Err("host cancellation fence gate failed".into());
            }
            flag.store(false, Ordering::Release);
            d.step(1, &flag)?;
            if d.status() != Status::Cancelled || d.result().is_some() {
                return Err("cancel fence resumed".into());
            }
            (d.total_units(), latency)
        };
        let before = Instant::now();
        drop(work);
        let reclaim = before.elapsed().as_secs_f64();
        println!("{{\"case\":\"{case}\",\"status\":\"PASS\",\"rows\":{rows},\"cancel_seconds\":{latency},\"drop_seconds\":{reclaim},\"payload_bytes\":{payload},\"units\":{units},\"protocol_sha256\":\"{protocol_hash}\",\"asset_sha256\":\"{assets_hash}\",\"density_sha256\":\"{density_hash}\"}}");
        return Ok(());
    }
    let s3 = run(g3.graph(), &density, Mode::Score)?;
    let s4 = run(g4.graph(), &density, Mode::Score)?;
    let (meter, graph, selected) = if s4.score > s3.score {
        (4, g4.graph(), &s4)
    } else {
        (3, g3.graph(), &s3)
    };
    if meter != target {
        return Err("generated meter witness not recovered".into());
    }
    let traced = run(graph, &density, Mode::Trace)?;
    if traced.score.to_bits() != selected.score.to_bits() || traced.state != selected.state {
        return Err("score/trace parity failed".into());
    }
    let max_step = s3.max_step.max(s4.max_step).max(traced.max_step);
    let elapsed = s3.steps + s4.steps + traced.steps;
    let begin = s3.begin + s4.begin + traced.begin;
    let allocation = s3.allocation + s4.allocation + traced.allocation;
    let units = s3.units + s4.units + traced.units;
    println!("{{\"case\":\"{case}\",\"status\":\"PASS\",\"rows\":{rows},\"meter\":{meter},\"score_bits\":\"{:016x}\",\"path_sha256\":\"{}\",\"trace_payload_bytes\":{},\"step_seconds\":{elapsed},\"begin_seconds\":{begin},\"allocation_seconds\":{allocation},\"max_step_seconds\":{max_step},\"units\":{units},\"protocol_sha256\":\"{protocol_hash}\",\"asset_sha256\":\"{assets_hash}\",\"density_sha256\":\"{density_hash}\"}}",traced.score.to_bits(),traced.path_hash,traced.payload);
    Ok(())
}
