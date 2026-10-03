// SPDX-License-Identifier: Apache-2.0
//! Allocation-free framing and waveform/META/detail semantic reader/writer.
//! Other recognized payloads return `Unsupported` from the waveform reader.
use crate::{Error, FeatureState, SourceInfo, WaveformColumn, WaveformOverview, WaveformSpan};

#[derive(Clone, Copy, Debug)]
pub struct ParseOptions {
    pub strict: bool,
    pub maximum_file_bytes: usize,
    pub maximum_section_count: usize,
    /// Defaults to 1024: packed interval alias checks are quadratic and allocate nothing.
    pub maximum_overview_spans: usize,
    pub maximum_waveform_columns: usize,
    /// Aggregate across WDTL sections; bounds duplicate-identity comparisons.
    pub maximum_detail_tiles: usize,
}
impl Default for ParseOptions {
    fn default() -> Self {
        Self {
            strict: true,
            maximum_file_bytes: 268435456,
            maximum_section_count: 64,
            maximum_overview_spans: 1024,
            maximum_waveform_columns: 16777216,
            maximum_detail_tiles: 1024,
        }
    }
}

pub fn crc32c(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0x82f63b78 & (0u32.wrapping_sub(crc & 1)));
        }
    }
    !crc
}
fn u16at(b: &[u8], p: usize) -> u16 {
    u16::from_le_bytes([b[p], b[p + 1]])
}
fn u32at(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
fn u64at(b: &[u8], p: usize) -> u64 {
    u64::from_le_bytes(b[p..p + 8].try_into().unwrap())
}
fn range(b: &[u8], offset: u64, size: u64) -> Result<&[u8], Error> {
    let start = usize::try_from(offset).map_err(|_| Error::Corrupt)?;
    let size = usize::try_from(size).map_err(|_| Error::Corrupt)?;
    b.get(start..start.checked_add(size).ok_or(Error::Corrupt)?)
        .ok_or(Error::Corrupt)
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
fn overlap(a: u64, an: u64, b: u64, bn: u64) -> bool {
    an != 0 && bn != 0 && a < b.saturating_add(bn) && b < a.saturating_add(an)
}

#[derive(Clone, Copy, Debug)]
pub struct Section<'a> {
    pub fourcc: [u8; 4],
    pub version: u16,
    pub flags: u16,
    pub payload: &'a [u8],
}
#[derive(Clone, Copy, Debug)]
pub struct Container<'a> {
    bytes: &'a [u8],
    stream: Option<crate::stream::StreamSections<'a>>,
    directory: usize,
    count: usize,
    pub source: SourceInfo,
    pub flags: u32,
    pub specification_minor: u16,
    pub producer_api_version: u32,
}
impl<'a> Container<'a> {
    /// Validates framing, CRCs, registry and padding. Payload semantics require a typed reader.
    pub fn parse(bytes: &'a [u8], options: ParseOptions) -> Result<Self, Error> {
        if bytes.len() > options.maximum_file_bytes {
            return Err(Error::LimitExceeded);
        }
        if bytes.len() < 96 || &bytes[..4] != b"APTA" {
            return Err(Error::Corrupt);
        }
        if u16at(bytes, 6) != 1 || u16at(bytes, 8) != 1 || u16at(bytes, 10) > 1 {
            return Err(Error::Unsupported);
        }
        let header = usize::from(u16at(bytes, 4));
        let count = u32at(bytes, 20) as usize;
        let directory = usize::try_from(u64at(bytes, 24)).map_err(|_| Error::Corrupt)?;
        if header < 96
            || header > bytes.len()
            || directory < header
            || directory % 8 != 0
            || count == 0
            || u64at(bytes, 32) != bytes.len() as u64
            || crc32c(&bytes[..92]) != u32at(bytes, 92)
        {
            return Err(Error::Corrupt);
        }
        if count > options.maximum_section_count {
            return Err(Error::LimitExceeded);
        }
        range(bytes, directory as u64, (count as u64) * 40)?;
        let directory_end = directory + count * 40;
        let flags = u32at(bytes, 16);
        let total = u64at(bytes, 40);
        if flags & !7 != 0
            || (total == u64::MAX && flags & 3 != 3)
            || (total != u64::MAX && flags & 2 != 0)
            || u32at(bytes, 48) == 0
            || u16at(bytes, 52) == 0
        {
            return Err(Error::Corrupt);
        }
        let fingerprint_kind = u32at(bytes, 88);
        if fingerprint_kind > 2 {
            return Err(Error::Unsupported);
        }
        if fingerprint_kind == 0 && bytes[56..88].iter().any(|b| *b != 0) {
            return Err(Error::Corrupt);
        }
        let source = SourceInfo {
            sample_rate: u32at(bytes, 48),
            channel_count: u16at(bytes, 52),
            channel_layout: u16at(bytes, 54),
            total_frames: if total == u64::MAX { None } else { Some(total) },
            fingerprint_kind,
            fingerprint: bytes[56..88].try_into().unwrap(),
        };
        let result = Self {
            bytes,
            stream: None,
            directory,
            count,
            source,
            flags,
            specification_minor: u16at(bytes, 10),
            producer_api_version: u32at(bytes, 12),
        };
        let mut wovr = false;
        let mut temp = false;
        let mut local = false;
        let mut global = None;
        let mut revision = None;
        for i in 0..count {
            let e = result.entry(i);
            let id: [u8; 4] = e[..4].try_into().unwrap();
            let f = u16at(e, 6);
            let start = u64at(e, 8);
            let size = u64at(e, 16);
            if f & 6 != 0 {
                return Err(Error::Unsupported);
            }
            if f & !1 != 0
                || size != u64at(e, 24)
                || start % 8 != 0
                || overlap(start, size, 0, header as u64)
                || overlap(start, size, directory as u64, (count as u64) * 40)
                || (options.strict && u32at(e, 36) != 0)
            {
                return Err(Error::Corrupt);
            }
            let payload = range(bytes, start, size)?;
            if crc32c(payload) != u32at(e, 32) {
                return Err(Error::Corrupt);
            }
            if known(&id) {
                if u16at(e, 4) != 1 {
                    return Err(Error::Unsupported);
                }
                if (id == *b"WOVR") != (f == 1) {
                    return Err(Error::Corrupt);
                }
            } else if f == 1 {
                return Err(Error::Unsupported);
            }
            for j in 0..i {
                let prev = result.entry(j);
                if overlap(start, size, u64at(prev, 8), u64at(prev, 16))
                    || (known(&id) && id != *b"WDTL" && prev[..4] == id)
                {
                    return Err(Error::Corrupt);
                }
            }
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
        // Validate all inter-structure padding even when payload offsets are not directory-ordered.
        if options.strict {
            let mut cursor = header;
            while cursor < bytes.len() {
                let mut next = bytes.len();
                let mut next_end = bytes.len();
                if directory >= cursor {
                    next = directory;
                    next_end = directory_end;
                }
                for i in 0..count {
                    let e = result.entry(i);
                    let start = u64at(e, 8) as usize;
                    let size = u64at(e, 16) as usize;
                    if size > 0 && start >= cursor && start < next {
                        next = start;
                        next_end = start + size;
                    }
                }
                if bytes[cursor..next].iter().any(|b| *b != 0) {
                    return Err(Error::Corrupt);
                }
                cursor = next_end;
            }
        }
        Ok(result)
    }
    fn entry(&self, index: usize) -> &'a [u8] {
        &self.bytes[self.directory + index * 40..self.directory + (index + 1) * 40]
    }
    pub fn section_count(&self) -> usize {
        self.count
    }
    pub fn section(&self, index: usize) -> Option<Section<'a>> {
        if let Some(stream) = self.stream {
            return stream.section(index);
        }
        if index >= self.count {
            return None;
        }
        let e = self.entry(index);
        Some(Section {
            fourcc: e[..4].try_into().unwrap(),
            version: u16at(e, 4),
            flags: u16at(e, 6),
            payload: range(self.bytes, u64at(e, 8), u64at(e, 16)).unwrap(),
        })
    }
    pub fn parse_waveform(&self, options: ParseOptions) -> Result<WovrView<'a>, Error> {
        Ok(self.parse_waveform_result(options)?.overview)
    }
    pub(crate) fn from_stream(stream: crate::stream::StreamSections<'a>) -> Self {
        let h = &stream.header;
        Self {
            bytes: &[],
            stream: Some(stream),
            directory: 0,
            count: stream.retained_count(),
            source: SourceInfo {
                sample_rate: u32at(h, 48),
                channel_count: u16at(h, 52),
                channel_layout: u16at(h, 54),
                total_frames: (u64at(h, 40) != u64::MAX).then_some(u64at(h, 40)),
                fingerprint_kind: u32at(h, 88),
                fingerprint: h[56..88].try_into().unwrap(),
            },
            flags: u32at(h, 16),
            specification_minor: u16at(h, 10),
            producer_api_version: u32at(h, 12),
        }
    }
    /// Validates every recognized payload in the supported waveform result slice.
    /// Unsupported analysis sections fail rather than being silently ignored.
    pub fn parse_waveform_result(
        &self,
        options: ParseOptions,
    ) -> Result<WaveformResultView<'a>, Error> {
        self.parse_waveform_fields(options, false)
    }
    pub(crate) fn parse_waveform_fields(
        &self,
        options: ParseOptions,
        allow_analysis: bool,
    ) -> Result<WaveformResultView<'a>, Error> {
        let fields = self.parse_selected_waveform(options, allow_analysis)?;
        Ok(WaveformResultView {
            container: fields.container,
            overview: fields.overview.ok_or(Error::Corrupt)?,
            metadata: fields.metadata,
            tile_count: fields.tile_count,
        })
    }
    pub(crate) fn parse_selected_waveform(
        &self,
        options: ParseOptions,
        allow_analysis: bool,
    ) -> Result<WaveformFields<'a>, Error> {
        let mut payload = None;
        let mut metadata = None;
        let mut tile_count = 0usize;
        let mut detail_columns = 0usize;
        for i in 0..self.count {
            let section = self.section(i).unwrap();
            if section.fourcc == *b"WOVR" {
                payload = Some(section.payload);
            } else if section.fourcc == *b"META" {
                metadata = Some(crate::meta::parse(section.payload)?);
            } else if section.fourcc == *b"WDTL" {
                let detail = self.parse_detail(i, options)?;
                tile_count = tile_count
                    .checked_add(detail.tile_count())
                    .ok_or(Error::LimitExceeded)?;
                detail_columns = detail_columns
                    .checked_add(detail.column_count())
                    .ok_or(Error::LimitExceeded)?;
                if tile_count > options.maximum_detail_tiles
                    || detail_columns > options.maximum_waveform_columns
                {
                    return Err(Error::LimitExceeded);
                }
                for j in 0..i {
                    if self.section(j).unwrap().fourcc != *b"WDTL" {
                        continue;
                    }
                    let previous = self.section(j).unwrap().payload;
                    for a in 0..detail.tile_count() {
                        let a = detail.tile(a).unwrap();
                        for b in 0..u32at(previous, 0) as usize {
                            let b = crate::detail::tile_from_validated(previous, b).unwrap();
                            if a.level_id == b.level_id && a.tile_index == b.tile_index {
                                return Err(Error::Corrupt);
                            }
                        }
                    }
                }
            } else if known(&section.fourcc) && !allow_analysis {
                return Err(Error::Unsupported);
            }
        }
        let overview = payload
            .map(|p| WovrView::parse(p, &self.source, self.flags, options))
            .transpose()?;
        if overview
            .map_or(0, |o| o.column_count())
            .checked_add(detail_columns)
            .ok_or(Error::LimitExceeded)?
            > options.maximum_waveform_columns
        {
            return Err(Error::LimitExceeded);
        }
        Ok(WaveformFields {
            container: *self,
            overview,
            metadata,
            tile_count,
        })
    }
    fn parse_detail(
        &self,
        index: usize,
        options: ParseOptions,
    ) -> Result<crate::detail::DetailPayload<'a>, Error> {
        crate::detail::DetailPayload::parse(
            self.section(index).ok_or(Error::InvalidArgument)?.payload,
            self.source.total_frames,
            self.flags & 1 != 0,
            crate::detail::DetailOptions {
                strict: options.strict,
                maximum_tiles: options.maximum_detail_tiles,
                maximum_columns: options.maximum_waveform_columns,
            },
        )
    }
}

