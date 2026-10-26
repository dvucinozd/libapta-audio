// SPDX-License-Identifier: Apache-2.0
//! Fixed generated HS-03 workloads; no arbitrary PCM/model/file inputs.
#![forbid(unsafe_code)]
use libapta_runtime::compact_resampler::{synthetic as fx, *};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use std::time::{Duration, Instant};
fn run(c: fx::Case) -> String {
    let t = Instant::now();
    let mut d = fx::Driver::new(c, 512, 256, 4096);
    let construction = t.elapsed().as_secs_f64();
    let flag = AtomicBool::new(false);
    let start = Instant::now();
    let mut steps = 0.0;
    let mut max_step: f64 = 0.0;
    while d.session.status() == Status::Running {
        let t = Instant::now();
        let report = d.step(&flag);
        let dt = t.elapsed().as_secs_f64();
        steps += dt;
        max_step = max_step.max(dt);
        assert!(
            report.error.is_none(),
            "fixed input refused {:?}",
            report.error
        );
    }
    let elapsed = start.elapsed().as_secs_f64();
    let p = d.session.progress();
    let (input, output) = d.hashes();
    let first = p
        .source_frames_before_first_output
        .map_or("null".into(), |x| x.to_string());
    let low = d.minimum.map_or("null".into(), |x| x.to_string());
    let high = d.maximum.map_or("null".into(), |x| x.to_string());
    format!("{{\"id\":\"{}\",\"status\":\"{:?}\",\"source_frames\":{},\"output_count\":{},\"macs\":{},\"units\":{},\"input_sha256\":\"{input}\",\"output_sha256\":\"{output}\",\"first_output_source_frames\":{first},\"minimum\":{low},\"maximum\":{high},\"construction_seconds\":{construction},\"step_seconds\":{steps},\"driver_wall_seconds\":{elapsed},\"max_step_seconds\":{max_step},\"final_certificate\":true}}",c.name,d.session.status(),p.source_frames,p.output_samples,p.macs,p.units)
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 2 || args[0] != "--case" {
        return Err("only fixed --case NAME accepted".into());
    }
    let name = &args[1];
    let ph = fx::protocol_hash();
    let fh = fx::fixture_hash();
    let ah = fx::asset_hash();
    let observations = if name == "fixtures" {
        (0..fx::FIXTURE_COUNT)
            .map(|i| run(fx::fixture(i)))
            .collect::<Vec<_>>()
            .join(",")
    } else if name == "cancel" {
        let (p, latency, phase, input, output, t) = {
            let mut d = fx::Driver::new(fx::full("full48000-stereo").unwrap(), 512, 256, 4096);
            let flag = Arc::new(AtomicBool::new(false));
            let clock = Arc::new(Mutex::new(None));
            let (f, c) = (flag.clone(), clock.clone());
            let requester = std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(10));
                *c.lock().expect("clock") = Some(Instant::now());
                f.store(true, Ordering::Release);
            });
            let returned = loop {
                d.step(&flag);
                if d.session.status() != Status::Running {
                    break Instant::now();
                }
            };
            requester.join().map_err(|_| "requester failure")?;
            let request = clock
                .lock()
                .map_err(|_| "clock poisoned")?
                .ok_or("no request")?;
            let latency = returned
                .checked_duration_since(request)
                .ok_or("completed before cancel")?
                .as_secs_f64();
            if d.session.status() != Status::Cancelled || latency > 0.05 {
                return Err("host cancel fence gate".into());
            }
            let p = d.session.progress();
            flag.store(false, Ordering::Release);
            let report = d.step(&flag);
            if d.session.status() != Status::Cancelled
                || report.produced != 0
                || report.consumed_frames != 0
            {
                return Err("cancel resumed".into());
            }
            let phase = p.cancellation_phase.ok_or("phase missing")?;
            let (input, output) = d.hashes();
            (p, latency, phase, input, output, Instant::now())
        };
        let cleanup = t.elapsed().as_secs_f64();
        format!("{{\"id\":\"cancel\",\"status\":\"Cancelled\",\"source_frames\":{},\"output_count\":{},\"macs\":{},\"units\":{},\"phase_at_cancel\":\"{phase:?}\",\"cancel_seconds\":{latency},\"drop_seconds\":{cleanup},\"input_sha256\":\"{input}\",\"output_sha256\":\"{output}\",\"final_certificate\":false}}",p.source_frames,p.output_samples,p.macs,p.units)
    } else {
        run(fx::full(name).ok_or("unknown fixed case")?)
    };
    let size = std::mem::size_of::<Session>();
    let driver = std::mem::size_of::<fx::Driver>();
    println!("{{\"case\":\"{name}\",\"status\":\"COMPLETE\",\"protocol_sha256\":\"{ph}\",\"fixture_sha256\":\"{fh}\",\"asset_sha256\":\"{ah}\",\"session_value_bytes\":{size},\"driver_value_bytes\":{driver},\"ring_bytes\":260,\"table_bytes\":76960,\"input_buffer_bytes\":4096,\"output_buffer_bytes\":1024,\"observations\":[{observations}]}}");
    Ok(())
}
