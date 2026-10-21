// SPDX-License-Identifier: Apache-2.0
//! Experimental source-aligned S6 window evidence. This is not a transport grid
//! or a serializable result: revision, meter and coverage authority are separate.
use crate::{
    analysis::OnsetBin,
    global_analysis::{estimate_window, BIN_CAPACITY, BIN_FRAMES},
    Error, FrameRange,
};

pub const WINDOW_BINS: usize = 128;
pub const WINDOW_FRAMES: u64 = WINDOW_BINS as u64 * BIN_FRAMES;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WindowEstimate {
    pub anchor_frame: u64,
    pub tempo_millibpm: u32,
    pub confidence: u8,
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::{vec, vec::Vec};

    // Stage fixtures construct complete valid bin summaries. Real PCM and the
    // public Session are exercised separately; these are not musical scores.
    fn fill(bins: &mut [OnsetBin], first: u64, count: usize, silence: bool) {
        for offset in 0..count {
            let i = first + offset as u64;
            bins[i as usize % BIN_CAPACITY] = OnsetBin {
                index: i as u32,
                sum: if silence {
                    0
                } else if i % 4 == 0 {
                    16777216
                } else {
                    3355443
                },
                count: BIN_FRAMES as u32,
                occupied: true,
            };
        }
    }

    #[test]
    fn global_history_preserves_three_ring_turnovers_and_rejected_islands() {
        let mut storage = vec![WindowRecord::default(); 3 * BIN_CAPACITY / WINDOW_BINS];
        let mut h = WindowHistory::new(8000, &mut storage).unwrap();
        let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
        let mut prefix = Vec::new();
        for n in 0..3 * BIN_CAPACITY / WINDOW_BINS {
            let first = n as u64 * WINDOW_BINS as u64;
            let silence = n % 17 == 1;
            fill(&mut bins, first, WINDOW_BINS, silence);
            h.observed = (first + WINDOW_BINS as u64) * BIN_FRAMES;
            assert_eq!(h.step(&bins, None), 1);
            let v = h.view(None);
            assert_eq!(v.records[n].range.first_frame, first * BIN_FRAMES);
            assert_eq!(v.records[n].range.end_frame, h.observed);
            if silence {
                assert_eq!(v.records[n].outcome, WindowOutcome::Rejected);
            } else {
                assert!(matches!(v.records[n].outcome, WindowOutcome::Accepted(_)));
            }
            if n == 0 {
                prefix = v.records.to_vec();
            }
            assert_eq!(&v.records[..prefix.len()], prefix.as_slice());
        }
        let v = h.view(Some(h.observed));
        assert!(v.finished);
        assert!(!v.capacity_exhausted);
        assert_eq!(v.records.len(), 384);
        assert_eq!(v.records[0].range.first_frame, 0);
        assert_eq!(h.step(&bins, Some(h.observed)), 0);
    }

    #[test]
    fn global_history_reports_eviction_and_capacity_without_overwriting_prefix() {
        let mut storage = [WindowRecord::default(); 2];
        let mut h = WindowHistory::new(8000, &mut storage).unwrap();
        let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
        fill(&mut bins, 0, WINDOW_BINS, false);
        h.observed = WINDOW_FRAMES;
        assert_eq!(h.step(&bins, None), 1);
        let prefix = h.view(None).records[0];
        fill(&mut bins, BIN_CAPACITY as u64, BIN_CAPACITY, false);
        h.observed = 2 * BIN_CAPACITY as u64 * BIN_FRAMES;
        assert_eq!(h.step(&bins, None), 1);
        let v = h.view(Some(h.observed));
        assert_eq!(v.records[0], prefix);
        assert_eq!(v.records[1].outcome, WindowOutcome::Unavailable);
        assert!(v.capacity_exhausted);
        assert!(!v.finished);
        assert!(!h.pending(Some(h.observed)));
        assert_eq!(h.step(&bins, None), 0);
        assert_eq!(h.view(None).records[0], prefix);
    }

    #[test]
    fn global_history_partial_eof_waits_for_resolution_and_keeps_short_tail() {
        for count in [1, 63 * 2048, 63 * 2048 + 1, 64 * 2048, 127 * 2048 + 17] {
            let mut storage = [WindowRecord::default(); 1];
            let mut h = WindowHistory::new(8000, &mut storage).unwrap();
            let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
            let bin_count = (count as u64).div_ceil(BIN_FRAMES) as usize;
            fill(&mut bins, 0, bin_count, false);
            bins[bin_count - 1].count = ((count - 1) % 2048 + 1) as u32;
            bins[bin_count - 1].sum = bins[bin_count - 1].count * 8192;
            h.observed = count as u64;
            assert_eq!(h.step(&bins, None), 0);
            assert_eq!(h.step(&bins, Some(h.observed)), 1);
            let v = h.view(Some(h.observed));
            assert!(v.finished);
            assert_eq!(v.records[0].range.end_frame, count as u64);
            if bin_count < 64 {
                assert_eq!(v.records[0].outcome, WindowOutcome::TooShort);
            } else {
                assert!(matches!(v.records[0].outcome, WindowOutcome::Accepted(_)));
            }
        }
    }

    #[test]
    fn global_history_rejects_invalid_storage_and_discontinuous_source() {
        assert!(matches!(
            WindowHistory::new(0, &mut []),
            Err(Error::InvalidArgument)
        ));
        assert!(matches!(
            WindowHistory::new(8000, &mut []),
            Err(Error::BufferTooSmall)
        ));
        let mut records = [WindowRecord::default(); 1];
        let mut h = WindowHistory::new(8000, &mut records).unwrap();
        h.observe(0);
        h.observe(2);
        assert!(h.view(Some(3)).discontinuous_input);
        assert!(!h.view(Some(3)).finished);
        assert!(!h.pending(Some(3)));
    }

    #[test]
    fn global_history_resource_ceiling() {
        let control = core::mem::size_of::<WindowHistory<'_>>();
        let record = core::mem::size_of::<WindowRecord>();
        std::println!("history control={control} bytes, record={record} bytes");
        assert!(control < 1024);
        assert!(record <= 64);
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum WindowOutcome {
    Accepted(WindowEstimate),
    Rejected,
    #[default]
    Unavailable,
    TooShort,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct WindowRecord {
    pub range: FrameRange,
    pub outcome: WindowOutcome,
}

/// Borrowed diagnostics, scoped to the attached sequential source. `finished`
/// means every input frame has a record, including rejected/unavailable ranges;
/// it does not mean that every range supplies timing, or that timing is accurate.
#[derive(Clone, Copy, Debug)]
pub struct HistoryView<'a> {
    pub sample_rate: u32,
    pub records: &'a [WindowRecord],
    pub observed_frames: u64,
    pub inspected_frames: u64,
    pub capacity_exhausted: bool,
    pub discontinuous_input: bool,
    pub finished: bool,
}

/// Caller-owned control and fixed scratch plus caller-sized append-only records.
/// Attach once before input with `Session::enable_global_history`. Copy records
/// to independent caller storage when a retained history must outlive a borrow.
pub struct WindowHistory<'a> {
    pub(crate) sample_rate: u32,
    pub(crate) attached: bool,
    records: &'a mut [WindowRecord],
    used: usize,
    scratch: [f32; WINDOW_BINS],
    observed: u64,
    next: u64,
    discontinuous: bool,
}

impl<'a> WindowHistory<'a> {
    pub fn new(sample_rate: u32, records: &'a mut [WindowRecord]) -> Result<Self, Error> {
        if sample_rate == 0 || sample_rate > 768000 {
            return Err(Error::InvalidArgument);
        }
        if records.is_empty() {
            return Err(Error::BufferTooSmall);
        }
        Ok(Self {
            sample_rate,
            attached: false,
            records,
            used: 0,
            scratch: [0.0; WINDOW_BINS],
            observed: 0,
            next: 0,
            discontinuous: false,
        })
    }

    pub(crate) fn observe(&mut self, frame: u64) {
        if frame != self.observed {
            self.discontinuous = true;
        }
        self.observed = self.observed.max(frame.saturating_add(1));
    }

    pub(crate) fn pending(&self, eof: Option<u64>) -> bool {
        !self.discontinuous
            && self.used < self.records.len()
            && (self.observed >= self.next.saturating_add(WINDOW_FRAMES)
                || (eof == Some(self.observed) && self.observed > self.next))
    }

    /// One bounded processing step. New windows are never inferred from earlier
    /// result segments; missing ring identities produce an explicit diagnostic.
    pub(crate) fn step(&mut self, bins: &[OnsetBin], eof: Option<u64>) -> u32 {
        if !self.pending(eof) {
            return 0;
        }
        let end = self.next.saturating_add(WINDOW_FRAMES).min(self.observed);
        let first_bin = self.next / BIN_FRAMES;
        let count = (end - self.next).div_ceil(BIN_FRAMES) as usize;
        let mut previous = 0.0;
        let mut available = true;
        for offset in 0..count {
            let index = first_bin + offset as u64;
            let bin = &bins[index as usize % BIN_CAPACITY];
            let expected = (end - index * BIN_FRAMES).min(BIN_FRAMES) as u32;
            if !bin.occupied || u64::from(bin.index) != index || bin.count != expected {
                available = false;
                break;
            }
            let energy = bin.sum as f32 / (bin.count as f32 * 32768.0);
            self.scratch[offset] = if offset == 0 {
                energy
            } else {
                (energy - previous).max(0.0)
            };
            previous = energy;
        }
        let outcome = if !available {
            WindowOutcome::Unavailable
        } else if count < 64 {
            WindowOutcome::TooShort
        } else {
            estimate_window(self.sample_rate, &self.scratch[..count]).map_or(
                WindowOutcome::Rejected,
                |(tempo, phase, confidence)| {
                    WindowOutcome::Accepted(WindowEstimate {
                        anchor_frame: self.next + u64::from(phase) * BIN_FRAMES,
                        tempo_millibpm: tempo,
                        confidence,
                    })
                },
            )
        };
        self.records[self.used] = WindowRecord {
            range: FrameRange {
                first_frame: self.next,
                end_frame: end,
            },
            outcome,
        };
        self.used += 1;
        self.next = end;
        1
    }

    pub(crate) fn view(&self, eof: Option<u64>) -> HistoryView<'_> {
        HistoryView {
            sample_rate: self.sample_rate,
            records: &self.records[..self.used],
            observed_frames: self.observed,
            inspected_frames: self.next,
            capacity_exhausted: self.used == self.records.len() && self.next < self.observed,
            discontinuous_input: self.discontinuous,
            finished: !self.discontinuous
                && eof == Some(self.observed)
                && self.next == self.observed,
        }
    }
}
