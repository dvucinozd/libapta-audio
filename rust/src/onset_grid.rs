// SPDX-License-Identifier: Apache-2.0
//! Experimental energy-novelty front end. Grid vetoes are shared with the
//! sparse candidate; dense/irregular evidence is never silently repaired.
use crate::{
    attack_grid::{estimate_events, SelectionOutcome, MAX_EVENTS},
    global_history::WINDOW_FRAMES,
    history_meter::{AttackBin, ATTACK_BIN_CAPACITY, ATTACK_BIN_FRAMES},
    FrameRange,
};

pub const MAX_WINDOW_BINS: usize = (WINDOW_FRAMES / ATTACK_BIN_FRAMES) as usize;

/// Full existing novelty array, captured before any peak filtering. Coordinates
/// and the partial final bin belong to the containing SelectionRecord range.
#[cfg(feature = "experimental-s6-temporal-trace")]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DenseTrace {
    pub values: [f32; MAX_WINDOW_BINS],
    pub count: u16,
    /// Initial source bins with no preceding 80 ms background. This is not a gap.
    pub startup_bins: u16,
    pub complete: bool,
}

#[cfg(feature = "experimental-s6-temporal-trace")]
impl Default for DenseTrace {
    fn default() -> Self {
        Self {
            values: [0.0; MAX_WINDOW_BINS],
            count: 0,
            startup_bins: 0,
            complete: false,
        }
    }
}

#[cfg(feature = "experimental-s6-onset-trace")]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct OnsetEvent {
    pub frame: u64,
    pub strength: f32,
}

/// Caller-owned diagnostic capture. Incomplete evidence cannot authorize an
/// oracle proposal. The original extraction and selection verdict are retained.
#[cfg(feature = "experimental-s6-onset-trace")]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct OnsetTrace {
    pub events: [OnsetEvent; MAX_EVENTS],
    pub count: u16,
    pub complete: bool,
}

#[cfg(feature = "experimental-s6-onset-trace")]
impl Default for OnsetTrace {
    fn default() -> Self {
        Self {
            events: [OnsetEvent::default(); MAX_EVENTS],
            count: 0,
            complete: false,
        }
    }
}

#[cfg(feature = "experimental-s6-onset-trace")]
pub(crate) fn estimate_traced(
    rate: u32,
    range: FrameRange,
    bins: &[AttackBin],
    trace: &mut OnsetTrace,
    #[cfg(feature = "experimental-s6-temporal-trace")] dense: &mut DenseTrace,
) -> SelectionOutcome {
    *trace = OnsetTrace::default();
    #[cfg(feature = "experimental-s6-temporal-trace")]
    {
        *dense = DenseTrace::default();
    }
    let outcome = estimate_observing(
        rate,
        range,
        bins,
        |frame, strength| {
            trace.events[usize::from(trace.count)] = OnsetEvent { frame, strength };
            trace.count += 1;
        },
        |_values, _startup| {
            #[cfg(feature = "experimental-s6-temporal-trace")]
            {
                dense.values[.._values.len()].copy_from_slice(_values);
                dense.count = _values.len() as u16;
                dense.startup_bins = _startup;
                dense.complete = true;
            }
        },
    );
    trace.complete = !matches!(
        outcome,
        SelectionOutcome::Unavailable | SelectionOutcome::EventLimit
    );
    outcome
}

pub(crate) fn estimate(rate: u32, range: FrameRange, bins: &[AttackBin]) -> SelectionOutcome {
    estimate_observing(rate, range, bins, |_, _| {}, |_, _| {})
}

