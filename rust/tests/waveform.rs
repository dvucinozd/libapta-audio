// SPDX-License-Identifier: Apache-2.0
use libapta::{waveform::*, Error, WaveformColumn};

#[test]
fn normalization_extrema_and_stereo() {
    assert_eq!(
        normalize_s16(i16::MIN),
        NormalizedSample {
            value: -1.0,
            clipped: true
        }
    );
    assert_eq!(
        normalize_s16(i16::MAX),
        NormalizedSample {
            value: 1.0,
            clipped: true
        }
    );
    assert_eq!(
        normalize_s24([0, 0, 128]),
        NormalizedSample {
            value: -1.0,
            clipped: true
        }
    );
    assert_eq!(
        normalize_s24([255, 255, 127]),
        NormalizedSample {
            value: 1.0,
            clipped: true
        }
    );
    assert_eq!(
        normalize_s24([255, 255, 255]),
        NormalizedSample {
            value: -1.0 / 8388608.0,
            clipped: false
        }
    );
    assert_eq!(
        normalize_s32(i32::MIN),
        NormalizedSample {
            value: -1.0,
            clipped: true
        }
    );
    assert_eq!(
        normalize_s32(i32::MAX),
        NormalizedSample {
            value: 1.0,
            clipped: true
        }
    );
    assert_eq!(
        normalize_s32(2147483583).value,
        (2147483583f64 / 2147483647.0) as f32
    );
    assert_eq!(
        mix_frame(&[1.0, -1.0]),
        Ok(NormalizedSample {
            value: 0.0,
            clipped: false
        })
    );
    assert_eq!(
        mix_frame(&[2.0, 0.0]),
        Ok(NormalizedSample {
            value: 0.5,
            clipped: false
        })
    );
    assert_eq!(
        mix_frame(&[2.0]),
        Ok(NormalizedSample {
            value: 1.0,
            clipped: true
        })
    );
    assert_eq!(mix_frame(&[]), Err(Error::InvalidArgument));
    assert_eq!(mix_frame(&[0.0; 3]), Err(Error::InvalidArgument));
}

#[test]
fn empty_silence_full_scale_and_invalid_transaction() {
    let mut a = WaveformAccumulator::default();
    assert_eq!(a.column(), WaveformColumn::default());
    a.push_normalized(0.0, false).unwrap();
    assert_eq!(a.column().flags, 1);
    a.clear();
    a.push_normalized(-1.0, true).unwrap();
    a.push_normalized(1.0, true).unwrap();
    assert_eq!(
        a.column(),
        WaveformColumn {
            minimum: i16::MIN,
            maximum: i16::MAX,
            rms: u16::MAX,
            flags: 5,
            ..Default::default()
        }
    );
    let before = a.column();
    for invalid in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1.001] {
        assert_eq!(
            a.push_normalized(invalid, false),
            Err(Error::InvalidArgument)
        );
        assert_eq!(a.column(), before);
        assert_eq!(a.sample_count(), 2);
    }
    for invalid in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
        assert_eq!(normalize_f32(invalid), Err(Error::InvalidArgument));
    }
}

#[test]
fn order_block_determinism_and_overflow() {
    let samples: Vec<f32> = (0..1024)
        .map(|i| ((i * 7919 % 65535) as f32 - 32767.0) / 32768.0)
        .collect();
    let mut a = WaveformAccumulator::default();
    let mut b = a;
    for s in &samples {
        a.push_normalized(*s, false).unwrap();
    }
    for chunk in samples.chunks(17).rev() {
        for s in chunk {
            b.push_normalized(*s, false).unwrap();
        }
    }
    assert_eq!(a.column(), b.column());
    a.clear();
    for _ in 0..262143 {
        a.push_normalized(1.0, true).unwrap();
    }
    let before = a.column();
    assert_eq!(a.push_normalized(1.0, true), Err(Error::LimitExceeded));
    assert_eq!(a.sample_count(), 262143);
    assert_eq!(a.column(), before);
}