/// Selected waveform fields; omitted features have no retained payload storage.
#[derive(Clone, Copy, Debug)]
pub struct WaveformFields<'a> {
    container: Container<'a>,
    pub overview: Option<WovrView<'a>>,
    pub metadata: Option<crate::Metadata<'a>>,
    tile_count: usize,
}
impl<'a> WaveformFields<'a> {
    pub fn tile_count(&self) -> usize {
        self.tile_count
    }
    pub fn tile(&self, mut index: usize) -> Option<crate::detail::PackedTile<'a>> {
        for i in 0..self.container.count {
            let section = self.container.section(i)?;
            if section.fourcc != *b"WDTL" {
                continue;
            }
            let count = u32at(section.payload, 0) as usize;
            if index < count {
                return crate::detail::tile_from_validated(section.payload, index);
            }
            index -= count;
        }
        None
    }
    pub(crate) fn require_overview(self) -> Result<WaveformResultView<'a>, Error> {
        Ok(WaveformResultView {
            container: self.container,
            overview: self.overview.ok_or(Error::Corrupt)?,
            metadata: self.metadata,
            tile_count: self.tile_count,
        })
    }
}

/// Validated borrowed waveform result. All backing bytes remain owned by the caller.
#[derive(Clone, Copy, Debug)]
pub struct WaveformResultView<'a> {
    container: Container<'a>,
    pub overview: WovrView<'a>,
    pub metadata: Option<crate::Metadata<'a>>,
    tile_count: usize,
}
impl<'a> WaveformResultView<'a> {
    pub fn tile_count(&self) -> usize {
        self.tile_count
    }
    pub fn tile(&self, mut index: usize) -> Option<crate::detail::PackedTile<'a>> {
        for i in 0..self.container.count {
            if self.container.section(i)?.fourcc != *b"WDTL" {
                continue;
            }
            let payload = self.container.section(i)?.payload;
            let count = u32at(payload, 0) as usize;
            if index < count {
                return crate::detail::tile_from_validated(payload, index);
            }
            index -= count;
        }
        None
    }
}