fn estimate_observing(
    rate: u32,
    range: FrameRange,
    bins: &[AttackBin],
    mut observe: impl FnMut(u64, f32),
    observe_dense: impl FnOnce(&[f32], u16),
) -> SelectionOutcome {
    if rate == 0
        || rate > 768000
        || bins.len() < ATTACK_BIN_CAPACITY
        || range.first_frame >= range.end_frame
        || range.first_frame % WINDOW_FRAMES != 0
        || range.end_frame - range.first_frame > WINDOW_FRAMES
    {
        return SelectionOutcome::Unavailable;
    }
    let first = range.first_frame / ATTACK_BIN_FRAMES;
    let end = range.end_frame.div_ceil(ATTACK_BIN_FRAMES);
    let background = (u64::from(rate) * 80).div_ceil(1000 * ATTACK_BIN_FRAMES);
    let radius = (u64::from(rate) * 60).div_ceil(1000 * ATTACK_BIN_FRAMES);
    let context = first.saturating_sub(background);
    for index in context..end {
        let bin = &bins[index as usize % ATTACK_BIN_CAPACITY];
        let expected = (range.end_frame - index * ATTACK_BIN_FRAMES).min(ATTACK_BIN_FRAMES);
        if bin.index != index || u64::from(bin.count) != expected {
            return SelectionOutcome::Unavailable;
        }
    }
    let energy = |index: u64| {
        let bin = &bins[index as usize % ATTACK_BIN_CAPACITY];
        bin.energy as f64 / f64::from(bin.count)
    };
    let mut novelty = [0.0f32; MAX_WINDOW_BINS];
    let mut preceding = 0.0;
    for index in context..first {
        preceding += energy(index);
    }
    for index in first..end {
        let value = energy(index);
        if index >= background {
            let mean = preceding / background as f64;
            if value + mean > 0.0 {
                novelty[(index - first) as usize] =
                    ((value - mean).max(0.0) / (value + mean)) as f32;
            }
        }
        preceding += value;
        if index >= background {
            preceding -= energy(index - background);
        }
    }
    // Dense capture is complete even if the subsequent sparse event list fills.
    observe_dense(
        &novelty[..(end - first) as usize],
        background.min(end).saturating_sub(first) as u16,
    );
    let mut events = [0u64; MAX_EVENTS];
    let mut count = 0;
    // Require a complete symmetric peak context inside this independent window.
    for index in first + radius..end.saturating_sub(radius) {
        if index < background {
            continue;
        }
        let value = novelty[(index - first) as usize];
        if value <= 0.20 {
            continue;
        }
        let is_peak = (index - radius..=index + radius).all(|neighbor| {
            let other = novelty[(neighbor - first) as usize];
            other < value || (other == value && neighbor >= index)
        });
        if is_peak {
            if count == events.len() {
                return SelectionOutcome::EventLimit;
            }
            events[count] = index * ATTACK_BIN_FRAMES;
            observe(events[count], value);
            count += 1;
        }
    }
    estimate_events(rate, range, &events[..count])
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use crate::history_meter::{MeterHistory, MeterRecord};
    use std::vec;

    fn pcm(total: u64, bursts: bool) -> std::vec::Vec<AttackBin> {
        let mut bins = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
        let mut records = [MeterRecord::default(); 2];
        {
            let mut meter = MeterHistory::new(8000, &mut bins, &mut records).unwrap();
            meter.novelty = true;
            for frame in 0..total {
                let amplitude = if bursts && frame >= 768 && (frame - 768) % 4096 < 512 {
                    0.5
                } else {
                    0.05
                };
                let carrier = [0.0, 0.5, 1.0, 0.5, 0.0, -0.5, -1.0, -0.5];
                meter.observe(frame, amplitude * carrier[frame as usize % carrier.len()]);
            }
        }
        bins
    }

    #[test]
    fn novelty_separates_bursts_over_a_continuous_carrier() {
        let range = FrameRange {
            first_frame: 0,
            end_frame: WINDOW_FRAMES,
        };
        let bins = pcm(WINDOW_FRAMES, true);
        assert!(bins[..MAX_WINDOW_BINS].iter().all(|b| b.sum != 0));
        assert_eq!(
            crate::attack_grid::estimate(8000, range, &bins),
            SelectionOutcome::InsufficientEvents
        );
        let SelectionOutcome::Accepted(e) = estimate(8000, range, &bins) else {
            panic!()
        };
        assert_eq!(e.anchor_frame, 768);
        assert_eq!(e.tempo_millibpm, 117188);
        assert_eq!(e.confidence, 49);
        assert_eq!(
            estimate(8000, range, &pcm(WINDOW_FRAMES, false)),
            SelectionOutcome::InsufficientEvents
        );
    }

    #[test]
    fn novelty_preserves_source_context_and_refuses_missing_evidence() {
        let range = FrameRange {
            first_frame: WINDOW_FRAMES,
            end_frame: 2 * WINDOW_FRAMES,
        };
        // Last 2048 bins retain the full requested window and its background.
        let mut bins = pcm(2 * WINDOW_FRAMES, true);
        assert!(matches!(
            estimate(8000, range, &bins),
            SelectionOutcome::Accepted(_)
        ));
        bins[1023].index = 0;
        assert_eq!(estimate(8000, range, &bins), SelectionOutcome::Unavailable);
        let mut bins = pcm(WINDOW_FRAMES, true);
        bins[100].count -= 1;
        assert_eq!(
            estimate(
                8000,
                FrameRange {
                    first_frame: 0,
                    end_frame: WINDOW_FRAMES
                },
                &bins
            ),
            SelectionOutcome::Unavailable
        );
        assert_eq!(
            estimate(
                8000,
                FrameRange {
                    first_frame: 0,
                    end_frame: 17
                },
                &pcm(17, true)
            ),
            SelectionOutcome::InsufficientEvents
        );
        assert_eq!(estimate(0, range, &bins), SelectionOutcome::Unavailable);
        assert!(core::mem::size_of::<AttackBin>() <= 24);
    }

    #[cfg(feature = "experimental-s6-onset-trace")]
    #[test]
    fn trace_preserves_actual_events_verdict_and_explicit_loss() {
        let range = FrameRange {
            first_frame: 0,
            end_frame: WINDOW_FRAMES,
        };
        let mut bins = pcm(WINDOW_FRAMES, true);
        let mut trace = OnsetTrace::default();
        assert_eq!(
            estimate_traced(
                8000,
                range,
                &bins,
                &mut trace,
                #[cfg(feature = "experimental-s6-temporal-trace")]
                &mut DenseTrace::default()
            ),
            estimate(8000, range, &bins)
        );
        assert!(trace.complete);
        assert_eq!(trace.count, 64);
        for (index, event) in trace.events[..usize::from(trace.count)].iter().enumerate() {
            assert_eq!(event.frame, 768 + index as u64 * 4096);
            assert!(event.strength > 0.20 && event.strength <= 1.0);
        }
        bins[5].count -= 1;
        assert_eq!(
            estimate_traced(
                8000,
                range,
                &bins,
                &mut trace,
                #[cfg(feature = "experimental-s6-temporal-trace")]
                &mut DenseTrace::default()
            ),
            SelectionOutcome::Unavailable
        );
        assert!(!trace.complete && trace.count == 0);
        let mut bins = pcm(WINDOW_FRAMES, false);
        for (index, bin) in bins[..MAX_WINDOW_BINS].iter_mut().enumerate() {
            bin.energy = if index % 6 == 3 {
                100000000 * 256
            } else {
                1000000 * 256
            };
        }
        assert_eq!(
            estimate_traced(
                8000,
                range,
                &bins,
                &mut trace,
                #[cfg(feature = "experimental-s6-temporal-trace")]
                &mut DenseTrace::default()
            ),
            SelectionOutcome::EventLimit
        );
        assert!(!trace.complete);
        assert_eq!(usize::from(trace.count), MAX_EVENTS);
        std::println!(
            "onset trace={} event={} selection={}",
            core::mem::size_of::<OnsetTrace>(),
            core::mem::size_of::<OnsetEvent>(),
            core::mem::size_of::<crate::attack_grid::SelectionRecord>()
        );
    }

    #[cfg(feature = "experimental-s6-temporal-trace")]
    #[test]
    fn dense_trace_retains_subthreshold_observation_and_silence() {
        let range = FrameRange {
            first_frame: 0,
            end_frame: WINDOW_FRAMES,
        };
        let mut bins = pcm(WINDOW_FRAMES, false);
        for bin in &mut bins[..MAX_WINDOW_BINS] {
            bin.energy = 1000000 * 256;
        }
        bins[100].energy = 1100000 * 256;
        let mut events = OnsetTrace::default();
        let mut dense = DenseTrace::default();
        assert_eq!(
            estimate_traced(8000, range, &bins, &mut events, &mut dense),
            estimate(8000, range, &bins)
        );
        assert!(events.complete && events.count == 0);
        assert!(dense.complete);
        assert_eq!(dense.count, 1024);
        assert_eq!(dense.startup_bins, 3);
        assert!(dense.values[..3].iter().all(|v| *v == 0.0));
        assert_eq!(dense.values[100], (1.0 / 21.0) as f32);
        assert_eq!(dense.values.iter().filter(|v| **v > 0.0).count(), 1);
        for bin in &mut bins[..MAX_WINDOW_BINS] {
            bin.energy = 0;
        }
        estimate_traced(8000, range, &bins, &mut events, &mut dense);
        assert!(dense.complete && dense.values.iter().all(|v| *v == 0.0));
        assert!(core::mem::size_of::<DenseTrace>() <= 4128);
        std::println!(
            "dense={} selection={}",
            core::mem::size_of::<DenseTrace>(),
            core::mem::size_of::<crate::attack_grid::SelectionRecord>()
        );
    }

    #[cfg(feature = "experimental-s6-temporal-trace")]
    #[test]
    fn dense_trace_survives_peak_overflow_but_rejects_missing_context() {
        let range = FrameRange {
            first_frame: 0,
            end_frame: WINDOW_FRAMES,
        };
        let mut bins = pcm(WINDOW_FRAMES, false);
        for (index, bin) in bins[..MAX_WINDOW_BINS].iter_mut().enumerate() {
            bin.energy = if index % 6 == 3 {
                100000000 * 256
            } else {
                1000000 * 256
            };
        }
        let mut events = OnsetTrace::default();
        let mut dense = DenseTrace::default();
        assert_eq!(
            estimate_traced(8000, range, &bins, &mut events, &mut dense),
            SelectionOutcome::EventLimit
        );
        assert!(!events.complete);
        assert!(dense.complete && dense.count == 1024);
        assert!(dense
            .values
            .iter()
            .all(|v| v.is_finite() && (0.0..=1.0).contains(v)));
        bins[100].index += 1;
        assert_eq!(
            estimate_traced(8000, range, &bins, &mut events, &mut dense),
            SelectionOutcome::Unavailable
        );
        assert!(!dense.complete && dense.count == 0 && dense.values.iter().all(|v| *v == 0.0));
        assert_eq!(
            estimate_traced(0, range, &bins, &mut events, &mut dense),
            SelectionOutcome::Unavailable
        );
        assert!(!dense.complete);
    }

    #[cfg(feature = "experimental-s6-temporal-trace")]
    #[test]
    fn dense_trace_preserves_partial_bins_ring_wrap_and_source_coordinates() {
        let end = 2 * WINDOW_FRAMES + 17;
        let mut bins = pcm(end, true);
        let range = FrameRange {
            first_frame: 2 * WINDOW_FRAMES,
            end_frame: end,
        };
        let mut events = OnsetTrace::default();
        let mut dense = DenseTrace::default();
        estimate_traced(8000, range, &bins, &mut events, &mut dense);
        assert!(dense.complete && dense.count == 1 && dense.startup_bins == 0);
        let expected = dense.values[0];
        assert!(expected.is_finite());
        let previous = (range.first_frame / ATTACK_BIN_FRAMES - 1) as usize % ATTACK_BIN_CAPACITY;
        bins[previous].count -= 1;
        assert_eq!(
            estimate_traced(8000, range, &bins, &mut events, &mut dense),
            SelectionOutcome::Unavailable
        );
        assert!(!dense.complete && dense.count == 0);
        let short = FrameRange {
            first_frame: 0,
            end_frame: 17,
        };
        estimate_traced(8000, short, &pcm(17, true), &mut events, &mut dense);
        assert!(dense.complete && dense.count == 1 && dense.startup_bins == 1);
        assert_eq!(dense.values[0], 0.0);
    }
}
