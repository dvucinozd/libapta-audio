// SPDX-License-Identifier: Apache-2.0
//! Native broadband onset evidence and cooperative local tempo analysis.
//! The default C profile uses 256-frame bins and a 4096-bin evidence ring.
use crate::{
    Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment, LocalGrid,
    TempoCandidate, TempoValue, TempoView,
};

pub const BIN_FRAMES: u64 = 256;
pub const BIN_CAPACITY: usize = 4096;
const RATIOS: [(u32, u32, u8); 8] = [
    (3, 2, 3),
    (2, 3, 4),
    (2, 1, 2),
    (1, 2, 1),
    (3, 1, 6),
    (1, 3, 5),
    (4, 1, 8),
    (1, 4, 7),
];

#[derive(Clone, Copy, Debug, Default)]
pub struct OnsetBin {
    pub(crate) index: u32,
    pub(crate) sum: u32,
    pub(crate) count: u32,
    pub(crate) occupied: bool,
}

/// Caller storage holds both the mutable ring and the frozen refresh evidence.
pub struct Analysis<'a, B = &'a mut [OnsetBin], F = &'a mut [f32]> {
    lifetime: core::marker::PhantomData<&'a ()>,
    bins: B,
    flux: F,
    rate: u32,
    active: bool,
    first: u64,
    end: u64,
    next_lag: u32,
    maximum_lag: u32,
    scores: [f32; 3],
    offsets: [f32; 3],
    ambiguity: f32,
    phase: u32,
    fit: f32,
    lags: [u32; 3],
    refreshed_end: u64,
    refreshed_eof: bool,
    selected: Option<TempoValue>,
    candidates: [TempoCandidate; 3],
    count: usize,
    grid: Option<LocalGrid>,
    serial: u64,
    meter_enabled: bool,
    meter_source_serial: u64,
    meter_serial: u64,
    meter: Option<[crate::MeterSegment; 1]>,
    final_meter: Option<[crate::MeterSegment; 1]>,
    completed: bool,
    global_proposal: Option<(u32, u8)>,
    focus: Option<crate::Focus>,
    locked: bool,
    ensemble_attempt: Option<(u64, u64, u32, u32, u8)>,
}
impl<'a> Analysis<'a> {
    pub fn new(rate: u32, bins: &'a mut [OnsetBin], flux: &'a mut [f32]) -> Result<Self, Error> {
        Self::with_storage(rate, bins, flux)
    }
}
impl<'a, B, F> Analysis<'a, B, F>
where
    B: AsRef<[OnsetBin]> + AsMut<[OnsetBin]>,
    F: AsRef<[f32]> + AsMut<[f32]>,
{
    pub fn with_storage(rate: u32, mut bins: B, mut flux: F) -> Result<Self, Error> {
        if rate == 0 || rate > 768000 {
            return Err(Error::InvalidArgument);
        }
        if bins.as_ref().len() < BIN_CAPACITY || flux.as_ref().len() < BIN_CAPACITY {
            return Err(Error::BufferTooSmall);
        }
        bins.as_mut()[..BIN_CAPACITY].fill(OnsetBin::default());
        flux.as_mut()[..BIN_CAPACITY].fill(0.0);
        Ok(Self {
            lifetime: core::marker::PhantomData,
            bins,
            flux,
            rate,
            active: false,
            first: 0,
            end: 0,
            next_lag: 0,
            maximum_lag: 0,
            scores: [0.0; 3],
            offsets: [0.0; 3],
            ambiguity: 0.0,
            phase: 0,
            fit: 0.0,
            lags: [0; 3],
            refreshed_end: 0,
            refreshed_eof: false,
            selected: None,
            candidates: [TempoCandidate::default(); 3],
            count: 0,
            grid: None,
            serial: 0,
            meter_enabled: false,
            meter_source_serial: 0,
            meter_serial: 0,
            meter: None,
            final_meter: None,
            completed: false,
            global_proposal: None,
            focus: None,
            locked: false,
            ensemble_attempt: None,
        })
    }
    pub fn preflight(&self, first: u64, count: usize) -> Result<(), Error> {
        if count != 0
            && first
                .checked_add(count as u64 - 1)
                .ok_or(Error::LimitExceeded)?
                / BIN_FRAMES
                > u64::from(u32::MAX)
        {
            return Err(Error::LimitExceeded);
        }
        Ok(())
    }
    pub(crate) fn push(&mut self, frame: u64, sample: f32) {
        let index = (frame / BIN_FRAMES) as u32;
        let bin = &mut self.bins.as_mut()[index as usize % BIN_CAPACITY];
        if !bin.occupied || bin.index != index {
            *bin = OnsetBin {
                index,
                occupied: true,
                ..OnsetBin::default()
            };
        }
        bin.sum += (sample.abs().min(1.0) * 32768.0) as u32;
        bin.count += 1;
    }
    pub(crate) fn set_focus(&mut self, focus: crate::Focus) -> Result<(), Error> {
        if focus.playhead_frame == u64::MAX
            || focus.feature_mask & !crate::result::ALL_FEATURES != 0
        {
            return Err(Error::InvalidArgument);
        }
        self.focus = Some(focus);
        Ok(())
    }
    fn contextual_ranges(&self, evidence: FrameRange) -> (FrameRange, FrameRange) {
        let requested = self
            .focus
            .filter(|f| {
                f.feature_mask
                    & (crate::result::BPM
                        | crate::result::LOCAL_BEATGRID
                        | crate::result::GRID_LOCKING)
                    != 0
            })
            .map_or(evidence, |f| FrameRange {
                first_frame: f.playhead_frame.saturating_sub(f.lookbehind_frames),
                end_frame: f.playhead_frame.saturating_add(f.lookahead_frames),
            });
        let mut applicability = FrameRange {
            first_frame: requested.first_frame.max(evidence.first_frame),
            end_frame: requested.end_frame.min(evidence.end_frame),
        };
        if applicability.first_frame >= applicability.end_frame {
            applicability = evidence;
        }
        (requested, applicability)
    }
    pub(crate) fn lock_checkpoint(&self) -> (Option<LocalGrid>, bool, u64) {
        (self.grid, self.locked, self.serial)
    }
    pub(crate) fn restore_lock(&mut self, checkpoint: (Option<LocalGrid>, bool, u64)) {
        self.grid = checkpoint.0;
        self.locked = checkpoint.1;
        self.serial = checkpoint.2;
    }
    pub(crate) fn lock_range(&mut self, range: FrameRange) -> Result<(), Error> {
        if range.first_frame >= range.end_frame {
            return Err(Error::InvalidArgument);
        }
        let mut grid = self.grid.ok_or(Error::InvalidState)?;
        if !matches!(
            grid.segment.state,
            FeatureState::Stable | FeatureState::Final
        ) && !self.completed
        {
            return Err(Error::InvalidState);
        }
        if range.first_frame < grid.applicability_range.first_frame
            || range.end_frame > grid.applicability_range.end_frame
        {
            return Err(Error::Conflict);
        }
        if self.locked {
            return if range == grid.applicability_range {
                Ok(())
            } else {
                Err(Error::Conflict)
            };
        }
        grid.applicability_range = range;
        grid.coverage = range;
        grid.segment.applicability_range = range;
        grid.segment.flags |= 256;
        grid.flags = grid.segment.flags;
        grid.segment.beat_count = beat_count(&grid.segment);
        self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        self.grid = Some(grid);
        self.locked = true;
        Ok(())
    }
    pub(crate) fn apply_revision(
        &mut self,
        segment: GridSegment,
        dynamic: bool,
    ) -> Result<(), Error> {
        let mut grid = self.grid.ok_or(Error::InvalidState)?;
        let mut selected = self.selected.ok_or(Error::InvalidState)?;
        let serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        grid.segment.anchor_position = segment.anchor_position;
        grid.segment.anchor_ordinal = segment.anchor_ordinal;
        grid.segment.frames_per_beat = segment.frames_per_beat;
        grid.segment.nominal_tempo_millibpm = segment.nominal_tempo_millibpm;
        grid.segment.confidence = segment.confidence;
        grid.segment.revision = segment.revision;
        grid.segment.flags = segment.flags | 256;
        grid.segment.beat_count = beat_count(&grid.segment);
        grid.flags = grid.segment.flags;
        grid.confidence = segment.confidence;
        selected.tempo_millibpm = segment.nominal_tempo_millibpm;
        selected.confidence = segment.confidence;
        if dynamic {
            selected.flags |= 8;
        }
        self.grid = Some(grid);
        self.selected = Some(selected);
        self.serial = serial;
        Ok(())
    }
    pub(crate) fn set_global_proposal(&mut self, proposal: Option<(u32, u8)>) {
        self.global_proposal = proposal;
    }
    pub(crate) fn set_completed(&mut self, completed: bool) {
        self.completed = completed;
    }
    pub(crate) fn meter_enabled(&self) -> bool {
        self.meter_enabled
    }
    pub(crate) fn enable_meter(&mut self) -> Result<(), Error> {
        if self.meter_enabled {
            return Err(Error::InvalidState);
        }
        self.meter_enabled = true;
        Ok(())
    }
    pub fn meter(&self) -> Option<crate::Meter<'_>> {
        (if self.completed {
            self.final_meter.as_ref()
        } else {
            self.meter.as_ref()
        })
        .map(|segments| {
            let s = segments[0];
            crate::Meter {
                state: s.state,
                confidence: s.confidence,
                numerator: s.numerator,
                denominator: s.denominator,
                downbeat_frame: s.downbeat_frame,
                downbeat_ordinal: s.downbeat_ordinal,
                segments,
            }
        })
    }
    pub fn meter_serial(&self) -> u64 {
        self.meter_serial
    }
    pub(crate) fn meter_pending(&self) -> bool {
        self.meter_enabled && self.grid.is_some() && self.meter_source_serial != self.serial
    }
    pub(crate) fn refresh_meter(&mut self, steps: u32) -> Result<u32, Error> {
        if steps == 0 || !self.meter_pending() {
            return Ok(0);
        }
        self.meter_source_serial = self.serial;
        let grid = self.grid.unwrap();
        let tempo = self.selected.unwrap().tempo_millibpm;
        let d = u64::from(tempo) * BIN_FRAMES;
        let lag = (u64::from(self.rate) * 60000 + d / 2) / d;
        let anchor = grid.segment.anchor_position.whole_frame / BIN_FRAMES;
        let first = if anchor < self.first {
            anchor + (self.first - anchor).div_ceil(lag) * lag
        } else {
            anchor
        };
        if first >= self.end {
            return Ok(1);
        }
        let mut strengths = [0.0f32; 128];
        let mut count = 0;
        for bin in (first..self.end).step_by(lag as usize).take(128) {
            let offset = (bin - self.first) as usize;
            let mut value = self.flux.as_ref()[offset];
            if offset > 0 {
                value = value.max(self.flux.as_ref()[offset - 1]);
            }
            if offset + 1 < (self.end - self.first) as usize {
                value = value.max(self.flux.as_ref()[offset + 1]);
            }
            strengths[count] = value;
            count += 1;
        }
        if count < 12 {
            return Ok(1);
        }
        let (phase3, score3) = meter_phase(&strengths[..count], 3);
        let (phase4, score4) = meter_phase(&strengths[..count], 4);
        let (mut meter, mut phase, mut best, mut runner) = if score4 >= score3 {
            (4, phase4, score4, score3)
        } else {
            (3, phase3, score3, score4)
        };
        let mut confidence = meter_confidence(best, runner, count);
        if meter == 3 && confidence < 50 {
            meter = 4;
            phase = phase4;
            best = score4;
            runner = score3;
            confidence = meter_confidence(best, runner, count);
        }
        let ordinal = grid.segment.anchor_ordinal + ((first - anchor) / lag) as i64 + phase as i64;
        let position = crate::grid::segment_position_at_ordinal(&grid.segment, ordinal)
            .ok_or(Error::LimitExceeded)?;
        let mut next = crate::MeterSegment {
            first_frame: grid.applicability_range.first_frame,
            end_frame: grid.applicability_range.end_frame,
            downbeat_frame: position.whole_frame,
            downbeat_ordinal: ordinal,
            numerator: meter,
            denominator: 4,
            state: if count >= 24 {
                FeatureState::Stable
            } else {
                FeatureState::Provisional
            },
            confidence,
            segment_id: self.meter.map_or(0, |m| m[0].segment_id),
        };
        if self.meter != Some([next]) {
            next.segment_id = next.segment_id.checked_add(1).ok_or(Error::LimitExceeded)?;
            self.meter = Some([next]);
            let mut final_segment = next;
            final_segment.state = FeatureState::Final;
            self.final_meter = Some([final_segment]);
            self.meter_serial = self
                .meter_serial
                .checked_add(1)
                .ok_or(Error::LimitExceeded)?;
        }
        Ok(1)
    }
    fn bin(&self, index: u64) -> Option<&OnsetBin> {
        let b = &self.bins.as_ref()[index as usize % BIN_CAPACITY];
        (b.occupied && u64::from(b.index) == index).then_some(b)
    }
    fn complete(&self, index: u64, eof: Option<u64>) -> bool {
        let expected = eof.map_or(BIN_FRAMES, |end| {
            end.saturating_sub(index * BIN_FRAMES).min(BIN_FRAMES)
        });
        expected != 0
            && self
                .bin(index)
                .is_some_and(|b| u64::from(b.count) == expected)
    }
    fn evidence(&self, eof: Option<u64>) -> Option<(u64, u64)> {
        let maximum = self.bins.as_ref()[..BIN_CAPACITY]
            .iter()
            .filter(|b| b.occupied && self.complete(u64::from(b.index), eof))
            .map(|b| u64::from(b.index))
            .max()?;
        let minimum = maximum.saturating_sub(BIN_CAPACITY as u64 - 1);
        let mut best = (0, 0);
        let mut start = None;
        for i in minimum..=maximum + 1 {
            if i <= maximum && self.complete(i, eof) {
                if start.is_none() {
                    start = Some(i);
                }
            } else if let Some(first) = start.take() {
                if i - first > best.1 - best.0 {
                    best = (first, i);
                }
            }
        }
        (best.1 > best.0).then_some(best)
    }
    fn energy(&self, index: u64) -> f32 {
        self.bin(index)
            .map_or(0.0, |b| b.sum as f32 / (b.count as f32 * 32768.0))
    }
    pub fn tempo(&self) -> Option<TempoView<'_>> {
        self.selected.map(|selected| TempoView {
            selected,
            candidates: &self.candidates[..self.count],
        })
    }
    pub fn local_grid(&self) -> Option<LocalGrid> {
        self.grid
    }
    pub fn mutation_serial(&self) -> u64 {
        self.serial
    }
    pub fn pending(&self, eof: Option<u64>) -> bool {
        !self.locked
            && (self.active
                || self.evidence(eof).is_some_and(|(first, end)| {
                    end - first >= 512
                        && (u64::from(self.rate) * 60)
                            .div_ceil(300 * BIN_FRAMES)
                            .max(1)
                            <= (end - first) / 2
                        && (self.refreshed_end == 0
                            || end < self.refreshed_end
                            || end >= self.refreshed_end + 32
                            || (eof.is_some() && !self.refreshed_eof))
                }))
    }
    /// A fill step, four lags per sweep step, then one atomic commit step.
    /// Zero available steps defers work. Frozen flux survives ring replacement.
    pub(crate) fn refresh(
        &mut self,
        steps: u32,
        eof: Option<u64>,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<u32, Error> {
        if self.locked {
            self.active = false;
            if eof.is_some() {
                if let Some(t) = &mut self.selected {
                    if t.state != FeatureState::Final {
                        t.state = FeatureState::Final;
                        if let Some(g) = &mut self.grid {
                            g.state = FeatureState::Final;
                            g.segment.state = FeatureState::Final;
                        }
                        self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
                    }
                }
            }
            return Ok(0);
        }
        if let (Some(mut t), Some(mut g)) = (self.selected, self.grid) {
            let (requested, applicability) = self.contextual_ranges(t.evidence_range);
            if requested != g.requested_range || applicability != g.applicability_range {
                t.applicability_range = applicability;
                g.requested_range = requested;
                g.applicability_range = applicability;
                g.coverage = applicability;
                g.segment.applicability_range = applicability;
                g.segment.beat_count = beat_count(&g.segment);
                self.selected = Some(t);
                self.grid = Some(g);
                self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
            }
        }
        let mut done = 0;
        loop {
            if !self.active {
                let Some((first, end)) = self.evidence(eof) else {
                    return Ok(done);
                };
                if end - first < 512 {
                    return Ok(done);
                }
                let needs = self.refreshed_end == 0
                    || end < self.refreshed_end
                    || end >= self.refreshed_end + 32
                    || (eof.is_some() && !self.refreshed_eof);
                if !needs {
                    self.commit(eof, Some((first, end)))?;
                    return Ok(done);
                }
                let minimum = (u64::from(self.rate) * 60)
                    .div_ceil(300 * BIN_FRAMES)
                    .max(1) as u32;
                let maximum =
                    (u64::from(self.rate) * 60 / (40 * BIN_FRAMES)).min((end - first) / 2) as u32;
                if maximum < minimum || done == steps {
                    return Ok(done);
                }
                let mut previous = 0.0;
                for i in first..end {
                    let energy = self.energy(i);
                    self.flux.as_mut()[(i - first) as usize] = (energy - previous).max(0.0);
                    previous = energy;
                }
                self.first = first;
                self.end = end;
                self.next_lag = minimum;
                self.maximum_lag = maximum;
                self.scores = [0.0; 3];
                self.lags = [0; 3];
                self.active = true;
                done += 1;
            }
            if done == steps {
                return Ok(done);
            }
            let flux = &self.flux.as_ref()[..(self.end - self.first) as usize];
            if self.next_lag <= self.maximum_lag {
                let last = (self.next_lag + 3).min(self.maximum_lag);
                for lag in self.next_lag..=last {
                    let score = correlation(flux, lag) * prior(tempo_at_lag(self.rate, lag));
                    if score <= 0.0 {
                        continue;
                    }
                    for position in 0..3 {
                        if score > self.scores[position] {
                            for j in (position + 1..3).rev() {
                                self.scores[j] = self.scores[j - 1];
                                self.lags[j] = self.lags[j - 1];
                            }
                            self.scores[position] = score;
                            self.lags[position] = lag;
                            break;
                        }
                    }
                }
                self.next_lag = last + 1;
                done += 1;
                if deadline.expired() && self.next_lag <= self.maximum_lag {
                    return Ok(done);
                }
            } else {
                self.commit(eof, None)?;
                self.active = false;
                self.refreshed_end = self.end;
                let follow = self.evidence(eof).is_some_and(|(first, end)| {
                    first < self.first
                        || end < self.end
                        || (eof.is_some() && (first != self.first || end != self.end))
                        || end >= self.end + 32
                });
                self.refreshed_eof = eof.is_some() && !follow;
                done += 1;
                // C resumes newer evidence after committing a frozen scan, even
                // after the last lag sampled an expired clock. The next scan's
                // first lag supplies its next cooperative clock boundary.
                if !follow || done == steps || self.lags[0] == 0 || self.scores[0] < 0.05 {
                    return Ok(done);
                }
            }
        }
    }
    pub(crate) fn ensemble_pending(&self) -> bool {
        let (Some(t), Some((proposed, confidence))) = (self.selected, self.global_proposal) else {
            return false;
        };
        self.ensemble_clock_check()
            && self.ensemble_attempt
                != Some((self.first, self.end, t.tempo_millibpm, proposed, confidence))
    }
    // Sample C's cooperative clock even when native draining has already
    // consumed a rejected proposal against unchanged evidence.
    pub(crate) fn ensemble_clock_check(&self) -> bool {
        let (Some(t), Some((proposed, confidence))) = (self.selected, self.global_proposal) else {
            return false;
        };
        !self.locked
            && !self.active
            && (proposed as f32 - t.tempo_millibpm as f32).abs() / proposed as f32 > 0.01
            && (relation(t.tempo_millibpm, proposed) != 0
                || (confidence != 255 && t.confidence != 255 && confidence > t.confidence))
    }
    pub(crate) fn ensemble(
        &mut self,
        steps: u32,
        previous: Option<TempoValue>,
    ) -> Result<u32, Error> {
        if steps == 0 || !self.ensemble_pending() {
            return Ok(0);
        }
        let mut t = self.selected.unwrap();
        let (proposed, global_confidence) = self.global_proposal.unwrap();
        // Rejected proposals must not starve later stages under a one-step budget.
        // A changed evidence span, selection or proposal makes the attempt eligible again.
        self.ensemble_attempt = Some((
            self.first,
            self.end,
            t.tempo_millibpm,
            proposed,
            global_confidence,
        ));
        let lag_for = |tempo: u32| {
            let d = u64::from(tempo) * BIN_FRAMES;
            ((u64::from(self.rate) * 60000 + d / 2) / d) as u32
        };
        let proposed_lag = lag_for(proposed);
        let selected_lag = lag_for(t.tempo_millibpm);
        let minimum = (u64::from(self.rate) * 60)
            .div_ceil(300 * BIN_FRAMES)
            .max(1) as u32;
        let span = (self.end - self.first) as usize;
        if proposed_lag < minimum
            || proposed_lag > self.maximum_lag
            || selected_lag < minimum
            || selected_lag > self.maximum_lag
            || proposed_lag as usize >= span
            || selected_lag as usize >= span
        {
            return Ok(0);
        }
        let flux = &self.flux.as_ref()[..span];
        let offset = refine(flux, proposed_lag);
        let base = tempo_at_lag(self.rate, proposed_lag);
        let refined = if offset == 0.0 {
            base
        } else {
            (base as f32 * proposed_lag as f32 / (proposed_lag as f32 + offset) + 0.5) as u32
        };
        if !(40000..=300000).contains(&refined)
            || (refined as f32 - proposed as f32).abs() / proposed as f32 > 0.01
            || (refined as f32 - t.tempo_millibpm as f32).abs() / refined as f32 <= 0.01
        {
            return Ok(1);
        }
        let score = (correlation(flux, proposed_lag) * prior(refined) / self.scores[0] * 65535.0
            + 0.5)
            .clamp(0.0, 65535.0) as u16;
        let (selected_fit, _) = best_fit(flux, selected_lag);
        let (proposed_fit, phase) = best_fit(flux, proposed_lag);
        let existing = self.candidates[..self.count].iter().position(|c| {
            u64::from(c.tempo_millibpm.abs_diff(refined)) * 100 <= u64::from(refined)
        });
        let r = relation(t.tempo_millibpm, refined);
        if score < 55000
            || proposed_fit <= 0.0
            || proposed_fit <= selected_fit
            || (r == 0
                && (existing.is_none()
                    || global_confidence == 255
                    || t.confidence == 255
                    || global_confidence <= t.confidence))
        {
            return Ok(1);
        }
        let mut promoted = existing.map_or(
            TempoCandidate {
                tempo_millibpm: refined,
                ..TempoCandidate::default()
            },
            |i| self.candidates[i],
        );
        if score > promoted.score {
            promoted.score = score;
            promoted.confidence = (25 + u32::from(score) * 70 / 65535) as u8;
        }
        let index = existing.unwrap_or_else(|| {
            self.count = (self.count + 1).min(3);
            self.count - 1
        });
        for j in (1..=index).rev() {
            self.candidates[j] = self.candidates[j - 1];
        }
        if self.count > 1 {
            promoted.score = promoted.score.max(self.candidates[1].score);
        }
        self.candidates[0] = promoted;
        t.tempo_millibpm = promoted.tempo_millibpm;
        t.confidence = t.confidence.min(promoted.confidence);
        if r != 0 {
            t.flags |= 128;
        }
        if let Some(old) = previous {
            if t.tempo_millibpm == old.tempo_millibpm {
                t.candidate_set_id = old.candidate_set_id;
            } else if t.candidate_set_id == old.candidate_set_id {
                t.candidate_set_id = t
                    .candidate_set_id
                    .checked_add(1)
                    .ok_or(Error::LimitExceeded)?;
            }
        }
        let mut g = self.grid.unwrap();
        g.segment.nominal_tempo_millibpm = t.tempo_millibpm;
        let n = u64::from(self.rate) * 60000;
        let d = u64::from(t.tempo_millibpm);
        g.segment.frames_per_beat = FramePeriod {
            whole_frames: n / d,
            fraction_q32: (((n % d) << 32) / d) as u32,
        };
        g.segment.anchor_position = FractionalFrame {
            whole_frame: (self.first + u64::from(phase)) * BIN_FRAMES,
            fraction_q32: 0,
        };
        g.segment.anchor_ordinal = 0;
        g.segment.confidence = t.confidence;
        g.segment.revision = t.candidate_set_id;
        if r != 0 {
            g.segment.flags |= 128;
        }
        g.segment.beat_count = beat_count(&g.segment);
        for (i, c) in self.candidates[..self.count].iter_mut().enumerate() {
            c.relation_to_selected = relation(t.tempo_millibpm, c.tempo_millibpm);
            if i != 0 {
                if c.relation_to_selected != 0 {
                    t.flags |= 128;
                    g.segment.flags |= 128;
                }
                if c.relation_to_selected == 1 {
                    t.flags |= 1;
                    g.segment.flags |= 1;
                }
                if c.relation_to_selected == 2 {
                    t.flags |= 2;
                    g.segment.flags |= 2;
                }
            }
        }
        g.flags = g.segment.flags;
        g.confidence = t.confidence;
        self.selected = Some(t);
        self.grid = Some(g);
        self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        Ok(1)
    }
    fn commit(&mut self, eof: Option<u64>, cached: Option<(u64, u64)>) -> Result<(), Error> {
        if self.lags[0] == 0 || self.scores[0] < 0.05 {
            return Ok(());
        }
        let flux = &self.flux.as_ref()[..(self.end - self.first) as usize];
        let mut candidates = [TempoCandidate::default(); 3];
        let mut count = 0;
        for i in 0..3 {
            let lag = self.lags[i];
            if lag == 0 {
                continue;
            }
            let base = tempo_at_lag(self.rate, lag);
            let offset = if cached.is_some() {
                self.offsets[i]
            } else {
                let offset = refine(flux, lag);
                self.offsets[i] = offset;
                offset
            };
            let tempo = if offset == 0.0 {
                base
            } else {
                (base as f32 * lag as f32 / (lag as f32 + offset) + 0.5) as u32
            };
            if !(40000..=300000).contains(&tempo)
                || candidates[..count]
                    .iter()
                    .any(|c| c.tempo_millibpm.abs_diff(tempo) <= 500)
            {
                continue;
            }
            let score = (self.scores[i] / self.scores[0] * 65535.0 + 0.5).min(65535.0) as u16;
            candidates[count] = TempoCandidate {
                tempo_millibpm: tempo,
                score,
                confidence: (25 + u32::from(score) * 70 / 65535) as u8,
                ..TempoCandidate::default()
            };
            count += 1;
        }
        if count == 0 {
            return Ok(());
        }
        let mut endorsed = false;
        if let Some((tempo, _)) = self.global_proposal {
            let mut best = None;
            for (i, c) in candidates[..count].iter().enumerate().skip(1) {
                if c.score >= 55000
                    && (c.tempo_millibpm as f32 - tempo as f32).abs() / tempo as f32 <= 0.01
                    && best.map_or(true, |b: usize| c.score > candidates[b].score)
                {
                    best = Some(i);
                }
            }
            if let Some(i) = best {
                let mut c = candidates[i];
                for j in (1..=i).rev() {
                    candidates[j] = candidates[j - 1];
                }
                c.score = c.score.max(candidates[1].score);
                candidates[0] = c;
                endorsed = true;
            }
        }
        let selected = candidates[0].tempo_millibpm;
        let mut flags = if endorsed { 128 } else { 0 };
        for (i, c) in candidates[..count].iter_mut().enumerate() {
            c.relation_to_selected = relation(selected, c.tempo_millibpm);
            if i != 0 && u32::from(c.score) >= 65535 * 7 / 10 {
                if c.relation_to_selected != 0 {
                    flags |= 128;
                }
                if c.relation_to_selected == 1 {
                    flags |= 1;
                }
                if c.relation_to_selected == 2 {
                    flags |= 2;
                }
            }
        }
        if cached.is_none() {
            let minimum = (u64::from(self.rate) * 60)
                .div_ceil(300 * BIN_FRAMES)
                .max(1) as u32;
            let mut sibling = 0.0f32;
            for (n, d, _) in RATIOS {
                let lag = (self.lags[0] as f32 * d as f32 / n as f32 + 0.5) as u32;
                if lag >= minimum && lag <= self.maximum_lag && lag != self.lags[0] {
                    sibling =
                        sibling.max(correlation(flux, lag) * prior(tempo_at_lag(self.rate, lag)));
                }
            }
            let ratio = sibling / self.scores[0];
            let ambiguity = if ratio <= 0.85 {
                0.0
            } else {
                ((ratio - 0.85) / (1.0 - 0.85)).min(1.0)
            };
            let lag = self.lags[0];
            let mut phase = 0;
            let mut phase_score = -1.0;
            for p in 0..lag {
                let mut score = 0.0;
                for i in (p as usize..flux.len()).step_by(lag as usize) {
                    score += flux[i];
                }
                if score > phase_score {
                    phase_score = score;
                    phase = p;
                }
            }
            let mut on = 0.0;
            let mut off = 0.0;
            let mut on_count = 0;
            let mut off_count = 0;
            for (i, v) in flux.iter().enumerate() {
                let distance = (i as u32 + lag - phase % lag) % lag;
                if distance.min(lag - distance) <= 1 {
                    on += v;
                    on_count += 1;
                } else {
                    off += v;
                    off_count += 1;
                }
            }
            let fit = if on_count == 0 || off_count == 0 {
                0.0
            } else {
                let a = on / on_count as f32;
                let b = off / off_count as f32;
                if a + b <= 1e-12 {
                    0.0
                } else {
                    (a - b) / (a + b)
                }
            };
            self.ambiguity = ambiguity;
            self.phase = phase;
            self.fit = fit;
        }
        let ambiguity = self.ambiguity;
        let phase = self.phase;
        let fit = self.fit;
        if ambiguity > 0.0 {
            flags |= 128;
        }
        let (first, end) = cached.unwrap_or((self.first, self.end));
        let mut confidence = 35 + ((fit.max(0.0) * 2.0).min(1.0) * 50.0) as u32;
        if count > 1 {
            let separation = 1.0 - self.scores[1] / self.scores[0];
            if separation > 0.0 {
                confidence += (separation * 15.0) as u32;
            }
        }
        if end - first >= 1024 {
            confidence += 5;
        }
        confidence = (confidence.min(100) as f32 * (1.0 - ambiguity)) as u32;
        let range = FrameRange {
            first_frame: first * BIN_FRAMES,
            end_frame: eof.unwrap_or(u64::MAX).min(end * BIN_FRAMES),
        };
        let (requested, applicability) = self.contextual_ranges(range);
        let mut state = if end - first >= 1024 && confidence >= 50 {
            FeatureState::Stable
        } else {
            FeatureState::Provisional
        };
        if state == FeatureState::Stable && range.first_frame == 0 && eof == Some(range.end_frame) {
            state = FeatureState::Final;
        }
        let old = self.selected;
        let id = match old {
            None => 1,
            Some(t) if t.tempo_millibpm != selected => t
                .candidate_set_id
                .checked_add(1)
                .ok_or(Error::LimitExceeded)?,
            Some(t) => t.candidate_set_id,
        };
        let value = TempoValue {
            state,
            confidence: confidence as u8,
            flags,
            tempo_millibpm: selected,
            candidate_set_id: id,
            evidence_range: range,
            applicability_range: applicability,
        };
        let numerator = u64::from(self.rate) * 60000;
        let period = FramePeriod {
            whole_frames: numerator / u64::from(selected),
            fraction_q32: (((numerator % u64::from(selected)) << 32) / u64::from(selected)) as u32,
        };
        let anchor = (first + u64::from(phase)) * BIN_FRAMES;
        let rounded = period.whole_frames + u64::from(period.fraction_q32 != 0);
        let beats = if anchor >= range.end_frame {
            0
        } else {
            1 + (range.end_frame - 1 - anchor) / rounded
        };
        let mut segment = GridSegment {
            applicability_range: applicability,
            anchor_position: FractionalFrame {
                whole_frame: anchor,
                fraction_q32: 0,
            },
            anchor_ordinal: 0,
            frames_per_beat: period,
            beat_count: beats.min(u64::from(u32::MAX)) as u32,
            nominal_tempo_millibpm: selected,
            confidence: confidence as u8,
            state,
            flags,
            segment_id: 1,
            revision: id,
        };
        segment.beat_count = beat_count(&segment);
        self.grid = Some(LocalGrid {
            requested_range: requested,
            evidence_range: range,
            applicability_range: applicability,
            coverage: applicability,
            segment,
            state,
            confidence: confidence as u8,
            flags,
        });
        self.selected = Some(value);
        self.candidates = candidates;
        self.count = count;
        if old != Some(value) {
            self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        }
        Ok(())
    }
}
fn tempo_at_lag(rate: u32, lag: u32) -> u32 {
    let d = u64::from(lag) * BIN_FRAMES;
    ((u64::from(rate) * 60000 + d / 2) / d) as u32
}
pub(crate) fn prior(tempo: u32) -> f32 {
    let logarithm = libm::logf(tempo as f32 / 125000.0) / 0.55;
    libm::expf(-0.5 * logarithm * logarithm)
}
fn relation(selected: u32, candidate: u32) -> u8 {
    if selected == candidate {
        return 0;
    }
    for (n, d, r) in RATIOS {
        let expected = u64::from(selected) * u64::from(n) / u64::from(d);
        if u64::from(candidate).abs_diff(expected) <= expected / 50 + 1 {
            return r;
        }
    }
    0
}
pub(crate) fn correlation(flux: &[f32], lag: u32) -> f32 {
    let lag = lag as usize;
    if lag == 0 || lag >= flux.len() {
        return 0.0;
    }
    let mut numerator = 0.0;
    let mut left = 0.0;
    let mut right = 0.0;
    for i in lag..flux.len() {
        let a = flux[i];
        let b = flux[i - lag];
        numerator += a * b;
        left += a * a;
        right += b * b;
    }
    if left <= 1e-12 || right <= 1e-12 {
        0.0
    } else {
        numerator / libm::sqrtf(left * right)
    }
}
pub(crate) fn refine(flux: &[f32], lag: u32) -> f32 {
    let mut multiple = 16;
    while multiple > 1 && multiple * lag > flux.len() as u32 / 2 {
        multiple -= 1;
    }
    if multiple < 2 {
        return 0.0;
    }
    let window = multiple / 2;
    let mut best = multiple * lag;
    let mut score = 0.0;
    for extended in multiple * lag - window..=multiple * lag + window {
        let s = correlation(flux, extended);
        if s > score {
            score = s;
            best = extended;
        }
    }
    if score <= 0.0 {
        0.0
    } else {
        (best as f32 / multiple as f32 - lag as f32).clamp(-0.5, 0.5)
    }
}

