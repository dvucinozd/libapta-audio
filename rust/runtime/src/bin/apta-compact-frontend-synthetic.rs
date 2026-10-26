// SPDX-License-Identifier: Apache-2.0
//! HS-04 fixed synthetic binary transport; no arbitrary source/file input.
#![forbid(unsafe_code)]
use libapta_runtime::compact_frontend::{synthetic as fx, *};
use sha2::{Digest, Sha256};
use std::io::{self, Write};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use std::time::{Duration, Instant};
fn row(writer: &mut impl Write, index: u16, r: u32, values: &[f32; FEATURES]) -> io::Result<()> {
    let mut bytes = [0u8; 1159];
    bytes[0] = 2;
    bytes[1..3].copy_from_slice(&index.to_le_bytes());
    bytes[3..7].copy_from_slice(&r.to_le_bytes());
    for (j, x) in values.iter().enumerate() {
        bytes[7 + 4 * j..11 + 4 * j].copy_from_slice(&x.to_le_bytes());
    }
    writer.write_all(&bytes)
}
fn run(
    writer: &mut impl Write,
    c: fx::Case,
    index: u16,
    cancel_case: bool,
) -> Result<String, Box<dyn std::error::Error>> {
    let mut drop_clock = None;
    let observation = {
        let t = Instant::now();
        let mut d = fx::Driver::new(c);
        let construction = t.elapsed().as_secs_f64();
        let flag = Arc::new(AtomicBool::new(false));
        let request_clock = Arc::new(Mutex::new(None));
        let requester = if cancel_case {
            let f = flag.clone();
            let clock = request_clock.clone();
            Some(std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(10));
                *clock.lock().unwrap() = Some(Instant::now());
                f.store(true, Ordering::Release);
            }))
        } else {
            None
        };
        let start = Instant::now();
        let mut step_seconds = 0.0;
        let mut max_step: f64 = 0.0;
        let mut pipe_seconds = 0.0;
        let returned = loop {
            if d.session.status() != Status::Running {
                break Instant::now();
            }
            d.prepare();
            let r = d.step(&flag);
            let returned = Instant::now();
            step_seconds += d.last_step_seconds;
            max_step = max_step.max(d.last_step_seconds);
            if r.error.is_some() {
                return Err(format!("fixed input refused {:?}", r.error).into());
            }
            if r.produced_rows > 0 {
                let t = Instant::now();
                row(writer, index, r.first_row as u32, &d.output[0])?;
                pipe_seconds += t.elapsed().as_secs_f64();
            }
            if d.session.status() != Status::Running {
                break returned;
            }
        };
        let elapsed = start.elapsed().as_secs_f64();
        let mut cancel_seconds = 0.0;
        if let Some(requester) = requester {
            requester.join().map_err(|_| "requester failure")?;
            let request = request_clock
                .lock()
                .map_err(|_| "clock poisoned")?
                .ok_or("no request")?;
            cancel_seconds = returned
                .checked_duration_since(request)
                .ok_or("completed before cancellation")?
                .as_secs_f64();
            if d.session.status() != Status::Cancelled || cancel_seconds > 0.05 {
                return Err("cancel fence failed".into());
            }
            flag.store(false, Ordering::Release);
            let p = d.session.progress();
            let r = d.step(&flag);
            if r.units != 0
                || r.consumed_samples != 0
                || r.produced_rows != 0
                || d.session.progress() != p
            {
                return Err("cancel resumed".into());
            }
        }
        let p = d.session.progress();
        let (input, output) = d.hashes();
        let certificate = !cancel_case;
        let phase = p
            .cancellation_phase
            .map_or("null".into(), |p| format!("\"{p:?}\""));
        let text=format!("{{\"id\":\"{}\",\"status\":\"{:?}\",\"consumed_samples\":{},\"produced_rows\":{},\"macs\":{},\"units\":{},\"input_sha256\":\"{input}\",\"output_sha256\":\"{output}\",\"construction_seconds\":{construction},\"step_seconds\":{step_seconds},\"max_step_seconds\":{max_step},\"pipe_write_seconds\":{pipe_seconds},\"driver_wall_seconds\":{elapsed},\"cancel_seconds\":{cancel_seconds},\"phase_at_cancel\":{phase},\"final_certificate\":{certificate}}}",c.name,d.session.status(),p.consumed_samples,p.produced_rows,p.macs,p.units);
        if cancel_case {
            drop_clock = Some(Instant::now());
        }
        text
    };
    let drop_seconds = drop_clock.map_or(0.0, |t| t.elapsed().as_secs_f64());
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
    writer.write_all(b"HS04BIN1")?;
    let mut observations = Vec::new();
    let mut primitive_units = Vec::new();
    if name == "fixtures" {
        for i in 0..fx::PRIMITIVE_COUNT {
            let (_, input) = fx::primitive(i);
            let mut fft = Transform::new();
            let flag = AtomicBool::new(false);
            while !fft.done() {
                fft.step(4096, &input, &flag);
            }
            writer.write_all(&[1])?;
            writer.write_all(&(i as u16).to_le_bytes())?;
            writer.write_all(&(SIZE as u16).to_le_bytes())?;
            for z in fft.values() {
                writer.write_all(&z.re.to_le_bytes())?;
                writer.write_all(&z.im.to_le_bytes())?;
            }
            primitive_units.push(format!(
                "{{\"units\":{},\"macs\":{}}}",
                fft.units(),
                fft.macs()
            ));
        }
        for i in 0..fx::FIXTURE_COUNT {
            observations.push(run(&mut writer, fx::fixture(i), i as u16, false)?);
        }
    } else {
        let c = fx::full(if name == "cancel" {
            "fullpulse441-48000"
        } else {
            name
        })
        .unwrap();
        observations.push(run(&mut writer, c, 0, name == "cancel")?);
    }
    let protocol = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/PRODUCT-HOST-FRONTEND-PROTOCOL.json"
    ));
    let fixtures = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-host-frontend-hs04/cases.json"
    ));
    let protocol_hash = format!("{:x}", Sha256::digest(protocol));
    let fixture_hash = format!("{:x}", Sha256::digest(fixtures));
    let size = std::mem::size_of::<Session>();
    let driver = std::mem::size_of::<fx::Driver>();
    let metadata=format!("{{\"case\":\"{name}\",\"status\":\"COMPLETE\",\"protocol_sha256\":\"{protocol_hash}\",\"fixture_sha256\":\"{fixture_hash}\",\"session_value_bytes\":{size},\"driver_value_bytes\":{driver},\"table_bytes\":{TABLE_BYTES},\"input_buffer_bytes\":2048,\"output_buffer_bytes\":1152,\"primitive_work\":[{}],\"observations\":[{}]}}",primitive_units.join(","),observations.join(","));
    if metadata.len() > 16384 || size > 49152 {
        return Err("metadata/storage bound".into());
    }
    writer.write_all(&[3])?;
    writer.write_all(&(metadata.len() as u32).to_le_bytes())?;
    writer.write_all(metadata.as_bytes())?;
    writer.flush()?;
    Ok(())
}
