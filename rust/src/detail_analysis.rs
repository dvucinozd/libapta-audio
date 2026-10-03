// SPDX-License-Identifier: Apache-2.0
//! Four-tile detail waveform cache. Feed copied normalized PCM at acceptance,
//! then refresh completed columns before publishing. All storage is caller-owned.
use crate::{
    waveform::{NormalizedSample, WaveformAccumulator},
    Error, FeatureState, FrameRange, NativeDetail, NativeTile, WaveformColumn,
};
pub const TILE_COUNT: usize = 4;
pub const COLUMNS_PER_TILE: usize = 64;
pub const FRAMES_PER_COLUMN: u64 = 256;
pub const TILE_FRAMES: u64 = FRAMES_PER_COLUMN * COLUMNS_PER_TILE as u64;
#[derive(Clone, Copy, Debug)]
pub struct DetailTile {
    index: Option<u32>,
    access: u64,
    accumulators: [WaveformAccumulator; COLUMNS_PER_TILE],
    complete: [bool; COLUMNS_PER_TILE],
    run_first: usize,
    run_count: usize,
}
impl Default for DetailTile {
    fn default() -> Self {
        Self {
            index: None,
            access: 0,
            accumulators: [WaveformAccumulator::default(); COLUMNS_PER_TILE],
            complete: [false; COLUMNS_PER_TILE],
            run_first: 0,
            run_count: 0,
        }
    }
}
pub struct DetailCache<'a> {
    tiles: &'a mut [DetailTile],
    access: u64,
    mutation: u64,
    eof: Option<u64>,
    greatest_sample_end: u64,
}
impl<'a> DetailCache<'a> {
    pub fn new(tiles: &'a mut [DetailTile]) -> Result<Self, Error> {
        if tiles.len() < TILE_COUNT {
            return Err(Error::BufferTooSmall);
        }
        let tiles = &mut tiles[..TILE_COUNT];
        tiles.fill(DetailTile::default());
        Ok(Self {
            tiles,
            access: 0,
            mutation: 0,
            eof: None,
            greatest_sample_end: 0,
        })
    }
    pub fn mutation_serial(&self) -> u64 {
        self.mutation
    }
    /// Return false when all resident tiles are protected and the incoming tile
    /// is not. The ordinary acceptance loop stops on false; replay may continue.
    /// The predicate identifies tile indices protected by focus or active requests.
    pub fn push_normalized(
        &mut self,
        frame: u64,
        sample: NormalizedSample,
        protected: impl Fn(u32) -> bool,
    ) -> Result<bool, Error> {
        if !sample.value.is_finite()
            || !(-1.0..=1.0).contains(&sample.value)
            || self.eof.is_some_and(|end| frame >= end)
        {
            return Err(Error::InvalidArgument);
        }
        let tile64 = frame / TILE_FRAMES;
        if tile64 > u64::from(u32::MAX) / (COLUMNS_PER_TILE as u64) {
            return Err(Error::LimitExceeded);
        }
        let index = tile64 as u32;
        let column = ((frame % TILE_FRAMES) / FRAMES_PER_COLUMN) as usize;
        let existing = self.tiles.iter().position(|t| t.index == Some(index));
        let slot = if let Some(slot) = existing {
            slot
        } else if let Some(slot) = self.tiles.iter().position(|t| t.index.is_none()) {
            slot
        } else {
            let unprotected = self
                .tiles
                .iter()
                .enumerate()
                .filter(|(_, t)| !protected(t.index.unwrap()))
                .min_by_key(|(_, t)| t.access)
                .map(|(i, _)| i);
            if let Some(slot) = unprotected {
                slot
            } else if !protected(index) {
                return Ok(false);
            } else {
                self.tiles
                    .iter()
                    .enumerate()
                    .min_by_key(|(_, t)| t.access)
                    .unwrap()
                    .0
            }
        };
        // Validate accumulator overflow before touching access order or evicting.
        let mut value = if existing.is_some() {
            self.tiles[slot].accumulators[column]
        } else {
            WaveformAccumulator::default()
        };
        // C derives clipping from the mixed sample, not a separately supplied flag.
        value.push_normalized(sample.value, sample.value <= -1.0 || sample.value >= 1.0)?;
        self.access = self.access.wrapping_add(1);
        if existing.is_none() {
            if self.tiles[slot].run_count != 0 {
                self.mutation = self.mutation.wrapping_add(1)
            }
            self.tiles[slot] = DetailTile {
                index: Some(index),
                ..DetailTile::default()
            };
        }
        let tile = &mut self.tiles[slot];
        tile.access = self.access;
        tile.accumulators[column] = value;
        self.greatest_sample_end = self.greatest_sample_end.max(frame + 1);
        Ok(true)
    }
    /// EOF completes short final columns. Invalid EOF leaves the cache unchanged.
    pub fn refresh_completed(&mut self, eof: Option<u64>) -> Result<(), Error> {
        if eof == Some(u64::MAX)
            || eof.is_some_and(|end| end < self.greatest_sample_end)
            || self.eof.is_some_and(|old| eof != Some(old))
        {
            return Err(Error::InvalidArgument);
        }
        self.eof = eof;
        for tile in self.tiles.iter_mut().filter(|t| t.index.is_some()) {
            let base = u64::from(tile.index.unwrap()) * TILE_FRAMES;
            for (column, value) in tile.accumulators.iter().enumerate() {
                let first = base + column as u64 * FRAMES_PER_COLUMN;
                let expected = eof.map_or(FRAMES_PER_COLUMN, |end| {
                    end.saturating_sub(first).min(FRAMES_PER_COLUMN)
                });
                if !tile.complete[column]
                    && expected != 0
                    && u64::from(value.sample_count()) == expected
                {
                    tile.complete[column] = true;
                }
            }
            let old = (tile.run_first, tile.run_count);
            if tile.run_count != 0 {
                let mut end = tile.run_first + tile.run_count;
                while tile.run_first > 0 && tile.complete[tile.run_first - 1] {
                    tile.run_first -= 1;
                }
                while end < COLUMNS_PER_TILE && tile.complete[end] {
                    end += 1;
                }
                tile.run_count = end - tile.run_first;
            } else {
                let mut current = 0;
                for column in 0..COLUMNS_PER_TILE {
                    if tile.complete[column] {
                        current += 1;
                        if current > tile.run_count {
                            tile.run_first = column + 1 - current;
                            tile.run_count = current;
                        }
                    } else {
                        current = 0;
                    }
                }
            }
            if old != (tile.run_first, tile.run_count) {
                self.mutation = self.mutation.wrapping_add(1)
            }
        }
        Ok(())
    }
    pub fn column_is_empty(&self, global_column: u64) -> bool {
        let tile = global_column / COLUMNS_PER_TILE as u64;
        if tile > u64::from(u32::MAX) {
            return false;
        }
        self.tiles
            .iter()
            .find(|t| t.index == Some(tile as u32))
            .map_or(true, |t| {
                t.accumulators[(global_column % COLUMNS_PER_TILE as u64) as usize].sample_count()
                    == 0
            })
    }
    pub fn range_complete(&self, range: FrameRange) -> bool {
        if range.first_frame >= range.end_frame {
            return false;
        }
        let first = range.first_frame / FRAMES_PER_COLUMN;
        let last = (range.end_frame - 1) / FRAMES_PER_COLUMN;
        // At most 256 columns can be resident; avoid unbounded scans of host ranges.
        if last - first >= (TILE_COUNT * COLUMNS_PER_TILE) as u64 {
            return false;
        }
        (first..=last).all(|global| {
            let tile = global / COLUMNS_PER_TILE as u64;
            tile <= u64::from(u32::MAX)
                && self.tiles.iter().any(|t| {
                    t.index == Some(tile as u32)
                        && t.complete[(global % COLUMNS_PER_TILE as u64) as usize]
                })
        })
    }
    pub fn range_has_output(&self, range: FrameRange) -> bool {
        self.tiles.iter().any(|tile| {
            if tile.index.is_none() || tile.run_count == 0 {
                return false;
            }
            let first = u64::from(tile.index.unwrap()) * TILE_FRAMES
                + tile.run_first as u64 * FRAMES_PER_COLUMN;
            let end = first + tile.run_count as u64 * FRAMES_PER_COLUMN;
            let end = self.eof.map_or(end, |e| end.min(e));
            range.first_frame < range.end_frame
                && first < range.end_frame
                && range.first_frame < end
        })
    }
    /// Return packed tiles sorted by tile index. Capacity errors do not alter output.
    pub fn snapshot_into<'b>(
        &self,
        tiles: &'b mut [NativeTile],
        columns: &'b mut [WaveformColumn],
    ) -> Result<NativeDetail<'b>, Error> {
        let mut slots = [0usize; TILE_COUNT];
        let mut n = 0;
        let mut total = 0;
        for (i, t) in self
            .tiles
            .iter()
            .enumerate()
            .filter(|(_, t)| t.run_count != 0)
        {
            slots[n] = i;
            n += 1;
            total += t.run_count;
        }
        if tiles.len() < n || columns.len() < total {
            return Err(Error::BufferTooSmall);
        }
        slots[..n].sort_unstable_by_key(|i| self.tiles[*i].index);
        let mut offset = 0;
        for (output, slot) in slots[..n].iter().enumerate() {
            let tile = &self.tiles[*slot];
            let index = tile.index.unwrap();
            let base = u64::from(index) * TILE_FRAMES;
            let first = base + tile.run_first as u64 * FRAMES_PER_COLUMN;
            let end = first + tile.run_count as u64 * FRAMES_PER_COLUMN;
            let end = self.eof.map_or(end, |e| end.min(e));
            let expected = self.eof.map_or(COLUMNS_PER_TILE, |e| {
                e.saturating_sub(base)
                    .min(TILE_FRAMES)
                    .div_ceil(FRAMES_PER_COLUMN) as usize
            });
            let state = if tile.run_first == 0 && tile.run_count == expected {
                if self.eof.is_some() {
                    FeatureState::Final
                } else {
                    FeatureState::Stable
                }
            } else {
                FeatureState::Partial
            };
            tiles[output] = NativeTile {
                level_id: 1,
                tile_index: index,
                first_frame: first,
                end_frame: end,
                first_column_index: index * COLUMNS_PER_TILE as u32 + tile.run_first as u32,
                state,
                confidence: 255,
                data_column_offset: offset,
                column_count: tile.run_count,
            };
            for c in 0..tile.run_count {
                columns[offset + c] = tile.accumulators[tile.run_first + c].column();
            }
            offset += tile.run_count;
        }
        Ok(NativeDetail {
            tiles: &tiles[..n],
            columns: &columns[..total],
        })
    }
}