fn meter_phase(strengths: &[f32], meter: usize) -> (usize, f32) {
    let mut phase = 0;
    let mut best = -1.0;
    for p in 0..meter {
        let mut on = 0.0;
        let mut off = 0.0;
        let mut on_count = 0;
        let mut off_count = 0;
        for (i, v) in strengths.iter().enumerate() {
            if i % meter == p {
                on += v;
                on_count += 1;
            } else {
                off += v;
                off_count += 1;
            }
        }
        let score = if on_count == 0 || off_count == 0 {
            0.0
        } else {
            let a = on / on_count as f32;
            let b = off / off_count as f32;
            if a + b > 1e-12 {
                (a - b) / (a + b)
            } else {
                0.0
            }
        };
        if score > best {
            phase = p;
            best = score;
        }
    }
    (phase, best.max(0.0))
}
fn meter_confidence(best: f32, runner: f32, count: usize) -> u8 {
    (25 + (best * 45.0 + 0.5) as u32
        + ((best - runner).max(0.0) * 30.0 + 0.5) as u32
        + if count >= 24 { 10 } else { 0 })
    .min(100) as u8
}

/// Accepted C model, unchanged. The model can only lower raw BPM confidence.
pub fn calibrated_bpm_confidence(raw: u8) -> u8 {
    const LUT: [u8; 101] = [
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 31, 31, 38, 45, 45, 45, 45,
        45, 45, 45, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
        76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 93, 95, 96, 97, 98,
        99, 100,
    ];
    LUT.get(raw as usize).copied().unwrap_or(raw)
}
/// Exact overflow-free coverage, equivalent to C's decimal long division.
pub fn coverage_permille(covered: u64, total: u64) -> u16 {
    if total == 0 {
        0
    } else {
        ((u128::from(covered.min(total)) * 1000) / u128::from(total)) as u16
    }
}

