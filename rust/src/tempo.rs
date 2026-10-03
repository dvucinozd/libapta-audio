// SPDX-License-Identifier: Apache-2.0
//! Allocation-free TEMP/LGRD payload validation and canonical writing.
use crate::{
    Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment, LocalGrid,
    TempoCandidate, TempoValue, TempoView,
};

pub const MAXIMUM_CANDIDATES: usize = 3;
pub const LOCAL_GRID_PAYLOAD_SIZE: usize = 144;
fn u16at(b: &[u8], n: usize) -> u16 {
    u16::from_le_bytes(b[n..n + 2].try_into().unwrap())
}
fn u32at(b: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(b[n..n + 4].try_into().unwrap())
}
fn u64at(b: &[u8], n: usize) -> u64 {
    u64::from_le_bytes(b[n..n + 8].try_into().unwrap())
}
fn range(b: &[u8], n: usize) -> FrameRange {
    FrameRange {
        first_frame: u64at(b, n),
        end_frame: u64at(b, n + 8),
    }
}
fn state(s: u8, partial: bool) -> Result<FeatureState, Error> {
    match s {
        2 if partial => Ok(FeatureState::Provisional),
        3 if partial => Ok(FeatureState::Stable),
        4 => Ok(FeatureState::Final),
        _ => Err(Error::Corrupt),
    }
}
fn valid_range(r: FrameRange) -> bool {
    r.first_frame < r.end_frame
}
fn valid_bpm(b: u32) -> bool {
    (40000..=300000).contains(&b)
}
fn candidate(b: &[u8]) -> TempoCandidate {
    TempoCandidate {
        tempo_millibpm: u32at(b, 0),
        score: u16at(b, 4),
        confidence: b[6],
        relation_to_selected: b[7],
        flags: u32at(b, 8),
    }
}
fn valid_candidate(c: TempoCandidate) -> bool {
    valid_bpm(c.tempo_millibpm) && c.confidence <= 100 && c.relation_to_selected <= 8
}
fn valid_selected(t: TempoValue) -> bool {
    t.state != FeatureState::Partial
        && t.confidence <= 100
        && valid_bpm(t.tempo_millibpm)
        && valid_range(t.evidence_range)
        && valid_range(t.applicability_range)
}

