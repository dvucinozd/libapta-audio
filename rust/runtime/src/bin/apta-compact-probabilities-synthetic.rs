// SPDX-License-Identifier: Apache-2.0
//! Fixed HS06 public inputs only. No model/file/input constructor or expected outputs.
#![forbid(unsafe_code)]
use libapta_runtime::compact_probabilities::{synthetic as fx, *};
use sha2::{Digest, Sha256};
use std::io::{self, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Instant;
fn optional(value: Option<impl std::fmt::Debug>) -> String {
    value.map_or("null".into(), |v| format!("\"{v:?}\""))
}
fn score(s: f64) -> String {
    if s == f64::NEG_INFINITY {
        "\"-Inf\"".into()
    } else {
        s.to_string()
    }
}
fn text(s: Option<&str>) -> String {
    s.map_or("null".into(), |s| format!("\"{s}\""))
}
fn row(w: &mut impl Write, index: u16, r: u32, p: [f32; 3], d: [f64; 3]) -> io::Result<()> {
    let mut raw = [0u8; 43];
    raw[0] = 1;
    raw[1..3].copy_from_slice(&index.to_le_bytes());
    raw[3..7].copy_from_slice(&r.to_le_bytes());
    for j in 0..3 {
        raw[7 + 4 * j..11 + 4 * j].copy_from_slice(&p[j].to_le_bytes());
        raw[19 + 8 * j..27 + 8 * j].copy_from_slice(&d[j].to_le_bytes());
    }
    w.write_all(&raw)
}
fn composition(w: &mut impl Write, index: u16, c: &fx::Composition) -> io::Result<String> {
    w.write_all(&[2])?;
    w.write_all(&index.to_le_bytes())?;
    w.write_all(&(c.events.len() as u16).to_le_bytes())?;
    for e in &c.events {
        w.write_all(&e.global_row.to_le_bytes())?;
        w.write_all(&[e.beat_number, e.winning_class, e.reserved[0], e.reserved[1]])?;
    }
    let summary = if let Some(s) = c.summary {
        format!("{{\"tempo_millibpm\":{},\"anchor_q32\":\"{}\",\"period_q32\":\"{}\",\"downbeat_ordinal\":{},\"beat_count\":{},\"meter_denominator\":4,\"segment_count\":1,\"explicit_beat_count\":0,\"confidence\":0,\"key_present\":false,\"calibrated_quality_present\":false,\"downbeat_whole_frame\":{},\"evidence_frames\":[{},{}],\"applicability_frames\":[0,{}]}}",
        s.tempo_millibpm,s.anchor_q32,s.period_q32,s.downbeat_ordinal,s.beat_count,s.downbeat_whole_frame,s.evidence_frames[0],s.evidence_frames[1],s.geometry.total_frames)
    } else {
        "null".into()
    };
    let wire = c.wire.as_ref().map(|b| format!("{:x}", Sha256::digest(b)));
    Ok(format!("{{\"status\":\"{}\",\"reason\":{},\"meter\":{},\"scores\":[{},{}],\"path_sha256\":{},\"native_wire_checked\":{},\"wire_sha256\":{},\"summary\":{},\"maximum_workspace_payload\":{},\"graph_numeric_payload\":{},\"event_scratch_payload\":{}}}",
        c.status,optional(c.reason),c.meter.map_or("null".into(),|m|m.to_string()),score(c.scores[0]),score(c.scores[1]),text(c.path_hash.as_deref()),c.wire.is_some(),text(wire.as_deref()),summary,c.maximum_workspace_payload,c.maximum_graph_payload,c.event_scratch_payload))
}
fn run(
    w: &mut impl Write,
    case: fx::Case,
    index: u16,
    cancel_case: bool,
) -> Result<String, Box<dyn std::error::Error>> {
    let mut probabilities = vec![[0.; 3]; case.rows];
    let mut densities = vec![[0.; 3]; case.rows];
    let capacities = [probabilities.capacity(), densities.capacity()];
    let (
        progress,
        status,
        error,
        present,
        support,
        comp,
        emit_and_composition,
        composition_seconds,
        pipe_seconds,
        construction,
        driver,
        latency,
    ) = {
        let t = Instant::now();
        let mut s = Session::new(case.geometry, &mut probabilities, &mut densities)?;
        let construction = t.elapsed().as_secs_f64();
        let flag = AtomicBool::new(false);
        let mut driver = fx::Driver::new(case);
        let mut latency = None;
        while s.status() == Status::Running {
            let quota = if cancel_case {
                31.min((686 - s.progress().units) as usize)
            } else {
                31
            };
            if cancel_case && quota == 0 {
                flag.store(true, Ordering::Release);
                let request = Instant::now();
                let r = driver.step(&mut s, 64, 31, &flag);
                let seconds = request.elapsed().as_secs_f64();
                if s.status() != Status::Cancelled
                    || r.units != 0
                    || s.result().is_some()
                    || seconds > 0.05
                {
                    return Err("pending cancellation failed".into());
                }
                latency = Some(seconds);
                flag.store(false, Ordering::Release);
                let r = s.step(
                    1,
                    InputBlock {
                        first_row: u64::MAX,
                        rows: &[],
                    },
                    &flag,
                );
                if r.units != 0 || s.status() != Status::Cancelled || s.result().is_some() {
                    return Err("cancel resumed".into());
                }
                break;
            }
            let r = driver.step(
                &mut s,
                if case.raw_is_fixture() { 7 } else { 64 },
                quota,
                &flag,
            );
            if r.error.is_some() {
                break;
            }
        }
        let progress = s.progress();
        let status = s.status();
        let error = s
            .step(
                1,
                InputBlock {
                    first_row: u64::MAX,
                    rows: &[],
                },
                &flag,
            )
            .error;
        let t = Instant::now();
        let view = s.result();
        let present = view.is_some();
        let mut support = "null".into();
        let mut comp = "null".into();
        let mut composition_seconds = 0.;
        let mut pipe_seconds = 0.;
        if let Some(ref v) = view {
            if let Some(b) = v.support {
                support = format!("[{},{}]", b.first_row, b.end_row);
            }
            let pipe_start = Instant::now();
            for r in 0..case.rows {
                row(w, index, r as u32, v.probabilities[r], v.densities[r])?;
            }
            pipe_seconds += pipe_start.elapsed().as_secs_f64();
            if case.composition {
                let compose_start = Instant::now();
                let c = fx::compose(v, 4096)?;
                composition_seconds = compose_start.elapsed().as_secs_f64();
                let pipe_start = Instant::now();
                comp = composition(w, index, &c)?;
                pipe_seconds += pipe_start.elapsed().as_secs_f64();
            }
        } else if case.composition {
            return Err("composition bridge unexpectedly failed".into());
        }
        let emit_and_composition = t.elapsed().as_secs_f64();
        (
            progress,
            status,
            error,
            present,
            support,
            comp,
            emit_and_composition,
            composition_seconds,
            pipe_seconds,
            construction,
            driver,
            latency,
        )
    };
    // The borrow ends here. Cancel/failure prefix inspection is instrumentation,
    // never a PreparedView or a product certificate.
    let mut ph = Sha256::new();
    let mut dh = Sha256::new();
    for r in 0..progress.committed_rows as usize {
        for v in probabilities[r] {
            ph.update(v.to_le_bytes());
        }
        for v in densities[r] {
            dh.update(v.to_le_bytes());
        }
    }
    let input = format!("{:x}", driver.input_hash.clone().finalize());
    let ph = format!("{:x}", ph.finalize());
    let dh = format!("{:x}", dh.finalize());
    Ok(format!("{{\"id\":\"{}\",\"sample_rate\":{},\"total_frames\":{},\"rows\":{},\"status\":\"{status:?}\",\"error\":{},\"accepted_rows\":{},\"committed_rows\":{},\"units\":{},\"result_present\":{present},\"support\":{support},\"input_sha256\":\"{input}\",\"probability_sha256\":\"{ph}\",\"density_sha256\":\"{dh}\",\"cancellation_phase\":{},\"cancel_seconds\":{},\"construction_seconds\":{construction},\"step_seconds\":{},\"max_step_seconds\":{},\"emit_and_composition_seconds\":{emit_and_composition},\"composition_seconds\":{composition_seconds},\"pipe_seconds\":{pipe_seconds},\"probability_capacity\":{},\"density_capacity\":{},\"composition\":{comp}}}",
        case.name,case.geometry.sample_rate,case.geometry.total_frames,case.rows,optional(error),progress.accepted_rows,progress.committed_rows,progress.units,optional(progress.cancellation_phase),latency.map_or("null".into(),|v|v.to_string()),driver.step_seconds,driver.max_step_seconds,capacities[0],capacities[1]))
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a: Vec<_> = std::env::args().skip(1).collect();
    if a.len() != 2
        || a[0] != "--case"
        || !["fixtures", "full-mixed", "full-no-support", "cancel"].contains(&a[1].as_str())
    {
        return Err("only --case fixtures|full-mixed|full-no-support|cancel".into());
    }
    let name = &a[1];
    let mut w = io::BufWriter::new(io::stdout().lock());
    w.write_all(b"HS06BIN1")?;
    let mut observations = Vec::new();
    if name == "fixtures" {
        for i in 0..fx::FIXTURE_COUNT {
            observations.push(run(&mut w, fx::fixture(i), i as u16, false)?);
        }
    } else {
        observations.push(run(
            &mut w,
            fx::full(if name == "cancel" { "full-mixed" } else { name }).ok_or("fixed source")?,
            0,
            name == "cancel",
        )?);
    }
    let mut selectors = Vec::new();
    if name == "fixtures" {
        for (a, b) in [
            (-10., -10.),
            (-10., -9.),
            (-9., -10.),
            (f64::NEG_INFINITY, -9.),
            (-9., f64::NEG_INFINITY),
            (f64::NEG_INFINITY, f64::NEG_INFINITY),
            (-0., 0.),
            (f64::NAN, -9.),
            (-9., f64::INFINITY),
            (1., -9.),
            (-9., f64::NAN),
        ] {
            selectors.push(match choose_meter(a, b) {
                Ok(MeterChoice::Three) => "3",
                Ok(MeterChoice::Four) => "4",
                Ok(MeterChoice::NoPath) => "\"NoPath\"",
                Err(_) => "\"InvalidScore\"",
            });
        }
    }
    let protocol = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/PRODUCT-HOST-PROBABILITY-PROTOCOL.json"
    ));
    let fixtures = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../docs/rust/fixtures/product-host-probability-hs06/cases.json"
    ));
    let size = std::mem::size_of::<Session<'static>>();
    let driver = std::mem::size_of::<fx::Driver>();
    let data=format!("{{\"case\":\"{name}\",\"status\":\"COMPLETE\",\"protocol_sha256\":\"{:x}\",\"fixture_sha256\":\"{:x}\",\"session_value_bytes\":{size},\"driver_value_bytes\":{driver},\"input_buffer_bytes\":768,\"pending_payload_bytes\":36,\"selectors\":[{}],\"observations\":[{}]}}",Sha256::digest(protocol),Sha256::digest(fixtures),selectors.join(","),observations.join(","));
    if data.len() > 65536 || size > 512 || driver > 4096 {
        return Err("registered size cap".into());
    }
    w.write_all(&[3])?;
    w.write_all(&(data.len() as u32).to_le_bytes())?;
    w.write_all(data.as_bytes())?;
    w.flush()?;
    Ok(())
}
