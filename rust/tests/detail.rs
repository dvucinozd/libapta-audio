// SPDX-License-Identifier: Apache-2.0
use libapta::{
    container::{Container, ParseOptions},
    detail::*,
    Error, FeatureState, WaveformColumn, WaveformTile,
};
fn columns() -> [WaveformColumn; 2] {
    [WaveformColumn {
        minimum: -100,
        maximum: 200,
        rms: 125,
        flags: 1,
        ..WaveformColumn::default()
    }; 2]
}
fn tile(c: &[WaveformColumn]) -> WaveformTile<'_> {
    WaveformTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 512,
        first_column_index: 0,
        state: FeatureState::Final,
        confidence: 255,
        columns: c,
    }
}
fn bytes() -> Vec<u8> {
    let c = columns();
    let mut out = vec![0; 84];
    write_payload(&[tile(&c)], Some(512), &mut out).unwrap();
    out
}
fn parse(b: &[u8]) -> Result<DetailPayload<'_>, Error> {
    DetailPayload::parse(b, Some(512), false, DetailOptions::default())
}
#[test]
fn roundtrip_owned_copy_and_short_buffer() {
    let b = bytes();
    let p = parse(&b).unwrap();
    assert_eq!(p.tile_count(), 1);
    assert_eq!(p.column_count(), 2);
    let t = p.tile(0).unwrap();
    assert_eq!(t.column(0), Some(columns()[0]));
    assert_eq!(t.column(2), None);
    assert_eq!(t.column(usize::MAX), None);
    assert!(p.tile(1).is_none());
    let mut c = [WaveformColumn::default(); 2];
    assert_eq!(t.copy_columns(&mut c), Ok(2));
    assert_eq!(c, columns());
    let mut short = [WaveformColumn::default(); 1];
    assert_eq!(t.copy_columns(&mut short), Err(Error::BufferTooSmall));
    assert_eq!(short, [WaveformColumn::default(); 1]);
    let mut out = [0xaa; 83];
    assert_eq!(
        write_payload(&[tile(&c)], Some(512), &mut out),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(out, [0xaa; 83]);
}
#[test]
fn every_prefix_rejected() {
    let b = bytes();
    for n in 0..b.len() {
        assert!(parse(&b[..n]).is_err(), "prefix {n}");
    }
}
#[test]
fn malformed_fields_and_columns() {
    for (offset, value) in [
        (0, 0),
        (4, 1),
        (8, 0),
        (16, 2),
        (24, 1),
        (33, 0),
        (40, 1),
        (44, 0),
        (48, 0),
        (56, 0),
        (60, 1),
        (62, 101),
        (63, 1),
        (73, 0),
        (70, 1),
        (73, 0x81),
    ] {
        let mut b = bytes();
        b[offset] = value;
        assert!(parse(&b).is_err(), "offset {offset}");
    }
    let mut b = bytes();
    b[64..66].copy_from_slice(&300i16.to_le_bytes());
    assert!(parse(&b).is_err());
    for off in [8, 48] {
        let mut b = bytes();
        b[off..off + 8].copy_from_slice(&u64::MAX.to_le_bytes());
        assert!(parse(&b).is_err());
    }
}
#[test]
fn resource_limits_and_relaxed_reserved() {
    let b = bytes();
    for o in [
        DetailOptions {
            maximum_tiles: 0,
            ..DetailOptions::default()
        },
        DetailOptions {
            maximum_columns: 1,
            ..DetailOptions::default()
        },
    ] {
        assert_eq!(
            DetailPayload::parse(&b, Some(512), false, o).unwrap_err(),
            Error::LimitExceeded
        );
    }
    let mut b = bytes();
    for off in [4, 60, 61, 63] {
        b[off] = 1;
    }
    b[70] = 20;
    b[73] |= 0x80;
    assert!(parse(&b).is_err());
    assert!(DetailPayload::parse(
        &b,
        Some(512),
        false,
        DetailOptions {
            strict: false,
            ..DetailOptions::default()
        }
    )
    .is_ok());
}
#[test]
fn state_duration_and_clipped_tail() {
    let c = columns();
    let mut t = tile(&c);
    t.state = FeatureState::Stable;
    let mut b = bytes();
    write_payload(&[t], None, &mut b).unwrap();
    assert!(parse(&b).is_err());
    assert!(DetailPayload::parse(&b, None, true, DetailOptions::default()).is_ok());
    t.state = FeatureState::Final;
    assert_eq!(payload_size(&[t], None), Err(Error::InvalidArgument));
    t.end_frame = 400;
    write_payload(&[t], Some(400), &mut b).unwrap();
    assert!(DetailPayload::parse(&b, Some(400), false, DetailOptions::default()).is_ok());
    assert!(DetailPayload::parse(&b, None, false, DetailOptions::default()).is_err());
    // Preserve C reader/writer asymmetry: aligned final tiles with unknown total parse.
    assert!(DetailPayload::parse(&bytes(), None, true, DetailOptions::default()).is_ok());
}
#[test]
fn sparse_tiles_unsorted_reader_sorted_writer_duplicates_and_aliases() {
    let c = columns();
    let a = tile(&c);
    let mut z = a;
    z.tile_index = 2;
    z.first_column_index = 130;
    z.first_frame = 130 * 256;
    z.end_frame = 132 * 256;
    let total = Some(z.end_frame);
    let mut b = vec![0; payload_size(&[a, z], total).unwrap()];
    write_payload(&[a, z], total, &mut b).unwrap();
    assert!(DetailPayload::parse(&b, total, false, DetailOptions::default()).is_ok());
    assert_eq!(payload_size(&[z, a], total), Err(Error::InvalidArgument));
    let first = b[16..64].to_vec();
    let second = b[64..112].to_vec();
    b[16..64].copy_from_slice(&second);
    b[64..112].copy_from_slice(&first);
    assert_eq!(
        DetailPayload::parse(&b, total, false, DetailOptions::default())
            .unwrap()
            .tile(0)
            .unwrap()
            .tile_index,
        2
    );
    b[64..112].copy_from_slice(&second);
    assert!(DetailPayload::parse(&b, total, false, DetailOptions::default()).is_err());
    write_payload(&[a, z], total, &mut b).unwrap();
    let offset = b[48..56].to_vec();
    b[96..104].copy_from_slice(&offset);
    assert!(DetailPayload::parse(&b, total, false, DetailOptions::default()).is_err());
}
#[test]
fn reference_fixture_exact_payload() {
    let hex = include_str!("../../conformance/fixtures/container-v1-suite/v1-wovr-wdtl.apta.hex");
    let h: String = hex.chars().filter(|c| !c.is_whitespace()).collect();
    let bytes: Vec<u8> = (0..h.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
        .collect();
    let envelope = Container::parse(&bytes, ParseOptions::default()).unwrap();
    let section = (0..envelope.section_count())
        .filter_map(|i| envelope.section(i))
        .find(|s| s.fourcc == *b"WDTL")
        .unwrap();
    let p = DetailPayload::parse(
        section.payload,
        envelope.source.total_frames,
        envelope.flags & 1 != 0,
        DetailOptions::default(),
    )
    .unwrap();
    let packed = p.tile(0).unwrap();
    let c: Vec<_> = (0..packed.column_count())
        .map(|i| packed.column(i).unwrap())
        .collect();
    let t = WaveformTile {
        level_id: packed.level_id,
        tile_index: packed.tile_index,
        first_frame: packed.first_frame,
        end_frame: packed.end_frame,
        first_column_index: packed.first_column_index,
        state: packed.state,
        confidence: packed.confidence,
        columns: &c,
    };
    let mut out = vec![0; payload_size(&[t], envelope.source.total_frames).unwrap()];
    write_payload(&[t], envelope.source.total_frames, &mut out).unwrap();
    assert_eq!(out, section.payload);
}

#[test]
fn geometry_boundaries_and_invalid_writer_are_atomic() {
    let c = columns();
    let base = tile(&c);
    for mutate in 0..6 {
        let mut t = base;
        match mutate {
            0 => t.level_id = 2,
            1 => t.tile_index = u32::MAX,
            2 => t.first_column_index = 63,
            3 => t.first_frame = 1,
            4 => t.end_frame = 513,
            _ => t.confidence = 101,
        }
        let mut out = [0x5a; 84];
        assert!(write_payload(&[t], Some(512), &mut out).is_err());
        assert_eq!(out, [0x5a; 84]);
    }
    let mut b = bytes();
    b[20..24].copy_from_slice(&u32::MAX.to_le_bytes());
    assert_eq!(parse(&b).unwrap_err(), Error::LimitExceeded);
    let mut last = base;
    last.tile_index = u32::MAX / 64;
    last.first_column_index = u32::MAX - 1;
    last.first_frame = u64::from(last.first_column_index) * 256;
    last.end_frame = (u64::from(u32::MAX) + 1) * 256;
    let mut out = bytes();
    write_payload(&[last], Some(last.end_frame), &mut out).unwrap();
    assert!(
        DetailPayload::parse(&out, Some(last.end_frame), false, DetailOptions::default()).is_ok()
    );
}

#[test]
fn eof_may_truncate_only_the_last_represented_column() {
    let mut b = bytes();
    b[32..40].copy_from_slice(&1u64.to_le_bytes());
    assert_eq!(
        DetailPayload::parse(&b, Some(1), false, DetailOptions::default()).unwrap_err(),
        Error::Corrupt
    );
    let c = columns();
    let mut t = tile(&c);
    t.end_frame = 1;
    assert_eq!(payload_size(&[t], Some(1)), Err(Error::InvalidArgument));
    t.end_frame = 256;
    assert_eq!(payload_size(&[t], Some(256)), Err(Error::InvalidArgument));
    t.end_frame = 257;
    assert!(payload_size(&[t], Some(257)).is_ok());
}

#[test]
fn permissive_view_normalizes_for_canonical_copy() {
    let mut b = bytes();
    b[70..74].copy_from_slice(&[1, 2, 3, 0xe1]);
    let p = DetailPayload::parse(
        &b,
        Some(512),
        false,
        DetailOptions {
            strict: false,
            ..DetailOptions::default()
        },
    )
    .unwrap();
    let packed = p.tile(0).unwrap();
    assert_eq!(packed.column(0), Some(columns()[0]));
    let mut c = [WaveformColumn::default(); 2];
    packed.copy_columns(&mut c).unwrap();
    assert_eq!(c, columns());
    let mut out = vec![0; 84];
    write_payload(&[tile(&c)], Some(512), &mut out).unwrap();
    assert_eq!(out, bytes());
    b[73] = 0xe9;
    let p = DetailPayload::parse(
        &b,
        Some(512),
        false,
        DetailOptions {
            strict: false,
            ..DetailOptions::default()
        },
    )
    .unwrap();
    let c = p.tile(0).unwrap().column(0).unwrap();
    assert_eq!((c.low, c.mid, c.high, c.flags), (1, 2, 3, 9));
}
