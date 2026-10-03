// SPDX-License-Identifier: Apache-2.0
use libapta::{
    band::{BandFilter, BandSums},
    waveform::WaveformAccumulator,
    Error, WaveformColumn,
};

fn column() -> WaveformColumn {
    WaveformColumn {
        minimum: -12000,
        maximum: 19000,
        rms: 32000,
        flags: 5,
        ..Default::default()
    }
}

#[test]
fn rejects_invalid_values_without_changing_filter_or_sums() {
    for rate in [0, 768001, u32::MAX] {
        assert!(matches!(BandFilter::new(rate), Err(Error::InvalidArgument)));
    }
    let mut filter = BandFilter::new(48000).unwrap();
    filter.split(0.5).unwrap();
    let mut expected = filter;
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1.01, -1.01] {
        assert_eq!(filter.split(bad), Err(Error::InvalidArgument));
    }
    assert_eq!(
        filter.split(-0.25).unwrap().map(f32::to_bits),
        expected.split(-0.25).unwrap().map(f32::to_bits)
    );
    let mut sums = BandSums::default();
    sums.add([0.5, -0.5, 0.25]).unwrap();
    let expected = sums;
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
        assert_eq!(sums.add([0.5, bad, 0.5]), Err(Error::InvalidArgument));
        assert_eq!(sums, expected);
    }
    for count in [0, 65537, u32::MAX] {
        assert_eq!(sums.apply(column(), count), Err(Error::InvalidArgument));
    }
    assert_eq!(
        sums.apply(WaveformColumn::default(), 1),
        Err(Error::InvalidArgument)
    );
}

#[test]
fn sums_clamp_truncate_and_cover_maximum_column_size() {
    let mut sums = BandSums::default();
    for _ in 0..65536 {
        sums.add([2.0, -2.0, 0.5]).unwrap();
    }
    let output = sums.apply(column(), 65536).unwrap();
    assert_eq!(
        (output.low, output.mid, output.high, output.flags),
        (255, 255, 127, 13)
    );
    assert_eq!(
        (output.minimum, output.maximum, output.rms),
        (-12000, 19000, 32000)
    );
    let mut sums = BandSums::default();
    sums.add([f32::from_bits(1), 1.0 / 32768.0, 1.0]).unwrap();
    let output = sums.apply(column(), 1).unwrap();
    assert_eq!((output.low, output.mid, output.high), (0, 0, 255));
}

fn signals() -> Vec<Vec<f32>> {
    let mut signals = vec![
        vec![0.0; 257],
        vec![1.0; 257],
        vec![-1.0; 257],
        (0..1025)
            .map(|i| if i % 2 == 0 { 1.0 } else { -1.0 })
            .collect(),
        (0..1025)
            .map(|i| if i == 0 || i == 512 { 1.0 } else { 0.0 })
            .collect(),
        (0..257)
            .map(|i| f32::from_bits(if i % 2 == 0 { 1 } else { 0x8000_0001 }))
            .collect(),
    ];
    let mut seed = 0x1234_5678u32;
    signals.push(
        (0..4097)
            .map(|_| {
                seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                ((seed >> 8) as f32 / 8388608.0) - 1.0
            })
            .collect(),
    );
    signals
}

#[test]
#[ignore = "requires APTA_C_BAND_ORACLE"]
fn filter_bits_and_quantized_columns_match_c() {
    use std::io::Write;
    let oracle = std::env::var_os("APTA_C_BAND_ORACLE").expect("set APTA_C_BAND_ORACLE");
    // Float tolerance is zero: compare all three f32 output bit patterns.
    // Published integer fields likewise require exact equality in both C paths.
    for rate in [1, 8000, 11025, 22050, 44100, 48000, 96000, 192000, 768000] {
        for (signal_index, samples) in signals().iter().enumerate() {
            let fpc = if signal_index % 2 == 0 { 64 } else { 1024 };
            let mut child = std::process::Command::new(&oracle)
                .args([rate.to_string(), fpc.to_string()])
                .stdin(std::process::Stdio::piped())
                .stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::piped())
                .spawn()
                .unwrap();
            let input: Vec<u8> = samples
                .iter()
                .flat_map(|sample| sample.to_ne_bytes())
                .collect();
            child.stdin.take().unwrap().write_all(&input).unwrap();
            let output = child.wait_with_output().unwrap();
            assert!(
                output.status.success(),
                "rate {rate} signal {signal_index}: {}",
                String::from_utf8_lossy(&output.stderr)
            );
            let output = String::from_utf8(output.stdout).unwrap();
            let mut lines = output.lines();
            let coefficients = lines.next().unwrap();
            assert!(coefficients.starts_with("K "));
            let mut filter = BandFilter::new(rate).unwrap();
            let mut sums = BandSums::default();
            let mut accumulator = WaveformAccumulator::default();
            let mut columns = vec![];
            for (index, sample) in samples.iter().copied().enumerate() {
                let split = filter.split(sample).unwrap();
                let line = lines.next().unwrap();
                let expected: Vec<u32> = line
                    .strip_prefix("F ")
                    .unwrap()
                    .split_whitespace()
                    .map(|v| u32::from_str_radix(v, 16).unwrap())
                    .collect();
                assert_eq!(
                    split.map(f32::to_bits).as_slice(),
                    expected,
                    "rate {rate} signal {signal_index} frame {index}, C {coefficients}"
                );
                sums.add(split).unwrap();
                accumulator
                    .push_normalized(sample, sample.abs() >= 1.0)
                    .unwrap();
                if accumulator.sample_count() == fpc || index + 1 == samples.len() {
                    columns.push(
                        sums.apply(accumulator.column(), accumulator.sample_count())
                            .unwrap(),
                    );
                    sums = BandSums::default();
                    accumulator.clear();
                }
            }
            for bounded in [false, true] {
                let header: Vec<u64> = lines
                    .next()
                    .unwrap()
                    .strip_prefix("P ")
                    .unwrap()
                    .split_whitespace()
                    .map(|v| v.parse().unwrap())
                    .collect();
                assert_eq!(header[0], u64::from(bounded));
                // Baseline pooled publication omits the feature bit while its
                // columns do contain HAS_3BAND. Keep this discrepancy explicit.
                let features = libapta::result::WAVEFORM_OVERVIEW
                    | if bounded {
                        0
                    } else {
                        libapta::result::WAVEFORM_3BAND
                    };
                assert_eq!(header[1], features);
                for column in &columns {
                    let line = lines.next().unwrap();
                    let fields: Vec<i32> = line
                        .strip_prefix("C ")
                        .unwrap()
                        .split_whitespace()
                        .map(|v| v.parse().unwrap())
                        .collect();
                    assert_eq!(
                        fields,
                        [
                            i32::from(column.minimum),
                            i32::from(column.maximum),
                            i32::from(column.rms),
                            i32::from(column.low),
                            i32::from(column.mid),
                            i32::from(column.high),
                            i32::from(column.flags)
                        ],
                        "rate {rate} signal {signal_index} bounded {bounded}"
                    );
                }
            }
            assert!(lines.next().is_none());
        }
    }
}
