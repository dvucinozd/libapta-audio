// SPDX-License-Identifier: Apache-2.0
//! PF-01 accounting probe, not allocation, inference or physical qualification.
//! Host: cargo run -p libapta --locked --example product_budget_probe
//! A no_std library compilation can expose PRODUCT_LAYOUT in target LLVM IR.
#![cfg_attr(target_os = "none", no_std)]

use core::mem::size_of;
use libapta::{
    analysis::OnsetBin, owned_result::OwnedResult, publication::ResultPool, session::Session,
    waveform::NormalizedSample, Beat, FrameRange, GridSegment, MeterSegment, TempoCandidate,
    WaveformColumn, WaveformSpan,
};

pub const LAYOUT_NAMES: [&str; 13] = [
    "usize",
    "waveform_column",
    "normalized_sample",
    "onset_bin",
    "beat",
    "grid_segment",
    "waveform_span",
    "frame_range",
    "tempo_candidate",
    "meter_segment",
    "owned_result_header",
    "session_control",
    "result_pool_control",
];

/// Compiler-derived type sizes for this exact target; no device execution.
pub static PRODUCT_LAYOUT: [u64; 13] = [
    size_of::<usize>() as u64,
    size_of::<WaveformColumn>() as u64,
    size_of::<NormalizedSample>() as u64,
    size_of::<OnsetBin>() as u64,
    size_of::<Beat>() as u64,
    size_of::<GridSegment>() as u64,
    size_of::<WaveformSpan>() as u64,
    size_of::<FrameRange>() as u64,
    size_of::<TempoCandidate>() as u64,
    size_of::<MeterSegment>() as u64,
    size_of::<OwnedResult<'static>>() as u64,
    size_of::<Session<'static>>() as u64,
    size_of::<ResultPool<'static>>() as u64,
];

#[cfg(not(target_os = "none"))]
fn main() -> Result<(), libapta::Error> {
    use libapta::{publication, result, session::SessionConfig, NativeLimits};

    // Existing default DSP accounting only. A future neural path needs its own
    // frontend/weights/activations/decoder/output lifetime inventory.
    let features = result::WAVEFORM_OVERVIEW
        | result::BPM
        | result::LOCAL_BEATGRID
        | result::GLOBAL_BEATGRID
        | result::METER_DOWNBEAT;
    let queue_frames = 17usize; // Retained diagnostic comparison, not product policy.
    let queue_bytes = queue_frames * size_of::<NormalizedSample>();
    println!("{{\"format\":\"libapta-product-budget-probe-1\",");
    println!("\"evidence\":\"compiler layout and canonical host planning only\",");
    println!("\"feature_mask\":{features},\"queue_frames\":{queue_frames},");
    println!("\"type_sizes\":{{");
    for (index, name) in LAYOUT_NAMES.iter().enumerate() {
        let comma = if index + 1 == LAYOUT_NAMES.len() {
            ""
        } else {
            ","
        };
        println!("\"{name}\":{}{comma}", PRODUCT_LAYOUT[index]);
    }
    println!("}},\"profiles\":[");
    for (index, (sample_rate, frames_per_column)) in
        [(44100, 256), (44100, 32768), (48000, 256), (48000, 32768)]
            .into_iter()
            .enumerate()
    {
        let config = SessionConfig {
            sample_rate,
            channel_count: 2,
            total_frames: u64::from(sample_rate) * 600,
            frames_per_column,
        };
        let plan = publication::plan_features(config, features, NativeLimits::default())?;
        let working_overview_bytes = plan.overview.working_columns * size_of::<WaveformColumn>();
        // Canonical graph accounting includes its logical header. Controls are
        // reported separately, never added blindly to this subtotal.
        let accounted_bytes = working_overview_bytes
            .checked_add(queue_bytes)
            .and_then(|n| n.checked_add(plan.optional_working_bytes))
            .and_then(|n| n.checked_add(plan.result_pool_bytes))
            .ok_or(libapta::Error::LimitExceeded)?;
        let comma = if index == 3 { "" } else { "," };
        println!(
            "{{\"sample_rate\":{sample_rate},\"source_frames\":{},\"frames_per_column\":{frames_per_column},\"working_columns\":{},\"working_overview_bytes\":{working_overview_bytes},\"queue_bytes\":{queue_bytes},\"optional_working_bytes\":{},\"result_bytes_per_slot\":{},\"two_result_slots_bytes\":{},\"accounted_bytes\":{accounted_bytes},\"tempo_bins\":{},\"global_bins\":{},\"global_beats\":{}}}{comma}",
            config.total_frames,
            plan.overview.working_columns,
            plan.optional_working_bytes,
            plan.result_slot.retained_bytes,
            plan.result_pool_bytes,
            plan.tempo_bins,
            plan.global_bins,
            plan.global_beats,
        );
    }
    println!("],\"product_readiness\":\"NOT_READY\",\"physical_test\":false}}");
    Ok(())
}
