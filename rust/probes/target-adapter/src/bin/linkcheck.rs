// SPDX-License-Identifier: Apache-2.0
//! Link-check only: boot initializes HAL and idles; retained probe entry is not called.
#![no_std]
#![no_main]
#![forbid(unsafe_code)]

use core::{hint::black_box, sync::atomic::AtomicBool};
use libapta_resource_probe_core as p;
use libapta_target_probe_adapter as a;
use static_cell::StaticCell;

const SELECTED: usize = cfg!(feature = "copy") as usize
    + cfg!(feature = "projection32") as usize
    + cfg!(feature = "projection64") as usize
    + cfg!(feature = "activation") as usize
    + cfg!(feature = "logs") as usize;
const _: () = assert!(SELECTED == 1, "select exactly one probe image variant");
#[cfg(feature = "copy")]
static STORAGE: StaticCell<([u32; p::COPY_WORDS], [u32; p::COPY_WORDS])> = StaticCell::new();
#[cfg(feature = "projection32")]
static STORAGE: StaticCell<([f32; p::INPUTS], [f32; p::WEIGHTS], [f32; p::OUTPUTS])> =
    StaticCell::new();
#[cfg(feature = "projection64")]
static STORAGE: StaticCell<([f64; p::INPUTS], [f64; p::WEIGHTS], [f64; p::OUTPUTS])> =
    StaticCell::new();
#[cfg(feature = "activation")]
static STORAGE: StaticCell<[f32; p::NONLINEAR_OUTPUTS]> = StaticCell::new();
#[cfg(feature = "logs")]
static STORAGE: StaticCell<[f64; p::NONLINEAR_OUTPUTS]> = StaticCell::new();

fn run<J: a::Job>(
    job: J,
    id: &a::Identity<'_>,
    out: &mut [u8],
    flag: &AtomicBool,
    case: &str,
    variant: &str,
) -> Result<usize, a::Error> {
    let mut clock = a::EspClock;
    let mut trial = a::Trial::new(job, &mut clock, 10000000)?;
    while trial.status() == a::Status::Running {
        trial.step(32, flag);
    }
    if trial.complete_job().is_none() {
        return Err(a::Error::Core);
    }
    let s = trial.stats();
    a::encode_observation(
        id,
        &a::Observation {
            id: "trial-0",
            case,
            variant,
            phase: "whole_trial",
            trial: 0,
            sequence: 0,
            status: a::ObservationStatus::Measured,
            start: s.start,
            end: s.end,
            elapsed: s.end - s.start,
            hz: s.hz,
            result_sha256: None,
        },
        out,
    )
}

// Kept through an opaque address reference. No entry/command transport is enabled.
// Static storage is single-use here: a real physical trial scheduler must own and
// reuse buffers explicitly and emit all warm-up/measured trials under its freeze.
#[inline(never)]
fn retained_trial(
    selector: u8,
    id: &a::Identity<'_>,
    out: &mut [u8],
    flag: &AtomicBool,
) -> Result<usize, a::Error> {
    id.validate()?;
    if id.kind != a::EvidenceKind::DeviceLog
        || id.chip_revision != esp_hal::efuse::chip_revision().combined()
        || id.image_id != option_env!("APTA_PROBE_IMAGE_ID").unwrap_or("UNSPECIFIED_LINKCHECK")
        || id.source_revision != option_env!("APTA_PROBE_SOURCE_REVISION").unwrap_or("UNSPECIFIED")
        || id.protocol_sha256 != option_env!("APTA_PROBE_PROTOCOL_SHA256").unwrap_or("UNSPECIFIED")
    {
        return Err(a::Error::Input);
    }
    #[cfg(feature = "copy")]
    {
        if selector != 0 {
            return Err(a::Error::Input);
        }
        let (source, destination) = STORAGE.init_with(|| ([0; p::COPY_WORDS], [0; p::COPY_WORDS]));
        run(
            p::CopyProbe::new(source, destination).map_err(|_| a::Error::Input)?,
            id,
            out,
            flag,
            "TP-02",
            "copy",
        )
    }
    #[cfg(any(feature = "projection32", feature = "projection64"))]
    {
        if selector != 0 {
            return Err(a::Error::Input);
        }
        let (input, weights, output) =
            STORAGE.init_with(|| ([0.; p::INPUTS], [0.; p::WEIGHTS], [0.; p::OUTPUTS]));
        for (i, v) in input.iter_mut().enumerate() {
            *v = p::input_value(i);
        }
        for (i, v) in weights.iter_mut().enumerate() {
            *v = p::weight_value(i);
        }
        run(
            p::Projection::new(input, weights, output).map_err(|_| a::Error::Input)?,
            id,
            out,
            flag,
            "TP-03",
            if cfg!(feature = "projection32") {
                "projection32"
            } else {
                "projection64"
            },
        )
    }
    #[cfg(feature = "activation")]
    {
        let kind = match selector {
            0 => p::Activation::Sigmoid,
            1 => p::Activation::Tanh,
            _ => return Err(a::Error::Input),
        };
        let output = STORAGE.init_with(|| [0.; p::NONLINEAR_OUTPUTS]);
        run(
            p::NonlinearProbe::new_activation(kind, output).map_err(|_| a::Error::Input)?,
            id,
            out,
            flag,
            "TP-04",
            if selector == 0 { "sigmoid" } else { "tanh" },
        )
    }
    #[cfg(feature = "logs")]
    {
        let kind = match selector {
            0 => p::LogKind::Log,
            1 => p::LogKind::LogDiv15,
            _ => return Err(a::Error::Input),
        };
        let output = STORAGE.init_with(|| [0.; p::NONLINEAR_OUTPUTS]);
        run(
            p::NonlinearProbe::new_logs(kind, output).map_err(|_| a::Error::Input)?,
            id,
            out,
            flag,
            "TP-04",
            if selector == 0 { "log" } else { "log_div15" },
        )
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    esp_hal::system::software_reset()
}
#[esp_hal::main]
fn main() -> ! {
    let _peripherals = esp_hal::init(esp_hal::Config::default());
    black_box(
        option_env!("APTA_PROBE_IMAGE_ID")
            .unwrap_or("UNSPECIFIED_LINKCHECK")
            .as_bytes(),
    );
    black_box(
        option_env!("APTA_PROBE_SOURCE_REVISION")
            .unwrap_or("UNSPECIFIED")
            .as_bytes(),
    );
    black_box(
        option_env!("APTA_PROBE_PROTOCOL_SHA256")
            .unwrap_or("UNSPECIFIED")
            .as_bytes(),
    );
    loop {
        black_box(retained_trial as *const ());
        core::hint::spin_loop();
    }
}
