// SPDX-License-Identifier: Apache-2.0
//! C-compatible three-band overview arithmetic with caller-owned accumulators.
//! The filter follows processing order continuously, including across seeks.
use crate::{Error, WaveformColumn};

#[derive(Clone, Copy, Debug)]
pub struct BandFilter {
    low_coefficient: f32,
    mid_coefficient: f32,
    low_state: f32,
    mid_state: f32,
}

impl BandFilter {
    pub fn new(sample_rate: u32) -> Result<Self, Error> {
        if sample_rate == 0 || sample_rate > 768000 {
            return Err(Error::InvalidArgument);
        }
        let rate = sample_rate as f32;
        // Keep C float constants and operation order. No fused multiply-add.
        // TAU has the same f32 bits as the C literal 6.2831853f.
        let tau = core::f32::consts::TAU;
        let low = 1.0_f32 - libm::expf(-tau * 200.0_f32 / rate);
        let mid = 1.0_f32 - libm::expf(-tau * 2000.0_f32 / rate);
        Ok(Self {
            low_coefficient: low.clamp(0.0, 1.0),
            mid_coefficient: mid.clamp(0.0, 1.0),
            low_state: 0.0,
            mid_state: 0.0,
        })
    }

    /// Split one normalized mono sample. Invalid input leaves filter state intact.
    pub fn split(&mut self, sample: f32) -> Result<[f32; 3], Error> {
        if !sample.is_finite() || !(-1.0..=1.0).contains(&sample) {
            return Err(Error::InvalidArgument);
        }
        self.low_state += self.low_coefficient * (sample - self.low_state);
        self.mid_state += self.mid_coefficient * (sample - self.mid_state);
        Ok([
            self.low_state,
            self.mid_state - self.low_state,
            sample - self.mid_state,
        ])
    }
}

/// Three integer sums, independent of the waveform column's sample counter.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct BandSums {
    sums: [u32; 3],
}

impl BandSums {
    /// C clamps each absolute band magnitude before truncating at scale 32768.
    /// Nonfinite input or sum overflow leaves all three accumulators unchanged.
    pub fn add(&mut self, bands: [f32; 3]) -> Result<(), Error> {
        if bands.iter().any(|sample| !sample.is_finite()) {
            return Err(Error::InvalidArgument);
        }
        let mut next = self.sums;
        for (sum, sample) in next.iter_mut().zip(bands) {
            let scaled = (sample.abs().min(1.0) * 32768.0) as u32;
            *sum = sum.checked_add(scaled).ok_or(Error::LimitExceeded)?;
        }
        self.sums = next;
        Ok(())
    }

    /// Add the canonical three band bytes and HAS_3BAND to a completed column.
    pub fn apply(&self, column: WaveformColumn, count: u32) -> Result<WaveformColumn, Error> {
        if !(1..=65536).contains(&count) || column.flags & 1 == 0 || column.minimum > column.maximum
        {
            return Err(Error::InvalidArgument);
        }
        Ok(self.apply_complete(column, count))
    }

    pub(crate) fn apply_complete(&self, mut column: WaveformColumn, count: u32) -> WaveformColumn {
        let denominator = u64::from(count) * 32768;
        let values = self
            .sums
            .map(|sum| ((u64::from(sum) * 255 / denominator).min(255)) as u8);
        column.low = values[0];
        column.mid = values[1];
        column.high = values[2];
        column.flags |= 8;
        column
    }
}

/// Optional caller-owned overview band storage. Filter history follows processing
/// order, rather than source order, when sparse requests seek between ranges.
pub(crate) struct OverviewBands<S> {
    pub(crate) sums: S,
    pub(crate) filter: BandFilter,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sum_overflow_is_atomic_for_all_bands() {
        let mut sums = BandSums {
            sums: [1, u32::MAX, 2],
        };
        let before = sums;
        assert_eq!(sums.add([0.5, 1.0, 0.5]), Err(Error::LimitExceeded));
        assert_eq!(sums, before);
    }
}
