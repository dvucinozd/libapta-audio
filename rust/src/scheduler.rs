// SPDX-License-Identifier: Apache-2.0
//! Caller-owned overview request scheduling matching the C reference policy.
//! Deadlines are ordering hints, never timeouts. Terminal requests retain their
//! identifiers and slots. Other feature schedulers are not implemented here.
use crate::{
    result::WAVEFORM_OVERVIEW, Error, Focus, FrameRange, PcmDemand, RegionRequest, RequestProgress,
    RequestState, WaveformSpan,
};
pub const MAX_REQUESTS: usize = 16;
pub const MAX_PCM_REQUEST_FRAMES: u64 = 4096;
#[derive(Clone, Copy, Debug)]
pub struct RequestSlot {
    request: Option<RegionRequest>,
    state: RequestState,
    enqueue_serial: u64,
    skip_count: u8,
}
impl Default for RequestSlot {
    fn default() -> Self {
        Self {
            request: None,
            state: RequestState::Queued,
            enqueue_serial: 0,
            skip_count: 0,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ScheduleScore {
    pub(crate) effective_priority: u8,
    pub(crate) request_id: u32,
    pub(crate) soft_deadline_monotonic_ns: u64,
    pub(crate) enqueue_serial: u64,
}
impl Default for ScheduleScore {
    fn default() -> Self {
        Self {
            effective_priority: 0,
            request_id: 0,
            soft_deadline_monotonic_ns: 0,
            enqueue_serial: u64::MAX,
        }
    }
}
impl ScheduleScore {
    pub(crate) fn better_than(&self, other: &Self) -> bool {
        if self.effective_priority != other.effective_priority {
            return self.effective_priority > other.effective_priority;
        }
        match (
            self.soft_deadline_monotonic_ns,
            other.soft_deadline_monotonic_ns,
        ) {
            (0, 0) => {}
            (0, _) => return false,
            (_, 0) => return true,
            (a, b) if a != b => return a < b,
            _ => {}
        }
        if self.enqueue_serial != other.enqueue_serial {
            return self.enqueue_serial < other.enqueue_serial;
        }
        self.request_id < other.request_id
    }
}
impl RequestSlot {
    fn active(&self) -> bool {
        self.request.is_some()
            && !matches!(
                self.state,
                RequestState::Cancelled | RequestState::Failed | RequestState::Satisfied
            )
    }
    fn score(&self) -> ScheduleScore {
        if !self.active() {
            return ScheduleScore::default();
        }
        let r = self.request.unwrap();
        ScheduleScore {
            effective_priority: (u16::from(r.priority) + u16::from(self.skip_count) * 8).min(255)
                as u8,
            request_id: r.request_id,
            soft_deadline_monotonic_ns: r.soft_deadline_monotonic_ns,
            enqueue_serial: self.enqueue_serial,
        }
    }
}
pub struct Scheduler<'a> {
    slots: &'a mut [RequestSlot],
    total_frames: Option<u64>,
    requested_features: u64,
    focus: Option<Focus>,
    next_request_id: u32,
    next_enqueue_serial: u64,
}
fn overlap(a: FrameRange, b: FrameRange) -> bool {
    a.first_frame < b.end_frame && b.first_frame < a.end_frame
}
impl<'a> Scheduler<'a> {
    /// Initialize at most 16 supplied slots. A smaller slice is a smaller hard cap.
    /// Only overview work is supported; zero configured features disables demand.
    pub fn new(
        total_frames: Option<u64>,
        requested_features: u64,
        slots: &'a mut [RequestSlot],
    ) -> Result<Self, Error> {
        if total_frames == Some(u64::MAX) {
            return Err(Error::InvalidArgument);
        }
        if requested_features & !WAVEFORM_OVERVIEW != 0 {
            return Err(Error::Unsupported);
        }
        let n = slots.len().min(MAX_REQUESTS);
        let slots = &mut slots[..n];
        slots.fill(RequestSlot::default());
        Ok(Self {
            slots,
            total_frames,
            requested_features,
            focus: None,
            next_request_id: 1,
            next_enqueue_serial: 0,
        })
    }
    fn id_exists(&self, id: u32) -> bool {
        self.slots
            .iter()
            .any(|s| s.request.is_some_and(|r| r.request_id == id))
    }
    pub fn request_region(&mut self, mut request: RegionRequest) -> Result<u32, Error> {
        if request.range.first_frame >= request.range.end_frame || request.feature_mask == 0 {
            return Err(Error::InvalidArgument);
        }
        if request.feature_mask & !WAVEFORM_OVERVIEW != 0 {
            return Err(Error::Unsupported);
        }
        if request.feature_mask & !self.requested_features != 0 {
            return Err(Error::InvalidState);
        }
        if request.request_id != 0 && self.id_exists(request.request_id) {
            return Err(Error::Conflict);
        }
        let slot = self
            .slots
            .iter()
            .position(|s| s.request.is_none())
            .ok_or(Error::LimitExceeded)?;
        if request.request_id == 0 {
            loop {
                let id = self.next_request_id;
                self.next_request_id = self.next_request_id.wrapping_add(1);
                if id != 0 && !self.id_exists(id) {
                    request.request_id = id;
                    break;
                }
            }
        }
        let serial = self
            .next_enqueue_serial
            .checked_add(1)
            .ok_or(Error::LimitExceeded)?;
        self.slots[slot] = RequestSlot {
            request: Some(request),
            state: RequestState::Queued,
            enqueue_serial: serial,
            skip_count: 0,
        };
        self.next_enqueue_serial = serial;
        Ok(request.request_id)
    }
    pub fn cancel_region_request(&mut self, id: u32) -> Result<(), Error> {
        if id == 0 {
            return Err(Error::InvalidArgument);
        }
        let slot = self
            .slots
            .iter_mut()
            .find(|s| s.request.is_some_and(|r| r.request_id == id))
            .ok_or(Error::NotAvailable)?;
        slot.state = RequestState::Cancelled;
        Ok(())
    }
    pub fn request_progress(&self, id: u32) -> Result<RequestProgress, Error> {
        if id == 0 {
            return Err(Error::InvalidArgument);
        }
        let slot = self
            .slots
            .iter()
            .find(|s| s.request.is_some_and(|r| r.request_id == id))
            .ok_or(Error::NotAvailable)?;
        let r = slot.request.unwrap();
        Ok(RequestProgress {
            request_id: id,
            state: slot.state,
            requested_range: r.range,
            requested_features: r.feature_mask,
            satisfied_features: if slot.state == RequestState::Satisfied {
                r.feature_mask
            } else {
                0
            },
            progress_permille: match slot.state {
                RequestState::Satisfied => 1000,
                RequestState::PartiallySatisfied => 500,
                _ => 0,
            },
            diagnostic_code: 0,
        })
    }
    pub fn set_focus(&mut self, focus: Focus) -> Result<(), Error> {
        if focus.playhead_frame == u64::MAX {
            return Err(Error::InvalidArgument);
        }
        if focus.feature_mask & !WAVEFORM_OVERVIEW != 0 {
            return Err(Error::Unsupported);
        }
        if self.total_frames.is_some_and(|n| focus.playhead_frame > n) {
            return Err(Error::InvalidArgument);
        }
        self.focus = Some(focus);
        Ok(())
    }
    fn focus_range(&self) -> Option<(FrameRange, u8)> {
        self.focus
            .filter(|f| f.feature_mask & WAVEFORM_OVERVIEW != 0)
            .map(|f| {
                (
                    FrameRange {
                        first_frame: f.playhead_frame.saturating_sub(f.lookbehind_frames),
                        end_frame: f.playhead_frame.saturating_add(f.lookahead_frames),
                    },
                    f.priority,
                )
            })
    }
    /// Find the first unaccepted gap in the selected target. Accepted ranges must
    /// be nonempty, ordered and non-overlapping. A fully accepted selected region
    /// returns NotAvailable; it does not fall back to another region or focus.
    pub fn next_pcm_request(&mut self, accepted: &[FrameRange]) -> Result<PcmDemand, Error> {
        let mut previous = 0;
        for (i, r) in accepted.iter().enumerate() {
            if r.first_frame >= r.end_frame
                || (i != 0 && r.first_frame < previous)
                || self.total_frames.is_some_and(|n| r.end_frame > n)
            {
                return Err(Error::InvalidArgument);
            }
            previous = r.end_frame;
        }
        if self.requested_features & WAVEFORM_OVERVIEW == 0 {
            return Err(Error::NotAvailable);
        }
        let mut selected: Option<(RegionRequest, ScheduleScore)> = None;
        for slot in self.slots.iter().filter(|s| s.active()) {
            let score = slot.score();
            if selected.map_or(true, |(_, best)| score.better_than(&best)) {
                selected = Some((slot.request.unwrap(), score));
            }
        }
        let (mut target, priority, token) = if let Some((r, s)) = selected {
            (r.range, s.effective_priority, r.request_id)
        } else if let Some((r, p)) = self.focus_range() {
            (r, p, 0)
        } else {
            (FrameRange::default(), 32, 0)
        };
        if target.end_frame <= target.first_frame {
            target = if let Some(n) = self.total_frames {
                FrameRange {
                    first_frame: 0,
                    end_frame: n,
                }
            } else {
                let mut first = 0;
                for range in accepted {
                    if range.first_frame > first {
                        break;
                    }
                    first = range.end_frame;
                }
                FrameRange {
                    first_frame: first,
                    end_frame: first.saturating_add(MAX_PCM_REQUEST_FRAMES),
                }
            };
        }
        if let Some(n) = self.total_frames {
            target.end_frame = target.end_frame.min(n);
        }
        let mut first = target.first_frame;
        let mut end = target.end_frame;
        for r in accepted {
            if r.end_frame <= first {
                continue;
            }
            if r.first_frame > first {
                end = end.min(r.first_frame);
                break;
            }
            first = r.end_frame;
            if first >= end {
                break;
            }
        }
        if first >= end {
            return Err(Error::NotAvailable);
        }
        end = end.min(first.saturating_add(MAX_PCM_REQUEST_FRAMES));
        self.note_choice(token);
        Ok(PcmDemand {
            range: FrameRange {
                first_frame: first,
                end_frame: end,
            },
            feature_mask: WAVEFORM_OVERVIEW,
            priority,
            request_token: token,
        })
    }
    pub(crate) fn score_range(&self, range: FrameRange) -> ScheduleScore {
        let mut best = ScheduleScore::default();
        if let Some((focus, priority)) = self.focus_range() {
            if focus.first_frame < focus.end_frame && overlap(range, focus) {
                best.effective_priority = priority;
            }
        }
        for slot in self.slots.iter().filter(|s| s.active()) {
            if overlap(range, slot.request.unwrap().range) {
                let score = slot.score();
                if score.better_than(&best) {
                    best = score;
                }
            }
        }
        best
    }
    pub(crate) fn note_choice(&mut self, id: u32) {
        for slot in self.slots.iter_mut().filter(|s| s.active()) {
            if slot.request.unwrap().request_id == id {
                slot.skip_count = 0;
            } else {
                slot.skip_count = slot.skip_count.saturating_add(1).min(32);
            }
        }
    }
    /// Refresh against completed column spans, including the nominal extent of a
    /// short EOF column. Called after processing and before request aging.
    pub(crate) fn refresh_waveform(
        &mut self,
        complete: &[WaveformSpan],
        frames_per_column: u32,
    ) -> Result<(), Error> {
        if frames_per_column == 0 {
            return Err(Error::InvalidArgument);
        }
        let mut previous = 0u64;
        for (i, s) in complete.iter().enumerate() {
            let first = u64::from(s.first_column_index);
            let end = first + u64::from(s.column_count);
            if s.column_count == 0 || (i != 0 && first < previous) {
                return Err(Error::InvalidArgument);
            }
            previous = end;
        }
        for slot in self.slots.iter_mut().filter(|s| s.active()) {
            let r = slot.request.unwrap().range;
            let first = r.first_frame / u64::from(frames_per_column);
            let last = (r.end_frame - 1) / u64::from(frames_per_column);
            let mut cursor = first;
            let mut any = false;
            for s in complete {
                let start = u64::from(s.first_column_index);
                let end = start + u64::from(s.column_count);
                any |= start <= last && end > first;
                if start <= cursor && end > cursor {
                    cursor = end;
                }
            }
            slot.state = if last <= u64::from(u32::MAX) && cursor > last {
                RequestState::Satisfied
            } else if any {
                RequestState::PartiallySatisfied
            } else {
                RequestState::WaitingForPcm
            };
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn req(first: u64, end: u64) -> RegionRequest {
        RegionRequest {
            range: FrameRange {
                first_frame: first,
                end_frame: end,
            },
            feature_mask: WAVEFORM_OVERVIEW,
            soft_deadline_monotonic_ns: 0,
            request_id: 0,
            priority: 96,
        }
    }
    #[test]
    fn request_id_wrap_and_enqueue_exhaustion_are_bounded() {
        let mut slots = [RequestSlot::default(); 4];
        let mut s = Scheduler::new(None, WAVEFORM_OVERVIEW, &mut slots).unwrap();
        s.request_region(RegionRequest {
            request_id: u32::MAX,
            ..req(0, 1)
        })
        .unwrap();
        s.request_region(RegionRequest {
            request_id: 1,
            ..req(0, 1)
        })
        .unwrap();
        s.next_request_id = u32::MAX;
        assert_eq!(s.request_region(req(0, 1)).unwrap(), 2);
        s.next_enqueue_serial = u64::MAX;
        assert_eq!(s.request_region(req(0, 1)), Err(Error::LimitExceeded));
        assert_eq!(s.slots.iter().filter(|x| x.request.is_some()).count(), 3);
        assert_eq!(s.next_request_id, 4);
    }
    #[test]
    fn focus_scoring_request_ties_and_age_saturation() {
        let mut slots = [RequestSlot::default(); 2];
        let mut s = Scheduler::new(None, WAVEFORM_OVERVIEW, &mut slots).unwrap();
        s.set_focus(Focus {
            playhead_frame: 100,
            lookahead_frames: 100,
            feature_mask: WAVEFORM_OVERVIEW,
            priority: 96,
            ..Default::default()
        })
        .unwrap();
        let id = s.request_region(req(0, 100)).unwrap();
        let requested = s.score_range(FrameRange {
            first_frame: 0,
            end_frame: 100,
        });
        let focused = s.score_range(FrameRange {
            first_frame: 100,
            end_frame: 200,
        });
        assert!(requested.better_than(&focused));
        assert_eq!(focused.request_id, 0);
        for _ in 0..100 {
            s.note_choice(0)
        }
        assert_eq!(s.slots[0].skip_count, 32);
        assert_eq!(
            s.score_range(FrameRange {
                first_frame: 0,
                end_frame: 100
            })
            .effective_priority,
            255
        );
        s.note_choice(id);
        assert_eq!(s.slots[0].skip_count, 0);
    }
    #[test]
    fn request_progress_uses_column_completion_and_terminal_cancel() {
        let mut slots = [RequestSlot::default(); 3];
        let mut s = Scheduler::new(Some(100), WAVEFORM_OVERVIEW, &mut slots).unwrap();
        let crossing = s.request_region(req(0, 128)).unwrap();
        let eof = s.request_region(req(64, 127)).unwrap();
        let missing = s.request_region(req(192, 256)).unwrap();
        let mut spans = [WaveformSpan {
            first_frame: 64,
            end_frame: 100,
            first_column_index: 1,
            column_count: 1,
            data_column_offset: 0,
        }];
        s.refresh_waveform(&spans, 64).unwrap();
        let p = s.request_progress(crossing).unwrap();
        assert_eq!(
            (p.state, p.progress_permille, p.satisfied_features),
            (RequestState::PartiallySatisfied, 500, 0)
        );
        let p = s.request_progress(eof).unwrap();
        assert_eq!(
            (p.state, p.progress_permille, p.satisfied_features),
            (RequestState::Satisfied, 1000, WAVEFORM_OVERVIEW)
        );
        assert_eq!(
            s.request_progress(missing).unwrap().state,
            RequestState::WaitingForPcm
        );
        spans[0].first_column_index = 0;
        spans[0].column_count = 2;
        s.refresh_waveform(&spans, 64).unwrap();
        assert_eq!(
            s.request_progress(crossing).unwrap().state,
            RequestState::Satisfied
        );
        s.cancel_region_request(eof).unwrap();
        s.refresh_waveform(&spans, 64).unwrap();
        assert_eq!(
            s.request_progress(eof).unwrap().state,
            RequestState::Cancelled
        );
    }
    #[test]
    fn no_missing_demand_does_not_age_requests() {
        let mut slots = [RequestSlot::default(); 2];
        let mut s = Scheduler::new(Some(1024), WAVEFORM_OVERVIEW, &mut slots).unwrap();
        s.request_region(req(0, 512)).unwrap();
        s.request_region(req(512, 1024)).unwrap();
        for _ in 0..10 {
            assert_eq!(
                s.next_pcm_request(&[FrameRange {
                    first_frame: 0,
                    end_frame: 512
                }]),
                Err(Error::NotAvailable)
            );
        }
        assert!(s.slots.iter().all(|x| x.skip_count == 0));
    }
}
