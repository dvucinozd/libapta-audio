// SPDX-License-Identifier: Apache-2.0
//! Opt-in PCM attack evidence measured on each selected history-window lattice.
//! Every window is independent; diagnostic estimates never become final here.
use crate::{
    global_history::{WindowOutcome, WindowRecord},
    Error,
};

pub const ATTACK_BIN_FRAMES: u64 = 256;
pub const ATTACK_BIN_CAPACITY: usize = 2048;
pub const MAX_METER_BEATS: usize = 128;

/// Opaque caller-owned ring storage. A missing identity is never zero evidence.
#[derive(Clone, Copy, Debug, Default)]
pub struct AttackBin {
    pub(crate) index: u64,
    pub(crate) sum: u32,
    pub(crate) count: u16,
    pub(crate) first_attack_offset: u8,
    #[cfg(feature = "experimental-s6-onset-grid")]
    pub(crate) energy: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MeterEstimate {
    /// Index within this exact window grid, not a legacy/local ordinal.
    pub downbeat_index: u32,
    pub numerator: u16,
    pub observed_beats: u16,
    pub confidence: u8,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum MeterOutcome {
    Estimated(MeterEstimate),
    GridUnsupported,
    InsufficientBeats,
    BeatLimit,
    Ambiguous,
    #[default]
    Unavailable,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MeterRecord {
    /// Exact grid observation used for scoring; prevents accidental rebinding.
    pub window: WindowRecord,
    pub outcome: MeterOutcome,
}

#[derive(Clone, Copy, Debug)]
pub struct MeterHistoryView<'a> {
    pub sample_rate: u32,
    pub records: &'a [MeterRecord],
    pub observed_frames: u64,
    pub inspected_frames: u64,
    pub capacity_exhausted: bool,
    pub discontinuous_input: bool,
    /// All source frames have a verdict, including unsupported verdicts.
    pub finished: bool,
}

pub struct MeterHistory<'a> {
    pub(crate) sample_rate: u32,
    pub(crate) attached: bool,
    bins: &'a mut [AttackBin],
    records: &'a mut [MeterRecord],
    used: usize,
    observed: u64,
    inspected: u64,
    previous_amplitude: u32,
    discontinuous: bool,
    #[cfg(feature = "experimental-s6-onset-grid")]
    pub(crate) novelty: bool,
}

impl<'a> MeterHistory<'a> {
    pub fn new(
        sample_rate: u32,
        bins: &'a mut [AttackBin],
        records: &'a mut [MeterRecord],
    ) -> Result<Self, Error> {
        if sample_rate == 0 || sample_rate > 768000 {
            return Err(Error::InvalidArgument);
        }
        if bins.len() < ATTACK_BIN_CAPACITY || records.is_empty() {
            return Err(Error::BufferTooSmall);
        }
        bins[..ATTACK_BIN_CAPACITY].fill(AttackBin::default());
        Ok(Self {
            sample_rate,
            attached: false,
            bins,
            records,
            used: 0,
            observed: 0,
            inspected: 0,
            previous_amplitude: 0,
            discontinuous: false,
            #[cfg(feature = "experimental-s6-onset-grid")]
            novelty: false,
        })
    }

    pub(crate) fn observe(&mut self, frame: u64, sample: f32) {
        if frame != self.observed {
            self.discontinuous = true;
        }
        self.observed = self.observed.max(frame.saturating_add(1));
        let index = frame / ATTACK_BIN_FRAMES;
        let bin = &mut self.bins[index as usize % ATTACK_BIN_CAPACITY];
        if bin.count == 0 || bin.index != index {
            *bin = AttackBin {
                index,
                ..AttackBin::default()
            };
        }
        // Input was normalized/validated by Session. Quantization is bounded;
        // 256 positive differences of at most 32768 cannot overflow u32.
        let amplitude = (sample.abs().min(1.0) * 32768.0) as u32;
        #[cfg(feature = "experimental-s6-onset-grid")]
        if self.novelty {
            // At most 256 * 32768^2, safely below u64::MAX.
            bin.energy += u64::from(amplitude) * u64::from(amplitude);
        }
        let attack = amplitude.saturating_sub(self.previous_amplitude);
        if attack != 0 && bin.sum == 0 {
            bin.first_attack_offset = (frame % ATTACK_BIN_FRAMES) as u8;
        }
        bin.sum += attack;
        bin.count += 1;
        self.previous_amplitude = amplitude;
    }

    pub(crate) fn capture(&mut self, window: WindowRecord) {
        if self.used == self.records.len() || self.discontinuous {
            return;
        }
        if window.range.first_frame != self.inspected {
            self.discontinuous = true;
            return;
        }
        let outcome = self.estimate(window);
        self.records[self.used] = MeterRecord { window, outcome };
        self.used += 1;
        self.inspected = window.range.end_frame;
    }

