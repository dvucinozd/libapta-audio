// SPDX-License-Identifier: Apache-2.0
//! META version 1: borrowed fields, bounded deterministic CBOR, and caller-owned copies.
//!
//! Unknown top-level integer keys are validated and ignored by `parse`. Use
//! `copy_canonical` to preserve them. Limits match the C reader's bounded walker.
//! Unknown text, nested maps and floats additionally obey RFC 8949 core
//! deterministic encoding, where the C unknown-value walker is less strict.
use crate::{Error, Metadata, SourceId};

pub const MAX_MAP_ITEMS: u64 = 64;
pub const MAX_DEPTH: usize = 8;
pub const MAX_ITEMS: usize = 256;
pub const MAX_STRING_BYTES: usize = 8192;

struct Cursor<'a> {
    bytes: &'a [u8],
    pos: usize,
    items: usize,
}
impl<'a> Cursor<'a> {
    fn take(&mut self, n: u64) -> Result<&'a [u8], Error> {
        let n = usize::try_from(n).map_err(|_| Error::Corrupt)?;
        let end = self.pos.checked_add(n).ok_or(Error::Corrupt)?;
        let result = self.bytes.get(self.pos..end).ok_or(Error::Corrupt)?;
        self.pos = end;
        Ok(result)
    }
    fn head(&mut self) -> Result<(u8, u64), Error> {
        let initial = self.take(1)?[0];
        let additional = initial & 31;
        let value = match additional {
            0..=23 => additional as u64,
            24..=27 => {
                let n = 1 << (additional - 24);
                let mut value = 0u64;
                for byte in self.take(n)? {
                    value = (value << 8) | *byte as u64;
                }
                let minimum = [24, 256, 65536, 4294967296][(additional - 24) as usize];
                if value < minimum {
                    return Err(Error::Corrupt);
                }
                value
            }
            _ => return Err(Error::Corrupt),
        };
        Ok((initial >> 5, value))
    }
    fn string(&mut self, major: u8, size: u64, limit: usize) -> Result<&'a [u8], Error> {
        if size > limit as u64 {
            return Err(Error::LimitExceeded);
        }
        let bytes = self.take(size)?;
        if major == 3 {
            core::str::from_utf8(bytes).map_err(|_| Error::Corrupt)?;
        }
        Ok(bytes)
    }
    fn text(&mut self, limit: usize) -> Result<&'a str, Error> {
        let (major, size) = self.head()?;
        if major != 3 {
            return Err(Error::Corrupt);
        }
        core::str::from_utf8(self.string(major, size, limit)?).map_err(|_| Error::Corrupt)
    }
    fn skip(&mut self, depth: usize) -> Result<(), Error> {
        if depth > MAX_DEPTH || self.items == MAX_ITEMS {
            return Err(Error::LimitExceeded);
        }
        self.items += 1;
        let initial = *self.bytes.get(self.pos).ok_or(Error::Corrupt)?;
        if initial >> 5 == 7 {
            self.pos += 1;
            match initial & 31 {
                0..=23 => (),
                24 => {
                    if self.take(1)?[0] < 32 {
                        return Err(Error::Corrupt);
                    }
                }
                25 => {
                    let b = self.take(2)?;
                    let bits = u16::from_be_bytes([b[0], b[1]]);
                    if bits & 0x7c00 == 0x7c00 && bits & 0x03ff != 0 && bits != 0x7e00 {
                        return Err(Error::Corrupt);
                    }
                }
                26 => {
                    let b = self.take(4)?;
                    let bits = u32::from_be_bytes(b.try_into().map_err(|_| Error::Corrupt)?);
                    if fits_half(bits) {
                        return Err(Error::Corrupt);
                    }
                }
                27 => {
                    let b = self.take(8)?;
                    let value = f64::from_bits(u64::from_be_bytes(
                        b.try_into().map_err(|_| Error::Corrupt)?,
                    ));
                    if value.is_nan() || (value as f32) as f64 == value {
                        return Err(Error::Corrupt);
                    }
                }
                _ => return Err(Error::Corrupt),
            }
            return Ok(());
        }
        let (major, value) = self.head()?;
        match major {
            0 | 1 => (),
            2 | 3 => {
                self.string(major, value, MAX_STRING_BYTES)?;
            }
            4 => {
                if value > MAX_ITEMS as u64 {
                    return Err(Error::LimitExceeded);
                }
                for _ in 0..value {
                    self.skip(depth + 1)?;
                }
            }
            5 => {
                if value > (MAX_ITEMS / 2) as u64 {
                    return Err(Error::LimitExceeded);
                }
                let mut previous: Option<&[u8]> = None;
                for _ in 0..value {
                    let start = self.pos;
                    self.skip(depth + 1)?;
                    let key = &self.bytes[start..self.pos];
                    if previous.is_some_and(|p| p >= key) {
                        return Err(Error::Corrupt);
                    }
                    previous = Some(key);
                    self.skip(depth + 1)?;
                }
            }
            6 => self.skip(depth + 1)?,
            _ => return Err(Error::Corrupt),
        }
        Ok(())
    }
}

// Exact binary32 representability in binary16, including signed zero and infinity.
fn fits_half(bits: u32) -> bool {
    let exponent = ((bits >> 23) & 255) as i32;
    let fraction = bits & 0x7fffff;
    if exponent == 255 || bits & 0x7fffffff == 0 {
        return true;
    }
    let e = exponent - 127;
    if (-14..=15).contains(&e) {
        return fraction & 0x1fff == 0;
    }
    if (-24..=-15).contains(&e) {
        let shift = (-e - 1) as u32;
        return (fraction | 0x800000) & ((1u32 << shift) - 1) == 0;
    }
    false
}

