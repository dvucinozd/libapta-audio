// SPDX-License-Identifier: Apache-2.0
//! Allocation-free reference waveform arithmetic.
//!
//! Normalization and quantization follow the 1.1.0 C reference. Nonfinite float
//! input is rejected, as permitted by the PCM contract. Clipping flags retain
//! the C mixed-signal behavior, including its loss of pre-mix clipping provenance.

use crate::{Error, WaveformColumn};

/// One normalized mono sample owned by the caller.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct NormalizedSample {
    pub value: f32,
    pub clipped: bool,
}

/// Mix normalized channels with the C reference clipping behavior.
pub fn reduce_stereo(left: NormalizedSample, right: NormalizedSample) -> NormalizedSample {
    let value = (left.value + right.value) * 0.5;
    NormalizedSample {
        value,
        clipped: value <= -1.0 || value >= 1.0,
    }
}

/// Normalize a signed native 16-bit PCM sample.
pub fn normalize_s16(value: i16) -> NormalizedSample {
    let sample = if value < 0 {
        value as f32 / 32768.0
    } else {
        value as f32 / 32767.0
    };
    NormalizedSample {
        value: sample,
        clipped: value == i16::MIN || value == i16::MAX,
    }
}

/// Normalize a signed 24-bit little-endian packed PCM sample.
pub fn normalize_s24(bytes: [u8; 3]) -> NormalizedSample {
    let value = (i32::from_le_bytes([bytes[0], bytes[1], bytes[2], 0]) << 8) >> 8;
    let sample = if value < 0 {
        value as f32 / 8388608.0
    } else {
        value as f32 / 8388607.0
    };
    NormalizedSample {
        value: sample,
        clipped: value == -8388608 || value == 8388607,
    }
}

/// Normalize a signed native 32-bit PCM sample.
pub fn normalize_s32(value: i32) -> NormalizedSample {
    let sample = if value < 0 {
        value as f32 / 2147483648.0
    } else {
        (value as f64 / 2147483647.0) as f32
    };
    NormalizedSample {
        value: sample,
        // C marks clipping after normalization, which can round nearby S32
        // magnitudes to full scale even when the integer is not an endpoint.
        clipped: sample <= -1.0 || sample >= 1.0,
    }
}

/// Clamp finite float PCM; reject NaN and infinity before analysis.
pub fn normalize_f32(value: f32) -> Result<NormalizedSample, Error> {
    if !value.is_finite() {
        return Err(Error::InvalidArgument);
    }
    let sample = value.clamp(-1.0, 1.0);
    Ok(NormalizedSample {
        value: sample,
        clipped: sample <= -1.0 || sample >= 1.0,
    })
}

/// Reduce one mono/stereo float frame using the reference per-channel clamp.
/// The returned clipping flag matches the C mixed-signal behavior.
pub fn mix_frame(channels: &[f32]) -> Result<NormalizedSample, Error> {
    let sample = match channels {
        [mono] => normalize_f32(*mono)?.value,
        [left, right] => (normalize_f32(*left)?.value + normalize_f32(*right)?.value) * 0.5,
        _ => return Err(Error::InvalidArgument),
    };
    Ok(NormalizedSample {
        value: sample,
        clipped: sample <= -1.0 || sample >= 1.0,
    })
}

/// Integer energy accumulation is independent of processing block boundaries.
/// No input memory is retained and no allocation occurs. Overflow is rejected
/// before mutation; the caller controls column geometry and coverage.
#[derive(Clone, Copy, Debug)]
pub struct WaveformAccumulator {
    sum_squares: u64,
    count: u32,
    minimum: f32,
    maximum: f32,
    clipped: bool,
}

impl Default for WaveformAccumulator {
    fn default() -> Self {
        Self {
            sum_squares: 0,
            count: 0,
            minimum: 1.0,
            maximum: -1.0,
            clipped: false,
        }
    }
}

impl WaveformAccumulator {
    /// Reconstruct the C checkpoint accumulator from quantized overview data.
    /// C divides both signed peaks by 32767, including i16::MIN; that value
    /// falls below -1 and must bypass the normalized PCM input validator.
    pub(crate) fn from_seed_column(
        column: WaveformColumn,
        sample_count: u32,
    ) -> Result<Self, Error> {
        if !(1..=65536).contains(&sample_count)
            || column.flags & 1 == 0
            || column.minimum > column.maximum
        {
            return Err(Error::InvalidArgument);
        }
        let rms = f32::from(column.rms) / 65535.0;
        let scaled = (rms * 8388608.0) as u64;
        let sum_squares = scaled
            .checked_mul(scaled)
            .and_then(|value| value.checked_mul(u64::from(sample_count)))
            .ok_or(Error::LimitExceeded)?;
        Ok(Self {
            sum_squares,
            count: sample_count,
            minimum: f32::from(column.minimum) / 32767.0,
            maximum: f32::from(column.maximum) / 32767.0,
            clipped: column.flags & 4 != 0,
        })
    }
    pub fn clear(&mut self) {
        *self = Self::default();
    }
    pub fn sample_count(&self) -> u32 {
        self.count
    }

    /// Add one already normalized mixed sample. Invalid input or capacity
    /// overflow leaves the entire accumulator unchanged.
    pub fn push_normalized(&mut self, sample: f32, clipped: bool) -> Result<(), Error> {
        if !sample.is_finite() || !(-1.0..=1.0).contains(&sample) {
            return Err(Error::InvalidArgument);
        }
        let scaled = (sample.abs() * 8388608.0) as u32;
        let squares = self
            .sum_squares
            .checked_add(u64::from(scaled) * u64::from(scaled))
            .ok_or(Error::LimitExceeded)?;
        let count = self.count.checked_add(1).ok_or(Error::LimitExceeded)?;
        self.sum_squares = squares;
        self.count = count;
        self.minimum = self.minimum.min(sample);
        self.maximum = self.maximum.max(sample);
        self.clipped |= clipped;
        Ok(())
    }

