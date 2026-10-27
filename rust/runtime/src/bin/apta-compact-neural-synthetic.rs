// SPDX-License-Identifier: Apache-2.0
//! Fixed HS-05 artificial workloads. No model, file or caller input arguments.
#![forbid(unsafe_code)]
use libapta_runtime::compact_neural::{synthetic as fx, *};
use sha2::{Digest, Sha256};
use std::io::{self, Write};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use std::time::{Duration, Instant};
fn row(writer: &mut impl Write, index: u16, r: u32, trace: [f32; 11]) -> io::Result<()> {
    let mut raw = [0u8; 51];
    raw[0] = 2;
    raw[1..3].copy_from_slice(&index.to_le_bytes());
    raw[3..7].copy_from_slice(&r.to_le_bytes());
    for (j, x) in trace.iter().enumerate() {
        raw[7 + 4 * j..11 + 4 * j].copy_from_slice(&x.to_le_bytes());
    }
    writer.write_all(&raw)
}
fn run(
    writer: &mut impl Write,
    c: fx::Case,
    index: u16,
    cancel_case: bool,
) -> Result<String, Box<dyn std::error::Error>> {
    let mut end_clock = None;
    let observation = {
        let t = Instant::now();
        let mut d = fx::Driver::new(c);
        let construction = t.elapsed().as_secs_f64();
        let flag = Arc::new(AtomicBool::new(false));
        let clock = Arc::new(Mutex::new(None));
        let requester = if cancel_case {
            let f = flag.clone();
            let cl = clock.clone();
            Some(std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(10));
                *cl.lock().unwrap() = Some(Instant::now());
                f.store(true, Ordering::Release);
            }))
        } else {
            None
        };
        let start = Instant::now();
        let mut steps = 0.;
        let mut max_step: f64 = 0.;
        let mut pipe_seconds = 0.;
        let returned = loop {
            if d.session.status() != Status::Running {
                break Instant::now();
            }
            let r = d.step(&flag);
            let returned = Instant::now();
            steps += d.last_step_seconds;
            max_step = max_step.max(d.last_step_seconds);
            if let Some(e) = r.error {
                return Err(format!("fixed row refused {e:?}").into());
            }
            if r.produced_rows > 0 {
                let t = Instant::now();
                row(writer, index, r.first_output_row as u32, d.trace())?;
                pipe_seconds += t.elapsed().as_secs_f64();
            }
            if d.session.status() != Status::Running {
                break returned;
            }
        };
        let elapsed = start.elapsed().as_secs_f64();
        let mut latency = 0.;
        if let Some(requester) = requester {
            requester.join().map_err(|_| "requester failed")?;
            let request = clock
                .lock()
                .map_err(|_| "poisoned clock")?
                .ok_or("request missing")?;
            latency = returned
                .checked_duration_since(request)
                .ok_or("completed before request")?
                .as_secs_f64();
            if d.session.status() != Status::Cancelled || latency > 0.05 {
                return Err("cancel fence failed".into());
            }
            flag.store(false, Ordering::Release);
            let p = d.session.progress();
            let state = d.session.state_snapshot();
            let r = d.step(&flag);
            if r.units != 0
                || r.consumed_rows != 0
                || r.produced_rows != 0
                || d.session.progress() != p
                || d.session.state_snapshot() != state
            {
                return Err("cancel resumed".into());
            }
        }
        let p = d.session.progress();
        let state_bits = d.session.state_snapshot().map(f32::to_bits);
        let (input, trace) = d.hashes();
        let reset_seconds = d.reset_seconds;
        let phase = p
            .cancellation_phase
            .map_or("null".into(), |p| format!("\"{p:?}\""));
        let cert = !cancel_case;
        let text=format!("{{\"id\":\"{}\",\"status\":\"{:?}\",\"accepted_rows\":{},\"committed_rows\":{},\"macs\":{},\"step_units\":{},\"reset_units\":{},\"total_units\":{},\"reset_count\":{},\"input_sha256\":\"{input}\",\"trace_sha256\":\"{trace}\",\"construction_seconds\":{construction},\"step_seconds\":{steps},\"max_step_seconds\":{max_step},\"reset_seconds\":{reset_seconds},\"pipe_write_seconds\":{pipe_seconds},\"driver_wall_seconds\":{elapsed},\"cancel_seconds\":{latency},\"phase_at_cancel\":{phase},\"final_state_bits\":{state_bits:?},\"final_certificate\":{cert}}}",c.name,d.session.status(),p.accepted_rows,p.committed_rows,p.macs,p.step_units,p.reset_units,p.total_units,p.reset_count);
        if cancel_case {
            end_clock = Some(Instant::now());
        }
        text
    };
    let drop_seconds = end_clock.map_or(0., |t| t.elapsed().as_secs_f64());
    Ok(format!(
        "{{\"stream\":{observation},\"scope_end_seconds\":{drop_seconds}}}"
    ))
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 2 || args[0] != "--case" {
        return Err("only fixed --case NAME accepted".into());
    }
    let name = &args[1];
    if name != "fixtures" && name != "cancel" && fx::full(name).is_none() {
        return Err("unknown case".into());
    }
    let mut writer = io::stdout().lock();
    writer.write_all(b"HS05BIN1")?;
    let mut probes = Vec::new();
    let mut observations = Vec::new();
    if name == "fixtures" {
        for i in 0..fx::PROBE_COUNT {
            let mut p = ProbeSession::new(i)?;
            let flag = AtomicBool::new(false);
            while p.status() == Status::Running {
                let r = p.step(17, &flag);
                if let Some(e) = r.error {
                    return Err(e.into());
                }
            }
            let result = p.result().ok_or("probe missing")?;
            let progress = p.progress();
            let mut bytes = [0u8; 149];
            bytes[0] = 1;
            bytes[1..3].copy_from_slice(&(i as u16).to_le_bytes());
            bytes[3..5].copy_from_slice(&u16::from(result.len).to_le_bytes());
            for j in 0..usize::from(result.len) {
                bytes[5 + 4 * j..9 + 4 * j].copy_from_slice(&result.values[j].to_le_bytes());
            }
            writer.write_all(&bytes[..5 + 4 * usize::from(result.len)])?;
            probes.push(format!(
                "{{\"index\":{i},\"units\":{},\"macs\":{}}}",
                progress.units, progress.macs
            ));
        }
        for i in 0..fx::FIXTURE_COUNT {
            observations.push(run(&mut writer, fx::fixture(i), i as u16, false)?);
        }
    } else {
        observations.push(run(
            &mut writer,
            fx::full(if name == "cancel" { "long-carry" } else { name }).unwrap(),
            0,
            name == "cancel",
        )?);
    }
    let protocol = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/PRODUCT-HOST-NEURAL-PROTOCOL.json"
    ));
    let fixtures = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-host-neural-hs05/cases.json"
    ));
    let ph = format!("{:x}", Sha256::digest(protocol));
    let fh = format!("{:x}", Sha256::digest(fixtures));
    let size = std::mem::size_of::<Session>();
    let probe_size = std::mem::size_of::<ProbeSession>();
    let driver = std::mem::size_of::<fx::Driver>();
    let metadata=format!("{{\"case\":\"{name}\",\"status\":\"COMPLETE\",\"protocol_sha256\":\"{ph}\",\"fixture_sha256\":\"{fh}\",\"session_value_bytes\":{size},\"probe_value_bytes\":{probe_size},\"driver_value_bytes\":{driver},\"profile_bytes\":{PROFILE_BYTES},\"input_buffer_bytes\":768,\"output_buffer_bytes\":12,\"snapshot_bytes\":32,\"probes\":[{}],\"observations\":[{}]}}",probes.join(","),observations.join(","));
    if metadata.len() > 16384 || size > 4096 || probe_size > 4096 {
        return Err("storage/trailer bound".into());
    }
    writer.write_all(&[3])?;
    writer.write_all(&(metadata.len() as u32).to_le_bytes())?;
    writer.write_all(metadata.as_bytes())?;
    writer.flush()?;
    Ok(())
}
