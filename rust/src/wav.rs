// SPDX-License-Identifier: Apache-2.0
//! Borrowed RIFF/WAVE PCM decoding. File I/O remains the host's responsibility.
//! Supports the C adapter's mono/stereo S16, packed S24, S32 and F32 formats,
//! including WAVE_FORMAT_EXTENSIBLE. No codec or allocation is involved.
use crate::waveform::{normalize_f32, normalize_s16, normalize_s24, normalize_s32};
use crate::{Error, SourceInfo};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Encoding {
    S16,
    S24,
    S32,
    F32,
}

#[derive(Debug)]
pub struct Wav<'a> {
    data: &'a [u8],
    source: SourceInfo,
    encoding: Encoding,
    bytes_per_sample: usize,
}

fn u16_at(b: &[u8], p: usize) -> u16 {
    u16::from_le_bytes([b[p], b[p + 1]])
}
fn u32_at(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes([b[p], b[p + 1], b[p + 2], b[p + 3]])
}

impl<'a> Wav<'a> {
    /// Validate framing and format before exposing PCM. Trailing bytes outside
    /// the RIFF size are ignored, matching the reference desktop adapter.
    pub fn parse(bytes: &'a [u8]) -> Result<Self, Error> {
        if bytes.len() < 12 || &bytes[..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
            return Err(Error::Corrupt);
        }
        let end = usize::try_from(u32_at(bytes, 4))
            .map_err(|_| Error::LimitExceeded)?
            .checked_add(8)
            .ok_or(Error::LimitExceeded)?;
        if end < 12 || end > bytes.len() {
            return Err(Error::Corrupt);
        }
        let mut cursor = 12usize;
        let mut fmt = None;
        let mut data = None;
        while end - cursor >= 8 {
            let size =
                usize::try_from(u32_at(bytes, cursor + 4)).map_err(|_| Error::LimitExceeded)?;
            let start = cursor + 8;
            let stop = start.checked_add(size).ok_or(Error::LimitExceeded)?;
            let next = stop.checked_add(size & 1).ok_or(Error::LimitExceeded)?;
            if next > end {
                return Err(Error::Corrupt);
            }
            match &bytes[cursor..cursor + 4] {
                b"fmt " => {
                    if fmt.is_some() || size < 16 {
                        return Err(Error::Corrupt);
                    }
                    fmt = Some(&bytes[start..stop]);
                }
                b"data" => {
                    if data.is_some() {
                        return Err(Error::Corrupt);
                    }
                    data = Some(&bytes[start..stop]);
                }
                _ => {}
            }
            cursor = next;
        }
        let fmt = fmt.ok_or(Error::Corrupt)?;
        let data = data.ok_or(Error::Corrupt)?;
        let format = parse_format(fmt, data.len() as u64)?;
        Ok(Self {
            data,
            source: format.source,
            encoding: format.encoding,
            bytes_per_sample: format.bytes_per_sample,
        })
    }
    pub fn source(&self) -> SourceInfo {
        self.source
    }
    pub fn encoding(&self) -> Encoding {
        self.encoding
    }
    pub fn frame_count(&self) -> u64 {
        self.source.total_frames.unwrap_or(0)
    }

    /// Decode complete interleaved frames into host scratch. Returns frames,
    /// not samples. Errors (including nonfinite F32) leave output unchanged.
    pub fn read_frames(&self, first: u64, output: &mut [f32]) -> Result<usize, Error> {
        let channels = usize::from(self.source.channel_count);
        if output.len() % channels != 0 || first > self.frame_count() {
            return Err(Error::InvalidArgument);
        }
        let frames = (output.len() / channels)
            .min(usize::try_from(self.frame_count() - first).map_err(|_| Error::LimitExceeded)?);
        let begin = usize::try_from(first)
            .map_err(|_| Error::LimitExceeded)?
            .checked_mul(channels * self.bytes_per_sample)
            .ok_or(Error::LimitExceeded)?;
        let raw = &self.data[begin..begin + frames * channels * self.bytes_per_sample];
        if self.encoding == Encoding::F32 {
            for b in raw.chunks_exact(4) {
                normalize_f32(f32::from_le_bytes([b[0], b[1], b[2], b[3]]))?;
            }
        }
        for (sample, b) in output
            .iter_mut()
            .zip(raw.chunks_exact(self.bytes_per_sample))
        {
            *sample = match self.encoding {
                Encoding::S16 => normalize_s16(i16::from_le_bytes([b[0], b[1]])).value,
                Encoding::S24 => normalize_s24([b[0], b[1], b[2]]).value,
                Encoding::S32 => normalize_s32(i32::from_le_bytes([b[0], b[1], b[2], b[3]])).value,
                Encoding::F32 => normalize_f32(f32::from_le_bytes([b[0], b[1], b[2], b[3]]))?.value,
            };
        }
        Ok(frames)
    }
}

#[derive(Clone, Copy, Debug)]
struct Format {
    source: SourceInfo,
    encoding: Encoding,
    bytes_per_sample: usize,
}
fn parse_format(fmt: &[u8], data_len: u64) -> Result<Format, Error> {
    if fmt.len() < 16 {
        return Err(Error::Corrupt);
    }
    let mut tag = u16_at(fmt, 0);
    let channels = u16_at(fmt, 2);
    let rate = u32_at(fmt, 4);
    let bits = u16_at(fmt, 14);
    let align = u16_at(fmt, 12);
    let mut layout = channels;
    if tag == 0xfffe {
        const SUFFIX: [u8; 14] = [0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113];
        if fmt.len() < 40 || u16_at(fmt, 16) < 22 || fmt[26..40] != SUFFIX {
            return Err(Error::Unsupported);
        }
        let valid_bits = u16_at(fmt, 18);
        if valid_bits == 0 || valid_bits > bits {
            return Err(Error::Corrupt);
        }
        let mask = u32_at(fmt, 20);
        layout = match (channels, mask) {
            (1, 4) => 1,
            (2, 3) => 2,
            _ => 0,
        };
        tag = u16_at(fmt, 24);
    }
    if !(1..=2).contains(&channels)
        || rate == 0
        || rate > 768000
        || bits == 0
        || bits % 8 != 0
        || u32::from(align) != u32::from(channels) * u32::from(bits / 8)
        || align == 0
        || data_len % u64::from(align) != 0
        || u64::from(u32_at(fmt, 8)) != u64::from(rate) * u64::from(align)
    {
        return Err(Error::Corrupt);
    }
    let encoding = match (tag, bits) {
        (1, 16) => Encoding::S16,
        (1, 24) => Encoding::S24,
        (1, 32) => Encoding::S32,
        (3, 32) => Encoding::F32,
        _ => return Err(Error::Unsupported),
    };
    Ok(Format {
        source: SourceInfo {
            sample_rate: rate,
            channel_count: channels,
            channel_layout: layout,
            total_frames: Some(data_len / u64::from(align)),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        encoding,
        bytes_per_sample: usize::from(bits / 8),
    })
}

mod streaming;
pub use streaming::{WavLayout, WavScanner};