    fn estimate(&self, window: WindowRecord) -> MeterOutcome {
        if !matches!(window.outcome, WindowOutcome::Accepted(_)) {
            return MeterOutcome::GridUnsupported;
        }
        // Share the exact Q32 period/beat-count construction with conversion.
        let Ok(Some(grid)) = crate::history_grid::prepare(self.sample_rate, 1, 0, 0, &window)
        else {
            return MeterOutcome::Unavailable;
        };
        if grid.beat_count < 8 {
            return MeterOutcome::InsufficientBeats;
        }
        if grid.beat_count as usize > MAX_METER_BEATS {
            return MeterOutcome::BeatLimit;
        }
        let first = window.range.first_frame / ATTACK_BIN_FRAMES;
        let end = window.range.end_frame.div_ceil(ATTACK_BIN_FRAMES);
        for index in first..end {
            let bin = &self.bins[index as usize % ATTACK_BIN_CAPACITY];
            let expected =
                (window.range.end_frame - index * ATTACK_BIN_FRAMES).min(ATTACK_BIN_FRAMES);
            if bin.count == 0 || bin.index != index || u64::from(bin.count) != expected {
                return MeterOutcome::Unavailable;
            }
        }
        let mut strengths = [0.0; MAX_METER_BEATS];
        for n in 0..grid.beat_count {
            let Ok(Some(beat)) = grid.beat_at(n) else {
                return MeterOutcome::Unavailable;
            };
            let center = beat.position.whole_frame / ATTACK_BIN_FRAMES;
            let mut strength = 0;
            for index in center.saturating_sub(1).max(first)..=(center + 1).min(end - 1) {
                strength = strength.max(self.bins[index as usize % ATTACK_BIN_CAPACITY].sum);
            }
            strengths[n as usize] = strength as f32;
        }
        select_meter(&strengths[..grid.beat_count as usize])
    }

    pub(crate) fn view(&self, eof: Option<u64>) -> MeterHistoryView<'_> {
        MeterHistoryView {
            sample_rate: self.sample_rate,
            records: &self.records[..self.used],
            observed_frames: self.observed,
            inspected_frames: self.inspected,
            capacity_exhausted: self.used == self.records.len() && self.inspected < self.observed,
            discontinuous_input: self.discontinuous,
            finished: !self.discontinuous
                && eof == Some(self.observed)
                && self.inspected == self.observed,
        }
    }

    #[cfg(feature = "experimental-s6-attack-grid")]
    pub(crate) fn select_grid(
        &self,
        range: crate::FrameRange,
    ) -> crate::attack_grid::SelectionOutcome {
        #[cfg(feature = "experimental-s6-onset-grid")]
        if self.novelty {
            return crate::onset_grid::estimate(self.sample_rate, range, self.bins);
        }
        crate::attack_grid::estimate(self.sample_rate, range, self.bins)
    }

    #[cfg(feature = "experimental-s6-onset-trace")]
    pub(crate) fn select_grid_traced(
        &self,
        range: crate::FrameRange,
        trace: &mut crate::onset_grid::OnsetTrace,
        #[cfg(feature = "experimental-s6-temporal-trace")]
        dense: &mut crate::onset_grid::DenseTrace,
    ) -> crate::attack_grid::SelectionOutcome {
        if self.novelty {
            crate::onset_grid::estimate_traced(
                self.sample_rate,
                range,
                self.bins,
                trace,
                #[cfg(feature = "experimental-s6-temporal-trace")]
                dense,
            )
        } else {
            self.select_grid(range)
        }
    }
}

