// SPDX-License-Identifier: Apache-2.0
//! Allocation-free GGRD/REVN version 1 payloads. Parsing follows the baseline
//! reader; external construction additionally checks the builder contract.
use crate::{
    Beat, Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GlobalGrid,
    GridRepresentation, GridRevision, GridSegment, RevisionState,
};

pub const MAX_SEGMENTS: usize = 8;
pub const MAX_BEATS: usize = 3072;
pub const HEADER_SIZE: usize = 96;
pub const SEGMENT_SIZE: usize = 80;
pub const BEAT_SIZE: usize = 40;
pub const REVISION_SIZE: usize = 80;

#[derive(Clone, Copy, Debug)]
pub struct GridOptions {
    pub strict: bool,
    pub maximum_segments: usize,
    pub maximum_beats: usize,
}
impl Default for GridOptions {
    fn default() -> Self {
        Self {
            strict: true,
            maximum_segments: MAX_SEGMENTS,
            maximum_beats: MAX_BEATS,
        }
    }
}

fn u32at(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes(b[o..o + 4].try_into().unwrap())
}
fn u64at(b: &[u8], o: usize) -> u64 {
    u64::from_le_bytes(b[o..o + 8].try_into().unwrap())
}
fn range(b: &[u8], o: usize) -> FrameRange {
    FrameRange {
        first_frame: u64at(b, o),
        end_frame: u64at(b, o + 8),
    }
}
fn valid_range(r: FrameRange) -> bool {
    r.first_frame < r.end_frame
}
fn inside(r: FrameRange, outer: FrameRange) -> bool {
    r.first_frame >= outer.first_frame && r.end_frame <= outer.end_frame
}
fn state(v: u8, partial: bool) -> Result<FeatureState, Error> {
    match v {
        2 if partial => Ok(FeatureState::Provisional),
        3 if partial => Ok(FeatureState::Stable),
        4 => Ok(FeatureState::Final),
        _ => Err(Error::Corrupt),
    }
}
fn representation(v: u32) -> Result<GridRepresentation, Error> {
    match v {
        1 => Ok(GridRepresentation::Segments),
        2 => Ok(GridRepresentation::Explicit),
        3 => Ok(GridRepresentation::Hybrid),
        _ => Err(Error::Corrupt),
    }
}
fn counts(r: GridRepresentation, s: usize, b: usize) -> bool {
    match r {
        GridRepresentation::Segments => s > 0 && b == 0,
        GridRepresentation::Explicit => s == 0 && b > 0,
        GridRepresentation::Hybrid => s > 0 && b > 0,
    }
}
fn ordered(a: FractionalFrame, b: FractionalFrame) -> bool {
    (a.whole_frame, a.fraction_q32) <= (b.whole_frame, b.fraction_q32)
}
fn segment(b: &[u8]) -> GridSegment {
    GridSegment {
        applicability_range: range(b, 0),
        anchor_position: FractionalFrame {
            whole_frame: u64at(b, 16),
            fraction_q32: u32at(b, 24),
        },
        anchor_ordinal: u64at(b, 32) as i64,
        frames_per_beat: FramePeriod {
            whole_frames: u64at(b, 40),
            fraction_q32: u32at(b, 48),
        },
        beat_count: u32at(b, 52),
        nominal_tempo_millibpm: u32at(b, 56),
        segment_id: u32at(b, 60),
        revision: u32at(b, 64),
        flags: u32at(b, 68),
        state: state(b[72], true).unwrap(),
        confidence: b[73],
    }
}
fn beat(b: &[u8]) -> Beat {
    Beat {
        position: FractionalFrame {
            whole_frame: u64at(b, 0),
            fraction_q32: u32at(b, 8),
        },
        ordinal: u64at(b, 16) as i64,
        revision: u32at(b, 24),
        flags: u32at(b, 28),
        confidence: b[32],
    }
}
fn segment_valid(s: &GridSegment, applicability: FrameRange, partial: bool) -> bool {
    valid_range(s.applicability_range)
        && inside(s.applicability_range, applicability)
        && s.frames_per_beat.whole_frames != 0
        && (40000..=300000).contains(&s.nominal_tempo_millibpm)
        && state(s.state as u8, partial).is_ok()
        && s.confidence <= 100
}
fn beat_valid(b: &Beat, applicability: FrameRange) -> bool {
    b.confidence <= 100
        && b.position.whole_frame >= applicability.first_frame
        && b.position.whole_frame < applicability.end_frame
}

