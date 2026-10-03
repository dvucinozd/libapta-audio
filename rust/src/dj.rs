// SPDX-License-Identifier: Apache-2.0
//! Allocation-free MKEY, MTRD and CONF version 1 payloads.
//! Meter parsing checks local semantics. Result assembly must additionally call
//! `MeterView::validate_grid` when a local or global grid is present.
use crate::{Error, FeatureState, Key, KeyCandidate, Meter, MeterSegment, QualityRecord};
pub const MAX_KEY_CANDIDATES: usize = 24;
pub const MAX_METER_SEGMENTS: usize = 65536;
pub const MAX_QUALITY_RECORDS: usize = 11;
fn u16_at(b: &[u8], p: usize) -> u16 {
    u16::from_le_bytes(b[p..p + 2].try_into().unwrap())
}
fn u32_at(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
fn u64_at(b: &[u8], p: usize) -> u64 {
    u64::from_le_bytes(b[p..p + 8].try_into().unwrap())
}
fn state(v: u8, partial: bool) -> Result<FeatureState, Error> {
    match (v, partial) {
        (1, true) => Ok(FeatureState::Partial),
        (2, true) => Ok(FeatureState::Provisional),
        (3, true) => Ok(FeatureState::Stable),
        (4, _) => Ok(FeatureState::Final),
        _ => Err(Error::Corrupt),
    }
}
fn check(ok: bool) -> Result<(), Error> {
    if ok {
        Ok(())
    } else {
        Err(Error::Corrupt)
    }
}
fn confidence(v: u8) -> bool {
    v <= 100 || v == 255
}
fn range(first: u64, end: u64, total: Option<u64>) -> bool {
    first < end && total.map_or(true, |t| end <= t)
}
fn key_value(t: u8, m: u8, c: i16) -> bool {
    t < 12 && (m == 1 || m == 2) && (-100..=100).contains(&c)
}
fn meter_value(n: u16, d: u16) -> bool {
    (1..=32).contains(&n) && (1..=32).contains(&d) && d.is_power_of_two()
}
fn size(
    count: usize,
    limit: usize,
    cap: usize,
    head: usize,
    stride: usize,
) -> Result<usize, Error> {
    if count > limit.min(cap) {
        return Err(Error::LimitExceeded);
    }
    count
        .checked_mul(stride)
        .and_then(|n| n.checked_add(head))
        .ok_or(Error::LimitExceeded)
}
fn zero(b: &[u8]) -> bool {
    b.iter().all(|v| *v == 0)
}
fn candidate(b: &[u8]) -> KeyCandidate {
    KeyCandidate {
        tonic: b[0],
        mode: b[1],
        tuning_offset_cents: u16_at(b, 2) as i16,
        score: u16_at(b, 4),
        confidence: b[6],
    }
}
fn segment(b: &[u8]) -> MeterSegment {
    MeterSegment {
        first_frame: u64_at(b, 0),
        end_frame: u64_at(b, 8),
        downbeat_frame: u64_at(b, 16),
        downbeat_ordinal: u64_at(b, 24) as i64,
        numerator: u16_at(b, 32),
        denominator: u16_at(b, 34),
        state: state(b[36], true).unwrap(),
        confidence: b[37],
        segment_id: u32_at(b, 44),
    }
}
fn quality(b: &[u8]) -> QualityRecord {
    QualityRecord {
        feature: u64_at(b, 0),
        calibration_model_id: u32_at(b, 8),
        evidence_coverage_permille: u16_at(b, 12),
        confidence: b[14],
        state: state(b[15], true).unwrap(),
        flags: u32_at(b, 16),
    }
}
#[derive(Clone, Copy, Debug)]
pub struct KeyView<'a> {
    bytes: &'a [u8],
}
impl KeyView<'_> {
    pub fn state(&self) -> FeatureState {
        state(self.bytes[2], true).unwrap()
    }
    pub fn confidence(&self) -> u8 {
        self.bytes[3]
    }
    pub fn candidate_count(&self) -> usize {
        (self.bytes.len() - 40) / 16
    }
    pub fn candidate(&self, index: usize) -> Option<KeyCandidate> {
        if index >= self.candidate_count() {
            None
        } else {
            Some(candidate(&self.bytes[40 + index * 16..]))
        }
    }
    pub fn copy_into<'a>(&self, out: &'a mut [KeyCandidate]) -> Result<Key<'a>, Error> {
        let n = self.candidate_count();
        if out.len() < n {
            return Err(Error::BufferTooSmall);
        }
        for (i, v) in out[..n].iter_mut().enumerate() {
            *v = self.candidate(i).unwrap();
        }
        let b = self.bytes;
        Ok(Key {
            state: state(b[2], true)?,
            confidence: b[3],
            tonic: b[4],
            mode: b[5],
            tuning_offset_cents: u16_at(b, 6) as i16,
            first_frame: u64_at(b, 16),
            end_frame: u64_at(b, 24),
            candidates: &out[..n],
        })
    }
}
pub fn parse_key(
    b: &[u8],
    total: Option<u64>,
    partial: bool,
    limit: usize,
) -> Result<KeyView<'_>, Error> {
    check(b.len() >= 40)?;
    check(
        u16_at(b, 0) == 1
            && confidence(b[3])
            && key_value(b[4], b[5], u16_at(b, 6) as i16)
            && zero(&b[8..12])
            && range(u64_at(b, 16), u64_at(b, 24), total)
            && u32_at(b, 32) == 40
            && zero(&b[36..40]),
    )?;
    state(b[2], partial)?;
    let n = u32_at(b, 12) as usize;
    check(b.len() == size(n, limit, MAX_KEY_CANDIDATES, 40, 16)?)?;
    let view = KeyView { bytes: b };
    let mut found = n == 0;
    for i in 0..n {
        let r = &b[40 + i * 16..40 + (i + 1) * 16];
        let c = candidate(r);
        check(
            key_value(c.tonic, c.mode, c.tuning_offset_cents)
                && confidence(c.confidence)
                && zero(&r[7..16]),
        )?;
        if i > 0 {
            check(c.score < view.candidate(i - 1).unwrap().score)?;
        }
        for j in 0..i {
            let p = view.candidate(j).unwrap();
            check(
                (c.tonic, c.mode, c.tuning_offset_cents)
                    != (p.tonic, p.mode, p.tuning_offset_cents),
            )?;
        }
        found |= (c.tonic, c.mode, c.tuning_offset_cents) == (b[4], b[5], u16_at(b, 6) as i16);
    }
    check(found)?;
    Ok(view)
}
#[derive(Clone, Copy, Debug)]
pub struct MeterView<'a> {
    bytes: &'a [u8],
}
impl MeterView<'_> {
    pub fn state(&self) -> FeatureState {
        state(self.bytes[2], true).unwrap()
    }
    pub fn confidence(&self) -> u8 {
        self.bytes[3]
    }
    pub fn segment_count(&self) -> usize {
        (self.bytes.len() - 48) / 56
    }
    pub fn segment(&self, index: usize) -> Option<MeterSegment> {
        if index >= self.segment_count() {
            None
        } else {
            Some(segment(&self.bytes[48 + index * 56..]))
        }
    }
    /// Call once per segment in increasing source order. A matcher using monotonic
    /// grid cursors keeps cross-validation linear; match whole frame and ordinal.
    pub fn validate_grid(&self, mut matches: impl FnMut(u64, i64) -> bool) -> Result<(), Error> {
        for i in 0..self.segment_count() {
            let s = self.segment(i).unwrap();
            check(matches(s.downbeat_frame, s.downbeat_ordinal))?;
        }
        Ok(())
    }
    pub fn copy_into<'a>(&self, out: &'a mut [MeterSegment]) -> Result<Meter<'a>, Error> {
        let n = self.segment_count();
        if out.len() < n {
            return Err(Error::BufferTooSmall);
        }
        for (i, v) in out[..n].iter_mut().enumerate() {
            *v = self.segment(i).unwrap();
        }
        let b = self.bytes;
        Ok(Meter {
            state: state(b[2], true)?,
            confidence: b[3],
            numerator: u16_at(b, 4),
            denominator: u16_at(b, 6),
            downbeat_frame: u64_at(b, 16),
            downbeat_ordinal: u64_at(b, 24) as i64,
            segments: &out[..n],
        })
    }
}
pub fn parse_meter(
    b: &[u8],
    total: Option<u64>,
    partial: bool,
    limit: usize,
) -> Result<MeterView<'_>, Error> {
    check(b.len() >= 48)?;
    check(
        u16_at(b, 0) == 1
            && confidence(b[3])
            && meter_value(u16_at(b, 4), u16_at(b, 6))
            && zero(&b[8..12])
            && u32_at(b, 32) == 48
            && zero(&b[36..48]),
    )?;
    state(b[2], partial)?;
    let n = u32_at(b, 12) as usize;
    check(n != 0)?;
    check(b.len() == size(n, limit, MAX_METER_SEGMENTS, 48, 56)?)?;
    let mut previous: Option<MeterSegment> = None;
    for i in 0..n {
        let r = &b[48 + i * 56..48 + (i + 1) * 56];
        state(r[36], partial)?;
        let s = segment(r);
        check(
            range(s.first_frame, s.end_frame, total)
                && s.downbeat_frame >= s.first_frame
                && s.downbeat_frame < s.end_frame
                && meter_value(s.numerator, s.denominator)
                && r[36] >= b[2]
                && confidence(s.confidence)
                && zero(&r[38..44])
                && s.segment_id != 0
                && zero(&r[48..56]),
        )?;
        if let Some(p) = previous {
            check(
                p.end_frame <= s.first_frame
                    && p.downbeat_ordinal < s.downbeat_ordinal
                    && p.segment_id < s.segment_id,
            )?;
        } else {
            check(
                s.downbeat_frame == u64_at(b, 16)
                    && s.downbeat_ordinal == u64_at(b, 24) as i64
                    && s.numerator == u16_at(b, 4)
                    && s.denominator == u16_at(b, 6),
            )?;
        }
        previous = Some(s);
    }
    Ok(MeterView { bytes: b })
}
#[derive(Clone, Copy, Debug)]
pub struct QualityView<'a> {
    bytes: &'a [u8],
}
impl QualityView<'_> {
    pub fn record_count(&self) -> usize {
        (self.bytes.len() - 16) / 32
    }
    pub fn record(&self, index: usize) -> Option<QualityRecord> {
        if index >= self.record_count() {
            None
        } else {
            Some(quality(&self.bytes[16 + index * 32..]))
        }
    }
    pub fn copy_into<'a>(
        &self,
        out: &'a mut [QualityRecord],
    ) -> Result<&'a [QualityRecord], Error> {
        let n = self.record_count();
        if out.len() < n {
            return Err(Error::BufferTooSmall);
        }
        for (i, v) in out[..n].iter_mut().enumerate() {
            *v = self.record(i).unwrap();
        }
        Ok(&out[..n])
    }
}
fn valid_quality(q: QualityRecord, partial: bool, available: u64) -> bool {
    q.feature.is_power_of_two()
        && q.feature < 1 << 11
        && q.feature & available != 0
        && (q.evidence_coverage_permille <= 1000 || q.evidence_coverage_permille == 65535)
        && confidence(q.confidence)
        && state(q.state as u8, partial).is_ok()
        && q.flags & !15 == 0
}
pub fn parse_quality(
    b: &[u8],
    partial: bool,
    available: u64,
    limit: usize,
) -> Result<QualityView<'_>, Error> {
    check(b.len() >= 16)?;
    check(u16_at(b, 0) == 1 && u16_at(b, 2) == 32 && u32_at(b, 8) == 16 && zero(&b[12..16]))?;
    let n = u32_at(b, 4) as usize;
    check(n != 0)?;
    check(b.len() == size(n, limit, MAX_QUALITY_RECORDS, 16, 32)?)?;
    let mut prev = 0;
    for i in 0..n {
        let r = &b[16 + i * 32..16 + (i + 1) * 32];
        state(r[15], partial)?;
        let q = quality(r);
        check(valid_quality(q, partial, available) && q.feature > prev && zero(&r[20..32]))?;
        prev = q.feature;
    }
    Ok(QualityView { bytes: b })
}
fn put16(b: &mut [u8], p: usize, v: u16) {
    b[p..p + 2].copy_from_slice(&v.to_le_bytes());
}
fn put32(b: &mut [u8], p: usize, v: u32) {
    b[p..p + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(b: &mut [u8], p: usize, v: u64) {
    b[p..p + 8].copy_from_slice(&v.to_le_bytes());
}
/// Validate native key storage without allocating or touching output.
fn validate_key_inner(k: Key<'_>, total: Option<u64>, partial: bool) -> Result<(), Error> {
    check(
        range(k.first_frame, k.end_frame, total)
            && key_value(k.tonic, k.mode, k.tuning_offset_cents)
            && confidence(k.confidence),
    )?;
    state(k.state as u8, partial)?;
    size(
        k.candidates.len(),
        MAX_KEY_CANDIDATES,
        MAX_KEY_CANDIDATES,
        40,
        16,
    )?;
    let mut found = k.candidates.is_empty();
    for (i, c) in k.candidates.iter().enumerate() {
        check(key_value(c.tonic, c.mode, c.tuning_offset_cents) && confidence(c.confidence))?;
        if i > 0 {
            check(c.score < k.candidates[i - 1].score)?;
        }
        for p in &k.candidates[..i] {
            check(
                (c.tonic, c.mode, c.tuning_offset_cents)
                    != (p.tonic, p.mode, p.tuning_offset_cents),
            )?;
        }
        found |=
            (c.tonic, c.mode, c.tuning_offset_cents) == (k.tonic, k.mode, k.tuning_offset_cents);
    }
    check(found)
}
pub fn write_key(
    k: Key<'_>,
    total: Option<u64>,
    partial: bool,
    out: &mut [u8],
) -> Result<usize, Error> {
    validate_key(k, total, partial)?;
    let n = size(
        k.candidates.len(),
        MAX_KEY_CANDIDATES,
        MAX_KEY_CANDIDATES,
        40,
        16,
    )?;
    if out.len() < n {
        return Err(Error::BufferTooSmall);
    }
    let b = &mut out[..n];
    b.fill(0);
    put16(b, 0, 1);
    b[2] = k.state as u8;
    b[3] = k.confidence;
    b[4] = k.tonic;
    b[5] = k.mode;
    put16(b, 6, k.tuning_offset_cents as u16);
    put32(b, 12, k.candidates.len() as u32);
    put64(b, 16, k.first_frame);
    put64(b, 24, k.end_frame);
    put32(b, 32, 40);
    for (i, c) in k.candidates.iter().enumerate() {
        let r = &mut b[40 + i * 16..];
        r[0] = c.tonic;
        r[1] = c.mode;
        put16(r, 2, c.tuning_offset_cents as u16);
        put16(r, 4, c.score);
        r[6] = c.confidence;
    }
    Ok(n)
}
fn validate_meter_inner(m: Meter<'_>, total: Option<u64>, partial: bool) -> Result<(), Error> {
    check(
        !m.segments.is_empty()
            && meter_value(m.numerator, m.denominator)
            && confidence(m.confidence),
    )?;
    state(m.state as u8, partial)?;
    size(
        m.segments.len(),
        MAX_METER_SEGMENTS,
        MAX_METER_SEGMENTS,
        48,
        56,
    )?;
    for (i, s) in m.segments.iter().enumerate() {
        state(s.state as u8, partial)?;
        check(
            range(s.first_frame, s.end_frame, total)
                && s.downbeat_frame >= s.first_frame
                && s.downbeat_frame < s.end_frame
                && meter_value(s.numerator, s.denominator)
                && s.state as u8 >= m.state as u8
                && confidence(s.confidence)
                && s.segment_id != 0,
        )?;
        if i > 0 {
            let p = m.segments[i - 1];
            check(
                p.end_frame <= s.first_frame
                    && p.downbeat_ordinal < s.downbeat_ordinal
                    && p.segment_id < s.segment_id,
            )?;
        } else {
            check(
                (
                    m.numerator,
                    m.denominator,
                    m.downbeat_frame,
                    m.downbeat_ordinal,
                ) == (
                    s.numerator,
                    s.denominator,
                    s.downbeat_frame,
                    s.downbeat_ordinal,
                ),
            )?;
        }
    }
    Ok(())
}
/// Local validation only: the owning result must validate grid references first.
pub fn write_meter(
    m: Meter<'_>,
    total: Option<u64>,
    partial: bool,
    out: &mut [u8],
) -> Result<usize, Error> {
    validate_meter(m, total, partial)?;
    let n = size(
        m.segments.len(),
        MAX_METER_SEGMENTS,
        MAX_METER_SEGMENTS,
        48,
        56,
    )?;
    if out.len() < n {
        return Err(Error::BufferTooSmall);
    }
    let b = &mut out[..n];
    b.fill(0);
    put16(b, 0, 1);
    b[2] = m.state as u8;
    b[3] = m.confidence;
    put16(b, 4, m.numerator);
    put16(b, 6, m.denominator);
    put32(b, 12, m.segments.len() as u32);
    put64(b, 16, m.downbeat_frame);
    put64(b, 24, m.downbeat_ordinal as u64);
    put32(b, 32, 48);
    for (i, s) in m.segments.iter().enumerate() {
        let r = &mut b[48 + i * 56..];
        put64(r, 0, s.first_frame);
        put64(r, 8, s.end_frame);
        put64(r, 16, s.downbeat_frame);
        put64(r, 24, s.downbeat_ordinal as u64);
        put16(r, 32, s.numerator);
        put16(r, 34, s.denominator);
        r[36] = s.state as u8;
        r[37] = s.confidence;
        put32(r, 44, s.segment_id);
    }
    Ok(n)
}
fn validate_quality_inner(
    records: &[QualityRecord],
    partial: bool,
    available: u64,
) -> Result<(), Error> {
    check(!records.is_empty())?;
    size(
        records.len(),
        MAX_QUALITY_RECORDS,
        MAX_QUALITY_RECORDS,
        16,
        32,
    )?;
    let mut seen = 0;
    for q in records {
        check(valid_quality(*q, partial, available) && q.feature & seen == 0)?;
        seen |= q.feature;
    }
    Ok(())
}
/// Emit canonical feature order without mutating caller storage.
pub fn write_quality(
    records: &[QualityRecord],
    partial: bool,
    available: u64,
    out: &mut [u8],
) -> Result<usize, Error> {
    validate_quality(records, partial, available)?;
    let n = size(
        records.len(),
        MAX_QUALITY_RECORDS,
        MAX_QUALITY_RECORDS,
        16,
        32,
    )?;
    if out.len() < n {
        return Err(Error::BufferTooSmall);
    }
    let b = &mut out[..n];
    b.fill(0);
    put16(b, 0, 1);
    put16(b, 2, 32);
    put32(b, 4, records.len() as u32);
    put32(b, 8, 16);
    let mut i = 0;
    for bit in 0..11 {
        if let Some(q) = records.iter().find(|q| q.feature == 1 << bit) {
            let r = &mut b[16 + i * 32..];
            put64(r, 0, q.feature);
            put32(r, 8, q.calibration_model_id);
            put16(r, 12, q.evidence_coverage_permille);
            r[14] = q.confidence;
            r[15] = q.state as u8;
            put32(r, 16, q.flags);
            i += 1;
        }
    }
    Ok(n)
}

pub fn validate_key(value: Key<'_>, total: Option<u64>, partial: bool) -> Result<(), Error> {
    validate_key_inner(value, total, partial).map_err(|e| {
        if e == Error::Corrupt {
            Error::InvalidArgument
        } else {
            e
        }
    })
}

pub fn validate_meter(value: Meter<'_>, total: Option<u64>, partial: bool) -> Result<(), Error> {
    validate_meter_inner(value, total, partial).map_err(|e| {
        if e == Error::Corrupt {
            Error::InvalidArgument
        } else {
            e
        }
    })
}

pub fn validate_quality(
    value: &[QualityRecord],
    partial: bool,
    available: u64,
) -> Result<(), Error> {
    validate_quality_inner(value, partial, available).map_err(|e| {
        if e == Error::Corrupt {
            Error::InvalidArgument
        } else {
            e
        }
    })
}