#[derive(Clone, Copy, Debug)]
pub struct TempoPayload<'a> {
    selected: TempoValue,
    candidates: &'a [u8],
}
impl<'a> TempoPayload<'a> {
    pub fn parse(payload: &'a [u8], partial: bool, strict: bool) -> Result<Self, Error> {
        if payload.len() < 56 || u16at(payload, 0) != 1 {
            return Err(Error::Corrupt);
        }
        let selected = TempoValue {
            state: state(payload[2], partial)?,
            confidence: payload[3],
            flags: u32at(payload, 4),
            tempo_millibpm: u32at(payload, 8),
            candidate_set_id: u32at(payload, 12),
            evidence_range: range(payload, 16),
            applicability_range: range(payload, 32),
        };
        let count = u32at(payload, 48) as usize;
        if !valid_selected(selected)
            || !(1..=MAXIMUM_CANDIDATES).contains(&count)
            || payload.len() != 56 + count * 16
            || (strict && u32at(payload, 52) != 0)
        {
            return Err(Error::Corrupt);
        }
        let candidates = &payload[56..];
        let mut previous = u16::MAX;
        for b in candidates.chunks_exact(16) {
            let c = candidate(b);
            if !valid_candidate(c) || c.score > previous || (strict && u32at(b, 12) != 0) {
                return Err(Error::Corrupt);
            }
            previous = c.score;
        }
        Ok(Self {
            selected,
            candidates,
        })
    }
    pub fn selected(&self) -> TempoValue {
        self.selected
    }
    pub fn candidate_count(&self) -> usize {
        self.candidates.len() / 16
    }
    pub fn candidate(&self, index: usize) -> Option<TempoCandidate> {
        let p = index.checked_mul(16)?;
        self.candidates.get(p..p.checked_add(16)?).map(candidate)
    }
    /// Copies to caller storage. Insufficient storage is left untouched.
    pub fn copy_candidates(&self, output: &mut [TempoCandidate]) -> Result<usize, Error> {
        if output.len() < self.candidate_count() {
            return Err(Error::BufferTooSmall);
        }
        for (o, b) in output.iter_mut().zip(self.candidates.chunks_exact(16)) {
            *o = candidate(b);
        }
        Ok(self.candidate_count())
    }
}
pub fn tempo_payload_size(tempo: &TempoView<'_>) -> Result<usize, Error> {
    if !valid_selected(tempo.selected)
        || !(1..=MAXIMUM_CANDIDATES).contains(&tempo.candidates.len())
    {
        return Err(Error::InvalidArgument);
    }
    let mut previous = u16::MAX;
    for c in tempo.candidates {
        if !valid_candidate(*c) || c.score > previous {
            return Err(Error::InvalidArgument);
        }
        previous = c.score;
    }
    Ok(56 + tempo.candidates.len() * 16)
}
fn put32(b: &mut [u8], n: usize, v: u32) {
    b[n..n + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(b: &mut [u8], n: usize, v: u64) {
    b[n..n + 8].copy_from_slice(&v.to_le_bytes());
}
fn putrange(b: &mut [u8], n: usize, r: FrameRange) {
    put64(b, n, r.first_frame);
    put64(b, n + 8, r.end_frame);
}
/// Validates all native values before touching the destination. Container writers
/// must mark the result partial whenever the selected state is not final.
pub fn write_tempo_payload(tempo: &TempoView<'_>, output: &mut [u8]) -> Result<usize, Error> {
    let size = tempo_payload_size(tempo)?;
    let b = output.get_mut(..size).ok_or(Error::BufferTooSmall)?;
    b.fill(0);
    b[0] = 1;
    let t = tempo.selected;
    b[2] = t.state as u8;
    b[3] = t.confidence;
    put32(b, 4, t.flags);
    put32(b, 8, t.tempo_millibpm);
    put32(b, 12, t.candidate_set_id);
    putrange(b, 16, t.evidence_range);
    putrange(b, 32, t.applicability_range);
    put32(b, 48, tempo.candidates.len() as u32);
    for (b, c) in b[56..].chunks_exact_mut(16).zip(tempo.candidates) {
        put32(b, 0, c.tempo_millibpm);
        b[4..6].copy_from_slice(&c.score.to_le_bytes());
        b[6] = c.confidence;
        b[7] = c.relation_to_selected;
        put32(b, 8, c.flags);
    }
    Ok(size)
}
/// LGRD version 1 contains exactly one segment and coverage range. Its nominal
/// tempo must agree with the enclosing TEMP selection.
pub fn parse_local_grid(
    b: &[u8],
    partial: bool,
    strict: bool,
    selected_tempo: u32,
) -> Result<LocalGrid, Error> {
    if b.len() != 144
        || u16at(b, 0) != 1
        || u32at(b, 8) != 1
        || u32at(b, 12) != 1
        || (strict && (u32at(b, 92) != 0 || u16at(b, 138) != 0 || u32at(b, 140) != 0))
    {
        return Err(Error::Corrupt);
    }
    let grid = LocalGrid {
        requested_range: range(b, 16),
        evidence_range: range(b, 32),
        applicability_range: range(b, 48),
        coverage: range(b, 64),
        state: state(b[2], partial)?,
        confidence: b[3],
        flags: u32at(b, 4),
        segment: GridSegment {
            applicability_range: range(b, 48),
            anchor_position: FractionalFrame {
                whole_frame: u64at(b, 80),
                fraction_q32: u32at(b, 88),
            },
            anchor_ordinal: u64at(b, 96) as i64,
            frames_per_beat: FramePeriod {
                whole_frames: u64at(b, 104),
                fraction_q32: u32at(b, 112),
            },
            beat_count: u32at(b, 116),
            nominal_tempo_millibpm: u32at(b, 120),
            segment_id: u32at(b, 124),
            revision: u32at(b, 128),
            flags: u32at(b, 132),
            state: state(b[136], partial)?,
            confidence: b[137],
        },
    };
    local_grid_payload_size(&grid, selected_tempo).map_err(|_| Error::Corrupt)?;
    Ok(grid)
}
pub fn local_grid_payload_size(g: &LocalGrid, selected_tempo: u32) -> Result<usize, Error> {
    let s = g.segment;
    if !valid_range(g.requested_range)
        || !valid_range(g.evidence_range)
        || !valid_range(g.applicability_range)
        || !valid_range(g.coverage)
        || g.confidence > 100
        || g.state == FeatureState::Partial
        || s.confidence > 100
        || s.state == FeatureState::Partial
        || s.frames_per_beat.whole_frames == 0
        || !valid_bpm(s.nominal_tempo_millibpm)
        || s.nominal_tempo_millibpm != selected_tempo
        || s.applicability_range != g.applicability_range
    {
        return Err(Error::InvalidArgument);
    }
    Ok(144)
}
/// Writes a canonical LGRD payload after complete validation. Container writers
/// must mark it partial whenever the grid or segment state is not final.
pub fn write_local_grid(
    g: &LocalGrid,
    selected_tempo: u32,
    output: &mut [u8],
) -> Result<usize, Error> {
    let size = local_grid_payload_size(g, selected_tempo)?;
    let b = output.get_mut(..size).ok_or(Error::BufferTooSmall)?;
    b.fill(0);
    b[0] = 1;
    b[2] = g.state as u8;
    b[3] = g.confidence;
    put32(b, 4, g.flags);
    put32(b, 8, 1);
    put32(b, 12, 1);
    putrange(b, 16, g.requested_range);
    putrange(b, 32, g.evidence_range);
    putrange(b, 48, g.applicability_range);
    putrange(b, 64, g.coverage);
    let s = g.segment;
    put64(b, 80, s.anchor_position.whole_frame);
    put32(b, 88, s.anchor_position.fraction_q32);
    put64(b, 96, s.anchor_ordinal as u64);
    put64(b, 104, s.frames_per_beat.whole_frames);
    put32(b, 112, s.frames_per_beat.fraction_q32);
    put32(b, 116, s.beat_count);
    put32(b, 120, s.nominal_tempo_millibpm);
    put32(b, 124, s.segment_id);
    put32(b, 128, s.revision);
    put32(b, 132, s.flags);
    b[136] = s.state as u8;
    b[137] = s.confidence;
    Ok(size)
}