/// Validated immutable borrowed wire storage. Indexed access decodes values;
/// caller-owned copies allow the input bytes to be released independently.
#[derive(Clone, Copy, Debug)]
pub struct GridPayload<'a> {
    bytes: &'a [u8],
    segments: usize,
    beats: usize,
}
impl<'a> GridPayload<'a> {
    pub fn parse(bytes: &'a [u8], partial: bool, options: GridOptions) -> Result<Self, Error> {
        if bytes.len() < HEADER_SIZE
            || bytes[0..2] != [1, 0]
            || state(bytes[2], partial).is_err()
            || bytes[3] > 100
            || u32at(bytes, 12) != 1
        {
            return Err(Error::Corrupt);
        }
        let r = representation(u32at(bytes, 8))?;
        let segments = u32at(bytes, 16) as usize;
        let beats = u32at(bytes, 20) as usize;
        if segments > MAX_SEGMENTS || beats > MAX_BEATS || !counts(r, segments, beats) {
            return Err(Error::Corrupt);
        }
        if segments > options.maximum_segments || beats > options.maximum_beats {
            return Err(Error::LimitExceeded);
        }
        let expected = HEADER_SIZE
            .checked_add(segments.checked_mul(SEGMENT_SIZE).ok_or(Error::Corrupt)?)
            .and_then(|n| n.checked_add(beats.checked_mul(BEAT_SIZE)?))
            .ok_or(Error::Corrupt)?;
        if bytes.len() != expected
            || [24, 40, 56, 72]
                .iter()
                .any(|o| !valid_range(range(bytes, *o)))
            || (options.strict && bytes[88..96].iter().any(|b| *b != 0))
        {
            return Err(Error::Corrupt);
        }
        let p = Self {
            bytes,
            segments,
            beats,
        };
        let applicability = p.applicability_range();
        let mut previous = None;
        for i in 0..segments {
            let b = &bytes[HEADER_SIZE + i * SEGMENT_SIZE..HEADER_SIZE + (i + 1) * SEGMENT_SIZE];
            if state(b[72], partial).is_err()
                || (options.strict && (b[28..32].iter().chain(b[74..80].iter()).any(|v| *v != 0)))
            {
                return Err(Error::Corrupt);
            }
            let s = segment(b);
            if !segment_valid(&s, applicability, partial)
                || previous.is_some_and(|end| end > s.applicability_range.first_frame)
            {
                return Err(Error::Corrupt);
            }
            previous = Some(s.applicability_range.end_frame);
        }
        let mut previous: Option<Beat> = None;
        for i in 0..beats {
            let o = HEADER_SIZE + segments * SEGMENT_SIZE + i * BEAT_SIZE;
            let b = &bytes[o..o + BEAT_SIZE];
            let v = beat(b);
            if !beat_valid(&v, applicability)
                || (options.strict && b[12..16].iter().chain(b[33..40].iter()).any(|v| *v != 0))
                || previous
                    .is_some_and(|a| !ordered(a.position, v.position) || a.ordinal >= v.ordinal)
            {
                return Err(Error::Corrupt);
            }
            previous = Some(v);
        }
        Ok(p)
    }
    pub fn state(&self) -> FeatureState {
        state(self.bytes[2], true).unwrap()
    }
    pub fn confidence(&self) -> u8 {
        self.bytes[3]
    }
    pub fn flags(&self) -> u32 {
        u32at(self.bytes, 4)
    }
    pub fn representation(&self) -> GridRepresentation {
        representation(u32at(self.bytes, 8)).unwrap()
    }
    pub fn requested_range(&self) -> FrameRange {
        range(self.bytes, 24)
    }
    pub fn evidence_range(&self) -> FrameRange {
        range(self.bytes, 40)
    }
    pub fn applicability_range(&self) -> FrameRange {
        range(self.bytes, 56)
    }
    pub fn coverage_range(&self) -> FrameRange {
        range(self.bytes, 72)
    }
    pub fn segment_count(&self) -> usize {
        self.segments
    }
    pub fn beat_count(&self) -> usize {
        self.beats
    }
    pub fn segment(&self, index: usize) -> Option<GridSegment> {
        if index >= self.segments {
            return None;
        }
        let o = HEADER_SIZE + index * SEGMENT_SIZE;
        Some(segment(&self.bytes[o..o + SEGMENT_SIZE]))
    }
    pub fn beat(&self, index: usize) -> Option<Beat> {
        if index >= self.beats {
            return None;
        }
        let o = HEADER_SIZE + self.segments * SEGMENT_SIZE + index * BEAT_SIZE;
        Some(beat(&self.bytes[o..o + BEAT_SIZE]))
    }
    pub fn copy_into<'b>(
        &self,
        segments: &'b mut [GridSegment],
        beats: &'b mut [Beat],
    ) -> Result<GlobalGrid<'b>, Error> {
        if segments.len() < self.segments || beats.len() < self.beats {
            return Err(Error::BufferTooSmall);
        }
        for (i, s) in segments[..self.segments].iter_mut().enumerate() {
            *s = self.segment(i).unwrap()
        }
        for (i, b) in beats[..self.beats].iter_mut().enumerate() {
            *b = self.beat(i).unwrap()
        }
        Ok(GlobalGrid {
            state: self.state(),
            confidence: self.confidence(),
            flags: self.flags(),
            representation: self.representation(),
            requested_range: self.requested_range(),
            evidence_range: self.evidence_range(),
            applicability_range: self.applicability_range(),
            coverage_range: self.coverage_range(),
            segments: &segments[..self.segments],
            beats: &beats[..self.beats],
        })
    }
    /// Canonical reserved bytes are zero even after permissive parsing.
    pub fn write_canonical(&self, out: &mut [u8]) -> Result<usize, Error> {
        if out.len() < self.bytes.len() {
            return Err(Error::BufferTooSmall);
        }
        let n = self.bytes.len();
        out[..n].copy_from_slice(self.bytes);
        out[88..96].fill(0);
        for i in 0..self.segments {
            let o = HEADER_SIZE + i * SEGMENT_SIZE;
            out[o + 28..o + 32].fill(0);
            out[o + 74..o + 80].fill(0)
        }
        for i in 0..self.beats {
            let o = HEADER_SIZE + self.segments * SEGMENT_SIZE + i * BEAT_SIZE;
            out[o + 12..o + 16].fill(0);
            out[o + 33..o + 40].fill(0)
        }
        Ok(n)
    }
}