    /// Empty accumulators produce an invalid zero column, never valid silence.
    pub fn column(&self) -> WaveformColumn {
        if self.count == 0 {
            return WaveformColumn::default();
        }
        let rms = libm::sqrt(self.sum_squares as f64 / f64::from(self.count)) / 8388608.0;
        WaveformColumn {
            minimum: quantize_peak(self.minimum),
            maximum: quantize_peak(self.maximum),
            rms: round_ties_even(rms.min(1.0) * 65535.0) as u16,
            flags: 1 | if self.clipped { 4 } else { 0 },
            low: 0,
            mid: 0,
            high: 0,
        }
    }
}

fn round_ties_even(value: f64) -> f64 {
    let lower = libm::floor(value);
    let fraction = value - lower;
    if fraction < 0.5 || (fraction == 0.5 && (lower as i64) % 2 == 0) {
        lower
    } else {
        lower + 1.0
    }
}

fn quantize_peak(sample: f32) -> i16 {
    if sample <= -1.0 {
        i16::MIN
    } else if sample >= 1.0 {
        i16::MAX
    } else {
        round_ties_even(f64::from(sample) * 32767.0) as i16
    }
}

/// Borrowed decoded PCM in the five baseline APTA formats. Native integer
/// slices express alignment and byte order safely; packed S24 is always LE.
#[derive(Clone, Copy, Debug)]
pub enum PcmView<'a> {
    S16Interleaved(&'a [i16]),
    S24Interleaved(&'a [u8]),
    S32Interleaved(&'a [i32]),
    F32Interleaved(&'a [f32]),
    F32Planar(&'a [&'a [f32]]),
}

impl PcmView<'_> {
    /// Validate geometry without scanning PCM values. A session can then
    /// validate only the prefix it has capacity to accept.
    pub fn frame_count(&self, channels: u16) -> Result<usize, Error> {
        if !(1..=2).contains(&channels) {
            return Err(Error::InvalidArgument);
        }
        let channels = usize::from(channels);
        let samples = match self {
            Self::S16Interleaved(v) => v.len(),
            Self::S24Interleaved(v) => {
                if v.len() % 3 != 0 {
                    return Err(Error::InvalidArgument);
                }
                v.len() / 3
            }
            Self::S32Interleaved(v) => v.len(),
            Self::F32Interleaved(v) => v.len(),
            Self::F32Planar(planes) => {
                if planes.len() != channels {
                    return Err(Error::InvalidArgument);
                }
                let count = planes[0].len();
                if planes.iter().any(|p| p.len() != count) {
                    return Err(Error::InvalidArgument);
                }
                return Ok(count);
            }
        };
        if samples % channels != 0 {
            return Err(Error::InvalidArgument);
        }
        Ok(samples / channels)
    }

    /// Read one normalized mono/stereo frame with checked indexing. Nonfinite
    /// F32 values fail before a sample is returned; PCM remains caller-owned.
    pub fn sample_frame(&self, index: usize, channels: u16) -> Result<NormalizedSample, Error> {
        if index >= self.frame_count(channels)? {
            return Err(Error::InvalidArgument);
        }
        let read = |channel: usize| -> Result<NormalizedSample, Error> {
            let sample_index = index * usize::from(channels) + channel;
            Ok(match self {
                Self::S16Interleaved(v) => normalize_s16(v[sample_index]),
                Self::S24Interleaved(v) => {
                    let byte_index = sample_index * 3;
                    normalize_s24([v[byte_index], v[byte_index + 1], v[byte_index + 2]])
                }
                Self::S32Interleaved(v) => normalize_s32(v[sample_index]),
                Self::F32Interleaved(v) => normalize_f32(v[sample_index])?,
                Self::F32Planar(v) => normalize_f32(v[channel][index])?,
            })
        };
        let left = read(0)?;
        if channels == 2 {
            Ok(reduce_stereo(left, read(1)?))
        } else {
            Ok(left)
        }
    }
}

#[cfg(test)]
mod seed_tests {
    use super::*;

    #[test]
    fn seed_full_scale_preserves_clipping_without_pcm_clamping() {
        let source = WaveformColumn {
            minimum: i16::MIN,
            maximum: i16::MAX,
            rms: u16::MAX,
            flags: 5,
            ..WaveformColumn::default()
        };
        let accumulator = WaveformAccumulator::from_seed_column(source, 65536).unwrap();
        assert!(accumulator.minimum < -1.0);
        assert_eq!(accumulator.sample_count(), 65536);
        assert_eq!(accumulator.column(), source);
    }

    #[test]
    fn seed_rebuilds_only_peak_rms_and_clipped_fields() {
        let source = WaveformColumn {
            minimum: -12000,
            maximum: 19000,
            rms: 32000,
            low: 123,
            mid: 234,
            high: 245,
            flags: 9,
        };
        let output = WaveformAccumulator::from_seed_column(source, 452)
            .unwrap()
            .column();
        assert_eq!(output.minimum, source.minimum);
        assert_eq!(output.maximum, source.maximum);
        assert_eq!(output.rms, source.rms);
        assert_eq!(
            (output.flags, output.low, output.mid, output.high),
            (1, 0, 0, 0)
        );
        for count in [0, 65537, u32::MAX] {
            assert!(matches!(
                WaveformAccumulator::from_seed_column(source, count),
                Err(Error::InvalidArgument)
            ));
        }
        assert!(matches!(
            WaveformAccumulator::from_seed_column(WaveformColumn::default(), 1),
            Err(Error::InvalidArgument)
        ));
    }
}