#[derive(Clone, Copy, Debug)]
pub struct WovrView<'a> {
    payload: &'a [u8],
    pub level_id: u32,
    pub frames_per_column: u32,
    pub origin_frame: u64,
    pub logical_column_count: u32,
    pub state: FeatureState,
    span_count: usize,
    span_offset: usize,
    column_offset: usize,
    column_count: usize,
}
impl<'a> WovrView<'a> {
    fn parse(
        p: &'a [u8],
        source: &SourceInfo,
        flags: u32,
        options: ParseOptions,
    ) -> Result<Self, Error> {
        if p.len() < 48 {
            return Err(Error::Corrupt);
        }
        let state = match u32at(p, 40) {
            1 => FeatureState::Partial,
            2 => FeatureState::Provisional,
            3 => FeatureState::Stable,
            4 => FeatureState::Final,
            _ => return Err(Error::Corrupt),
        };
        let span_count = u32at(p, 20) as usize;
        let logical = u32at(p, 16);
        let fpc = u32at(p, 4);
        let origin = u64at(p, 8);
        if span_count > options.maximum_overview_spans
            || logical as usize > options.maximum_waveform_columns
        {
            return Err(Error::LimitExceeded);
        }
        if fpc == 0
            || logical == 0
            || span_count == 0
            || (options.strict && u32at(p, 44) != 0)
            || (state != FeatureState::Final && flags & 1 == 0)
            || (state == FeatureState::Final && source.total_frames.is_none())
        {
            return Err(Error::Corrupt);
        }
        if let Some(total) = source.total_frames {
            let relative = total.checked_sub(origin).ok_or(Error::Corrupt)?;
            if relative / u64::from(fpc) + u64::from(relative % u64::from(fpc) != 0)
                != u64::from(logical)
            {
                return Err(Error::Corrupt);
            }
        }
        let so = u64at(p, 24);
        let co = u64at(p, 32);
        range(p, so, (span_count as u64) * 32)?;
        range(p, co, 0)?;
        if so < 48 || co < 48 {
            return Err(Error::Corrupt);
        }
        let mut view = Self {
            payload: p,
            level_id: u32at(p, 0),
            frames_per_column: fpc,
            origin_frame: origin,
            logical_column_count: logical,
            state,
            span_count,
            span_offset: so as usize,
            column_offset: co as usize,
            column_count: 0,
        };
        let mut previous_frame = origin;
        let mut previous_column = 0u64;
        for i in 0..span_count {
            let span = view.span(i).unwrap();
            let raw = &p[view.span_offset + i * 32..][..32];
            let end_col = u64::from(span.first_column_index) + u64::from(span.column_count);
            let expected_first = origin
                .checked_add(u64::from(span.first_column_index) * u64::from(fpc))
                .ok_or(Error::Corrupt)?;
            let expected_end = origin
                .checked_add(end_col * u64::from(fpc))
                .ok_or(Error::Corrupt)?;
            if span.first_frame >= span.end_frame
                || span.column_count == 0
                || end_col > u64::from(logical)
                || span.first_frame < previous_frame
                || u64::from(span.first_column_index) < previous_column
                || span.first_frame != expected_first
                || (span.end_frame != expected_end
                    && !(source.total_frames == Some(span.end_frame)
                        && end_col == u64::from(logical)
                        && span.end_frame < expected_end))
                || source.total_frames.is_some_and(|t| span.end_frame > t)
                || (options.strict && u32at(raw, 28) != 0)
            {
                return Err(Error::Corrupt);
            }
            if state == FeatureState::Final
                && (span.first_frame != previous_frame
                    || u64::from(span.first_column_index) != previous_column)
            {
                return Err(Error::Corrupt);
            }
            let start = co
                .checked_add(u64::from(span.data_column_offset) * 10)
                .ok_or(Error::Corrupt)?;
            let size = u64::from(span.column_count) * 10;
            let data = range(p, start, size)?;
            if overlap(start, size, 0, 48) || overlap(start, size, so, (span_count as u64) * 32) {
                return Err(Error::Corrupt);
            }
            for j in 0..i {
                let prev = view.span(j).unwrap();
                if overlap(
                    u64::from(span.data_column_offset),
                    u64::from(span.column_count),
                    u64::from(prev.data_column_offset),
                    u64::from(prev.column_count),
                ) {
                    return Err(Error::Corrupt);
                }
            }
            for column in data.chunks_exact(10) {
                validate_column(decode_column(column), options.strict)?;
            }
            view.column_count = view
                .column_count
                .checked_add(span.column_count as usize)
                .ok_or(Error::LimitExceeded)?;
            if view.column_count > options.maximum_waveform_columns
                || view.column_count > u32::MAX as usize
            {
                return Err(Error::LimitExceeded);
            }
            previous_frame = span.end_frame;
            previous_column = end_col;
        }
        if state == FeatureState::Final
            && (Some(previous_frame) != source.total_frames
                || previous_column != u64::from(logical))
        {
            return Err(Error::Corrupt);
        }
        Ok(view)
    }
    pub fn span_count(&self) -> usize {
        self.span_count
    }
    pub fn column_count(&self) -> usize {
        self.column_count
    }
    pub fn span(&self, index: usize) -> Option<WaveformSpan> {
        if index >= self.span_count {
            return None;
        }
        let b = &self.payload[self.span_offset + index * 32..];
        Some(WaveformSpan {
            first_frame: u64at(b, 0),
            end_frame: u64at(b, 8),
            first_column_index: u32at(b, 16),
            column_count: u32at(b, 20),
            data_column_offset: u32at(b, 24),
        })
    }
    pub fn column(&self, span_index: usize, column_index: usize) -> Option<WaveformColumn> {
        let span = self.span(span_index)?;
        if column_index >= span.column_count as usize {
            return None;
        }
        let start = self.column_offset + (span.data_column_offset as usize + column_index) * 10;
        let mut column = decode_column(&self.payload[start..start + 10]);
        column.flags &= 31;
        if column.flags & 8 == 0 {
            column.low = 0;
            column.mid = 0;
            column.high = 0;
        }
        Some(column)
    }
    /// Copies into caller-owned storage, compacting packed column offsets.
    pub fn copy_into<'b>(
        &self,
        spans: &'b mut [WaveformSpan],
        columns: &'b mut [WaveformColumn],
    ) -> Result<WaveformOverview<'b>, Error> {
        if spans.len() < self.span_count || columns.len() < self.column_count {
            return Err(Error::BufferTooSmall);
        }
        let mut offset = 0;
        for (i, dst) in spans[..self.span_count].iter_mut().enumerate() {
            *dst = self.span(i).unwrap();
            dst.data_column_offset = offset as u32;
            for j in 0..dst.column_count as usize {
                columns[offset] = self.column(i, j).unwrap();
                offset += 1;
            }
        }
        Ok(WaveformOverview {
            level_id: self.level_id,
            frames_per_column: self.frames_per_column,
            origin_frame: self.origin_frame,
            logical_column_count: self.logical_column_count,
            state: self.state,
            spans: &spans[..self.span_count],
            columns: &columns[..self.column_count],
        })
    }
}
fn decode_column(b: &[u8]) -> WaveformColumn {
    WaveformColumn {
        minimum: u16at(b, 0) as i16,
        maximum: u16at(b, 2) as i16,
        rms: u16at(b, 4),
        low: b[6],
        mid: b[7],
        high: b[8],
        flags: b[9],
    }
}
fn validate_column(c: WaveformColumn, strict: bool) -> Result<(), Error> {
    if c.flags & 1 == 0
        || c.minimum > c.maximum
        || (strict && c.flags & !31 != 0)
        || (strict && c.flags & 8 == 0 && (c.low != 0 || c.mid != 0 || c.high != 0))
    {
        Err(Error::Corrupt)
    } else {
        Ok(())
    }
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
/// Required output size for a canonical WOVR-only container.
pub fn waveform_size(overview: &WaveformOverview<'_>) -> Result<usize, Error> {
    136usize
        .checked_add(48)
        .and_then(|n| n.checked_add(overview.spans.len().checked_mul(32)?))
        .and_then(|n| n.checked_add(overview.columns.len().checked_mul(10)?))
        .ok_or(Error::LimitExceeded)
}
/// Writes into caller storage; output is only valid on success. Input is semantically checked.
pub fn write_waveform(
    source: &SourceInfo,
    overview: &WaveformOverview<'_>,
    output: &mut [u8],
) -> Result<usize, Error> {
    let size = waveform_size(overview)?;
    if overview.spans.len() > u32::MAX as usize || overview.columns.len() > u32::MAX as usize {
        return Err(Error::LimitExceeded);
    }
    if output.len() < size {
        return Err(Error::BufferTooSmall);
    }
    let b = &mut output[..size];
    b.fill(0);
    b[..4].copy_from_slice(b"APTA");
    put16(b, 4, 96);
    put16(b, 6, 1);
    put16(b, 8, 1);
    put32(b, 12, 1 << 22);
    let flags = if source.total_frames.is_none() {
        3
    } else if overview.state != FeatureState::Final {
        1
    } else {
        0
    };
    put32(b, 16, flags);
    put32(b, 20, 1);
    put64(b, 24, 96);
    put64(b, 32, size as u64);
    put64(b, 40, source.total_frames.unwrap_or(u64::MAX));
    put32(b, 48, source.sample_rate);
    put16(b, 52, source.channel_count);
    put16(b, 54, source.channel_layout);
    b[56..88].copy_from_slice(&source.fingerprint);
    put32(b, 88, source.fingerprint_kind);
    let crc = crc32c(&b[..92]);
    put32(b, 92, crc);
    b[96..100].copy_from_slice(b"WOVR");
    put16(b, 100, 1);
    put16(b, 102, 1);
    put64(b, 104, 136);
    put64(b, 112, (size - 136) as u64);
    put64(b, 120, (size - 136) as u64);
    let p = &mut b[136..];
    put32(p, 0, overview.level_id);
    put32(p, 4, overview.frames_per_column);
    put64(p, 8, overview.origin_frame);
    put32(p, 16, overview.logical_column_count);
    put32(p, 20, overview.spans.len() as u32);
    put64(p, 24, 48);
    let co = 48 + overview.spans.len() * 32;
    put64(p, 32, co as u64);
    put32(p, 40, overview.state as u32);
    for (i, s) in overview.spans.iter().enumerate() {
        let n = 48 + i * 32;
        put64(p, n, s.first_frame);
        put64(p, n + 8, s.end_frame);
        put32(p, n + 16, s.first_column_index);
        put32(p, n + 20, s.column_count);
        put32(p, n + 24, s.data_column_offset);
    }
    for (i, c) in overview.columns.iter().enumerate() {
        let n = co + i * 10;
        put16(p, n, c.minimum as u16);
        put16(p, n + 2, c.maximum as u16);
        put16(p, n + 4, c.rms);
        p[n + 6] = c.low;
        p[n + 7] = c.mid;
        p[n + 8] = c.high;
        p[n + 9] = c.flags;
    }
    let options = ParseOptions {
        maximum_file_bytes: usize::MAX,
        maximum_section_count: 1,
        maximum_overview_spans: overview.spans.len(),
        maximum_waveform_columns: overview
            .columns
            .len()
            .max(overview.logical_column_count as usize),
        strict: true,
        maximum_detail_tiles: 0,
    };
    let view = WovrView::parse(p, source, flags, options)?;
    if view.column_count() != overview.columns.len() {
        return Err(Error::InvalidArgument);
    }
    let crc = crc32c(p);
    put32(b, 128, crc);
    Container::parse(b, options)?;
    Ok(size)
}

fn align8(size: usize) -> Result<usize, Error> {
    size.checked_add(7)
        .map(|n| n & !7)
        .ok_or(Error::LimitExceeded)
}

/// Canonical size for WOVR, optional WDTL and optional recognized META fields.
/// Unknown META keys can be retained separately with `meta::copy_canonical`.
pub fn waveform_result_size(
    source: &SourceInfo,
    overview: &WaveformOverview<'_>,
    tiles: &[crate::WaveformTile<'_>],
    metadata: Option<&crate::Metadata<'_>>,
) -> Result<usize, Error> {
    let extra = usize::from(!tiles.is_empty()) + usize::from(metadata.is_some());
    let mut size = waveform_size(overview)?
        .checked_add(extra * 40)
        .ok_or(Error::LimitExceeded)?;
    if !tiles.is_empty() {
        size = align8(size)?
            .checked_add(crate::detail::payload_size(tiles, source.total_frames)?)
            .ok_or(Error::LimitExceeded)?;
    }
    if let Some(metadata) = metadata {
        size = align8(size)?
            .checked_add(crate::meta::serialized_size(metadata)?)
            .ok_or(Error::LimitExceeded)?;
    }
    Ok(size)
}

/// Writes deterministic WOVR/WDTL/META bytes with caller-owned storage.
/// Tiles must be ordered by identity. Output is valid only on success.
pub fn write_waveform_result(
    source: &SourceInfo,
    overview: &WaveformOverview<'_>,
    tiles: &[crate::WaveformTile<'_>],
    metadata: Option<&crate::Metadata<'_>>,
    output: &mut [u8],
) -> Result<usize, Error> {
    let size = waveform_result_size(source, overview, tiles, metadata)?;
    if output.len() < size {
        return Err(Error::BufferTooSmall);
    }
    let base = write_waveform(source, overview, output)?;
    let extra = usize::from(!tiles.is_empty()) + usize::from(metadata.is_some());
    if extra == 0 {
        return Ok(base);
    }
    let b = &mut output[..size];
    b.copy_within(136..base, 136 + extra * 40);
    b[136..136 + extra * 40].fill(0);
    b[base + extra * 40..].fill(0);
    put64(b, 104, (136 + extra * 40) as u64);
    put32(b, 20, (1 + extra) as u32);
    put64(b, 32, size as u64);
    if tiles.iter().any(|t| t.state != FeatureState::Final) {
        let flags = u32at(b, 16) | 1;
        put32(b, 16, flags);
    }
    let mut end = base + extra * 40;
    let mut entry = 136;
    if !tiles.is_empty() {
        let offset = align8(end)?;
        let len = crate::detail::write_payload(tiles, source.total_frames, &mut b[offset..])?;
        write_entry(b, entry, b"WDTL", offset, len);
        entry += 40;
        end = offset + len;
    }
    if let Some(metadata) = metadata {
        let offset = align8(end)?;
        let len = crate::meta::write(metadata, &mut b[offset..])?;
        write_entry(b, entry, b"META", offset, len);
    }
    let crc = crc32c(&b[..92]);
    put32(b, 92, crc);
    Ok(size)
}

fn write_entry(b: &mut [u8], entry: usize, id: &[u8; 4], offset: usize, len: usize) {
    b[entry..entry + 4].copy_from_slice(id);
    put16(b, entry + 4, 1);
    put64(b, entry + 8, offset as u64);
    put64(b, entry + 16, len as u64);
    put64(b, entry + 24, len as u64);
    let crc = crc32c(&b[offset..offset + len]);
    put32(b, entry + 32, crc);
}