fn revision_valid(r: &GridRevision, partial: bool) -> bool {
    (partial || r.state == RevisionState::Applied)
        && r.confidence <= 100
        && r.flags & !7 == 0
        && r.revision_id != 0
        && valid_range(r.affected_range)
}
pub fn parse_revision(
    bytes: &[u8],
    grid: &GridPayload<'_>,
    partial: bool,
    strict: bool,
) -> Result<GridRevision, Error> {
    if bytes.len() != REVISION_SIZE
        || bytes[0..2] != [1, 0]
        || (strict
            && bytes[28..32]
                .iter()
                .chain(bytes[48..80].iter())
                .any(|b| *b != 0))
    {
        return Err(Error::Corrupt);
    }
    let r = GridRevision {
        state: match bytes[2] {
            1 => RevisionState::Pending,
            2 => RevisionState::Applied,
            _ => return Err(Error::Corrupt),
        },
        confidence: bytes[3],
        flags: u32at(bytes, 4),
        revision_id: u32at(bytes, 8),
        previous_revision_id: u32at(bytes, 12),
        proposed_representation: representation(u32at(bytes, 16))?,
        proposed_segment_count: u32at(bytes, 20),
        proposed_beat_count: u32at(bytes, 24),
        affected_range: range(bytes, 32),
    };
    if !revision_valid(&r, partial)
        || (r.state == RevisionState::Pending && grid.state() == FeatureState::Final)
        || r.proposed_representation != grid.representation()
        || r.proposed_segment_count as usize != grid.segment_count()
        || r.proposed_beat_count as usize != grid.beat_count()
        || (0..grid.segment_count()).any(|i| grid.segment(i).unwrap().revision != r.revision_id)
        || (0..grid.beat_count()).any(|i| grid.beat(i).unwrap().revision != r.revision_id)
    {
        return Err(Error::Corrupt);
    }
    Ok(r)
}
fn put32(b: &mut [u8], o: usize, v: u32) {
    b[o..o + 4].copy_from_slice(&v.to_le_bytes())
}
fn put64(b: &mut [u8], o: usize, v: u64) {
    b[o..o + 8].copy_from_slice(&v.to_le_bytes())
}
fn put_range(b: &mut [u8], o: usize, r: FrameRange) {
    put64(b, o, r.first_frame);
    put64(b, o + 8, r.end_frame)
}