/// Requires the public C oracle built against the unmodified baseline.
#[test]
#[ignore = "set APTA_C_WAVEFORM_ORACLE to compiled fixtures/waveform_oracle.c"]
fn c_reference_exact_quantized_columns() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    let oracle = std::env::var("APTA_C_WAVEFORM_ORACLE").expect("C oracle path");
    for channels in [1, 2] {
        for pattern in 0..6 {
            let frames = 3079;
            let samples: Vec<f32> = (0..frames * channels)
                .map(|i| match pattern {
                    0 => 0.0,
                    1 => {
                        if i % 2 == 0 {
                            1.0
                        } else {
                            -1.0
                        }
                    }
                    2 => {
                        if i % 1024 == 0 {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    3 => ((i * 7919 % 65535) as f32 - 32767.0) / 32768.0,
                    4 => {
                        if i % 2 == 0 {
                            2.0
                        } else {
                            -0.5
                        }
                    }
                    _ => {
                        if i % 2 == 0 {
                            0.5
                        } else {
                            -0.5
                        }
                    }
                })
                .collect();
            let mut child = Command::new(&oracle)
                .arg(channels.to_string())
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .spawn()
                .unwrap();
            {
                let mut stdin = child.stdin.take().unwrap();
                for sample in &samples {
                    stdin.write_all(&sample.to_ne_bytes()).unwrap();
                }
            }
            let output = child.wait_with_output().unwrap();
            assert!(output.status.success());
            let text = String::from_utf8(output.stdout).unwrap();
            let mut lines = text.lines();
            let column_frames: usize = lines.next().unwrap().parse().unwrap();
            let expected: Vec<Vec<i32>> = lines
                .map(|line| {
                    line.split_whitespace()
                        .map(|v| v.parse().unwrap())
                        .collect()
                })
                .collect();
            let mut actual = Vec::new();
            for chunk in samples.chunks(column_frames * channels) {
                let mut a = WaveformAccumulator::default();
                for frame in chunk.chunks(channels) {
                    let sample = mix_frame(frame).unwrap();
                    a.push_normalized(sample.value, sample.clipped).unwrap();
                }
                let c = a.column();
                actual.push(vec![
                    c.minimum as i32,
                    c.maximum as i32,
                    c.rms as i32,
                    c.low as i32,
                    c.mid as i32,
                    c.high as i32,
                    c.flags as i32,
                ]);
            }
            assert_eq!(
                actual, expected,
                "channels={channels}, pattern={pattern}; quantized tolerance is exactly zero"
            );
        }
    }
}

#[test]
fn typed_pcm_formats_geometry_and_bounds() {
    let left = [0.5, -1.0];
    let right = [-0.5, 1.0];
    let planes = [&left[..], &right[..]];
    let f32_pcm = [0.5, -0.5, -1.0, 1.0];
    let s16_pcm = [0, 0, i16::MIN, i16::MAX];
    let s24_pcm = [0, 0, 0, 0, 0, 0, 0, 0, 128, 255, 255, 127];
    let s32_pcm = [0, 0, i32::MIN, i32::MAX];
    for view in [
        PcmView::F32Planar(&planes),
        PcmView::F32Interleaved(&f32_pcm),
        PcmView::S16Interleaved(&s16_pcm),
        PcmView::S24Interleaved(&s24_pcm),
        PcmView::S32Interleaved(&s32_pcm),
    ] {
        assert_eq!(view.frame_count(2), Ok(2));
        assert_eq!(view.sample_frame(0, 2), Ok(NormalizedSample::default()));
        assert_eq!(view.sample_frame(1, 2), Ok(NormalizedSample::default()));
        assert_eq!(view.sample_frame(2, 2), Err(Error::InvalidArgument));
        assert_eq!(
            view.sample_frame(usize::MAX, 2),
            Err(Error::InvalidArgument)
        );
        assert_eq!(view.frame_count(0), Err(Error::InvalidArgument));
        assert_eq!(view.frame_count(3), Err(Error::InvalidArgument));
    }
    assert_eq!(
        PcmView::S24Interleaved(&[0; 2]).frame_count(1),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        PcmView::S16Interleaved(&[0; 3]).frame_count(2),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        PcmView::F32Planar(&[&[0.0], &[]]).frame_count(2),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        PcmView::F32Planar(&[]).frame_count(1),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        PcmView::F32Interleaved(&[f32::NAN]).sample_frame(0, 1),
        Err(Error::InvalidArgument)
    );
    assert_eq!(PcmView::S16Interleaved(&[]).frame_count(1), Ok(0));
}

#[test]
#[ignore = "set APTA_C_WAVEFORM_ORACLE to compiled fixtures/waveform_oracle.c"]
fn c_reference_all_pcm_formats() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    let oracle = std::env::var("APTA_C_WAVEFORM_ORACLE").expect("C oracle path");
    for channels in [1u16, 2] {
        let count = 2049 * usize::from(channels);
        let signed: Vec<i32> = (0..count)
            .map(|i| match i % 101 {
                0 => i32::MIN,
                1 => i32::MAX,
                _ => (i as u32).wrapping_mul(123456791) as i32,
            })
            .collect();
        let s16: Vec<i16> = signed.iter().map(|v| (v >> 16) as i16).collect();
        let s24: Vec<u8> = signed
            .iter()
            .flat_map(|v| (v >> 8).to_le_bytes()[..3].to_vec())
            .collect();
        let f32: Vec<f32> = signed.iter().map(|v| *v as f32 / 2147483648.0).collect();
        let planes_storage: Vec<Vec<f32>> = (0..usize::from(channels))
            .map(|c| {
                f32.iter()
                    .skip(c)
                    .step_by(usize::from(channels))
                    .copied()
                    .collect()
            })
            .collect();
        let planes: Vec<&[f32]> = planes_storage.iter().map(Vec::as_slice).collect();
        let cases = [
            (
                1,
                PcmView::S16Interleaved(&s16),
                s16.iter().flat_map(|v| v.to_ne_bytes()).collect::<Vec<_>>(),
            ),
            (2, PcmView::S24Interleaved(&s24), s24.clone()),
            (
                3,
                PcmView::S32Interleaved(&signed),
                signed.iter().flat_map(|v| v.to_ne_bytes()).collect(),
            ),
            (
                4,
                PcmView::F32Interleaved(&f32),
                f32.iter().flat_map(|v| v.to_ne_bytes()).collect(),
            ),
            (
                5,
                PcmView::F32Planar(&planes),
                planes_storage
                    .iter()
                    .flatten()
                    .flat_map(|v| v.to_ne_bytes())
                    .collect(),
            ),
        ];
        for (format, view, bytes) in cases {
            let mut child = Command::new(&oracle)
                .args([channels.to_string(), format.to_string()])
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .spawn()
                .unwrap();
            child.stdin.take().unwrap().write_all(&bytes).unwrap();
            let output = child.wait_with_output().unwrap();
            assert!(output.status.success());
            let text = String::from_utf8(output.stdout).unwrap();
            let mut lines = text.lines();
            let column_frames: usize = lines.next().unwrap().parse().unwrap();
            let expected: Vec<Vec<i32>> = lines
                .map(|line| {
                    line.split_whitespace()
                        .map(|v| v.parse().unwrap())
                        .collect()
                })
                .collect();
            let mut actual = Vec::new();
            for first in (0..2049).step_by(column_frames) {
                let mut a = WaveformAccumulator::default();
                for i in first..(first + column_frames).min(2049) {
                    let sample = view.sample_frame(i, channels).unwrap();
                    a.push_normalized(sample.value, sample.clipped).unwrap();
                }
                let c = a.column();
                actual.push(vec![
                    c.minimum as i32,
                    c.maximum as i32,
                    c.rms as i32,
                    c.low as i32,
                    c.mid as i32,
                    c.high as i32,
                    c.flags as i32,
                ]);
            }
            assert_eq!(
                actual, expected,
                "format={format} channels={channels}; exact quantized columns"
            );
        }
    }
}

