// SPDX-License-Identifier: Apache-2.0
//! Native Rust migration of APTA. This crate is experimental and does not yet
//! replace the published C API or claim full APTA conformance.
//! Processing and container I/O use caller-owned buffers without allocation.
#![no_std]
#![forbid(unsafe_code)]

pub mod analysis;
#[cfg(feature = "experimental-s6-attack-grid")]
pub mod attack_grid;
pub mod band;
pub mod builder;
pub mod container;
mod deadline;
pub mod detail;
pub mod detail_analysis;
pub mod dj;
pub mod global_analysis;
#[cfg(feature = "experimental-s6-history")]
pub mod global_history;
pub mod grid;
#[cfg(feature = "experimental-s6-history")]
pub mod history_grid;
#[cfg(feature = "experimental-s6-history")]
pub mod history_meter;
#[cfg(feature = "experimental-s6-history")]
pub mod history_publication;
pub mod key_analysis;
pub mod meta;
pub mod native_validation;
#[cfg(feature = "experimental-s6-onset-grid")]
pub mod onset_grid;
pub mod owned_result;
pub mod publication;
pub mod pull;
pub mod result;
pub mod scheduler;
pub mod session;
pub mod session_snapshot;
pub mod sparse;
pub mod sparse_pull;
pub mod stream;
pub mod stream_write;
pub mod tempo;
mod types;
pub mod wav;
pub mod waveform;

pub use types::*;
