// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-compact-resampler")]
use libapta_runtime::compact_resampler::{synthetic as fx, *};
use std::sync::atomic::{AtomicBool, Ordering};
#[test]
fn all_frozen_oracles_match_all_sixty_streaming_schedules() {
    for i in 0..fx::FIXTURE_COUNT {
        let c = fx::fixture(i);
        for input in [1, 7, 256, 512] {
            for output in [1, 7, 256] {
                for quota in [1, 7, 256, 4096, usize::MAX] {
                    let mut d = fx::Driver::new(c, input, output, quota);
                    let flag = AtomicBool::new(false);
                    while d.session.status() == Status::Running {
                        assert!(d.step(&flag).error.is_none());
                    }
                    let p = d.session.progress();
                    let k = (c.config.frames * 22050).div_ceil(u64::from(c.config.source_rate));
                    assert_eq!(
                        d.session.status(),
                        if k == 0 {
                            Status::NoEvidence
                        } else {
                            Status::Done
                        }
                    );
                    assert_eq!(p.source_frames, c.config.frames);
                    assert_eq!(p.output_samples, k);
                    assert_eq!(p.macs, k * 65);
                    assert_eq!(p.units, c.config.frames + k * 67);
                    assert_eq!(
                        d.hashes(),
                        (c.expected_input.into(), c.expected_output.into()),
                        "{} {input}/{output}/{quota}",
                        c.name
                    );
                    assert_eq!(
                        p.source_frames_before_first_output,
                        if k == 0 {
                            None
                        } else {
                            Some(c.config.frames.min(33))
                        }
                    );
                }
            }
        }
    }
}
#[test]
fn positions_backpressure_mid_tap_and_output_canaries() {
    for fs in [44100, 48000] {
        let mut s = Session::new(Config {
            source_rate: fs,
            channels: 1,
            frames: 321,
        })
        .unwrap();
        let flag = AtomicBool::new(false);
        let source = [1.0; 321];
        let no = s.step(
            100,
            InputBlock {
                first_frame: 0,
                interleaved: &source,
            },
            &mut [],
            &flag,
        );
        assert_eq!(no.flow, Flow::NeedOutput);
        assert_eq!(no.units, 0);
        let mut out = [123.0; 3];
        let r = s.step(
            10,
            InputBlock {
                first_frame: 0,
                interleaved: &[],
            },
            &mut out[1..2],
            &flag,
        );
        assert_eq!(r.flow, Flow::NeedInput);
        assert_eq!(r.units, 0);
        let r = s.step(
            34,
            InputBlock {
                first_frame: 0,
                interleaved: &source,
            },
            &mut out[1..2],
            &flag,
        );
        assert_eq!(r.consumed_frames, 33);
        assert_eq!(r.produced, 0);
        assert_eq!(s.phase(), Phase::Convolve);
        assert_eq!(out, [123.0; 3]);
        let r = s.step(
            64,
            InputBlock {
                first_frame: 33,
                interleaved: &source[33..],
            },
            &mut out[1..2],
            &flag,
        );
        assert_eq!(r.produced, 0);
        assert_eq!(out, [123.0; 3]);
        let r = s.step(
            2,
            InputBlock {
                first_frame: 33,
                interleaved: &source[33..],
            },
            &mut out[1..2],
            &flag,
        );
        assert_eq!(r.produced, 1);
        assert_eq!(out[0], 123.0);
        assert_eq!(out[2], 123.0);
        for k in 0..s.output_count() {
            let p = s.source_position(k).unwrap();
            if fs == 44100 {
                assert_eq!((p.center, p.phase, p.phase_count), (2 * k, 0, 1));
            } else {
                assert_eq!(
                    (p.center, p.phase, p.phase_count),
                    (320 * k / 147, (320 * k % 147) as u16, 147)
                );
            }
            assert_eq!(p.required_frames, (p.center + 33).min(321));
        }
        assert_eq!(s.source_position(s.output_count()), None);
    }
}

