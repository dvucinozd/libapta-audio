// SPDX-License-Identifier: Apache-2.0
use libapta::wav::{Encoding, Wav};
use libapta::Error;

fn riff(tag: u16, bits: u16, channels: u16, samples: &[u8]) -> Vec<u8> {
    let mut b = b"RIFF\0\0\0\0WAVEfmt \x10\0\0\0".to_vec();
    b.extend(tag.to_le_bytes());
    b.extend(channels.to_le_bytes());
    b.extend(48000u32.to_le_bytes());
    let align = channels * (bits / 8);
    b.extend((48000u32 * u32::from(align)).to_le_bytes());
    b.extend(align.to_le_bytes());
    b.extend(bits.to_le_bytes());
    b.extend(b"data");
    b.extend((samples.len() as u32).to_le_bytes());
    b.extend(samples);
    if samples.len() % 2 != 0 {
        b.push(0);
    }
    let n = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&n.to_le_bytes());
    b
}

#[test]
fn formats_and_random_access() {
    for (tag, bits, raw, encoding) in [
        (1, 16, vec![0, 128, 255, 127], Encoding::S16),
        (1, 24, vec![0, 0, 128, 255, 255, 127], Encoding::S24),
        (1, 32, vec![0, 0, 0, 128, 255, 255, 255, 127], Encoding::S32),
        (
            3,
            32,
            [-1.0f32, 1.0]
                .iter()
                .flat_map(|n| n.to_le_bytes())
                .collect(),
            Encoding::F32,
        ),
    ] {
        let b = riff(tag, bits, 1, &raw);
        let wav = Wav::parse(&b).unwrap();
        assert_eq!(wav.encoding(), encoding);
        let mut output = [9.; 3];
        assert_eq!(wav.read_frames(0, &mut output), Ok(2));
        assert_eq!(output, [-1., 1., 9.]);
        assert_eq!(wav.read_frames(1, &mut output[..1]), Ok(1));
        assert_eq!(output[0], 1.);
        assert_eq!(wav.read_frames(2, &mut output), Ok(0));
        assert_eq!(
            wav.read_frames(u64::MAX, &mut output),
            Err(Error::InvalidArgument)
        );
    }
    let b = riff(1, 16, 2, &[0, 128, 255, 127]);
    let wav = Wav::parse(&b).unwrap();
    assert_eq!(wav.frame_count(), 1);
    assert_eq!(wav.source().channel_layout, 2);
    assert_eq!(
        wav.read_frames(0, &mut [0.; 1]),
        Err(Error::InvalidArgument)
    );
}

#[test]
fn truncations_sizes_duplicates_and_invalid_formats() {
    let valid = riff(1, 16, 1, &[0; 4]);
    for n in 0..valid.len() {
        assert!(Wav::parse(&valid[..n]).is_err(), "prefix {n}");
    }
    for (offset, value) in [(4, u32::MAX), (16, u32::MAX), (40, u32::MAX), (28, 1)] {
        let mut b = valid.clone();
        b[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        assert!(Wav::parse(&b).is_err());
    }
    let mut b = valid.clone();
    b.extend_from_slice(&valid[12..36]);
    let n = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&n.to_le_bytes());
    assert!(Wav::parse(&b).is_err());
    let mut b = valid.clone();
    b.extend_from_slice(b"data\0\0\0\0");
    let n = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&n.to_le_bytes());
    assert!(Wav::parse(&b).is_err());
    assert!(matches!(
        Wav::parse(&riff(7, 16, 1, &[0; 4])),
        Err(Error::Unsupported)
    ));
    assert!(Wav::parse(&riff(1, 16, 3, &[0; 6])).is_err());
}

#[test]
fn nonfinite_read_is_transactional_and_odd_chunks_are_skipped() {
    let raw = [0.5f32, f32::NAN]
        .iter()
        .flat_map(|n| n.to_le_bytes())
        .collect::<Vec<_>>();
    let b = riff(3, 32, 1, &raw);
    let wav = Wav::parse(&b).unwrap();
    let mut out = [42.; 2];
    assert_eq!(wav.read_frames(0, &mut out), Err(Error::InvalidArgument));
    assert_eq!(out, [42.; 2]);
    let mut b = riff(1, 24, 1, &[0, 0, 128]);
    b.extend(b"JUNK\x01\0\0\0x\0");
    let n = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&n.to_le_bytes());
    assert!(Wav::parse(&b).is_ok());
    b.pop();
    let n = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&n.to_le_bytes());
    assert!(Wav::parse(&b).is_err());
}

