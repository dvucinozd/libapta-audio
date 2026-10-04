// SPDX-License-Identifier: Apache-2.0
//! Native Rust migration of APTA. This crate is experimental and does not yet
//! replace the published C API or claim full APTA conformance.
//! Processing and container I/O use caller-owned buffers without allocation.
#![no_std]
#![forbid(unsafe_code)]

pub mod analysis;
pub mod band;
pub mod builder;
pub mod container;
mod deadline;
pub mod detail;
pub mod detail_analysis;
pub mod dj;
pub mod global_analysis;
pub mod grid;
pub mod key_analysis;
pub mod meta;
pub mod native_validation;
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
