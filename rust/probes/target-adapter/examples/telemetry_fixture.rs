// SPDX-License-Identifier: Apache-2.0
//! Explicitly synthetic formatter/parser interoperability fixture, no device clock.
use libapta_resource_probe_core as p;
use libapta_target_probe_adapter as a;
use std::{io::Write, sync::atomic::AtomicBool};
struct Fake(u64);
impl a::Clock for Fake {
    fn hz(&self) -> u64 {
        1000000
    }
    fn ticks(&mut self) -> u64 {
        let t = self.0;
        self.0 += 1;
        t
    }
}
fn main() {
    let mode = std::env::args().nth(1).expect("--manifest or --log");
    if mode == "--manifest" {
        println!("{{\"schema\":1,\"identity\":{{\"run_id\":\"synthetic-run\",\"unit_id\":\"synthetic-unit\",\"board\":\"JC1060P470C_I_W_Y\",\"image_id\":\"synthetic-image\",\"elf_sha256\":\"{}\",\"source_revision\":\"{}\",\"protocol_sha256\":\"{}\",\"chip_revision\":302,\"evidence_kind\":\"synthetic\"}},\"clock_hz\":1000000,\"counter_bits\":64,\"observations\":[{{\"id\":\"trial-0\",\"case\":\"TP-03\",\"variant\":\"projection32\",\"trial\":0,\"phase\":\"whole_trial\",\"max_elapsed_ms\":10000}}]}}","a".repeat(64),"b".repeat(40),"c".repeat(64));
        return;
    }
    assert_eq!(mode, "--log");
    let id = a::Identity {
        run_id: "synthetic-run",
        unit_id: "synthetic-unit",
        image_id: "synthetic-image",
        elf_sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        source_revision: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        protocol_sha256: "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        chip_revision: 302,
        kind: a::EvidenceKind::Synthetic,
    };
    let x = (0..p::INPUTS)
        .map(p::input_value::<f32>)
        .collect::<Vec<_>>();
    let w = (0..p::WEIGHTS)
        .map(p::weight_value::<f32>)
        .collect::<Vec<_>>();
    let mut out = vec![0f32; p::OUTPUTS];
    let mut clock = Fake(0);
    let mut t = a::Trial::new(
        p::Projection::new(&x, &w, &mut out).unwrap(),
        &mut clock,
        10000000,
    )
    .unwrap();
    while t.status() == a::Status::Running {
        t.step(32, &AtomicBool::new(false));
    }
    assert!(t.complete_job().is_some());
    let s = t.stats();
    let mut frame = [0u8; 8192];
    let obs = a::Observation {
        id: "trial-0",
        case: "TP-03",
        variant: "projection32",
        phase: "whole_trial",
        trial: 0,
        sequence: 0,
        status: a::ObservationStatus::Measured,
        start: s.start,
        end: s.end,
        elapsed: s.end - s.start,
        hz: s.hz,
        result_sha256: None,
    };
    let n = a::encode_observation(&id, &obs, &mut frame).unwrap();
    std::io::stdout().write_all(&frame[..n]).unwrap();
    let n = a::encode_end(&id, 1, &mut frame).unwrap();
    std::io::stdout().write_all(&frame[..n]).unwrap();
}
