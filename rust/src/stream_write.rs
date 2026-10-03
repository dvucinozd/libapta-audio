// SPDX-License-Identifier: Apache-2.0
//! Canonical synchronous output using fixed framing and record buffers.
use crate::{
    builder,
    container::crc32c,
    result::{Limits, ResultInput},
    stream::Output,
    *,
};
const IDS: [[u8; 4]; 10] = [
    *b"WOVR", *b"WDTL", *b"META", *b"TEMP", *b"LGRD", *b"GGRD", *b"REVN", *b"MKEY", *b"MTRD",
    *b"CONF",
];
fn p16(b: &mut [u8], p: usize, v: u16) {
    b[p..p + 2].copy_from_slice(&v.to_le_bytes());
}
fn p32(b: &mut [u8], p: usize, v: u32) {
    b[p..p + 4].copy_from_slice(&v.to_le_bytes());
}
fn p64(b: &mut [u8], p: usize, v: u64) {
    b[p..p + 8].copy_from_slice(&v.to_le_bytes());
}
fn range(b: &mut [u8], p: usize, r: FrameRange) {
    p64(b, p, r.first_frame);
    p64(b, p + 8, r.end_frame);
}
fn update_crc(mut crc: u32, bytes: &[u8]) -> u32 {
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0x82f63b78 & 0u32.wrapping_sub(crc & 1));
        }
    }
    crc
}
fn column(
    c: WaveformColumn,
    emit: &mut impl FnMut(&[u8]) -> Result<(), Error>,
) -> Result<(), Error> {
    let mut b = [0; 10];
    p16(&mut b, 0, c.minimum as u16);
    p16(&mut b, 2, c.maximum as u16);
    p16(&mut b, 4, c.rms);
    b[6..].copy_from_slice(&[c.low, c.mid, c.high, c.flags]);
    emit(&b)
}
fn cbor(
    major: u8,
    value: u64,
    emit: &mut impl FnMut(&[u8]) -> Result<(), Error>,
) -> Result<(), Error> {
    let n = match value {
        0..=23 => 0,
        24..=255 => 1,
        256..=65535 => 2,
        65536..=4294967295 => 4,
        _ => 8,
    };
    let mut b = [0; 9];
    b[0] = major << 5
        | match n {
            0 => value as u8,
            1 => 24,
            2 => 25,
            4 => 26,
            _ => 27,
        };
    b[1..1 + n].copy_from_slice(&value.to_be_bytes()[8 - n..]);
    emit(&b[..1 + n])
}
fn metadata(
    m: Metadata<'_>,
    emit: &mut impl FnMut(&[u8]) -> Result<(), Error>,
) -> Result<(), Error> {
    let fields = [
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
    ];
    cbor(
        5,
        (fields.iter().filter(|f| f.is_some()).count()
            + usize::from(m.creation_unix_time.is_some())) as u64,
        emit,
    )?;
    for (i, f) in fields.iter().enumerate() {
        if i == 4 {
            if let Some(t) = m.creation_unix_time {
                cbor(0, 5, emit)?;
                cbor(0, t, emit)?;
            }
        } else if let Some((major, bytes)) = f {
            cbor(0, (i + 1) as u64, emit)?;
            cbor(*major, bytes.len() as u64, emit)?;
            for chunk in bytes.chunks(144) {
                emit(chunk)?;
            }
        }
    }
    Ok(())
}
fn emit_section(
    id: usize,
    r: &ResultInput<'_>,
    emit: &mut impl FnMut(&[u8]) -> Result<(), Error>,
) -> Result<(), Error> {
    let mut b = [0; 144];
    match id {
        0 => {
            let w = r.overview;
            p32(&mut b, 0, w.level_id);
            p32(&mut b, 4, w.frames_per_column);
            p64(&mut b, 8, w.origin_frame);
            p32(&mut b, 16, w.logical_column_count);
            p32(&mut b, 20, w.spans.len() as u32);
            p64(&mut b, 24, 48);
            p64(&mut b, 32, (48 + w.spans.len() * 32) as u64);
            p32(&mut b, 40, w.state as u32);
            emit(&b[..48])?;
            for s in w.spans {
                b.fill(0);
                p64(&mut b, 0, s.first_frame);
                p64(&mut b, 8, s.end_frame);
                p32(&mut b, 16, s.first_column_index);
                p32(&mut b, 20, s.column_count);
                p32(&mut b, 24, s.data_column_offset);
                emit(&b[..32])?;
            }
            for c in w.columns {
                column(*c, emit)?;
            }
        }
        1 => {
            p32(&mut b, 0, r.tiles.len() as u32);
            p64(&mut b, 8, 16);
            emit(&b[..16])?;
            let mut offset = 16 + r.tiles.len() * 48;
            for t in r.tiles {
                b.fill(0);
                p32(&mut b, 0, t.level_id);
                p32(&mut b, 4, t.tile_index);
                p64(&mut b, 8, t.first_frame);
                p64(&mut b, 16, t.end_frame);
                p32(&mut b, 24, t.first_column_index);
                p32(&mut b, 28, t.columns.len() as u32);
                p64(&mut b, 32, offset as u64);
                p32(&mut b, 40, t.state as u32);
                b[46] = t.confidence;
                emit(&b[..48])?;
                offset += t.columns.len() * 10;
            }
            for t in r.tiles {
                for c in t.columns {
                    column(*c, emit)?;
                }
            }
        }
        2 => metadata(r.metadata.unwrap(), emit)?,
        3 => {
            let t = r.tempo.unwrap();
            b[0] = 1;
            b[2] = t.selected.state as u8;
            b[3] = t.selected.confidence;
            p32(&mut b, 4, t.selected.flags);
            p32(&mut b, 8, t.selected.tempo_millibpm);
            p32(&mut b, 12, t.selected.candidate_set_id);
            range(&mut b, 16, t.selected.evidence_range);
            range(&mut b, 32, t.selected.applicability_range);
            p32(&mut b, 48, t.candidates.len() as u32);
            emit(&b[..56])?;
            for c in t.candidates {
                b.fill(0);
                p32(&mut b, 0, c.tempo_millibpm);
                p16(&mut b, 4, c.score);
                b[6] = c.confidence;
                b[7] = c.relation_to_selected;
                p32(&mut b, 8, c.flags);
                emit(&b[..16])?;
            }
        }
        4 => {
            crate::tempo::write_local_grid(
                &r.local_grid.unwrap(),
                r.tempo.unwrap().selected.tempo_millibpm,
                &mut b,
            )?;
            emit(&b)?;
        }
        5 => {
            let g = r.global_grid.unwrap();
            b[0] = 1;
            b[2] = g.state as u8;
            b[3] = g.confidence;
            p32(&mut b, 4, g.flags);
            p32(&mut b, 8, g.representation as u32);
            p32(&mut b, 12, 1);
            p32(&mut b, 16, g.segments.len() as u32);
            p32(&mut b, 20, g.beats.len() as u32);
            range(&mut b, 24, g.requested_range);
            range(&mut b, 40, g.evidence_range);
            range(&mut b, 56, g.applicability_range);
            range(&mut b, 72, g.coverage_range);
            emit(&b[..96])?;
            for s in g.segments {
                b.fill(0);
                range(&mut b, 0, s.applicability_range);
                p64(&mut b, 16, s.anchor_position.whole_frame);
                p32(&mut b, 24, s.anchor_position.fraction_q32);
                p64(&mut b, 32, s.anchor_ordinal as u64);
                p64(&mut b, 40, s.frames_per_beat.whole_frames);
                p32(&mut b, 48, s.frames_per_beat.fraction_q32);
                p32(&mut b, 52, s.beat_count);
                p32(&mut b, 56, s.nominal_tempo_millibpm);
                p32(&mut b, 60, s.segment_id);
                p32(&mut b, 64, s.revision);
                p32(&mut b, 68, s.flags);
                b[72] = s.state as u8;
                b[73] = s.confidence;
                emit(&b[..80])?;
            }
            for v in g.beats {
                b.fill(0);
                p64(&mut b, 0, v.position.whole_frame);
                p32(&mut b, 8, v.position.fraction_q32);
                p64(&mut b, 16, v.ordinal as u64);
                p32(&mut b, 24, v.revision);
                p32(&mut b, 28, v.flags);
                b[32] = v.confidence;
                emit(&b[..40])?;
            }
        }
        6 => {
            crate::grid::write_revision(
                &r.revision.unwrap(),
                &r.global_grid.unwrap(),
                true,
                &mut b,
            )?;
            emit(&b[..80])?;
        }
        7 => {
            let k = r.key.unwrap();
            b[0] = 1;
            b[2] = k.state as u8;
            b[3] = k.confidence;
            b[4] = k.tonic;
            b[5] = k.mode;
            p16(&mut b, 6, k.tuning_offset_cents as u16);
            p32(&mut b, 12, k.candidates.len() as u32);
            p64(&mut b, 16, k.first_frame);
            p64(&mut b, 24, k.end_frame);
            p32(&mut b, 32, 40);
            emit(&b[..40])?;
            for c in k.candidates {
                b.fill(0);
                b[0] = c.tonic;
                b[1] = c.mode;
                p16(&mut b, 2, c.tuning_offset_cents as u16);
                p16(&mut b, 4, c.score);
                b[6] = c.confidence;
                emit(&b[..16])?;
            }
        }
        8 => {
            let m = r.meter.unwrap();
            b[0] = 1;
            b[2] = m.state as u8;
            b[3] = m.confidence;
            p16(&mut b, 4, m.numerator);
            p16(&mut b, 6, m.denominator);
            p32(&mut b, 12, m.segments.len() as u32);
            p64(&mut b, 16, m.downbeat_frame);
            p64(&mut b, 24, m.downbeat_ordinal as u64);
            p32(&mut b, 32, 48);
            emit(&b[..48])?;
            for s in m.segments {
                b.fill(0);
                p64(&mut b, 0, s.first_frame);
                p64(&mut b, 8, s.end_frame);
                p64(&mut b, 16, s.downbeat_frame);
                p64(&mut b, 24, s.downbeat_ordinal as u64);
                p16(&mut b, 32, s.numerator);
                p16(&mut b, 34, s.denominator);
                b[36] = s.state as u8;
                b[37] = s.confidence;
                p32(&mut b, 44, s.segment_id);
                emit(&b[..56])?;
            }
        }
        9 => {
            b[0] = 1;
            p16(&mut b, 2, 32);
            p32(&mut b, 4, r.quality.len() as u32);
            p32(&mut b, 8, 16);
            emit(&b[..16])?;
            for bit in 0..11 {
                if let Some(q) = r.quality.iter().find(|q| q.feature == 1 << bit) {
                    b.fill(0);
                    p64(&mut b, 0, q.feature);
                    p32(&mut b, 8, q.calibration_model_id);
                    p16(&mut b, 12, q.evidence_coverage_permille);
                    b[14] = q.confidence;
                    b[15] = q.state as u8;
                    p32(&mut b, 16, q.flags);
                    emit(&b[..32])?;
                }
            }
        }
        _ => unreachable!(),
    }
    Ok(())
}
fn write_all(output: &mut impl Output, mut bytes: &[u8]) -> Result<(), Error> {
    while !bytes.is_empty() {
        let n = output.write(bytes)?;
        if n == 0 || n > bytes.len() {
            return Err(Error::Source);
        }
        bytes = &bytes[n..];
    }
    Ok(())
}
/// Validate and emit canonical bytes. The destination must be empty or truncated.
/// Callback failure can leave partial output; only successful flush establishes
/// completion. Every write request is at most 144 bytes; no allocation occurs.
pub fn write(
    r: &ResultInput<'_>,
    output: &mut impl Output,
    limits: Limits,
) -> Result<usize, Error> {
    let expected = builder::validate(r, limits)?;
    let present = [
        true,
        !r.tiles.is_empty(),
        r.metadata.is_some(),
        r.tempo.is_some(),
        r.local_grid.is_some(),
        r.global_grid.is_some(),
        r.revision.is_some(),
        r.key.is_some(),
        r.meter.is_some(),
        !r.quality.is_empty(),
    ];
    let count = present.iter().filter(|p| **p).count();
    if count > limits.container.maximum_section_count {
        return Err(Error::LimitExceeded);
    }
    let mut lengths = [0usize; 10];
    let mut offsets = [0usize; 10];
    let mut crcs = [0u32; 10];
    let mut end = 96 + 40 * count;
    for i in 0..10 {
        if !present[i] {
            continue;
        }
        let mut crc = !0;
        emit_section(i, r, &mut |b| {
            lengths[i] = lengths[i]
                .checked_add(b.len())
                .ok_or(Error::LimitExceeded)?;
            crc = update_crc(crc, b);
            Ok(())
        })?;
        crcs[i] = !crc;
        end = end.checked_add(7).ok_or(Error::LimitExceeded)? & !7;
        offsets[i] = end;
        end = end.checked_add(lengths[i]).ok_or(Error::LimitExceeded)?;
    }
    if end != expected {
        return Err(Error::InvalidArgument);
    }
    let partial = r.overview.state != FeatureState::Final
        || r.tiles.iter().any(|t| t.state != FeatureState::Final)
        || r.tempo
            .is_some_and(|t| t.selected.state != FeatureState::Final)
        || r.local_grid.is_some_and(|g| {
            g.state != FeatureState::Final || g.segment.state != FeatureState::Final
        })
        || r.global_grid.is_some_and(|g| {
            g.state != FeatureState::Final
                || g.segments.iter().any(|s| s.state != FeatureState::Final)
        })
        || r.revision
            .is_some_and(|v| v.state == RevisionState::Pending)
        || r.key.is_some_and(|k| k.state != FeatureState::Final)
        || r.meter.is_some_and(|m| {
            m.state != FeatureState::Final
                || m.segments.iter().any(|s| s.state != FeatureState::Final)
        })
        || r.quality.iter().any(|q| q.state != FeatureState::Final);
    let mut h = [0; 96];
    h[..4].copy_from_slice(b"APTA");
    p16(&mut h, 4, 96);
    p16(&mut h, 6, 1);
    p16(&mut h, 8, 1);
    p32(&mut h, 12, 1 << 22);
    p32(
        &mut h,
        16,
        if r.source.total_frames.is_none() {
            3
        } else {
            u32::from(partial)
        },
    );
    p32(&mut h, 20, count as u32);
    p64(&mut h, 24, 96);
    p64(&mut h, 32, end as u64);
    p64(&mut h, 40, r.source.total_frames.unwrap_or(u64::MAX));
    p32(&mut h, 48, r.source.sample_rate);
    p16(&mut h, 52, r.source.channel_count);
    p16(&mut h, 54, r.source.channel_layout);
    h[56..88].copy_from_slice(&r.source.fingerprint);
    p32(&mut h, 88, r.source.fingerprint_kind);
    let crc = crc32c(&h[..92]);
    p32(&mut h, 92, crc);
    output.seek(0)?;
    write_all(output, &h)?;
    for i in 0..10 {
        if !present[i] {
            continue;
        }
        let mut d = [0; 40];
        d[..4].copy_from_slice(&IDS[i]);
        d[4] = 1;
        d[6] = u8::from(i == 0);
        p64(&mut d, 8, offsets[i] as u64);
        p64(&mut d, 16, lengths[i] as u64);
        p64(&mut d, 24, lengths[i] as u64);
        p32(&mut d, 32, crcs[i]);
        write_all(output, &d)?;
    }
    let mut cursor = 96 + 40 * count;
    for i in 0..10 {
        if !present[i] {
            continue;
        }
        write_all(output, &[0; 7][..offsets[i] - cursor])?;
        emit_section(i, r, &mut |b| write_all(output, b))?;
        cursor = offsets[i] + lengths[i];
    }
    output.seek(end as u64)?;
    output.flush()?;
    Ok(end)
}
