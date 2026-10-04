// SPDX-License-Identifier: Apache-2.0
//! Sequential framing inspection with constant storage. I/O and hashing belong
//! to the caller. Data may precede fmt; no PCM is exposed until complete framing.
use super::{parse_format, u32_at, Encoding, Format, Wav};
use crate::{Error, SourceInfo};

#[derive(Clone, Copy, Debug)]
pub struct WavLayout {
    format: Format,
    offset: u64,
    bytes: u64,
}
impl WavLayout {
    pub fn source(&self) -> SourceInfo {
        self.format.source
    }
    pub fn encoding(&self) -> Encoding {
        self.format.encoding
    }
    pub fn data_offset(&self) -> u64 {
        self.offset
    }
    pub fn data_bytes(&self) -> u64 {
        self.bytes
    }
    pub fn frame_bytes(&self) -> usize {
        self.format.bytes_per_sample * usize::from(self.format.source.channel_count)
    }
    /// Decode a caller-provided, frame-aligned data block using the same decoder
    /// as borrowed Wav. Output capacity may limit the returned frame count. PCM
    /// outside that returned prefix is not inspected. Nonfinite accepted-prefix
    /// PCM fails atomically. The caller owns block coordinates and source identity.
    pub fn decode(&self, bytes: &[u8], output: &mut [f32]) -> Result<usize, Error> {
        if bytes.len() % self.frame_bytes() != 0 {
            return Err(Error::InvalidArgument);
        }
        Wav {
            data: bytes,
            source: SourceInfo {
                total_frames: Some((bytes.len() / self.frame_bytes()) as u64),
                ..self.format.source
            },
            encoding: self.format.encoding,
            bytes_per_sample: self.format.bytes_per_sample,
        }
        .read_frames(0, output)
    }
}

/// Feed every object byte exactly once, in order, then consume with finish.
/// Retains 12 header bytes and at most 40 fmt bytes, skips payloads in bulk.
/// A framing failure is terminal. Trailing object bytes and fewer than eight
/// residual RIFF bytes match borrowed Wav's acceptance; neither is PCM.
#[derive(Debug)]
pub struct WavScanner {
    length: u64,
    offset: u64,
    end: u64,
    header: [u8; 12],
    have: usize,
    fmt: [u8; 40],
    fmt_len: usize,
    fmt_limit: usize,
    fmt_seen: bool,
    data: Option<(u64, u64)>,
    remaining: u64,
    capture: bool,
    failed: bool,
}
impl WavScanner {
    pub fn new(object_bytes: u64) -> Self {
        Self {
            length: object_bytes,
            offset: 0,
            end: 0,
            header: [0; 12],
            have: 0,
            fmt: [0; 40],
            fmt_len: 0,
            fmt_limit: 0,
            fmt_seen: false,
            data: None,
            remaining: 0,
            capture: false,
            failed: false,
        }
    }
    /// Work is linear in this slice's chunk framing, independent of total object
    /// length. Does not hash, retain PCM or validate floating samples.
    pub fn push(&mut self, bytes: &[u8]) -> Result<(), Error> {
        if self.failed {
            return Err(Error::InvalidState);
        }
        self.failed = true;
        self.push_inner(bytes)?;
        self.failed = false;
        Ok(())
    }
    fn push_inner(&mut self, mut bytes: &[u8]) -> Result<(), Error> {
        if bytes.len() as u64 > self.length - self.offset {
            return Err(Error::Corrupt);
        }
        while !bytes.is_empty() {
            if self.remaining != 0 {
                let n = self.remaining.min(bytes.len() as u64) as usize;
                if self.capture {
                    let copy = n.min(self.fmt_limit - self.fmt_len);
                    self.fmt[self.fmt_len..self.fmt_len + copy].copy_from_slice(&bytes[..copy]);
                    self.fmt_len += copy;
                }
                self.remaining -= n as u64;
                self.offset += n as u64;
                bytes = &bytes[n..];
                continue;
            }
            // Once RIFF is parsed, residual bytes and object trailer are opaque.
            if self.end != 0 && self.end.saturating_sub(self.offset) + (self.have as u64) < 8 {
                self.offset += bytes.len() as u64;
                break;
            }
            let needed = if self.end == 0 { 12 } else { 8 };
            let n = (needed - self.have).min(bytes.len());
            self.header[self.have..self.have + n].copy_from_slice(&bytes[..n]);
            self.have += n;
            self.offset += n as u64;
            bytes = &bytes[n..];
            if self.have != needed {
                continue;
            }
            self.have = 0;
            if self.end == 0 {
                if &self.header[..4] != b"RIFF" || &self.header[8..12] != b"WAVE" {
                    return Err(Error::Corrupt);
                }
                self.end = u64::from(u32_at(&self.header, 4)) + 8;
                if self.end < 12 || self.end > self.length {
                    return Err(Error::Corrupt);
                }
                continue;
            }
            let size = u64::from(u32_at(&self.header, 4));
            self.remaining = size + (size & 1);
            if self.remaining > self.end.saturating_sub(self.offset) {
                return Err(Error::Corrupt);
            }
            self.capture = false;
            match &self.header[..4] {
                b"fmt " => {
                    if self.fmt_seen || size < 16 {
                        return Err(Error::Corrupt);
                    }
                    self.fmt_seen = true;
                    self.fmt_limit = size.min(40) as usize;
                    // Pad never reaches retained bytes for any supported fmt.
                    self.capture = true;
                }
                b"data" => {
                    if self.data.is_some() {
                        return Err(Error::Corrupt);
                    }
                    self.data = Some((self.offset, size));
                }
                _ => (),
            }
        }
        Ok(())
    }
    pub fn finish(self) -> Result<WavLayout, Error> {
        if self.failed || self.offset != self.length || self.end == 0 || self.remaining != 0 {
            return Err(Error::Corrupt);
        }
        let (offset, bytes) = self.data.ok_or(Error::Corrupt)?;
        let format = parse_format(&self.fmt[..self.fmt_len], bytes)?;
        Ok(WavLayout {
            format,
            offset,
            bytes,
        })
    }
}

const _: () = assert!(core::mem::size_of::<WavScanner>() <= 192);