fn select_meter(strengths: &[f32]) -> MeterOutcome {
    let mut best = 0.0f32;
    let mut runner = 0.0f32;
    let mut selected = (0, 0);
    for meter in [3usize, 4] {
        for phase in 0..meter {
            let (mut on, mut off, mut on_count, mut off_count) = (0.0, 0.0, 0, 0);
            for (i, &value) in strengths.iter().enumerate() {
                if i % meter == phase {
                    on += value;
                    on_count += 1;
                } else {
                    off += value;
                    off_count += 1;
                }
            }
            let a = on / on_count as f32;
            let b = off / off_count as f32;
            let score = if a + b > 0.0 { (a - b) / (a + b) } else { 0.0 };
            if score > best {
                runner = best;
                best = score;
                selected = (meter, phase);
            } else {
                runner = runner.max(score);
            }
        }
    }
    if best <= runner || best <= 0.0 {
        return MeterOutcome::Ambiguous;
    }
    MeterOutcome::Estimated(MeterEstimate {
        downbeat_index: selected.1 as u32,
        numerator: selected.0 as u16,
        observed_beats: strengths.len() as u16,
        // Diagnostic separation only. No accepted confidence calibration.
        confidence: ((best - runner) * 49.0) as u8,
    })
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use crate::global_history::{WindowEstimate, WINDOW_FRAMES};
    use crate::FrameRange;
    use std::vec;

    fn window(first: u64) -> WindowRecord {
        WindowRecord {
            range: FrameRange {
                first_frame: first,
                end_frame: first + WINDOW_FRAMES,
            },
            outcome: WindowOutcome::Accepted(WindowEstimate {
                anchor_frame: first,
                tempo_millibpm: 117188,
                confidence: 80,
            }),
        }
    }

    #[test]
    fn real_attacks_select_all_declared_phases_on_a_known_lattice() {
        for numerator in [3, 4] {
            for phase in 0..numerator {
                let mut bins = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
                let mut records = [MeterRecord::default(); 1];
                let mut meter = MeterHistory::new(8000, &mut bins, &mut records).unwrap();
                for frame in 0..WINDOW_FRAMES {
                    let sample = if frame % 4096 < 64 {
                        if frame / 4096 % numerator == phase {
                            0.875
                        } else {
                            0.25
                        }
                    } else {
                        0.0
                    };
                    meter.observe(frame, sample);
                }
                meter.capture(window(0));
                let MeterOutcome::Estimated(estimate) =
                    meter.view(Some(WINDOW_FRAMES)).records[0].outcome
                else {
                    panic!("accent evidence unexpectedly unsupported");
                };
                assert_eq!(u64::from(estimate.numerator), numerator);
                assert_eq!(u64::from(estimate.downbeat_index), phase);
                assert_eq!(estimate.observed_beats, 65);
                assert!(estimate.confidence < 50);
            }
        }
    }

    #[test]
    fn missing_ring_evidence_ambiguity_limits_and_capacity_stay_explicit() {
        let mut bins = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
        let mut records = [MeterRecord::default(); 2];
        let mut meter = MeterHistory::new(8000, &mut bins, &mut records).unwrap();
        for frame in 0..WINDOW_FRAMES {
            meter.observe(frame, 0.0);
        }
        meter.capture(window(0));
        assert_eq!(meter.view(None).records[0].outcome, MeterOutcome::Ambiguous);
        let prefix = meter.view(None).records[0];
        // Replace more than the actual two-window ring before capture.
        for frame in WINDOW_FRAMES..4 * WINDOW_FRAMES {
            meter.observe(frame, if frame % 4096 == 0 { 1.0 } else { 0.0 });
        }
        meter.capture(window(WINDOW_FRAMES));
        let v = meter.view(Some(4 * WINDOW_FRAMES));
        assert_eq!(v.records[0], prefix);
        assert_eq!(v.records[1].outcome, MeterOutcome::Unavailable);
        assert!(v.capacity_exhausted && !v.finished);
        meter.capture(window(2 * WINDOW_FRAMES));
        assert_eq!(meter.view(None).records.len(), 2);

        let mut short = window(0);
        short.range.end_frame = 10000;
        assert_eq!(meter.estimate(short), MeterOutcome::InsufficientBeats);
        let mut fast = window(0);
        if let WindowOutcome::Accepted(ref mut e) = fast.outcome {
            e.tempo_millibpm = 300000;
        }
        assert_eq!(meter.estimate(fast), MeterOutcome::BeatLimit);
        assert_eq!(select_meter(&[1.0; 16]), MeterOutcome::Ambiguous);
    }

    #[test]
    fn source_discontinuity_and_invalid_storage_do_not_publish_estimates() {
        assert!(matches!(
            MeterHistory::new(0, &mut [], &mut []),
            Err(Error::InvalidArgument)
        ));
        assert!(matches!(
            MeterHistory::new(8000, &mut [], &mut []),
            Err(Error::BufferTooSmall)
        ));
        let mut bins = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
        let mut records = [MeterRecord::default(); 1];
        let mut meter = MeterHistory::new(8000, &mut bins, &mut records).unwrap();
        meter.observe(1, 1.0);
        meter.capture(window(0));
        let v = meter.view(Some(2));
        assert!(v.discontinuous_input && !v.finished && v.records.is_empty());
        assert!(core::mem::size_of::<MeterHistory<'_>>() < 256);
        assert!(core::mem::size_of::<AttackBin>() <= 24);
        assert!(core::mem::size_of::<MeterRecord>() <= 80);
        std::println!(
            "meter control={} attack-bin={} record={}",
            core::mem::size_of::<MeterHistory<'_>>(),
            core::mem::size_of::<AttackBin>(),
            core::mem::size_of::<MeterRecord>()
        );
    }
}