/// Checks serializable wire behavior, without imposing external-builder-only
/// restrictions on values accepted by the C reader.
pub fn validate(grid: &GlobalGrid<'_>, partial: bool) -> Result<usize, Error> {
    if grid.segments.len() > MAX_SEGMENTS || grid.beats.len() > MAX_BEATS {
        return Err(Error::LimitExceeded);
    }
    if state(grid.state as u8, partial).is_err()
        || grid.confidence > 100
        || !counts(grid.representation, grid.segments.len(), grid.beats.len())
        || [
            grid.requested_range,
            grid.evidence_range,
            grid.applicability_range,
            grid.coverage_range,
        ]
        .iter()
        .any(|r| !valid_range(*r))
    {
        return Err(Error::InvalidArgument);
    }
    for (i, s) in grid.segments.iter().enumerate() {
        if !segment_valid(s, grid.applicability_range, partial)
            || (i > 0
                && grid.segments[i - 1].applicability_range.end_frame
                    > s.applicability_range.first_frame)
        {
            return Err(Error::InvalidArgument);
        }
    }
    for (i, b) in grid.beats.iter().enumerate() {
        if !beat_valid(b, grid.applicability_range)
            || (i > 0
                && (!ordered(grid.beats[i - 1].position, b.position)
                    || grid.beats[i - 1].ordinal >= b.ordinal))
        {
            return Err(Error::InvalidArgument);
        }
    }
    Ok(HEADER_SIZE + grid.segments.len() * SEGMENT_SIZE + grid.beats.len() * BEAT_SIZE)
}
pub fn write_payload(grid: &GlobalGrid<'_>, partial: bool, out: &mut [u8]) -> Result<usize, Error> {
    let size = validate(grid, partial)?;
    if out.len() < size {
        return Err(Error::BufferTooSmall);
    }
    let b = &mut out[..size];
    b.fill(0);
    b[0] = 1;
    b[2] = grid.state as u8;
    b[3] = grid.confidence;
    put32(b, 4, grid.flags);
    put32(b, 8, grid.representation as u32);
    put32(b, 12, 1);
    put32(b, 16, grid.segments.len() as u32);
    put32(b, 20, grid.beats.len() as u32);
    put_range(b, 24, grid.requested_range);
    put_range(b, 40, grid.evidence_range);
    put_range(b, 56, grid.applicability_range);
    put_range(b, 72, grid.coverage_range);
    for (i, s) in grid.segments.iter().enumerate() {
        let o = HEADER_SIZE + i * SEGMENT_SIZE;
        let b = &mut b[o..o + SEGMENT_SIZE];
        put_range(b, 0, s.applicability_range);
        put64(b, 16, s.anchor_position.whole_frame);
        put32(b, 24, s.anchor_position.fraction_q32);
        put64(b, 32, s.anchor_ordinal as u64);
        put64(b, 40, s.frames_per_beat.whole_frames);
        put32(b, 48, s.frames_per_beat.fraction_q32);
        put32(b, 52, s.beat_count);
        put32(b, 56, s.nominal_tempo_millibpm);
        put32(b, 60, s.segment_id);
        put32(b, 64, s.revision);
        put32(b, 68, s.flags);
        b[72] = s.state as u8;
        b[73] = s.confidence
    }
    for (i, v) in grid.beats.iter().enumerate() {
        let o = HEADER_SIZE + grid.segments.len() * SEGMENT_SIZE + i * BEAT_SIZE;
        let b = &mut b[o..o + BEAT_SIZE];
        put64(b, 0, v.position.whole_frame);
        put32(b, 8, v.position.fraction_q32);
        put64(b, 16, v.ordinal as u64);
        put32(b, 24, v.revision);
        put32(b, 28, v.flags);
        b[32] = v.confidence
    }
    Ok(size)
}
pub fn write_revision(
    r: &GridRevision,
    grid: &GlobalGrid<'_>,
    partial: bool,
    out: &mut [u8],
) -> Result<usize, Error> {
    validate(grid, partial)?;
    if !revision_valid(r, partial)
        || (r.state == RevisionState::Pending && grid.state == FeatureState::Final)
        || r.proposed_representation != grid.representation
        || r.proposed_segment_count as usize != grid.segments.len()
        || r.proposed_beat_count as usize != grid.beats.len()
        || grid.segments.iter().any(|s| s.revision != r.revision_id)
        || grid.beats.iter().any(|b| b.revision != r.revision_id)
    {
        return Err(Error::InvalidArgument);
    }
    if out.len() < REVISION_SIZE {
        return Err(Error::BufferTooSmall);
    }
    let b = &mut out[..REVISION_SIZE];
    b.fill(0);
    b[0] = 1;
    b[2] = r.state as u8;
    b[3] = r.confidence;
    put32(b, 4, r.flags);
    put32(b, 8, r.revision_id);
    put32(b, 12, r.previous_revision_id);
    put32(b, 16, r.proposed_representation as u32);
    put32(b, 20, r.proposed_segment_count);
    put32(b, 24, r.proposed_beat_count);
    put_range(b, 32, r.affected_range);
    Ok(REVISION_SIZE)
}

