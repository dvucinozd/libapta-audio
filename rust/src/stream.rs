// SPDX-License-Identifier: Apache-2.0
//! Bounded synchronous container transport. Callbacks must not retain buffers.
use crate::{
    container::{crc32c, Section},
    result::*,
    Error,
};
pub trait Input {
    fn size(&mut self) -> Result<u64, Error>;
    fn read_at(&mut self, offset: u64, output: &mut [u8]) -> Result<usize, Error>;
}
pub trait Output {
    fn write(&mut self, bytes: &[u8]) -> Result<usize, Error>;
    fn seek(&mut self, absolute: u64) -> Result<(), Error>;
    fn flush(&mut self) -> Result<(), Error>;
}
#[derive(Clone, Copy, Debug)]
pub struct StreamOptions {
    pub limits: Limits,
    pub requested_features: u64,
    pub maximum_section_bytes: u64,
    pub maximum_retained_bytes: usize,
    pub maximum_scratch_bytes: usize,
}
impl Default for StreamOptions {
    fn default() -> Self {
        Self {
            limits: Limits::default(),
            requested_features: ALL_FEATURES,
            maximum_section_bytes: 268435456,
            maximum_retained_bytes: 268435456,
            maximum_scratch_bytes: 65536,
        }
    }
}
#[derive(Clone, Copy, Debug, Default)]
pub struct StoredSection {
    pub(crate) fourcc: [u8; 4],
    pub(crate) version: u16,
    pub(crate) flags: u16,
    pub(crate) source_offset: u64,
    pub(crate) size: u64,
    pub(crate) crc: u32,
    pub(crate) retained_offset: Option<usize>,
}
#[derive(Clone, Copy, Debug)]
pub struct StreamSections<'a> {
    pub(crate) header: [u8; 96],
    pub(crate) descriptors: &'a [StoredSection],
    pub(crate) payloads: &'a [u8],
    pub(crate) requested_features: u64,
}
impl<'a> StreamSections<'a> {
    pub fn section_count(&self) -> usize {
        self.descriptors.len()
    }
    pub fn retained_count(&self) -> usize {
        self.descriptors
            .iter()
            .filter(|d| d.retained_offset.is_some())
            .count()
    }
    pub fn section(&self, index: usize) -> Option<Section<'a>> {
        let d = self
            .descriptors
            .iter()
            .filter(|d| d.retained_offset.is_some())
            .nth(index)?;
        let p = d.retained_offset?;
        Some(Section {
            fourcc: d.fourcc,
            version: d.version,
            flags: d.flags,
            payload: &self.payloads[p..p + d.size as usize],
        })
    }
    pub fn retained_bytes(&self) -> usize {
        self.payloads.len()
    }
}
fn u16at(b: &[u8], p: usize) -> u16 {
    u16::from_le_bytes(b[p..p + 2].try_into().unwrap())
}
fn u32at(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
fn u64at(b: &[u8], p: usize) -> u64 {
    u64::from_le_bytes(b[p..p + 8].try_into().unwrap())
}
fn overlap(a: u64, n: u64, b: u64, m: u64) -> bool {
    n != 0 && m != 0 && a < b + m && b < a + n
}
fn known(id: &[u8; 4]) -> bool {
    matches!(
        id,
        b"WOVR"
            | b"WDTL"
            | b"META"
            | b"TEMP"
            | b"LGRD"
            | b"GGRD"
            | b"REVN"
            | b"MKEY"
            | b"MTRD"
            | b"CONF"
    )
}
fn retain(id: &[u8; 4], requested: u64) -> bool {
    let meter = requested & METER_DOWNBEAT != 0;
    match id {
        b"META" => true,
        b"WOVR" => requested & (WAVEFORM_OVERVIEW | WAVEFORM_3BAND) != 0,
        b"WDTL" => requested & WAVEFORM_DETAIL != 0,
        b"TEMP" => {
            meter
                || requested
                    & (BPM | LOCAL_BEATGRID | GLOBAL_BEATGRID | DYNAMIC_TEMPO | GRID_LOCKING)
                    != 0
        }
        b"LGRD" => meter || requested & (LOCAL_BEATGRID | GRID_LOCKING) != 0,
        b"GGRD" | b"REVN" => {
            meter || requested & (GLOBAL_BEATGRID | DYNAMIC_TEMPO | GRID_LOCKING) != 0
        }
        b"MKEY" => requested & MUSICAL_KEY != 0,
        b"MTRD" => meter,
        b"CONF" => requested & CALIBRATED_QUALITY != 0,
        _ => false,
    }
}
pub(crate) fn read_exact(
    input: &mut impl Input,
    mut offset: u64,
    mut output: &mut [u8],
) -> Result<(), Error> {
    while !output.is_empty() {
        let n = input.read_at(offset, output)?;
        if n == 0 || n > output.len() {
            return Err(Error::Source);
        }
        offset = offset.checked_add(n as u64).ok_or(Error::LimitExceeded)?;
        output = &mut output[n..];
    }
    Ok(())
}
fn read_capped(
    input: &mut impl Input,
    mut offset: u64,
    output: &mut [u8],
    cap: usize,
) -> Result<(), Error> {
    for chunk in output.chunks_mut(cap) {
        read_exact(input, offset, chunk)?;
        offset += chunk.len() as u64;
    }
    Ok(())
}
fn update_crc(mut crc: u32, bytes: &[u8]) -> u32 {
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0x82f63b78 & (0u32.wrapping_sub(crc & 1)));
        }
    }
    crc
}
fn check_zero(
    input: &mut impl Input,
    mut offset: u64,
    end: u64,
    scratch: &mut [u8],
) -> Result<(), Error> {
    while offset < end {
        let n = (end - offset).min(scratch.len() as u64) as usize;
        read_exact(input, offset, &mut scratch[..n])?;
        if scratch[..n].iter().any(|b| *b != 0) {
            return Err(Error::Corrupt);
        }
        offset += n as u64;
    }
    Ok(())
}
/// Framing stage only. The public semantic reader must validate retained payloads
/// before returning a result. Failed calls may modify scratch and retained storage.
pub(crate) fn read_transport<'a>(
    input: &mut impl Input,
    options: StreamOptions,
    scratch: &mut [u8],
    descriptors: &'a mut [StoredSection],
    arena: &'a mut [u8],
) -> Result<StreamSections<'a>, Error> {
    if scratch.is_empty()
        || options.maximum_scratch_bytes == 0
        || options.requested_features & !ALL_FEATURES != 0
    {
        return Err(Error::InvalidArgument);
    }
    let scratch_size = scratch.len().min(options.maximum_scratch_bytes);
    let scratch = &mut scratch[..scratch_size];
    let size = input.size()?;
    if size > options.limits.container.maximum_file_bytes as u64 {
        return Err(Error::LimitExceeded);
    }
    if size < 96 {
        return Err(Error::Corrupt);
    }
    let mut header = [0; 96];
    read_capped(input, 0, &mut header, scratch.len())?;
    if &header[..4] != b"APTA" {
        return Err(Error::Corrupt);
    }
    if u16at(&header, 6) != 1 || u16at(&header, 8) != 1 || u16at(&header, 10) > 1 {
        return Err(Error::Unsupported);
    }
    let head = u16at(&header, 4) as u64;
    let count = u32at(&header, 20) as usize;
    let directory = u64at(&header, 24);
    let directory_size = count as u64 * 40;
    let flags = u32at(&header, 16);
    let total = u64at(&header, 40);
    if head < 96
        || head > size
        || count == 0
        || directory < head
        || directory % 8 != 0
        || directory > size
        || directory_size > size - directory
        || u64at(&header, 32) != size
        || crc32c(&header[..92]) != u32at(&header, 92)
        || flags & !7 != 0
        || (total == u64::MAX && flags & 3 != 3)
        || (total != u64::MAX && flags & 2 != 0)
        || u32at(&header, 48) == 0
        || u16at(&header, 52) == 0
    {
        return Err(Error::Corrupt);
    }
    if u32at(&header, 88) > 2 {
        return Err(Error::Unsupported);
    }
    if u32at(&header, 88) == 0 && header[56..88].iter().any(|b| *b != 0) {
        return Err(Error::Corrupt);
    }
    if count > options.limits.container.maximum_section_count {
        return Err(Error::LimitExceeded);
    }
    if count > descriptors.len() {
        return Err(Error::BufferTooSmall);
    }
    let descriptors = &mut descriptors[..count];
    let mut retained = 0usize;
    let mut wovr = false;
    let mut temp = false;
    let mut local = false;
    let mut global = None;
    let mut revision = None;
    for i in 0..count {
        let mut e = [0; 40];
        read_capped(input, directory + i as u64 * 40, &mut e, scratch.len())?;
        let id = e[..4].try_into().unwrap();
        let version = u16at(&e, 4);
        let f = u16at(&e, 6);
        let start = u64at(&e, 8);
        let n = u64at(&e, 16);
        if f & 6 != 0 {
            return Err(Error::Unsupported);
        }
        if n > options.maximum_section_bytes {
            return Err(Error::LimitExceeded);
        }
        if f & !1 != 0
            || n != u64at(&e, 24)
            || start % 8 != 0
            || start > size
            || n > size - start
            || overlap(start, n, 0, head)
            || overlap(start, n, directory, directory_size)
            || (options.limits.container.strict && u32at(&e, 36) != 0)
        {
            return Err(Error::Corrupt);
        }
        if known(&id) {
            if version != 1 {
                return Err(Error::Unsupported);
            }
            if (id == *b"WOVR") != (f == 1) {
                return Err(Error::Corrupt);
            }
        } else if f == 1 {
            return Err(Error::Unsupported);
        }
        for p in &descriptors[..i] {
            if overlap(start, n, p.source_offset, p.size)
                || (known(&id) && id != *b"WDTL" && p.fourcc == id)
            {
                return Err(Error::Corrupt);
            }
        }
        descriptors[i] = StoredSection {
            fourcc: id,
            version,
            flags: f,
            source_offset: start,
            size: n,
            crc: u32at(&e, 32),
            retained_offset: None,
        };
        match &id {
            b"WOVR" => wovr = true,
            b"TEMP" => temp = true,
            b"LGRD" => local = true,
            b"GGRD" => global = Some(i),
            b"REVN" => revision = Some(i),
            _ => {}
        }
    }
    if !wovr || ((local || global.is_some()) && !temp) || global.map(|i| i + 1) != revision {
        return Err(Error::Corrupt);
    }
    // Meter dependencies are retained only when a meter section is present.
    let selection = if descriptors.iter().any(|d| d.fourcc == *b"MTRD") {
        options.requested_features
    } else {
        options.requested_features & !METER_DOWNBEAT
    };
    for d in descriptors.iter_mut() {
        if retain(&d.fourcc, selection) {
            let p = retained;
            retained = retained
                .checked_add(usize::try_from(d.size).map_err(|_| Error::LimitExceeded)?)
                .ok_or(Error::LimitExceeded)?;
            if retained > options.maximum_retained_bytes {
                return Err(Error::LimitExceeded);
            }
            if retained > arena.len() {
                return Err(Error::BufferTooSmall);
            }
            d.retained_offset = Some(p);
        }
    }
    for d in descriptors.iter() {
        let mut offset = 0;
        let mut crc = !0;
        while offset < d.size {
            let n = (d.size - offset).min(scratch.len() as u64) as usize;
            read_exact(input, d.source_offset + offset, &mut scratch[..n])?;
            crc = update_crc(crc, &scratch[..n]);
            if let Some(p) = d.retained_offset {
                let start = p + offset as usize;
                arena[start..start + n].copy_from_slice(&scratch[..n]);
            }
            offset += n as u64;
        }
        if !crc != d.crc {
            return Err(Error::Corrupt);
        }
    }
    if options.limits.container.strict {
        let mut cursor = head;
        while cursor < size {
            let mut next = size;
            let mut end = size;
            if directory >= cursor {
                next = directory;
                end = directory + directory_size;
            }
            for d in descriptors.iter() {
                if d.size != 0 && d.source_offset >= cursor && d.source_offset < next {
                    next = d.source_offset;
                    end = next + d.size;
                }
            }
            check_zero(input, cursor, next, scratch)?;
            cursor = end;
        }
    }
    if input.size()? != size {
        return Err(Error::Corrupt);
    }
    Ok(StreamSections {
        header,
        descriptors,
        payloads: &arena[..retained],
        requested_features: options.requested_features,
    })
}

