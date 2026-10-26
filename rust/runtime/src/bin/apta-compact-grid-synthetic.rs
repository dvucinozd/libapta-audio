// SPDX-License-Identifier: Apache-2.0
//! Fixed HS-02 generated inputs only; no file/audio/model input interface.
#![forbid(unsafe_code)]
use libapta_runtime::compact_grid::{synthetic as fx, *};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Instant;

fn run(c: fx::Case) -> String {
    let input_hash = fx::input_hash(&c);
    let mut scratch = vec![Event::default(); c.scratch_entries];
    let payload = c.probabilities.capacity() * 12
        + c.states.capacity() * 2
        + scratch.capacity() * 8
        + c.events.capacity() * 8;
    let begin = Instant::now();
    let mut s = match c.session(&mut scratch) {
        Ok(s) => s,
        Err(e) => {
            let construction = begin.elapsed().as_secs_f64();
            return format!(
                "{{\"id\":\"{}\",\"input_sha256\":\"{input_hash}\",\"outcome\":{},\"units\":0,\"payload_bytes\":{payload},\"construction_seconds\":{construction},\"step_seconds\":0,\"max_step_seconds\":0}}",
                c.name,
                fx::error_json(e)
            );
        }
    };
    let construction = begin.elapsed().as_secs_f64();
    let start = Instant::now();
    let mut max_step: f64 = 0.0;
    let flag = AtomicBool::new(false);
    while s.status() == Status::Running {
        let t = Instant::now();
        let result = s.step(4096, &flag);
        max_step = max_step.max(t.elapsed().as_secs_f64());
        if result.is_err() {
            break;
        }
    }
    let elapsed = start.elapsed().as_secs_f64();
    let (events, summary, wire_checked) = match s.result() {
        Some(ResultView::Grid { summary, events }) => {
            fx::validate_wire(summary).expect("unchanged native/wire validation");
            (
                Some(fx::event_hash(events)),
                Some(fx::summary_hash(summary)),
                true,
            )
        }
        Some(ResultView::Events(events)) => (Some(fx::event_hash(events)), None, false),
        _ => (None, None, false),
    };
    let eh = events.map_or("null".into(), |s| format!("\"{s}\""));
    let sh = summary.map_or("null".into(), |s| format!("\"{s}\""));
    let diagnostic = s.reason().map_or("null".into(), |r| format!("\"{r:?}\""));
    format!("{{\"id\":\"{}\",\"input_sha256\":\"{input_hash}\",\"outcome\":{},\"native_wire_checked\":{wire_checked},\"diagnostic_reason\":{diagnostic},\"event_sha256\":{eh},\"summary_sha256\":{sh},\"units\":{},\"payload_bytes\":{payload},\"construction_seconds\":{construction},\"step_seconds\":{elapsed},\"max_step_seconds\":{max_step}}}",c.name,fx::observation(&s),s.total_units())
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a: Vec<_> = std::env::args().skip(1).collect();
    if a.len() != 2 || a[0] != "--case" {
        return Err("only --case NAME is accepted".into());
    }
    let name = &a[1];
    let protocol = fx::protocol_hash();
    let fixture = fx::fixture_hash();
    let assets = libapta_runtime::compact_hmm::synthetic::asset_hash();
    let observations = if name == "fixtures" {
        (0..fx::FIXTURE_COUNT)
            .map(|i| run(fx::fixture(i)))
            .collect::<Vec<_>>()
            .join(",")
    } else if name == "cancel" {
        let c = fx::full("full4-48000").expect("fixed case");
        let input_hash = fx::input_hash(&c);
        let mut scratch = vec![Event::default(); MAX_EVENTS];
        let payload =
            c.probabilities.capacity() * 12 + c.states.capacity() * 2 + scratch.capacity() * 8;
        let (units, latency) = {
            let mut s = c.session(&mut scratch)?;
            let flag = AtomicBool::new(false);
            while s.phase() != Phase::CheckResiduals {
                if s.status() != Status::Running {
                    return Err("cancel checkpoint unreachable".into());
                }
                s.step(1, &flag)?;
            }
            let request = Instant::now();
            flag.store(true, Ordering::Release);
            s.step(usize::MAX, &flag)?;
            let latency = request.elapsed().as_secs_f64();
            if s.status() != Status::Cancelled || s.result().is_some() || latency > 0.05 {
                return Err("pending cancellation gate".into());
            }
            flag.store(false, Ordering::Release);
            s.step(1, &flag)?;
            if s.status() != Status::Cancelled || s.result().is_some() {
                return Err("cancelled session resumed".into());
            }
            (s.total_units(), latency)
        };
        let t = Instant::now();
        drop(scratch);
        let reclaim = t.elapsed().as_secs_f64();
        format!("{{\"id\":\"cancel\",\"input_sha256\":\"{input_hash}\",\"outcome\":{{\"status\":\"Cancelled\",\"result_exposed\":false}},\"phase_at_request\":\"CheckResiduals\",\"cancel_seconds\":{latency},\"drop_seconds\":{reclaim},\"units\":{units},\"payload_bytes\":{payload}}}")
    } else {
        run(fx::full(name).ok_or("unknown fixed case")?)
    };
    let session_bytes = std::mem::size_of::<Session<'static, 'static>>();
    let summary_bytes = std::mem::size_of::<Summary>();
    let event_bytes = std::mem::size_of::<Event>();
    println!("{{\"case\":\"{name}\",\"status\":\"COMPLETE\",\"protocol_sha256\":\"{protocol}\",\"fixture_sha256\":\"{fixture}\",\"asset_sha256\":\"{assets}\",\"session_value_size_bytes\":{session_bytes},\"summary_value_size_bytes\":{summary_bytes},\"event_size_bytes\":{event_bytes},\"observations\":[{observations}]}}");
    Ok(())
}