/// External-result builder validation. Source bounds, known flags, identifiers,
/// strict beat ordering, and hybrid Q32 positions are checked before publication.
pub fn validate_external(
    grid: &GlobalGrid<'_>,
    total_frames: Option<u64>,
    partial: bool,
) -> Result<usize, Error> {
    let n = validate(grid, partial)?;
    if grid.flags & !0x1ff != 0
        || grid.segments.iter().any(|s| s.flags & !0x1ff != 0)
        || grid.beats.iter().any(|b| b.flags & !0x1ff != 0)
    {
        return Err(Error::Unsupported);
    }
    if !inside(grid.coverage_range, grid.applicability_range)
        || [
            grid.requested_range,
            grid.evidence_range,
            grid.applicability_range,
            grid.coverage_range,
        ]
        .iter()
        .any(|r| total_frames.is_some_and(|t| r.end_frame > t))
    {
        return Err(Error::InvalidArgument);
    }
    for (i, s) in grid.segments.iter().enumerate() {
        if s.segment_id == 0
            || grid.segments[..i]
                .iter()
                .any(|p| p.segment_id == s.segment_id)
        {
            return Err(Error::InvalidArgument);
        }
    }
    for (i, b) in grid.beats.iter().enumerate() {
        if i > 0 && grid.beats[i - 1].position == b.position {
            return Err(Error::InvalidArgument);
        }
        if grid.representation == GridRepresentation::Hybrid {
            let s = grid
                .segments
                .iter()
                .find(|s| {
                    b.position.whole_frame >= s.applicability_range.first_frame
                        && b.position.whole_frame < s.applicability_range.end_frame
                })
                .ok_or(Error::InvalidArgument)?;
            if b.ordinal < s.anchor_ordinal {
                return Err(Error::InvalidArgument);
            }
            // Compute in u128 so the full signed-ordinal difference cannot overflow.
            let delta = (b.ordinal as i128 - s.anchor_ordinal as i128) as u128;
            let whole = delta
                .checked_mul(s.frames_per_beat.whole_frames as u128)
                .and_then(|n| n.checked_add(s.anchor_position.whole_frame as u128))
                .ok_or(Error::LimitExceeded)?;
            let fraction = delta * (s.frames_per_beat.fraction_q32 as u128);
            // Match the C builder's intermediate overflow limits.
            if whole > u64::MAX as u128 || fraction > u64::MAX as u128 {
                return Err(Error::LimitExceeded);
            }
            let fraction = fraction + s.anchor_position.fraction_q32 as u128;
            let whole = whole + (fraction >> 32);
            if whole > u64::MAX as u128 {
                return Err(Error::LimitExceeded);
            }
            if b.position.whole_frame != whole as u64 || b.position.fraction_q32 != fraction as u32
            {
                return Err(Error::InvalidArgument);
            }
        }
    }
    Ok(n)
}
pub fn validate_external_revision(
    r: &GridRevision,
    grid: &GlobalGrid<'_>,
    total_frames: Option<u64>,
    partial: bool,
) -> Result<(), Error> {
    validate_external(grid, total_frames, partial)?;
    let mut bytes = [0; REVISION_SIZE];
    write_revision(r, grid, partial, &mut bytes)?;
    if r.revision_id == r.previous_revision_id
        || !inside(r.affected_range, grid.applicability_range)
        || (r.flags & 2) != (grid.flags & 2)
    {
        return Err(Error::InvalidArgument);
    }
    Ok(())
}

