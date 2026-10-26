// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-compact-frontend")]
use libapta_runtime::compact_frontend::{synthetic as fx, *};
use std::sync::atomic::{AtomicBool, Ordering};
fn raw(name: &str, suffix: &str) -> Vec<u8> {
    std::fs::read(format!(
        "{}/../../docs/rust/fixtures/product-host-frontend-hs04/assets/{name}-{suffix}",
        env!("CARGO_MANIFEST_DIR")
    ))
    .unwrap()
}
fn f32s(raw: &[u8]) -> Vec<f32> {
    raw.chunks_exact(4)
        .map(|b| f32::from_le_bytes(b.try_into().unwrap()))
        .collect()
}
fn f64s(raw: &[u8]) -> Vec<f64> {
    raw.chunks_exact(8)
        .map(|b| f64::from_le_bytes(b.try_into().unwrap()))
        .collect()
}
#[test]
fn all_primitive_bins_both_oracles_and_work_schedules() {
    for i in 0..fx::PRIMITIVE_COUNT {
        let (name, input) = fx::primitive(i);
        let refs = [
            f64s(&raw(name, "analytic.f64le")),
            f64s(&raw(name, "direct.f64le")),
        ];
        let mut baseline = None;
        for quota in [1, 7, 256, 4096, usize::MAX] {
            let mut fft = Transform::new();
            let flag = AtomicBool::new(false);
            while !fft.done() {
                let u = fft.step(quota, &input, &flag);
                assert!(u > 0 && u <= quota);
            }
            assert_eq!(fft.macs(), 42336);
            if let Some(base) = &baseline {
                assert_eq!(fft.values(), base);
            } else {
                baseline = Some(*fft.values());
            }
            for reference in &refs {
                for (k, z) in fft.values().iter().enumerate() {
                    let (re, im) = (reference[2 * k], reference[2 * k + 1]);
                    assert!(
                        (z.re - re).hypot(z.im - im) <= 1e-4 + 2e-5 * re.hypot(im),
                        "{name} bin{k}: {z:?} vs {re}/{im}"
                    );
                }
            }
            let before = fft.units();
            assert_eq!(fft.step(1, &input, &flag), 0);
            assert_eq!(fft.units(), before);
        }
    }
}
fn stream(
    c: fx::Case,
    input: &[f32],
    chunk: usize,
    capacity: usize,
    quota: usize,
) -> Vec<[f32; FEATURES]> {
    let mut s = Session::new(c.config).unwrap();
    let mut rows = Vec::new();
    let flag = AtomicBool::new(false);
    let mut at = 0;
    let mut end = chunk.min(input.len());
    let mut output = [[123.0; FEATURES]; 9];
    while s.status() == Status::Running {
        let before = s.progress();
        let r = s.step(
            quota,
            InputBlock {
                first_sample: at as u64,
                samples: &input[at..end],
            },
            &mut output[1..1 + capacity],
            &flag,
        );
        assert!(r.error.is_none(), "{} {:?}", c.name, r);
        assert!(r.units <= quota);
        assert_eq!(r.first_row, rows.len() as u64);
        assert!(output[0]
            .iter()
            .chain(output[1 + capacity].iter())
            .all(|&x| x == 123.0));
        assert_eq!(s.progress().units - before.units, r.units as u64);
        at += r.consumed_samples;
        for row in &output[1..1 + r.produced_rows] {
            rows.push(*row);
        }
        if at == end {
            end = (end + chunk).min(input.len());
        }
        if at == input.len() {
            s.finish_input();
        }
        if r.flow == Flow::NeedInput {
            let t = s.progress();
            let starvation = s.step(
                1,
                InputBlock {
                    first_sample: at as u64,
                    samples: &[],
                },
                &mut output[1..2],
                &flag,
            );
            assert_eq!(starvation.flow, Flow::NeedInput);
            assert_eq!(s.progress(), t);
        }
        assert!(r.units > 0 || r.flow == Flow::NeedInput);
    }
    assert_eq!(at, input.len());
    assert_eq!(rows.len() as u64, s.row_count());
    assert_eq!(s.progress().macs, 42336 * rows.len() as u64);
    assert_eq!(
        s.status(),
        if input.is_empty() {
            Status::NoEvidence
        } else {
            Status::Done
        }
    );
    let before = s.progress();
    s.cancel();
    s.finish_input();
    let r = s.step(
        1,
        InputBlock {
            first_sample: u64::MAX,
            samples: &[f32::NAN],
        },
        &mut output,
        &flag,
    );
    assert_eq!(r.units, 0);
    assert_eq!(r.produced_rows, 0);
    assert_eq!(s.progress(), before);
    rows
}
#[test]
fn all_twenty_streams_both_oracles_all_forty_schedules_bit_identical() {
    for i in 0..fx::FIXTURE_COUNT {
        let c = fx::fixture(i);
        let input = f32s(c.raw);
        let reference = f32s(&raw(c.name, "features.f32le"));
        let ideal = f64s(&raw(c.name, "ideal.f64le"));
        let mut baseline = None;
        for chunk in [1, 7, 256, 512] {
            for output in [1, 7] {
                for quota in [1, 7, 256, 4096, usize::MAX] {
                    let rows = stream(c, &input, chunk, output, quota);
                    let bits: Vec<_> = rows.iter().flatten().map(|x| x.to_bits()).collect();
                    if let Some(ref base) = baseline {
                        assert_eq!(&bits, base, "{} schedule {chunk}/{output}/{quota}", c.name);
                    } else {
                        baseline = Some(bits);
                    }
                    assert_eq!(rows.len() * FEATURES, reference.len());
                    for (j, &x) in rows.iter().flatten().enumerate() {
                        assert!(x.is_finite() && (0.0..4.0).contains(&x));
                        for y in [f64::from(reference[j]), ideal[j]] {
                            assert!(
                                (f64::from(x) - y).abs() <= 2e-5 + 2e-5 * y.abs(),
                                "{} scalar{j} {x} vs {y}",
                                c.name
                            );
                        }
                        if (BANDS..FEATURES).contains(&j) {
                            assert_eq!(x.to_bits(), 0);
                        }
                    }
                }
            }
        }
    }
}
#[test]
fn exact_geometry_eof_and_storage_limits() {
    assert!(std::mem::size_of::<Session>() <= 49152);
    for fs in [44100, 48000] {
        for n in [0, 1, 2, 32, 321, 879, 880, 881, 1919, 600 * u64::from(fs)] {
            let s = Session::new(Config {
                source_rate: fs,
                source_frames: n,
            })
            .unwrap();
            let k = (n * 22050).div_ceil(u64::from(fs));
            assert_eq!(s.input_count(), k);
            assert_eq!(s.row_count(), k.div_ceil(441));
            assert_eq!(s.row_time(s.row_count()), None);
            for row in [0, 1, s.row_count().saturating_sub(1)] {
                if let Some(t) = s.row_time(row) {
                    assert_eq!(t.source_center_q32, row * u64::from(fs) * (1 << 32) / 50);
                    assert_eq!(t.frame_first_sample, (441 * row) as i64 - 882);
                    assert_eq!(t.frame_end_sample, 441 * row + 882);
                    let q = k.min(441 * row + 882);
                    assert_eq!(t.required_input_samples, q);
                    let required = if fs == 44100 {
                        2 * (q - 1) + 33
                    } else {
                        (q - 1) * 320 / 147 + 33
                    };
                    assert_eq!(t.required_original_source_frames, n.min(required));
                }
            }
        }
        for n in [600 * u64::from(fs) + 1, u64::MAX] {
            assert!(matches!(
                Session::new(Config {
                    source_rate: fs,
                    source_frames: n
                }),
                Err(Error::UnsupportedProfile)
            ));
        }
    }
    assert!(matches!(
        Session::new(Config {
            source_rate: 22050,
            source_frames: 1
        }),
        Err(Error::UnsupportedProfile)
    ));
}
fn step_one(s: &mut Session, flag: &AtomicBool) -> StepReport {
    let at = s.progress().consumed_samples;
    s.step(
        1,
        InputBlock {
            first_sample: at,
            samples: if at < s.input_count() { &[1.0] } else { &[] },
        },
        &mut [[123.0; FEATURES]],
        flag,
    )
}
#[test]
fn cancellation_every_phase_partial_fft_and_filter_no_publication_no_resume() {
    for phase in [
        Phase::Fill,
        Phase::Transform,
        Phase::Magnitudes,
        Phase::Filter,
        Phase::Log,
        Phase::Difference,
        Phase::CommitHistory,
        Phase::Emit,
    ] {
        for delay in [0, 1, 7, 65] {
            let mut s = Session::new(fx::fixture(11).config).unwrap();
            let flag = AtomicBool::new(false);
            while s.phase() != phase {
                assert_eq!(step_one(&mut s, &flag).produced_rows, 0);
            }
            for _ in 0..delay {
                if s.phase() == phase && phase != Phase::Emit {
                    step_one(&mut s, &flag);
                }
            }
            let noticed = s.phase();
            let before = s.progress();
            flag.store(true, Ordering::Release);
            let mut out = [[123.0; FEATURES]];
            let r = s.step(
                7,
                InputBlock {
                    first_sample: u64::MAX,
                    samples: &[f32::NAN],
                },
                &mut out,
                &flag,
            );
            assert_eq!(r.status, Status::Cancelled);
            assert_eq!(r.units, 0);
            assert_eq!(out, [[123.0; FEATURES]]);
            assert_eq!(s.progress().cancellation_phase, Some(noticed));
            assert_eq!(s.progress().consumed_samples, before.consumed_samples);
            flag.store(false, Ordering::Release);
            let p = s.progress();
            step_one(&mut s, &flag);
            assert_eq!(s.progress(), p);
            s.finish_input();
            assert_eq!(s.status(), Status::Cancelled);
        }
    }
    let (_, input) = fx::primitive(0);
    let mut fft = Transform::new();
    let flag = AtomicBool::new(false);
    fft.step(21, &input, &flag);
    let p = fft.units();
    flag.store(true, Ordering::Release);
    assert_eq!(fft.step(999, &input, &flag), 0);
    assert_eq!(fft.units(), p);
}
#[test]
fn errors_after_provisional_prefix_and_priority_backpressure() {
    let flag = AtomicBool::new(false);
    for invalid in [
        f32::NAN,
        f32::INFINITY,
        f32::NEG_INFINITY,
        2.000001,
        -2.000001,
    ] {
        let mut s = Session::new(fx::fixture(11).config).unwrap();
        while s.progress().produced_rows == 0 {
            step_one(&mut s, &flag);
        }
        let p = s.progress();
        let mut out = [[123.0; FEATURES]];
        let r = s.step(
            99,
            InputBlock {
                first_sample: p.consumed_samples,
                samples: &[0.0, invalid],
            },
            &mut out,
            &flag,
        );
        assert_eq!(r.error, Some(Error::InvalidPcm));
        assert_eq!(r.consumed_samples, 1);
        assert_eq!(r.produced_rows, 0);
        assert_eq!(s.progress().produced_rows, 1);
        assert_eq!(out, [[123.0; FEATURES]]);
    }
    for (offset, source, error) in [
        (1, &[][..], Error::SourceOffset),
        (0, &[0.0, 0.0][..], Error::SourceLength),
    ] {
        let mut s = Session::new(Config {
            source_rate: 48000,
            source_frames: 1,
        })
        .unwrap();
        let r = s.step(
            1,
            InputBlock {
                first_sample: offset,
                samples: source,
            },
            &mut [],
            &flag,
        );
        assert_eq!(r.error, Some(error));
    }
    let mut s = Session::new(fx::fixture(11).config).unwrap();
    let p = s.progress();
    let r = s.step(
        0,
        InputBlock {
            first_sample: 99,
            samples: &[f32::NAN],
        },
        &mut [],
        &flag,
    );
    assert_eq!(r.error, Some(Error::ZeroBudget));
    assert_eq!(s.progress(), p);
    let r = s.step(
        99,
        InputBlock {
            first_sample: 0,
            samples: &[f32::NAN],
        },
        &mut [],
        &flag,
    );
    assert_eq!(r.flow, Flow::NeedOutput);
    assert_eq!(s.progress(), p);
    s.finish_input();
    assert_eq!(s.error(), Some(Error::TruncatedSource));
    // Repeated/gapped offsets must also fail after real progress.
    for delta in [-1, 1] {
        let mut s = Session::new(fx::fixture(11).config).unwrap();
        step_one(&mut s, &flag);
        let r = s.step(
            1,
            InputBlock {
                first_sample: (1 + delta) as u64,
                samples: &[],
            },
            &mut [[0.0; FEATURES]],
            &flag,
        );
        assert_eq!(r.error, Some(Error::SourceOffset));
    }
}
#[test]
fn actual_hs03_small_compositions_both_rates_and_channels() {
    use libapta_runtime::compact_resampler as rs;
    for i in 16..20 {
        let c = fx::fixture(i);
        let channels = if i % 2 == 0 { 1 } else { 2 };
        let source = rs::synthetic::Case {
            name: "composition",
            config: rs::Config {
                source_rate: c.config.source_rate,
                frames: 321,
                channels,
            },
            signal: "periodic",
            impulse: 0,
            expected_input: "",
            expected_output: "",
        };
        let mut d = rs::synthetic::Driver::new(source, 7, 7, 7);
        let mut actual = Vec::new();
        let flag = AtomicBool::new(false);
        // Exercise the real resampler directly, retaining only this short fixture.
        let pcm: Vec<_> = (0..321)
            .flat_map(|j| (0..channels).map(move |ch| source.sample(j, ch)))
            .collect();
        let mut pos = 0;
        while d.session.status() == rs::Status::Running {
            let mut out = [0.0; 7];
            let end = (pos + 7 * channels as usize).min(pcm.len());
            let r = d.session.step(
                7,
                rs::InputBlock {
                    first_frame: (pos / channels as usize) as u64,
                    interleaved: &pcm[pos..end],
                },
                &mut out,
                &flag,
            );
            assert!(r.error.is_none());
            pos += r.consumed_frames * channels as usize;
            actual.extend_from_slice(&out[..r.produced]);
        }
        assert_eq!(
            actual.iter().map(|x| x.to_bits()).collect::<Vec<_>>(),
            f32s(c.raw).iter().map(|x| x.to_bits()).collect::<Vec<_>>()
        );
        stream(c, &actual, 7, 1, 7);
    }
}

#[test]
fn fixed_executable_refuses_arbitrary_inputs_before_transport() {
    let exe = env!("CARGO_BIN_EXE_apta-compact-frontend-synthetic");
    for args in [
        vec![],
        vec!["--case", "unknown"],
        vec!["--file", "input.wav"],
        vec!["--case", "fixtures", "--input-fd", "0"],
    ] {
        let r = std::process::Command::new(exe).args(args).output().unwrap();
        assert!(!r.status.success());
        assert!(r.stdout.is_empty());
    }
}