#[test]
fn cancellation_every_phase_and_terminal_fences() {
    let c = fx::fixture(15); // fixed nonempty ring-wrapping source
    for phase in [Phase::Fill, Phase::Convolve, Phase::Emit] {
        let mut d = fx::Driver::new(c, 7, 7, 1);
        let flag = AtomicBool::new(false);
        while d.session.phase() != phase {
            assert_eq!(d.session.status(), Status::Running);
            d.step(&flag);
        }
        let before = d.session.progress();
        flag.store(true, Ordering::Release);
        let r = d.step(&flag);
        assert_eq!((r.produced, r.consumed_frames, r.units), (0, 0, 0));
        assert_eq!(d.session.status(), Status::Cancelled);
        assert_eq!(d.session.progress().cancellation_phase, Some(phase));
        flag.store(false, Ordering::Release);
        let r = d.step(&flag);
        assert_eq!((r.produced, r.consumed_frames), (0, 0));
        assert_eq!(d.session.progress().output_samples, before.output_samples);
        assert_eq!(d.session.progress().source_frames, before.source_frames);
        d.session.finish_input().unwrap();
        assert_eq!(d.session.status(), Status::Cancelled);
    }
    let mut d = fx::Driver::new(c, 512, 256, 4096);
    let flag = AtomicBool::new(false);
    while d.session.status() == Status::Running {
        d.step(&flag);
    }
    let p = d.session.progress();
    let hashes = d.hashes();
    d.session.cancel();
    flag.store(true, Ordering::Release);
    let r = d.session.step(
        100,
        InputBlock {
            first_frame: u64::MAX,
            interleaved: &[f32::NAN],
        },
        &mut [99.0],
        &flag,
    );
    assert_eq!(r.status, Status::Done);
    assert_eq!(r.units, 0);
    assert_eq!(d.session.progress(), p);
    assert_eq!(d.hashes(), hashes);
    let r = d.session.step(
        0,
        InputBlock {
            first_frame: 0,
            interleaved: &[],
        },
        &mut [],
        &flag,
    );
    assert_eq!(r.error, Some(Error::ZeroBudget));
    assert_eq!(d.session.status(), Status::Done);
}

#[test]
fn invalid_geometry_offsets_shape_oversupply_and_short_eof() {
    for config in [
        Config {
            source_rate: 22050,
            channels: 1,
            frames: 1,
        },
        Config {
            source_rate: 44100,
            channels: 0,
            frames: 1,
        },
        Config {
            source_rate: 48000,
            channels: 3,
            frames: 1,
        },
        Config {
            source_rate: 48000,
            channels: 2,
            frames: 28800001,
        },
        Config {
            source_rate: 48000,
            channels: 1,
            frames: u64::MAX,
        },
    ] {
        assert!(matches!(
            Session::new(config),
            Err(Error::UnsupportedProfile)
        ));
    }
    let config = Config {
        source_rate: 48000,
        channels: 2,
        frames: 100,
    };
    let flag = AtomicBool::new(false);
    for (block, error) in [
        (
            InputBlock {
                first_frame: 1,
                interleaved: &[],
            },
            Error::SourceOffset,
        ),
        (
            InputBlock {
                first_frame: 0,
                interleaved: &[0.0],
            },
            Error::InvalidInput,
        ),
        (
            InputBlock {
                first_frame: 0,
                interleaved: &[0.0; 202],
            },
            Error::SourceLength,
        ),
    ] {
        let mut s = Session::new(config).unwrap();
        let mut out = [123.0];
        let r = s.step(1000, block, &mut out, &flag);
        assert_eq!(r.error, Some(error));
        assert_eq!(r.status, Status::Failed);
        assert_eq!((r.units, r.consumed_frames, r.produced), (0, 0, 0));
        assert_eq!(out, [123.0]);
    }
    for next in [0, 2] {
        let mut s = Session::new(config).unwrap();
        let mut out = [123.0];
        assert_eq!(
            s.step(
                1,
                InputBlock {
                    first_frame: 0,
                    interleaved: &[0.0; 2]
                },
                &mut out,
                &flag
            )
            .consumed_frames,
            1
        );
        let r = s.step(
            100,
            InputBlock {
                first_frame: next,
                interleaved: &[0.0; 2],
            },
            &mut out,
            &flag,
        );
        assert_eq!(r.error, Some(Error::SourceOffset));
    }
    let mut s = Session::new(config).unwrap();
    let r = s.step(
        0,
        InputBlock {
            first_frame: 9,
            interleaved: &[f32::NAN],
        },
        &mut [3.0],
        &flag,
    );
    assert_eq!(r.error, Some(Error::ZeroBudget));
    assert_eq!(s.status(), Status::Running);
    assert_eq!(s.progress().source_frames, 0);
    assert_eq!(s.finish_input(), Err(Error::TruncatedSource));
    assert_eq!(s.status(), Status::Failed);
    let mut s = Session::new(Config {
        source_rate: 44100,
        channels: 1,
        frames: 1,
    })
    .unwrap();
    let mut out = [123.0];
    let r = s.step(
        1,
        InputBlock {
            first_frame: 0,
            interleaved: &[1.0],
        },
        &mut out,
        &flag,
    );
    assert_eq!(r.produced, 0);
    s.finish_input().unwrap();
    let r = s.step(
        1000,
        InputBlock {
            first_frame: 1,
            interleaved: &[],
        },
        &mut out,
        &flag,
    );
    assert_eq!(r.status, Status::Done);
    assert_eq!(r.produced, 1);
    // Pending cancellation takes priority over malformed buffers.
    let mut s = Session::new(config).unwrap();
    flag.store(true, Ordering::Release);
    let r = s.step(
        100,
        InputBlock {
            first_frame: 9,
            interleaved: &[f32::NAN],
        },
        &mut [123.0],
        &flag,
    );
    assert_eq!(r.status, Status::Cancelled);
    assert_eq!(r.error, None);
}

