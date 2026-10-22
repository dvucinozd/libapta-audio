// SPDX-License-Identifier: Apache-2.0
//! Conservative sparse-attack lattice. No tempo prior or missing-beat inference.
use crate::{
    global_history::{WindowEstimate, WindowOutcome, WINDOW_FRAMES},
    history_meter::{AttackBin, ATTACK_BIN_CAPACITY, ATTACK_BIN_FRAMES},
    FrameRange,
};

pub const MAX_EVENTS: usize = 128;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SelectionOutcome {
    Accepted(WindowEstimate),
    #[default]
    Unavailable,
    InsufficientEvents,
    EventLimit,
    Irregular,
    TempoOutOfRange,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
#[cfg_attr(not(feature = "experimental-s6-onset-trace"), derive(Eq))]
pub struct SelectionRecord {
    pub range: FrameRange,
    pub baseline: WindowOutcome,
    pub outcome: SelectionOutcome,
    #[cfg(feature = "experimental-s6-onset-trace")]
    pub onsets: crate::onset_grid::OnsetTrace,
    #[cfg(feature = "experimental-s6-temporal-trace")]
    pub dense: crate::onset_grid::DenseTrace,
}

impl SelectionOutcome {
    pub(crate) fn window_outcome(self) -> WindowOutcome {
        match self {
            Self::Accepted(estimate) => WindowOutcome::Accepted(estimate),
            Self::Unavailable => WindowOutcome::Unavailable,
            Self::InsufficientEvents => WindowOutcome::TooShort,
            Self::EventLimit | Self::Irregular | Self::TempoOutOfRange => WindowOutcome::Rejected,
        }
    }
}

pub(crate) fn estimate(rate: u32, range: FrameRange, bins: &[AttackBin]) -> SelectionOutcome {
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
    // Preflight all identities/counts before interpreting any zero as evidence.
    for index in first..end {
        let bin = &bins[index as usize % ATTACK_BIN_CAPACITY];
        let expected = (range.end_frame - index * ATTACK_BIN_FRAMES).min(ATTACK_BIN_FRAMES);
        if bin.count == 0
            || bin.index != index
            || u64::from(bin.count) != expected
            || (bin.sum != 0 && u16::from(bin.first_attack_offset) >= bin.count)
        {
            return SelectionOutcome::Unavailable;
        }
    }
    let mut active = false;
    if first != 0 && bins[first as usize % ATTACK_BIN_CAPACITY].sum != 0 {
        let previous = &bins[(first - 1) as usize % ATTACK_BIN_CAPACITY];
        if previous.index != first - 1 || u64::from(previous.count) != ATTACK_BIN_FRAMES {
            return SelectionOutcome::Unavailable;
        }
        active = previous.sum != 0;
    }
    let mut events = [0u64; MAX_EVENTS];
    let mut count = 0;
    for index in first..end {
        let bin = &bins[index as usize % ATTACK_BIN_CAPACITY];
        let next_active = bin.sum != 0;
        if next_active && !active {
            if count == events.len() {
                return SelectionOutcome::EventLimit;
            }
            events[count] = index * ATTACK_BIN_FRAMES + u64::from(bin.first_attack_offset);
            count += 1;
        }
        active = next_active;
    }
    estimate_events(rate, range, &events[..count])
}

// Shared timing vetoes, independent of how source events were extracted.
pub(crate) fn estimate_events(rate: u32, range: FrameRange, events: &[u64]) -> SelectionOutcome {
    let count = events.len();
    if count < 8 {
        return SelectionOutcome::InsufficientEvents;
    }
    let intervals = (count - 1) as u64;
    let span = events[count - 1] - events[0];
    for pair in events[..count].windows(2) {
        let scaled = (pair[1] - pair[0]) * intervals * 100;
        if scaled < span * 99 || scaled > span * 101 {
            return SelectionOutcome::Irregular;
        }
    }
    let numerator = u64::from(rate) * 60000;
    let tempo = (numerator * intervals + span / 2) / span;
    if !(40000..=300000).contains(&tempo) {
        return SelectionOutcome::TempoOutOfRange;
    }
    let period = (u128::from(numerator) << 32) / u128::from(tempo);
    if period < (1u128 << 32) {
        return SelectionOutcome::TempoOutOfRange;
    }
    // A regular prefix/suffix cannot authorize missing beats in the rest of
    // the window. Apply the same declared 0.10-beat phase tolerance at edges.
    let anchor = u128::from(events[0]) << 32;
    let window_first = u128::from(range.first_frame) << 32;
    let window_end = u128::from(range.end_frame) << 32;
    if anchor >= period && anchor - period > window_first + period / 10 {
        return SelectionOutcome::Irregular;
    }
    if (u128::from(events[count - 1]) << 32) + period + period / 10 < window_end {
        return SelectionOutcome::Irregular;
    }
    for (index, &event) in events[..count].iter().enumerate() {
        let position = u128::from(event - events[0]) << 32;
        if position.abs_diff(period * index as u128) > period / 10 {
            return SelectionOutcome::Irregular;
        }
    }
    SelectionOutcome::Accepted(WindowEstimate {
        anchor_frame: events[0],
        tempo_millibpm: tempo as u32,
        confidence: 49,
    })
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec;

    // Complete fine-bin stage evidence; public Session/PCM tests are separate.
    fn evidence(range: FrameRange, events: &[u64]) -> std::vec::Vec<AttackBin> {
        let mut bins = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
        let first = range.first_frame / ATTACK_BIN_FRAMES;
        let end = range.end_frame.div_ceil(ATTACK_BIN_FRAMES);
        for index in first.saturating_sub(1)..end {
            bins[index as usize % ATTACK_BIN_CAPACITY] = AttackBin {
                index,
                sum: 0,
                count: (range.end_frame - index * ATTACK_BIN_FRAMES).min(ATTACK_BIN_FRAMES) as u16,
                first_attack_offset: 0,
                ..AttackBin::default()
            };
        }
        for &event in events {
            let bin = &mut bins[(event / ATTACK_BIN_FRAMES) as usize % ATTACK_BIN_CAPACITY];
            bin.sum = 12345;
            bin.first_attack_offset = (event % ATTACK_BIN_FRAMES) as u8;
        }
        bins
    }

    #[test]
    fn sparse_selector_refuses_gaps_edge_extrapolation_and_dense_audio() {
        let range = FrameRange {
            first_frame: 0,
            end_frame: WINDOW_FRAMES,
        };
        let events: std::vec::Vec<_> = (0..64).map(|n| 37 + n * 4096).collect();
        let bins = evidence(range, &events);
        let SelectionOutcome::Accepted(estimate) = estimate(8000, range, &bins) else {
            panic!()
        };
        assert_eq!(estimate.anchor_frame, 37);
        assert_eq!(estimate.tempo_millibpm, 117188);
        assert_eq!(estimate.confidence, 49);
        for missing in [20, 63] {
            let mut damaged = events.clone();
            damaged.remove(missing);
            assert_eq!(
                super::estimate(8000, range, &evidence(range, &damaged)),
                SelectionOutcome::Irregular
            );
        }
        assert_eq!(
            super::estimate(8000, range, &evidence(range, &events[2..])),
            SelectionOutcome::Irregular
        );
        let mut extra = events.clone();
        extra.insert(20, events[19] + 2048);
        assert_eq!(
            super::estimate(8000, range, &evidence(range, &extra)),
            SelectionOutcome::Irregular
        );
        assert_eq!(
            super::estimate(8000, range, &evidence(range, &events[..8])),
            SelectionOutcome::Irregular
        );
        let mut dense = bins;
        for bin in &mut dense[..1024] {
            bin.sum = 1;
        }
        assert_eq!(
            super::estimate(8000, range, &dense),
            SelectionOutcome::InsufficientEvents
        );
        assert_eq!(
            super::estimate(8000, range, &evidence(range, &[])),
            SelectionOutcome::InsufficientEvents
        );
    }

    #[test]
    fn sparse_selector_checks_identities_limits_and_boundary_clusters() {
        let range = FrameRange {
            first_frame: WINDOW_FRAMES,
            end_frame: 2 * WINDOW_FRAMES,
        };
        let events: std::vec::Vec<_> = (0..64).map(|n| range.first_frame + n * 4096).collect();
        let mut bins = evidence(range, &events);
        // First cluster began in the previous window. Skip it, without treating
        // the current bin's first change as a newly observed source onset.
        bins[1023].sum = 123;
        let SelectionOutcome::Accepted(accepted) = estimate(8000, range, &bins) else {
            panic!()
        };
        assert_eq!(accepted.anchor_frame, WINDOW_FRAMES + 4096);
        bins[1023].index = 999;
        assert_eq!(estimate(8000, range, &bins), SelectionOutcome::Unavailable);
        bins = evidence(range, &events);
        bins[1500].count = 255;
        assert_eq!(estimate(8000, range, &bins), SelectionOutcome::Unavailable);
        bins = evidence(range, &events);
        bins[1500].index += ATTACK_BIN_CAPACITY as u64;
        assert_eq!(estimate(8000, range, &bins), SelectionOutcome::Unavailable);
        let events: std::vec::Vec<_> = (0..129).map(|n| WINDOW_FRAMES + n * 512).collect();
        assert_eq!(
            estimate(8000, range, &evidence(range, &events)),
            SelectionOutcome::EventLimit
        );
        let events: std::vec::Vec<_> = (0..64).map(|n| WINDOW_FRAMES + n * 4096).collect();
        assert_eq!(
            estimate(48000, range, &evidence(range, &events)),
            SelectionOutcome::TempoOutOfRange
        );
        std::println!(
            "selection record={} bytes event scratch={} bytes",
            core::mem::size_of::<SelectionRecord>(),
            core::mem::size_of::<[u64; MAX_EVENTS]>()
        );
    }
}
