// SPDX-License-Identifier: Apache-2.0
//! Borrowed version-1 WDTL detail tiles and canonical caller-buffer writing.
//! The current reference geometry is level 1, 256 frames per column, 64 columns
//! per tile. Sparse coverage is represented by omitted tiles/columns.
use crate::{Error, FeatureState, WaveformColumn, WaveformTile};

pub const LEVEL_ID: u32 = 1;
pub const FRAMES_PER_COLUMN: u32 = 256;
pub const COLUMNS_PER_TILE: u32 = 64;

#[derive(Clone, Copy, Debug)]
pub struct DetailOptions {
    pub strict: bool,
    /// Bounds the allocation-free quadratic identity and packed overlap checks.
    pub maximum_tiles: usize,
    pub maximum_columns: usize,
}
impl Default for DetailOptions {
    fn default() -> Self {
        Self {
            strict: true,
            maximum_tiles: 1024,
            maximum_columns: 16777216,
        }
    }
}
fn u32at(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
fn u64at(b: &[u8], p: usize) -> u64 {
    u64::from_le_bytes(b[p..p + 8].try_into().unwrap())
}
fn range(b: &[u8], offset: u64, size: usize) -> Result<&[u8], Error> {
    let start = usize::try_from(offset).map_err(|_| Error::Corrupt)?;
    b.get(start..start.checked_add(size).ok_or(Error::Corrupt)?)
        .ok_or(Error::Corrupt)
}
fn state(value: u32) -> Result<FeatureState, Error> {
    match value {
        1 => Ok(FeatureState::Partial),
        2 => Ok(FeatureState::Provisional),
        3 => Ok(FeatureState::Stable),
        4 => Ok(FeatureState::Final),
        _ => Err(Error::Corrupt),
    }
}
fn geometry(
    level: u32,
    tile: u32,
    first: u64,
    end: u64,
    column: u32,
    count: usize,
    total: Option<u64>,
) -> Result<(), Error> {
    if level != LEVEL_ID {
        return Err(Error::Unsupported);
    }
    if tile > u32::MAX / COLUMNS_PER_TILE {
        return Err(Error::LimitExceeded);
    }
    let start_column = u64::from(tile) * u64::from(COLUMNS_PER_TILE);
    let end_column = u64::from(column)
        .checked_add(count as u64)
        .ok_or(Error::LimitExceeded)?;
    if first >= end
        || count == 0
        || u64::from(column) < start_column
        || end_column > start_column + 64
    {
        return Err(Error::Corrupt);
    }
    let expected_end = end_column * 256;
    if first != u64::from(column) * 256
        || (end != expected_end && !(total == Some(end) && expected_end > end))
        || (end_column - 1) * 256 >= end
        || total.is_some_and(|n| end > n)
    {
        return Err(Error::Corrupt);
    }
    Ok(())
}
fn column_valid(c: WaveformColumn, strict: bool) -> bool {
    c.flags & 1 != 0
        && c.minimum <= c.maximum
        && (!strict
            || (c.flags & !0x1f == 0
                && (c.flags & 8 != 0 || (c.low == 0 && c.mid == 0 && c.high == 0))))
}
fn decode(b: &[u8]) -> WaveformColumn {
    WaveformColumn {
        minimum: i16::from_le_bytes([b[0], b[1]]),
        maximum: i16::from_le_bytes([b[2], b[3]]),
        rms: u16::from_le_bytes([b[4], b[5]]),
        low: b[6],
        mid: b[7],
        high: b[8],
        flags: b[9],
    }
}
// Native permissive views normalize reserved flags and absent band bytes so
// copied values can be serialized canonically. Validation still uses raw bytes.
fn decode_normalized(b: &[u8]) -> WaveformColumn {
    let mut column = decode(b);
    column.flags &= 0x1f;
    if column.flags & 8 == 0 {
        column.low = 0;
        column.mid = 0;
        column.high = 0;
    }
    column
}
#[derive(Clone, Copy, Debug)]
pub struct PackedTile<'a> {
    pub level_id: u32,
    pub tile_index: u32,
    pub first_frame: u64,
    pub end_frame: u64,
    pub first_column_index: u32,
    pub state: FeatureState,
    pub confidence: u8,
    columns: &'a [u8],
}
impl PackedTile<'_> {
    pub fn column_count(&self) -> usize {
        self.columns.len() / 10
    }
    pub fn column(&self, index: usize) -> Option<WaveformColumn> {
        let start = index.checked_mul(10)?;
        self.columns
            .get(start..start.checked_add(10)?)
            .map(decode_normalized)
    }
    /// Copies values into independent caller storage; short buffers are untouched.
    pub fn copy_columns(&self, output: &mut [WaveformColumn]) -> Result<usize, Error> {
        if output.len() < self.column_count() {
            return Err(Error::BufferTooSmall);
        }
        for (out, input) in output.iter_mut().zip(self.columns.chunks_exact(10)) {
            *out = decode_normalized(input);
        }
        Ok(self.column_count())
    }
}
#[derive(Clone, Copy, Debug)]
pub struct DetailPayload<'a> {
    payload: &'a [u8],
    directory: usize,
    tiles: usize,
    columns: usize,
}
impl<'a> DetailPayload<'a> {
    /// Validates a payload. Container readers must additionally enforce globally
    /// unique tile identities across all WDTL sections and section dependencies.
    pub fn parse(
        payload: &'a [u8],
        total_frames: Option<u64>,
        partial: bool,
        options: DetailOptions,
    ) -> Result<Self, Error> {
        if payload.len() < 16 {
            return Err(Error::Corrupt);
        }
        let tiles = u32at(payload, 0) as usize;
        if tiles == 0 || (options.strict && u32at(payload, 4) != 0) {
            return Err(Error::Corrupt);
        }
        if tiles > options.maximum_tiles {
            return Err(Error::LimitExceeded);
        }
        let directory64 = u64at(payload, 8);
        if directory64 < 16 {
            return Err(Error::Corrupt);
        }
        let directory_bytes = tiles.checked_mul(48).ok_or(Error::LimitExceeded)?;
        range(payload, directory64, directory_bytes)?;
        let directory = directory64 as usize;
        let mut result = Self {
            payload,
            directory,
            tiles,
            columns: 0,
        };
        for i in 0..tiles {
            let d = result.descriptor(i);
            let count = u32at(d, 28) as usize;
            let offset = u64at(d, 32);
            let bytes = count.checked_mul(10).ok_or(Error::LimitExceeded)?;
            let packed = range(payload, offset, bytes)?;
            let end = offset + bytes as u64;
            if offset < 16 || (offset < directory64 + directory_bytes as u64 && directory64 < end) {
                return Err(Error::Corrupt);
            }
            geometry(
                u32at(d, 0),
                u32at(d, 4),
                u64at(d, 8),
                u64at(d, 16),
                u32at(d, 24),
                count,
                total_frames,
            )?;
            let feature_state = state(u32at(d, 40))?;
            if (!partial && feature_state != FeatureState::Final)
                || (d[46] > 100 && d[46] != 255)
                || (options.strict && (d[44] != 0 || d[45] != 0 || d[47] != 0))
            {
                return Err(Error::Corrupt);
            }
            result.columns = result
                .columns
                .checked_add(count)
                .ok_or(Error::LimitExceeded)?;
            if result.columns > options.maximum_columns {
                return Err(Error::LimitExceeded);
            }
            if packed
                .chunks_exact(10)
                .any(|c| !column_valid(decode(c), options.strict))
            {
                return Err(Error::Corrupt);
            }
            for j in 0..i {
                let prev = result.descriptor(j);
                let previous_offset = u64at(prev, 32);
                let previous_end = previous_offset + u64::from(u32at(prev, 28)) * 10;
                if (d[..8] == prev[..8]) || (offset < previous_end && previous_offset < end) {
                    return Err(Error::Corrupt);
                }
            }
        }
        Ok(result)
    }
    fn descriptor(&self, i: usize) -> &'a [u8] {
        &self.payload[self.directory + i * 48..self.directory + (i + 1) * 48]
    }
    pub fn tile_count(&self) -> usize {
        self.tiles
    }
    pub fn column_count(&self) -> usize {
        self.columns
    }
    /// Preserves input descriptor order. Canonical writers require sorted tiles.
    pub fn tile(&self, index: usize) -> Option<PackedTile<'a>> {
        tile_from_validated(self.payload, index)
    }
}
/// Internal O(1) accessor for immutable payloads already accepted by `parse`.
/// Callers must not expose this as a substitute for semantic validation.
pub(crate) fn tile_from_validated(payload: &[u8], index: usize) -> Option<PackedTile<'_>> {
    if index >= u32at(payload, 0) as usize {
        return None;
    }
    let directory = u64at(payload, 8) as usize;
    let d = &payload[directory + index * 48..directory + (index + 1) * 48];
    Some(PackedTile {
        level_id: u32at(d, 0),
        tile_index: u32at(d, 4),
        first_frame: u64at(d, 8),
        end_frame: u64at(d, 16),
        first_column_index: u32at(d, 24),
        state: state(u32at(d, 40)).ok()?,
        confidence: d[46],
        columns: range(payload, u64at(d, 32), u32at(d, 28) as usize * 10).ok()?,
    })
}
/// Validates canonical native values before any output is modified.
pub fn payload_size(tiles: &[WaveformTile<'_>], total_frames: Option<u64>) -> Result<usize, Error> {
    if tiles.is_empty() {
        return Err(Error::InvalidArgument);
    }
    if tiles.len() > u32::MAX as usize {
        return Err(Error::LimitExceeded);
    }
    let mut size = tiles
        .len()
        .checked_mul(48)
        .and_then(|n| n.checked_add(16))
        .ok_or(Error::LimitExceeded)?;
    let mut count = 0usize;
    for (i, t) in tiles.iter().enumerate() {
        geometry(
            t.level_id,
            t.tile_index,
            t.first_frame,
            t.end_frame,
            t.first_column_index,
            t.columns.len(),
            total_frames,
        )
        .map_err(|e| {
            if e == Error::Corrupt {
                Error::InvalidArgument
            } else {
                e
            }
        })?;
        if (i > 0 && (tiles[i - 1].level_id, tiles[i - 1].tile_index) >= (t.level_id, t.tile_index))
            || (t.confidence > 100 && t.confidence != 255)
            || (t.state == FeatureState::Final && total_frames.is_none())
            || t.columns.iter().any(|c| !column_valid(*c, true))
        {
            return Err(Error::InvalidArgument);
        }
        count = count
            .checked_add(t.columns.len())
            .ok_or(Error::LimitExceeded)?;
        if count > u32::MAX as usize {
            return Err(Error::LimitExceeded);
        }
        size = size
            .checked_add(
                t.columns
                    .len()
                    .checked_mul(10)
                    .ok_or(Error::LimitExceeded)?,
            )
            .ok_or(Error::LimitExceeded)?;
    }
    Ok(size)
}
/// Writes one canonical WDTL payload. The enclosing container must set its
/// partial flag whenever any tile is non-final.
pub fn write_payload(
    tiles: &[WaveformTile<'_>],
    total_frames: Option<u64>,
    output: &mut [u8],
) -> Result<usize, Error> {
    let size = payload_size(tiles, total_frames)?;
    let out = output.get_mut(..size).ok_or(Error::BufferTooSmall)?;
    out.fill(0);
    out[..4].copy_from_slice(&(tiles.len() as u32).to_le_bytes());
    out[8..16].copy_from_slice(&16u64.to_le_bytes());
    let mut offset = 16 + tiles.len() * 48;
    for (i, t) in tiles.iter().enumerate() {
        let d = &mut out[16 + i * 48..16 + (i + 1) * 48];
        d[..4].copy_from_slice(&t.level_id.to_le_bytes());
        d[4..8].copy_from_slice(&t.tile_index.to_le_bytes());
        d[8..16].copy_from_slice(&t.first_frame.to_le_bytes());
        d[16..24].copy_from_slice(&t.end_frame.to_le_bytes());
        d[24..28].copy_from_slice(&t.first_column_index.to_le_bytes());
        d[28..32].copy_from_slice(&(t.columns.len() as u32).to_le_bytes());
        d[32..40].copy_from_slice(&(offset as u64).to_le_bytes());
        d[40..44].copy_from_slice(&(t.state as u32).to_le_bytes());
        d[46] = t.confidence;
        for c in t.columns {
            let b = &mut out[offset..offset + 10];
            b[..2].copy_from_slice(&c.minimum.to_le_bytes());
            b[2..4].copy_from_slice(&c.maximum.to_le_bytes());
            b[4..6].copy_from_slice(&c.rms.to_le_bytes());
            b[6..10].copy_from_slice(&[c.low, c.mid, c.high, c.flags]);
            offset += 10;
        }
    }
    Ok(size)
}