fn beat_count(s: &GridSegment) -> u32 {
    let period = s.frames_per_beat.whole_frames + u64::from(s.frames_per_beat.fraction_q32 != 0);
    if period == 0 {
        return 0;
    }
    let mut anchor = s.anchor_position.whole_frame;
    if anchor < s.applicability_range.first_frame {
        let steps = (s.applicability_range.first_frame - anchor).div_ceil(period);
        let Some(next) = steps
            .checked_mul(period)
            .and_then(|d| anchor.checked_add(d))
        else {
            return 0;
        };
        anchor = next;
    }
    if anchor >= s.applicability_range.end_frame {
        0
    } else {
        (1 + (s.applicability_range.end_frame - 1 - anchor) / period).min(u64::from(u32::MAX))
            as u32
    }
}
fn best_fit(flux: &[f32], lag: u32) -> (f32, u32) {
    let mut best = -1.0;
    let mut phase = 0;
    for p in 0..lag {
        let mut score = 0.0;
        for i in (p as usize..flux.len()).step_by(lag as usize) {
            score += flux[i];
        }
        if score > best {
            best = score;
            phase = p;
        }
    }
    let mut on = 0.0;
    let mut off = 0.0;
    let mut on_count = 0;
    let mut off_count = 0;
    for (i, v) in flux.iter().enumerate() {
        let d = (i as u32 + lag - phase % lag) % lag;
        if d.min(lag - d) <= 1 {
            on += v;
            on_count += 1;
        } else {
            off += v;
            off_count += 1;
        }
    }
    if on_count == 0 || off_count == 0 {
        return (0.0, phase);
    }
    let a = on / on_count as f32;
    let b = off / off_count as f32;
    (
        if a + b > 1e-12 {
            (a - b) / (a + b)
        } else {
            0.0
        },
        phase,
    )
}