/// Writes already serialized, semantically validated container bytes with bounded
/// callback requests. The host must provide an empty or truncated destination.
/// Failure can leave a partial destination; transaction policy belongs to the host.
/// This transport does not replace native record-by-record result serialization.
pub fn write_bytes(
    output: &mut impl Output,
    bytes: &[u8],
    limits: Limits,
    maximum_write: usize,
) -> Result<u64, Error> {
    if maximum_write == 0 {
        return Err(Error::InvalidArgument);
    }
    crate::result::parse(bytes, limits)?;
    output.seek(0)?;
    for chunk in bytes.chunks(maximum_write) {
        let mut remaining = chunk;
        while !remaining.is_empty() {
            let n = output.write(remaining)?;
            if n == 0 || n > remaining.len() {
                return Err(Error::Source);
            }
            remaining = &remaining[n..];
        }
    }
    output.flush()?;
    Ok(bytes.len() as u64)
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::{vec, vec::Vec};
    struct Reader {
        bytes: Vec<u8>,
        progress: usize,
        calls: usize,
        largest: usize,
        stall: bool,
        changed: bool,
        sizes: usize,
    }
    impl Input for Reader {
        fn size(&mut self) -> Result<u64, Error> {
            self.sizes += 1;
            Ok(self.bytes.len() as u64 + u64::from(self.changed && self.sizes > 1))
        }
        fn read_at(&mut self, offset: u64, out: &mut [u8]) -> Result<usize, Error> {
            self.calls += 1;
            self.largest = self.largest.max(out.len());
            if self.stall {
                return Ok(0);
            }
            let p = offset as usize;
            let n = out
                .len()
                .min(self.progress)
                .min(self.bytes.len().saturating_sub(p));
            out[..n].copy_from_slice(&self.bytes[p..p + n]);
            Ok(n)
        }
    }
    fn reader() -> Reader {
        let hex = include_str!("../../tests/fixtures/dj-sections-v1-combined.apta.hex");
        let h: std::string::String = hex.chars().filter(|c| !c.is_whitespace()).collect();
        Reader {
            bytes: (0..h.len())
                .step_by(2)
                .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
                .collect(),
            progress: 3,
            calls: 0,
            largest: 0,
            stall: false,
            changed: false,
            sizes: 0,
        }
    }
    #[test]
    fn transport_tiny_scratch_selection_and_integrity() {
        let mut r = reader();
        let mut scratch = [0; 1];
        let mut descriptors = [StoredSection::default(); 4];
        let mut arena = vec![0; 1024];
        let s = read_transport(
            &mut r,
            StreamOptions {
                requested_features: MUSICAL_KEY,
                ..Default::default()
            },
            &mut scratch,
            &mut descriptors,
            &mut arena,
        )
        .unwrap();
        assert_eq!(s.retained_count(), 1);
        assert_eq!(s.section(0).unwrap().fourcc, *b"MKEY");
        assert!(s.retained_bytes() < r.bytes.len());
        assert_eq!(r.largest, 1);
        let mut r = reader();
        let p = u64at(&r.bytes, 96 + 8) as usize;
        r.bytes[p] ^= 1;
        assert_eq!(
            read_transport(
                &mut r,
                StreamOptions {
                    requested_features: 0,
                    ..Default::default()
                },
                &mut scratch,
                &mut descriptors,
                &mut arena
            )
            .unwrap_err(),
            Error::Corrupt
        );
    }
    #[test]
    fn transport_failure_limits_and_stability() {
        let mut scratch = [0; 11];
        let mut descriptors = [StoredSection::default(); 4];
        let mut arena = vec![0; 1024];
        for options in [
            StreamOptions {
                maximum_section_bytes: 1,
                ..Default::default()
            },
            StreamOptions {
                maximum_retained_bytes: 1,
                ..Default::default()
            },
            StreamOptions {
                limits: Limits {
                    container: crate::container::ParseOptions {
                        maximum_section_count: 1,
                        ..Default::default()
                    },
                    ..Default::default()
                },
                ..Default::default()
            },
        ] {
            assert_eq!(
                read_transport(
                    &mut reader(),
                    options,
                    &mut scratch,
                    &mut descriptors,
                    &mut arena
                )
                .unwrap_err(),
                Error::LimitExceeded
            );
        }
        let mut r = reader();
        r.stall = true;
        assert_eq!(
            read_transport(
                &mut r,
                StreamOptions::default(),
                &mut scratch,
                &mut descriptors,
                &mut arena
            )
            .unwrap_err(),
            Error::Source
        );
        let mut r = reader();
        r.changed = true;
        assert_eq!(
            read_transport(
                &mut r,
                StreamOptions::default(),
                &mut scratch,
                &mut descriptors,
                &mut arena
            )
            .unwrap_err(),
            Error::Corrupt
        );
        assert_eq!(
            read_transport(
                &mut reader(),
                StreamOptions::default(),
                &mut scratch,
                &mut descriptors[..3],
                &mut arena
            )
            .unwrap_err(),
            Error::BufferTooSmall
        );
        assert_eq!(
            read_transport(
                &mut reader(),
                StreamOptions::default(),
                &mut scratch,
                &mut descriptors,
                &mut []
            )
            .unwrap_err(),
            Error::BufferTooSmall
        );
    }
}