/// Reference Q32 arithmetic including its u64 intermediate overflow limits.
pub fn segment_position_at_ordinal(segment: &GridSegment, ordinal: i64) -> Option<FractionalFrame> {
    if ordinal < segment.anchor_ordinal {
        return None;
    }
    let delta = (ordinal as u64).wrapping_sub(segment.anchor_ordinal as u64);
    let whole = delta
        .checked_mul(segment.frames_per_beat.whole_frames)?
        .checked_add(segment.anchor_position.whole_frame)?;
    let product = delta.checked_mul(segment.frames_per_beat.fraction_q32 as u64)?;
    let whole = whole.checked_add(product >> 32)?;
    let (fraction_q32, carry) = segment
        .anchor_position
        .fraction_q32
        .overflowing_add(product as u32);
    Some(FractionalFrame {
        whole_frame: whole.checked_add(carry as u64)?,
        fraction_q32,
    })
}
impl GridPayload<'_> {
    /// MTRD stores the whole frame only; a nonzero Q32 remainder is allowed.
    pub fn matches_downbeat(&self, frame: u64, ordinal: i64) -> bool {
        (0..self.beats).any(|i| {
            let b = self.beat(i).unwrap();
            b.position.whole_frame == frame && b.ordinal == ordinal
        }) || (0..self.segments).any(|i| {
            let s = self.segment(i).unwrap();
            frame >= s.applicability_range.first_frame
                && frame < s.applicability_range.end_frame
                && segment_position_at_ordinal(&s, ordinal).is_some_and(|p| p.whole_frame == frame)
        })
    }
}

impl crate::GridSegment {
    /// Read an authoritative beat inside the segment's applicability range,
    /// skipping a phase-continuity anchor before that range. The declared count
    /// bounds iteration; inconsistent geometry and arithmetic overflow fail.
    pub fn beat_at(&self, index: u32) -> Result<Option<crate::Beat>, Error> {
        if index >= self.beat_count {
            return Ok(None);
        }
        let period = (u128::from(self.frames_per_beat.whole_frames) << 32)
            | u128::from(self.frames_per_beat.fraction_q32);
        if period == 0 || self.applicability_range.first_frame >= self.applicability_range.end_frame
        {
            return Err(Error::InvalidArgument);
        }
        let anchor = (u128::from(self.anchor_position.whole_frame) << 32)
            | u128::from(self.anchor_position.fraction_q32);
        let first = u128::from(self.applicability_range.first_frame) << 32;
        let skip = first.saturating_sub(anchor).div_ceil(period);
        let delta = skip
            .checked_add(u128::from(index))
            .ok_or(Error::LimitExceeded)?;
        let ordinal = i128::from(self.anchor_ordinal)
            .checked_add(i128::try_from(delta).map_err(|_| Error::LimitExceeded)?)
            .ok_or(Error::LimitExceeded)?;
        let ordinal = i64::try_from(ordinal).map_err(|_| Error::LimitExceeded)?;
        let position = segment_position_at_ordinal(self, ordinal).ok_or(Error::LimitExceeded)?;
        if position.whole_frame < self.applicability_range.first_frame
            || position.whole_frame >= self.applicability_range.end_frame
        {
            return Err(Error::InvalidArgument);
        }
        Ok(Some(crate::Beat {
            position,
            ordinal,
            revision: self.revision,
            flags: self.flags,
            confidence: self.confidence,
        }))
    }
}

impl crate::FractionalFrame {
    /// Round a nonnegative Q32 sample coordinate to milliseconds, ties upward.
    /// Keeps a u64 result for portable consumers; narrower destination models
    /// must check their own width. Zero sample rate and overflow are errors.
    pub fn rounded_milliseconds(self, sample_rate: u32) -> Result<u64, Error> {
        if sample_rate == 0 {
            return Err(Error::InvalidArgument);
        }
        let frames = (u128::from(self.whole_frame) << 32) | u128::from(self.fraction_q32);
        let divisor = u128::from(sample_rate) << 32;
        u64::try_from((frames * 1000 + divisor / 2) / divisor).map_err(|_| Error::LimitExceeded)
    }
}
