// SPDX-License-Identifier: Apache-2.0
//! Cooperative S6 broadband windows, global grids and revision identities.
use crate::{
    analysis::{correlation, prior, refine, OnsetBin},
    *,
};
pub const BIN_FRAMES: u64 = 2048;
pub const BIN_CAPACITY: usize = 16384;
pub const MAX_SEGMENTS: usize = 8;
pub const MAX_BEATS: usize = 3072;
pub(crate) const EMPTY: GridSegment = GridSegment {
    applicability_range: FrameRange {
        first_frame: 0,
        end_frame: 0,
    },
    anchor_position: FractionalFrame {
        whole_frame: 0,
        fraction_q32: 0,
    },
    anchor_ordinal: 0,
    frames_per_beat: FramePeriod {
        whole_frames: 0,
        fraction_q32: 0,
    },
    beat_count: 0,
    nominal_tempo_millibpm: 0,
    confidence: 0,
    state: FeatureState::Provisional,
    flags: 0,
    segment_id: 0,
    revision: 0,
};

pub struct GlobalAnalysis<'a> {
    bins: &'a mut [OnsetBin],
    flux: &'a mut [f32],
    beats: &'a mut [Beat],
    rate: u32,
    total: Option<u64>,
    dynamic: bool,
    active: bool,
    first: u64,
    end: u64,
    cursor: u64,
    refreshed_end: u64,
    refreshed_eof: bool,
    pending_segments: [GridSegment; 8],
    window_counts: [u32; 8],
    pending_count: usize,
    total_confidence: u32,
    windows: u32,
    degraded: bool,
    segments: [GridSegment; 8],
    final_segments: [GridSegment; 8],
    completed: bool,
    segment_count: usize,
    beat_count: usize,
    state: FeatureState,
    confidence: u8,
    flags: u32,
    representation: GridRepresentation,
    range: FrameRange,
    revision: Option<GridRevision>,
    signature: u64,
    serial: u64,
}
impl<'a> GlobalAnalysis<'a> {
    pub fn new(
        rate: u32,
        total: Option<u64>,
        dynamic: bool,
        bins: &'a mut [OnsetBin],
        flux: &'a mut [f32],
        beats: &'a mut [Beat],
    ) -> Result<Self, Error> {
        if rate == 0 || rate > 768000 {
            return Err(Error::InvalidArgument);
        }
        if bins.len() < BIN_CAPACITY || flux.len() < BIN_CAPACITY || beats.len() < MAX_BEATS {
            return Err(Error::BufferTooSmall);
        }
        bins[..BIN_CAPACITY].fill(OnsetBin::default());
        flux[..BIN_CAPACITY].fill(0.0);
        beats[..MAX_BEATS].fill(Beat::default());
        Ok(Self {
            bins: &mut bins[..BIN_CAPACITY],
            flux: &mut flux[..BIN_CAPACITY],
            beats: &mut beats[..MAX_BEATS],
            rate,
            total,
            dynamic,
            active: false,
            first: 0,
            end: 0,
            cursor: 0,
            refreshed_end: 0,
            refreshed_eof: false,
            pending_segments: [EMPTY; 8],
            window_counts: [0; 8],
            pending_count: 0,
            total_confidence: 0,
            windows: 0,
            degraded: false,
            segments: [EMPTY; 8],
            final_segments: [EMPTY; 8],
            completed: false,
            segment_count: 0,
            beat_count: 0,
            state: FeatureState::Provisional,
            confidence: 0,
            flags: 0,
            representation: GridRepresentation::Segments,
            range: FrameRange::default(),
            revision: None,
            signature: 0,
            serial: 0,
        })
    }
    pub(crate) fn preflight(&self, first: u64, count: usize) -> Result<(), Error> {
        if count != 0
            && first
                .checked_add(count as u64 - 1)
                .ok_or(Error::LimitExceeded)?
                / BIN_FRAMES
                > u64::from(u32::MAX)
        {
            Err(Error::LimitExceeded)
        } else {
            Ok(())
        }
    }
    pub(crate) fn push(&mut self, frame: u64, sample: f32) {
        let index = (frame / BIN_FRAMES) as u32;
        let b = &mut self.bins[index as usize % BIN_CAPACITY];
        if !b.occupied || b.index != index {
            *b = OnsetBin {
                index,
                occupied: true,
                ..OnsetBin::default()
            };
        }
        b.sum += (sample.abs().min(1.0) * 32768.0) as u32;
        b.count += 1;
    }
    fn bin(&self, index: u64) -> Option<&OnsetBin> {
        let b = &self.bins[index as usize % BIN_CAPACITY];
        (b.occupied && u64::from(b.index) == index).then_some(b)
    }
    fn complete(&self, index: u64, eof: Option<u64>) -> bool {
        let expected = eof.map_or(BIN_FRAMES, |e| {
            e.saturating_sub(index * BIN_FRAMES).min(BIN_FRAMES)
        });
        expected != 0
            && self
                .bin(index)
                .is_some_and(|b| u64::from(b.count) == expected)
    }
    fn energy(&self, index: u64) -> f32 {
        self.bin(index)
            .map_or(0.0, |b| b.sum as f32 / (b.count as f32 * 32768.0))
    }
    fn evidence(&self, eof: Option<u64>) -> Option<(u64, u64)> {
        // Walk only resident identities. Absolute sparse gaps cannot turn this
        // bounded ring into a scan over billions of absent source bins.
        let mut best = (0, 0);
        for b in self.bins.iter().filter(|b| b.occupied) {
            let first = u64::from(b.index);
            if !self.complete(first, eof) || (first != 0 && self.complete(first - 1, eof)) {
                continue;
            }
            let mut end = first + 1;
            while end - first < BIN_CAPACITY as u64 && self.complete(end, eof) {
                end += 1;
            }
            if end - first > best.1 - best.0 || (end - first == best.1 - best.0 && first < best.0) {
                best = (first, end);
            }
        }
        (best.1 > best.0).then_some(best)
    }
    fn requires(&self, end: u64, eof: Option<u64>) -> bool {
        end < self.refreshed_end
            || end.saturating_sub(self.refreshed_end) >= 32
            || (eof.is_some() && !self.refreshed_eof)
    }
    pub(crate) fn pending(&self, eof: Option<u64>) -> bool {
        self.active
            || self
                .evidence(eof)
                .is_some_and(|(f, e)| e - f >= 64 && self.requires(e, eof))
    }
    pub(crate) fn set_completed(&mut self, completed: bool) {
        self.completed = completed;
    }
    pub fn grid(&self) -> Option<GlobalGrid<'_>> {
        self.revision.map(|_| GlobalGrid {
            state: self.state,
            confidence: self.confidence,
            flags: self.flags,
            representation: self.representation,
            requested_range: FrameRange {
                first_frame: 0,
                end_frame: self.total.unwrap_or(self.end * BIN_FRAMES),
            },
            evidence_range: self.range,
            applicability_range: self.range,
            coverage_range: self.range,
            segments: if self.completed {
                &self.final_segments[..self.segment_count]
            } else {
                &self.segments[..self.segment_count]
            },
            beats: &self.beats[..self.beat_count],
        })
    }
    pub fn revision(&self) -> Option<GridRevision> {
        self.revision
    }
    pub(crate) fn proposal(&self) -> Option<(u32, u8)> {
        if self.segment_count == 0 {
            return None;
        }
        let mut best = 0;
        let mut support = 0u64;
        for (i, candidate) in self.segments[..self.segment_count].iter().enumerate() {
            let tempo = candidate.nominal_tempo_millibpm;
            let mut sum = 0u64;
            for s in &self.segments[..self.segment_count] {
                if u64::from(s.nominal_tempo_millibpm.abs_diff(tempo)) * 100 <= u64::from(tempo) {
                    let duration = s
                        .applicability_range
                        .end_frame
                        .saturating_sub(s.applicability_range.first_frame);
                    sum = sum.saturating_add(duration.saturating_mul(if s.confidence <= 100 {
                        u64::from(s.confidence) + 1
                    } else {
                        1
                    }));
                }
            }
            if sum > support {
                support = sum;
                best = i;
            }
        }
        Some((self.segments[best].nominal_tempo_millibpm, self.confidence))
    }
    pub(crate) fn revision_segment(
        &self,
        id: u32,
        local: GridSegment,
    ) -> Result<GridSegment, Error> {
        if id == 0 {
            return Err(Error::InvalidArgument);
        }
        let revision = self.revision.ok_or(Error::InvalidState)?;
        if revision.state != RevisionState::Pending {
            return Err(Error::InvalidState);
        }
        if revision.revision_id != id {
            return Err(Error::Conflict);
        }
        self.segments[..self.segment_count]
            .iter()
            .copied()
            .find(|s| {
                s.applicability_range.first_frame < local.applicability_range.end_frame
                    && local.applicability_range.first_frame < s.applicability_range.end_frame
            })
            .ok_or(Error::Conflict)
    }
    pub(crate) fn apply_revision(&mut self) -> Result<(), Error> {
        let serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        self.revision.as_mut().ok_or(Error::InvalidState)?.state = RevisionState::Applied;
        self.serial = serial;
        Ok(())
    }
    pub fn mutation_serial(&self) -> u64 {
        self.serial
    }
    pub(crate) fn refresh(
        &mut self,
        steps: u32,
        eof: Option<u64>,
        fallback: Option<TempoValue>,
        locked: Option<GridSegment>,
        deadline: &mut crate::deadline::Deadline<'_>,
    ) -> Result<u32, Error> {
        let mut done = 0;
        loop {
            if !self.active {
                let Some((first, end)) = self.evidence(eof) else {
                    return Ok(done);
                };
                if end - first < 64 || !self.requires(end, eof) || done == steps {
                    return Ok(done);
                }
                let mut previous = 0.0;
                for i in first..end {
                    let energy = self.energy(i);
                    self.flux[(i - first) as usize] = (energy - previous).max(0.0);
                    previous = energy;
                }
                self.first = first;
                self.end = end;
                self.cursor = first;
                self.pending_segments = [EMPTY; 8];
                self.window_counts = [0; 8];
                self.pending_count = 0;
                self.total_confidence = 0;
                self.windows = 0;
                self.degraded = false;
                self.active = true;
                done += 1;
            }
            if done == steps {
                return Ok(done);
            }
            if self.cursor < self.end {
                let end = (self.cursor + 128).min(self.end);
                self.flux[(self.cursor - self.first) as usize] = self.energy(self.cursor);
                if let Some((tempo, phase, confidence)) = self.window(self.cursor, end) {
                    self.add_window(self.cursor, end, tempo, phase, confidence);
                }
                self.cursor = end;
                done += 1;
            } else {
                if self.windows == 0 {
                    if let Some(t) = fallback {
                        self.degraded = true;
                        self.add_window(self.first, self.end, t.tempo_millibpm, 0, t.confidence);
                    }
                }
                self.commit(eof, locked)?;
                self.refreshed_end = self.end;
                self.refreshed_eof = eof.is_some();
                self.active = false;
                done += 1;
            }
            if deadline.expired() {
                return Ok(done);
            }
        }
    }
    fn window(&self, first: u64, end: u64) -> Option<(u32, u32, u8)> {
        if end - first < 64 {
            return None;
        }
        let flux = &self.flux[(first - self.first) as usize..(end - self.first) as usize];
        let minimum = (u64::from(self.rate) * 60)
            .div_ceil(300 * BIN_FRAMES)
            .max(1) as u32;
        let maximum = (u64::from(self.rate) * 60 / (40 * BIN_FRAMES)).min((end - first) / 2) as u32;
        let mut best = 0.0;
        let mut best_correlation = 0.0;
        let mut lag = 0;
        for l in minimum..=maximum {
            let c = correlation(flux, l);
            let score = c * prior(self.tempo_at_lag(l));
            if score > best {
                best = score;
                best_correlation = c;
                lag = l;
            }
        }
        if lag == 0 || best_correlation < 0.04 {
            return None;
        }
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
        let offset = refine(flux, lag);
        let base = self.tempo_at_lag(lag);
        let tempo = if offset == 0.0 {
            base
        } else {
            (base as f32 * lag as f32 / (lag as f32 + offset) + 0.5) as u32
        };
        if !(40000..=300000).contains(&tempo) {
            return None;
        }
        // C's confidence multiplication is deliberately double precision here.
        Some((
            tempo,
            phase,
            (40 + if best >= 1.0 {
                50
            } else {
                (f64::from(best) * 50.0) as u32
            })
            .min(100) as u8,
        ))
    }
    fn tempo_at_lag(&self, lag: u32) -> u32 {
        let d = u64::from(lag) * BIN_FRAMES;
        ((u64::from(self.rate) * 60000 + d / 2) / d) as u32
    }
    fn add_window(&mut self, first: u64, end: u64, tempo: u32, phase: u32, confidence: u8) {
        let difference = if self.pending_count == 0 {
            u32::MAX
        } else {
            self.pending_segments[self.pending_count - 1]
                .nominal_tempo_millibpm
                .abs_diff(tempo)
        };
        if self.pending_count == 0 || difference > 1500 {
            if self.pending_count == 8 {
                let segment = &mut self.pending_segments[7];
                segment.applicability_range.end_frame = end * BIN_FRAMES;
                segment.flags |= 128;
                self.degraded = true;
                return;
            }
            let segment = &mut self.pending_segments[self.pending_count];
            *segment = EMPTY;
            segment.applicability_range = FrameRange {
                first_frame: first * BIN_FRAMES,
                end_frame: end * BIN_FRAMES,
            };
            segment.anchor_position.whole_frame = (first + u64::from(phase)) * BIN_FRAMES;
            segment.nominal_tempo_millibpm = tempo;
            segment.confidence = confidence;
            segment.segment_id = self.pending_count as u32 + 1;
            self.window_counts[self.pending_count] = 1;
            self.pending_count += 1;
        } else {
            let index = self.pending_count - 1;
            let count = self.window_counts[index];
            let segment = &mut self.pending_segments[index];
            segment.nominal_tempo_millibpm =
                ((u64::from(segment.nominal_tempo_millibpm) * u64::from(count) + u64::from(tempo))
                    / u64::from(count + 1)) as u32;
            segment.confidence = ((u32::from(segment.confidence) * count + u32::from(confidence))
                / (count + 1)) as u8;
            segment.applicability_range.end_frame = end * BIN_FRAMES;
            self.window_counts[index] = count + 1;
        }
        self.total_confidence += u32::from(confidence);
        self.windows += 1;
    }
    fn commit(&mut self, eof: Option<u64>, locked: Option<GridSegment>) -> Result<(), Error> {
        if self.pending_count == 0 {
            return Ok(());
        }
        let old_state = self.state;
        self.segments = self.pending_segments;
        self.segment_count = self.pending_count;
        self.beat_count = 0;
        self.flags = 0;
        self.confidence = self
            .total_confidence
            .checked_div(self.windows)
            .map_or(255, |c| c as u8);
        self.state = if self.end - self.first >= 256 && self.confidence >= 50 {
            FeatureState::Stable
        } else {
            FeatureState::Provisional
        };
        if self.first == 0 && eof.is_some_and(|e| self.end * BIN_FRAMES >= e) {
            self.state = FeatureState::Final;
        }
        let dynamic = self.segment_count > 1;
        self.flags = if dynamic { 2 } else { 0 } | if self.degraded { 128 } else { 0 };
        self.representation = if self.dynamic || dynamic {
            GridRepresentation::Hybrid
        } else {
            GridRepresentation::Segments
        };
        let numerator = u64::from(self.rate) * 60000;
        for (i, s) in self.segments[..self.segment_count].iter_mut().enumerate() {
            if i + 1 == self.segment_count {
                if let Some(e) = eof {
                    s.applicability_range.end_frame = s.applicability_range.end_frame.min(e);
                }
            }
            s.state = self.state;
            s.flags |= self.flags;
            let tempo = u64::from(s.nominal_tempo_millibpm);
            s.frames_per_beat = FramePeriod {
                whole_frames: numerator / tempo,
                fraction_q32: (((numerator % tempo) << 32) / tempo) as u32,
            };
            let period =
                s.frames_per_beat.whole_frames + u64::from(s.frames_per_beat.fraction_q32 != 0);
            let mut anchor = s.anchor_position.whole_frame;
            if anchor < s.applicability_range.first_frame {
                anchor += (s.applicability_range.first_frame - anchor).div_ceil(period) * period;
            }
            s.beat_count = if anchor >= s.applicability_range.end_frame {
                0
            } else {
                (1 + (s.applicability_range.end_frame - 1 - anchor) / period)
                    .min(u64::from(u32::MAX)) as u32
            };
        }
        self.range = FrameRange {
            first_frame: self.first * BIN_FRAMES,
            end_frame: eof.unwrap_or(u64::MAX).min(self.end * BIN_FRAMES),
        };
        if self.representation != GridRepresentation::Segments {
            self.generate_beats()?;
        }
        let signature = self.signature();
        let old = self.revision;
        let id = match old {
            None => 1,
            Some(r) if signature != self.signature => {
                r.revision_id.checked_add(1).ok_or(Error::LimitExceeded)?
            }
            Some(r) => r.revision_id,
        };
        let previous = match old {
            None => 0,
            Some(r) if signature != self.signature => r.revision_id,
            Some(r) => r.previous_revision_id,
        };
        for s in &mut self.segments[..self.segment_count] {
            s.revision = id;
        }
        for b in &mut self.beats[..self.beat_count] {
            b.revision = id;
        }
        let conflict = locked.filter(|local| {
            self.segments[..self.segment_count].iter().any(|global| {
                global.applicability_range.first_frame < local.applicability_range.end_frame
                    && local.applicability_range.first_frame < global.applicability_range.end_frame
                    && (global
                        .nominal_tempo_millibpm
                        .abs_diff(local.nominal_tempo_millibpm)
                        > 500
                        || global
                            .anchor_position
                            .whole_frame
                            .abs_diff(local.anchor_position.whole_frame)
                            > BIN_FRAMES)
            })
        });
        let revision_state = if conflict.is_some() {
            RevisionState::Pending
        } else {
            RevisionState::Applied
        };
        self.revision = Some(GridRevision {
            state: revision_state,
            confidence: self.confidence,
            flags: if dynamic { 2 } else { 0 }
                | if self.degraded { 4 } else { 0 }
                | if conflict.is_some() { 1 } else { 0 },
            revision_id: id,
            previous_revision_id: previous,
            proposed_representation: self.representation,
            proposed_segment_count: self.segment_count as u32,
            proposed_beat_count: self.beat_count as u32,
            affected_range: conflict.map_or(self.range, |s| s.applicability_range),
        });
        if signature != self.signature
            || self.state != old_state
            || old.is_some_and(|r| r.state != revision_state)
            || self.serial == 0
        {
            self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        }
        self.signature = signature;
        self.final_segments = self.segments;
        for s in &mut self.final_segments[..self.segment_count] {
            s.state = FeatureState::Final;
        }
        Ok(())
    }
    fn generate_beats(&mut self) -> Result<(), Error> {
        let mut ordinal = 0;
        for segment in &mut self.segments[..self.segment_count] {
            if segment.frames_per_beat.whole_frames == 0
                || segment.frames_per_beat.whole_frames > u64::from(u32::MAX)
                || segment.anchor_position.whole_frame > u64::from(u32::MAX)
                || segment.applicability_range.end_frame > u64::from(u32::MAX)
            {
                self.flags |= 128;
                continue;
            }
            let period = (segment.frames_per_beat.whole_frames << 32)
                | u64::from(segment.frames_per_beat.fraction_q32);
            let mut position = (segment.anchor_position.whole_frame << 32)
                | u64::from(segment.anchor_position.fraction_q32);
            let first = segment.applicability_range.first_frame << 32;
            let end = segment.applicability_range.end_frame << 32;
            if position < first {
                let steps = (first - position).div_ceil(period);
                position = position
                    .checked_add(steps.checked_mul(period).ok_or(Error::LimitExceeded)?)
                    .ok_or(Error::LimitExceeded)?;
            }
            segment.anchor_ordinal = ordinal;
            let mut count = 0;
            while position < end {
                if self.beat_count == MAX_BEATS {
                    self.flags |= 128;
                    break;
                }
                self.beats[self.beat_count] = Beat {
                    position: FractionalFrame {
                        whole_frame: position >> 32,
                        fraction_q32: position as u32,
                    },
                    ordinal,
                    revision: segment.revision,
                    flags: segment.flags,
                    confidence: segment.confidence,
                };
                self.beat_count += 1;
                count += 1;
                ordinal += 1;
                let Some(next) = position.checked_add(period) else {
                    break;
                };
                position = next;
            }
            segment.beat_count = count;
        }
        Ok(())
    }
    fn signature(&self) -> u64 {
        let mut hash = 1469598103934665603u64;
        let mut add = |value: u64| {
            for byte in value.to_le_bytes() {
                hash ^= u64::from(byte);
                hash = hash.wrapping_mul(1099511628211);
            }
        };
        add(self.segment_count as u64);
        add(self.beat_count as u64);
        add(self.representation as u64);
        for s in &self.segments[..self.segment_count] {
            add(s.applicability_range.first_frame);
            add(s.applicability_range.end_frame);
            add(s.anchor_position.whole_frame);
            add(u64::from(s.anchor_position.fraction_q32));
            add(s.frames_per_beat.whole_frames);
            add(u64::from(s.frames_per_beat.fraction_q32));
            add(u64::from(s.nominal_tempo_millibpm));
        }
        hash
    }
}