/// Parse recognized fields without copying; returned references borrow `payload`.
/// Present empty text and an empty map remain distinguishable from absence.
pub fn parse(payload: &[u8]) -> Result<Metadata<'_>, Error> {
    let mut c = Cursor {
        bytes: payload,
        pos: 0,
        items: 0,
    };
    let (major, count) = c.head()?;
    if major != 5 {
        return Err(Error::Corrupt);
    }
    if count > MAX_MAP_ITEMS {
        return Err(Error::LimitExceeded);
    }
    let mut result = Metadata::default();
    let mut previous = None;
    for _ in 0..count {
        let (major, key) = c.head()?;
        if major != 0 || previous.is_some_and(|p| p >= key) {
            return Err(Error::Corrupt);
        }
        previous = Some(key);
        match key {
            1 => result.producer_name = Some(c.text(255)?),
            2 => result.producer_version_string = Some(c.text(127)?),
            3 => result.backend_name = Some(c.text(255)?),
            4 => result.backend_version = Some(c.text(127)?),
            5 => {
                let (major, value) = c.head()?;
                if major != 0 {
                    return Err(Error::Corrupt);
                }
                result.creation_unix_time = Some(value);
            }
            6 => {
                let (major, size) = c.head()?;
                if major != 2 && major != 3 {
                    return Err(Error::Corrupt);
                }
                let bytes = c.string(major, size, 1024)?;
                result.application_source_id = Some(if major == 2 {
                    SourceId::Bytes(bytes)
                } else {
                    SourceId::Text(core::str::from_utf8(bytes).map_err(|_| Error::Corrupt)?)
                });
            }
            7 => result.comments = Some(c.text(4096)?),
            _ => c.skip(1)?,
        }
    }
    if c.pos != payload.len() {
        return Err(Error::Corrupt);
    }
    Ok(result)
}

pub fn validate(payload: &[u8]) -> Result<(), Error> {
    parse(payload).map(|_| ())
}

/// Copy validated canonical bytes, preserving unknown keys. On error output is unchanged.
pub fn copy_canonical(payload: &[u8], output: &mut [u8]) -> Result<usize, Error> {
    validate(payload)?;
    let dest = output
        .get_mut(..payload.len())
        .ok_or(Error::BufferTooSmall)?;
    dest.copy_from_slice(payload);
    Ok(payload.len())
}

fn fields<'a>(m: &Metadata<'a>) -> [Option<(u8, &'a [u8])>; 7] {
    [
        m.producer_name.map(|s| (3, s.as_bytes())),
        m.producer_version_string.map(|s| (3, s.as_bytes())),
        m.backend_name.map(|s| (3, s.as_bytes())),
        m.backend_version.map(|s| (3, s.as_bytes())),
        None,
        m.application_source_id.map(|s| match s {
            SourceId::Text(s) => (3, s.as_bytes()),
            SourceId::Bytes(b) => (2, b),
        }),
        m.comments.map(|s| (3, s.as_bytes())),
    ]
}
fn head_size(value: u64) -> usize {
    match value {
        0..=23 => 1,
        24..=255 => 2,
        256..=65535 => 3,
        65536..=4294967295 => 5,
        _ => 9,
    }
}
/// Required size for deterministic recognized-field output, checking all field limits.
pub fn serialized_size(m: &Metadata<'_>) -> Result<usize, Error> {
    let limits = [255, 127, 255, 127, 0, 1024, 4096];
    let mut size = 1;
    for (field, limit) in fields(m).iter().zip(limits) {
        if let Some((_, bytes)) = field {
            if bytes.len() > limit {
                return Err(Error::LimitExceeded);
            }
            size += 1 + head_size(bytes.len() as u64) + bytes.len();
        }
    }
    if let Some(value) = m.creation_unix_time {
        size += 1 + head_size(value);
    }
    Ok(size)
}
fn put_head(output: &mut [u8], pos: &mut usize, major: u8, value: u64) {
    let size = head_size(value);
    output[*pos] = (major << 5)
        | match size {
            1 => value as u8,
            2 => 24,
            3 => 25,
            5 => 26,
            _ => 27,
        };
    *pos += 1;
    if size > 1 {
        let bytes = value.to_be_bytes();
        output[*pos..*pos + size - 1].copy_from_slice(&bytes[9 - size..]);
        *pos += size - 1;
    }
}
/// Write recognized keys in ascending order, with shortest definite encodings.
/// No output bytes are changed on a size or field-limit error.
pub fn write(m: &Metadata<'_>, output: &mut [u8]) -> Result<usize, Error> {
    let size = serialized_size(m)?;
    if output.len() < size {
        return Err(Error::BufferTooSmall);
    }
    let fields = fields(m);
    let count =
        fields.iter().filter(|f| f.is_some()).count() + usize::from(m.creation_unix_time.is_some());
    let mut pos = 0;
    put_head(output, &mut pos, 5, count as u64);
    for (index, field) in fields.iter().enumerate() {
        if index == 4 {
            if let Some(value) = m.creation_unix_time {
                put_head(output, &mut pos, 0, 5);
                put_head(output, &mut pos, 0, value);
            }
        } else if let Some((major, bytes)) = field {
            put_head(output, &mut pos, 0, (index + 1) as u64);
            put_head(output, &mut pos, *major, bytes.len() as u64);
            output[pos..pos + bytes.len()].copy_from_slice(bytes);
            pos += bytes.len();
        }
    }
    Ok(pos)
}
/// Copy recognized metadata into caller-owned CBOR storage. The returned view
/// borrows only `storage`, allowing the original input to be dropped or changed.
pub fn copy_to<'a>(metadata: &Metadata<'_>, storage: &'a mut [u8]) -> Result<Metadata<'a>, Error> {
    let size = write(metadata, storage)?;
    parse(&storage[..size])
}