#[test]
fn extensible_guid_and_channel_mask() {
    let mut b = riff(1, 16, 2, &[0; 4]);
    let extra = [
        22, 0, 16, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113,
    ];
    b.splice(36..36, extra);
    b[16..20].copy_from_slice(&40u32.to_le_bytes());
    b[20..22].copy_from_slice(&0xfffeu16.to_le_bytes());
    let n = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&n.to_le_bytes());
    let wav = Wav::parse(&b).unwrap();
    assert_eq!(wav.source().channel_layout, 2);
    b[40] = 0;
    assert_eq!(Wav::parse(&b).unwrap().source().channel_layout, 0);
    b[59] = 0;
    assert!(matches!(Wav::parse(&b), Err(Error::Unsupported)));
}

fn scan(bytes: &[u8], chunk: usize) -> Result<libapta::wav::WavLayout, Error> {
    let mut scanner = libapta::wav::WavScanner::new(bytes.len() as u64);
    for b in bytes.chunks(chunk) {
        scanner.push(b)?;
    }
    scanner.finish()
}
fn compare_scan(bytes: &[u8]) {
    let borrowed = Wav::parse(bytes);
    for chunk in [1, 2, 7, 8, 11, 17, 40, 137, 4096] {
        let streamed = scan(bytes, chunk);
        assert_eq!(
            borrowed.is_ok(),
            streamed.is_ok(),
            "chunk {chunk}, bytes {bytes:?}"
        );
        if let (Ok(wav), Ok(layout)) = (&borrowed, streamed) {
            assert_eq!(wav.source(), layout.source());
            assert_eq!(wav.encoding(), layout.encoding());
            let raw = &bytes[layout.data_offset() as usize
                ..(layout.data_offset() + layout.data_bytes()) as usize];
            let mut a = [42.; 32];
            let mut b = a;
            assert_eq!(wav.read_frames(0, &mut a), layout.decode(raw, &mut b));
            assert_eq!(a, b);
        }
    }
}
#[test]
fn streaming_framing_matches_borrowed_for_formats_boundaries_and_mutations() {
    for (tag, bits) in [(1, 16), (1, 24), (1, 32), (3, 32)] {
        for channels in [1, 2] {
            let bytes = riff(tag, bits, channels, &[0; 24]);
            compare_scan(&bytes);
            for end in 0..bytes.len() {
                compare_scan(&bytes[..end]);
            }
            for i in 0..bytes.len() {
                for value in [0, 1, 127, 255] {
                    let mut b = bytes.clone();
                    b[i] = value;
                    compare_scan(&b);
                }
            }
        }
    }
}
#[test]
fn streaming_data_before_fmt_ancillary_trailers_extensible_and_duplicates() {
    let base = riff(1, 24, 1, &[0, 0, 128]);
    let mut b = base[..12].to_vec();
    b.extend(&base[36..]);
    b.extend(b"JUNK\x01\0\0\0x\0");
    b.extend(&base[12..36]);
    let end = b.len() as u32 - 8;
    b[4..8].copy_from_slice(&end.to_le_bytes());
    b.extend(b"unparsed object trailer");
    compare_scan(&b);
    for id in [b"data", b"fmt "] {
        let mut b = base.clone();
        b.extend(id);
        b.extend(0u32.to_le_bytes());
        let end = b.len() as u32 - 8;
        b[4..8].copy_from_slice(&end.to_le_bytes());
        compare_scan(&b);
    }
    for size in 16..=43 {
        let mut b = riff(1, 16, 2, &[0; 4]);
        let extra = [
            22, 0, 16, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113, 0,
            0, 0,
        ];
        b.splice(36..36, extra[..size - 16].iter().copied());
        if size % 2 != 0 {
            b.insert(20 + size, 0);
        }
        b[16..20].copy_from_slice(&(size as u32).to_le_bytes());
        b[20..22].copy_from_slice(&0xfffeu16.to_le_bytes());
        let end = b.len() as u32 - 8;
        b[4..8].copy_from_slice(&end.to_le_bytes());
        compare_scan(&b);
    }
}
#[test]
fn streaming_terminal_errors_capacity_and_nonfinite_atomicity() {
    let b = riff(3, 32, 1, &[0, 0, 0, 63, 0, 0, 128, 127]);
    let l = scan(&b, 1).unwrap();
    let mut out = [99.; 2];
    assert_eq!(l.decode(&b[44..], &mut out), Err(Error::InvalidArgument));
    assert_eq!(out, [99.; 2]);
    assert_eq!(l.decode(&b[44..], &mut out[..1]), Ok(1));
    assert_eq!(out, [0.5, 99.]);
    assert_eq!(l.decode(&b[44..47], &mut out), Err(Error::InvalidArgument));
    let mut s = libapta::wav::WavScanner::new(b.len() as u64);
    assert!(s.push(&[0; 12]).is_err());
    assert_eq!(s.push(&b), Err(Error::InvalidState));
    assert!(s.finish().is_err());
    let mut s = libapta::wav::WavScanner::new(b.len() as u64);
    s.push(&b).unwrap();
    assert!(s.push(&[0]).is_err());
    assert!(s.finish().is_err());
}