#[test]
fn quantization_half_ties_and_neighboring_floats() {
    // 0.5 * 32767 == 16383.5 and 0.5 * 65535 == 32767.5:
    // both must round toward the even integer, including the negative peak.
    for (sample, peak, rms) in [
        (0.5, 16384, 32768),
        (-0.5, -16384, 32768),
        (f32::from_bits(0.5f32.to_bits() - 1), 16383, 32767),
        (f32::from_bits(0.5f32.to_bits() + 1), 16384, 32768),
        (f32::from_bits((-0.5f32).to_bits() - 1), -16383, 32767),
        (f32::from_bits((-0.5f32).to_bits() + 1), -16384, 32768),
    ] {
        let mut accumulator = WaveformAccumulator::default();
        accumulator.push_normalized(sample, false).unwrap();
        let column = accumulator.column();
        assert_eq!(column.minimum, peak, "sample={sample:?}");
        assert_eq!(column.maximum, peak, "sample={sample:?}");
        assert_eq!(column.rms, rms, "sample={sample:?}");
    }
}

#[test]
fn s32_clipping_follows_normalized_full_scale() {
    for offset in [0, 1, 2, 31, 32, 63, 64, 65, 127, 128, 255] {
        for input in [i32::MAX - offset, i32::MIN + offset] {
            let sample = normalize_s32(input);
            assert_eq!(sample.clipped, sample.value.abs() >= 1.0, "input={input}");
        }
    }
    assert!(normalize_s32(i32::MAX - 1).clipped);
    assert!(normalize_s32(i32::MIN + 1).clipped);
    assert!(!normalize_s32(i32::MAX - 128).clipped);
    assert!(!normalize_s32(i32::MIN + 128).clipped);
}

#[test]
#[ignore = "set APTA_C_WAVEFORM_ORACLE to compiled fixtures/waveform_oracle.c"]
fn c_reference_s32_near_endpoints() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    let oracle = std::env::var("APTA_C_WAVEFORM_ORACLE").expect("C oracle path");
    for offset in [0, 1, 2, 31, 32, 63, 64, 65, 127, 128, 255] {
        for input in [i32::MAX - offset, i32::MIN + offset] {
            let mut child = Command::new(&oracle)
                .args(["1", "3"])
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .spawn()
                .unwrap();
            child
                .stdin
                .take()
                .unwrap()
                .write_all(&input.to_ne_bytes())
                .unwrap();
            let output = child.wait_with_output().unwrap();
            assert!(output.status.success());
            let text = String::from_utf8(output.stdout).unwrap();
            let expected: Vec<i32> = text
                .lines()
                .nth(1)
                .unwrap()
                .split_whitespace()
                .map(|v| v.parse().unwrap())
                .collect();
            let mut a = WaveformAccumulator::default();
            let sample = normalize_s32(input);
            a.push_normalized(sample.value, sample.clipped).unwrap();
            let c = a.column();
            assert_eq!(
                vec![
                    c.minimum as i32,
                    c.maximum as i32,
                    c.rms as i32,
                    c.low as i32,
                    c.mid as i32,
                    c.high as i32,
                    c.flags as i32
                ],
                expected,
                "input={input}; exact quantized column"
            );
        }
    }
}