#[test]
fn malformed_pcm_is_not_consumed_and_prior_progress_is_reported() {
    for bad in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1.01, -1.01] {
        for channel in [0, 1] {
            let mut pcm = vec![0.5; 512];
            pcm[120 * 2 + channel] = bad;
            let mut s = Session::new(Config {
                source_rate: 48000,
                channels: 2,
                frames: 256,
            })
            .unwrap();
            let flag = AtomicBool::new(false);
            let mut out = [123.0; 256];
            let r = s.step(
                usize::MAX,
                InputBlock {
                    first_frame: 0,
                    interleaved: &pcm,
                },
                &mut out,
                &flag,
            );
            assert_eq!(r.error, Some(Error::InvalidPcm));
            assert_eq!(r.status, Status::Failed);
            assert_eq!(r.consumed_frames, 120);
            assert!(r.produced > 0);
            assert!(r.units > 0);
            assert_eq!(s.progress().source_frames, 120);
            assert!(out[..r.produced].iter().all(|x| x.is_finite()));
            assert!(out[r.produced..].iter().all(|x| *x == 123.0));
            let r = s.step(
                10,
                InputBlock {
                    first_frame: 120,
                    interleaved: &[0.0; 2],
                },
                &mut out,
                &flag,
            );
            assert_eq!((r.consumed_frames, r.produced), (0, 0));
        }
    }
    let mut s = Session::new(Config {
        source_rate: 48000,
        channels: 1,
        frames: 10,
    })
    .unwrap();
    let mut out = [123.0];
    let r = s.step(
        100,
        InputBlock {
            first_frame: 0,
            interleaved: &[0.0, 0.0, f32::NAN, 0.0],
        },
        &mut out,
        &AtomicBool::new(false),
    );
    assert_eq!((r.consumed_frames, r.produced), (2, 0));
    assert_eq!(r.error, Some(Error::InvalidPcm));
    assert_eq!(out, [123.0]);
}

#[test]
fn executable_refuses_files_and_nonregistered_cases() {
    for args in [
        vec!["--input", "audio.wav"],
        vec!["--case", "random"],
        vec!["--case", "fixtures", "extra"],
    ] {
        let r = std::process::Command::new(env!("CARGO_BIN_EXE_apta-compact-resampler-synthetic"))
            .args(args)
            .output()
            .unwrap();
        assert!(!r.status.success());
        assert!(r.stdout.is_empty());
    }
}
