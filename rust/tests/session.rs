// SPDX-License-Identifier: Apache-2.0
use libapta::session::{
    CancellationToken, Session, SessionConfig, SessionState, WorkBudget, TOTAL_FRAMES_UNKNOWN,
};
use libapta::waveform::{NormalizedSample, PcmView};
use libapta::{Error, WaveformColumn};

fn config(frames: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: frames,
        frames_per_column: 64,
    }
}
fn blank() -> NormalizedSample {
    NormalizedSample {
        value: 0.0,
        clipped: false,
    }
}

#[test]
fn bounded_queue_budget_wrap_and_final_partial() {
    let mut queue = [blank(); 300];
    let mut output = [WaveformColumn::default(); 6];
    let mut session = Session::new(config(321), &mut queue, &mut output).unwrap();
    assert_eq!(session.push_interleaved(&[0.5; 321]), Ok(300));
    assert_eq!(session.push_interleaved(&[0.5; 21]), Ok(0));
    let token = CancellationToken::new();
    let progress = session
        .process(
            WorkBudget {
                maximum_input_frames: 0,
                maximum_steps: 1,
            },
            &token,
        )
        .unwrap();
    assert_eq!(progress.consumed_input_frames, 256);
    assert_eq!(progress.completed_steps, 1);
    assert_eq!(session.push_interleaved(&[0.5; 21]), Ok(21));
    session.finish_input().unwrap();
    let progress = session
        .process(
            WorkBudget {
                maximum_input_frames: 3,
                maximum_steps: 0,
            },
            &token,
        )
        .unwrap();
    assert_eq!(progress.consumed_input_frames, 3);
    assert_eq!(session.state(), SessionState::Draining);
    session.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(session.processed_frames(), 321);
    assert_eq!(session.columns().len(), 6);
    assert_eq!(session.state(), SessionState::Complete);
    token.cancel();
    assert_eq!(
        session
            .process(WorkBudget::default(), &token)
            .unwrap()
            .consumed_input_frames,
        0
    );
    assert_eq!(session.state(), SessionState::Complete);
    assert_eq!(session.push_interleaved(&[]), Err(Error::InvalidState));
}

#[test]
fn malformed_pcm_is_atomic_and_cancel_precedes_work() {
    let mut queue = [blank(); 64];
    let mut output = [WaveformColumn::default(); 1];
    let mut session = Session::new(config(64), &mut queue, &mut output).unwrap();
    assert_eq!(
        session.push_interleaved(&[0.3, f32::NAN]),
        Err(Error::InvalidArgument)
    );
    assert_eq!(session.accepted_frames(), 0);
    assert_eq!(session.queued_frames(), 0);
    assert_eq!(session.finish_input(), Err(Error::InvalidState));
    session.push_interleaved(&[0.2; 64]).unwrap();
    let token = CancellationToken::new();
    token.cancel();
    assert_eq!(
        session.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(session.processed_frames(), 0);
    assert_eq!(session.state(), SessionState::Cancelled);
}

#[test]
fn copied_snapshot_survives_further_processing_and_session_drop() {
    let mut snapshot = [WaveformColumn::default(); 1];
    {
        let mut queue = [blank(); 128];
        let mut output = [WaveformColumn::default(); 2];
        let mut session = Session::new(config(128), &mut queue, &mut output).unwrap();
        session.push_interleaved(&[0.25; 64]).unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(
            session.copy_snapshot_into(&mut []),
            Err(Error::BufferTooSmall)
        );
        session.copy_snapshot_into(&mut snapshot).unwrap();
        session.push_interleaved(&[-0.5; 64]).unwrap();
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(snapshot[0], session.columns()[0]);
        assert_ne!(snapshot[0], session.columns()[1]);
    }
    assert!(snapshot[0].minimum > 0);
}

#[test]
fn geometry_limits_and_empty_source() {
    let mut queue = [blank(); 1];
    assert!(matches!(
        Session::new(config(u64::MAX - 1), &mut queue, &mut []),
        Err(Error::BufferTooSmall)
    ));
    let mut bad = config(0);
    bad.frames_per_column = 63;
    assert!(matches!(
        Session::new(bad, &mut queue, &mut []),
        Err(Error::InvalidArgument)
    ));
    let mut session = Session::new(config(0), &mut queue, &mut []).unwrap();
    session.finish_input().unwrap();
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(session.state(), SessionState::Complete);
    assert!(session.columns().is_empty());
}

#[test]
fn stereo_requires_whole_frames_and_mixes_channels() {
    let mut queue = [blank(); 64];
    let mut output = [WaveformColumn::default(); 1];
    let mut cfg = config(64);
    cfg.channel_count = 2;
    let mut session = Session::new(cfg, &mut queue, &mut output).unwrap();
    assert_eq!(
        session.push_interleaved(&[0.0]),
        Err(Error::InvalidArgument)
    );
    let mut input = [0.0; 128];
    for frame in input.chunks_exact_mut(2) {
        frame[0] = 0.5;
        frame[1] = -0.5;
    }
    session.push_interleaved(&input).unwrap();
    session.finish_input().unwrap();
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(session.columns()[0].minimum, 0);
    assert_eq!(session.columns()[0].maximum, 0);
}

#[test]
fn all_pcm_formats_reach_identical_fullscale_and_planar_validation_is_atomic() {
    let mut s16 = [0_i16; 64];
    let mut s24 = [0_u8; 192];
    let mut s32 = [0_i32; 64];
    let mut f32 = [0_f32; 64];
    for i in 0..64 {
        let positive = i % 2 == 0;
        s16[i] = if positive { i16::MAX } else { i16::MIN };
        s32[i] = if positive { i32::MAX } else { i32::MIN };
        f32[i] = if positive { 1.0 } else { -1.0 };
        s24[i * 3..i * 3 + 3].copy_from_slice(if positive {
            &[255, 255, 127]
        } else {
            &[0, 0, 128]
        });
    }
    let planes: [&[f32]; 1] = [&f32];
    let representations = [
        PcmView::S16Interleaved(&s16),
        PcmView::S24Interleaved(&s24),
        PcmView::S32Interleaved(&s32),
        PcmView::F32Interleaved(&f32),
        PcmView::F32Planar(&planes),
    ];
    for representation in representations {
        let mut queue = [blank(); 64];
        let mut output = [WaveformColumn::default(); 1];
        let mut session = Session::new(config(64), &mut queue, &mut output).unwrap();
        assert_eq!(session.push_pcm(representation), Ok(64));
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(session.columns()[0].rms, u16::MAX);
        assert_eq!(session.columns()[0].minimum, i16::MIN);
        assert_eq!(session.columns()[0].maximum, i16::MAX);
        assert_eq!(session.columns()[0].flags, 5);
    }
    let mut queue = [blank(); 64];
    let mut output = [WaveformColumn::default(); 1];
    let mut session = Session::new(config(64), &mut queue, &mut output).unwrap();
    let malformed = [0.0, f32::INFINITY];
    assert_eq!(
        session.push_pcm(PcmView::F32Planar(&[&malformed])),
        Err(Error::InvalidArgument)
    );
    assert_eq!(session.accepted_frames(), 0);
    assert_eq!(session.queued_frames(), 0);
}

#[test]
fn unknown_input_capacity_is_permanent_and_eof_resolves_length() {
    let mut queue = [blank(); 17];
    let mut output = [WaveformColumn::default(); 1];
    let mut session = Session::new(config(TOTAL_FRAMES_UNKNOWN), &mut queue, &mut output).unwrap();
    assert_eq!(session.total_frames(), None);
    assert_eq!(session.input_capacity_frames(), 64);
    assert_eq!(session.push_interleaved(&[0.25; 65]), Ok(17));
    assert_eq!(session.push_interleaved(&[0.25]), Ok(0));
    let token = CancellationToken::new();
    while session.accepted_frames() < 64 {
        let progress = session
            .process(
                WorkBudget {
                    maximum_input_frames: 3,
                    maximum_steps: 1,
                },
                &token,
            )
            .unwrap();
        assert!(progress.consumed_input_frames <= 3);
        assert!(progress.completed_steps <= 1);
        session.push_interleaved(&[0.25; 65]).unwrap();
    }
    assert_eq!(
        session.push_interleaved(&[0.25]),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(session.push_interleaved(&[]), Ok(0));
    session.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(
        session.push_interleaved(&[0.25]),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(session.state(), SessionState::Running);
    assert_eq!(session.total_frames(), None);
    session.finish_input().unwrap();
    assert_eq!(session.total_frames(), Some(64));
    assert_eq!(session.config().total_frames, 64);
    assert_eq!(session.state(), SessionState::Draining);
    assert_eq!(session.finish_input(), Err(Error::InvalidState));
    session.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(session.state(), SessionState::Complete);
    assert_eq!(session.columns().len(), 1);
}

#[test]
fn unknown_empty_source_and_zero_capacity() {
    let mut queue = [blank(); 1];
    let mut session = Session::new(config(TOTAL_FRAMES_UNKNOWN), &mut queue, &mut []).unwrap();
    assert_eq!(session.input_capacity_frames(), 0);
    assert_eq!(session.push_interleaved(&[0.1]), Err(Error::BufferTooSmall));
    assert_eq!(session.push_interleaved(&[]), Ok(0));
    session.finish_input().unwrap();
    assert_eq!(session.total_frames(), Some(0));
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(session.state(), SessionState::Complete);
    assert!(session.columns().is_empty());
}

#[test]
fn unknown_malformed_pcm_and_cancellation_preserve_accepted_prefix() {
    let mut queue = [blank(); 65];
    let mut output = [WaveformColumn::default(); 2];
    let mut session = Session::new(config(TOTAL_FRAMES_UNKNOWN), &mut queue, &mut output).unwrap();
    assert_eq!(
        session.push_interleaved(&[0.2, f32::NAN]),
        Err(Error::InvalidArgument)
    );
    assert_eq!(session.accepted_frames(), 0);
    let mut input = [0.25; 65];
    session.push_interleaved(&input).unwrap();
    input.fill(-0.5);
    let token = CancellationToken::new();
    session
        .process(
            WorkBudget {
                maximum_input_frames: 64,
                maximum_steps: 1,
            },
            &token,
        )
        .unwrap();
    assert!(session.columns()[0].minimum > 0);
    let mut snapshot = [WaveformColumn::default(); 1];
    session.copy_snapshot_into(&mut snapshot).unwrap();
    token.cancel();
    assert_eq!(
        session.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(session.processed_frames(), 64);
    assert_eq!(session.queued_frames(), 1);
    assert_eq!(session.columns(), &snapshot);
    assert_eq!(session.total_frames(), None);
    assert_eq!(session.finish_input(), Err(Error::InvalidState));
    assert_eq!(session.push_interleaved(&[]), Err(Error::InvalidState));
}

#[test]
fn unknown_partial_eof_matches_known_duration_and_serializes() {
    use libapta::container::{write_waveform, Container, ParseOptions};
    use libapta::{FeatureState, SourceInfo, WaveformOverview, WaveformSpan};
    let input: [f32; 129] = core::array::from_fn(|i| (i as f32 - 64.0) / 64.0);
    let mut results = [[WaveformColumn::default(); 3]; 2];
    for (duration, result) in [129, TOTAL_FRAMES_UNKNOWN].into_iter().zip(&mut results) {
        let mut queue = [blank(); 73];
        let mut session = Session::new(config(duration), &mut queue, result).unwrap();
        let token = CancellationToken::new();
        let mut accepted = 0;
        while accepted != input.len() {
            accepted += session.push_interleaved(&input[accepted..]).unwrap();
            session
                .process(
                    WorkBudget {
                        maximum_input_frames: 7,
                        maximum_steps: 1,
                    },
                    &token,
                )
                .unwrap();
        }
        session.finish_input().unwrap();
        assert_eq!(session.total_frames(), Some(129));
        while session.state() != SessionState::Complete {
            session
                .process(
                    WorkBudget {
                        maximum_input_frames: 7,
                        maximum_steps: 1,
                    },
                    &token,
                )
                .unwrap();
        }
        assert_eq!(session.columns().len(), 3);
    }
    assert_eq!(results[0], results[1]);
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(129),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let spans = [WaveformSpan {
        first_frame: 0,
        end_frame: 129,
        first_column_index: 0,
        column_count: 3,
        data_column_offset: 0,
    }];
    let overview = WaveformOverview {
        level_id: 0,
        frames_per_column: 64,
        origin_frame: 0,
        logical_column_count: 3,
        state: FeatureState::Final,
        spans: &spans,
        columns: &results[1],
    };
    let mut bytes = [0; 512];
    let size = write_waveform(&source, &overview, &mut bytes).unwrap();
    let parsed = Container::parse(&bytes[..size], ParseOptions::default()).unwrap();
    assert_eq!(parsed.source, source);
    let waveform = parsed.parse_waveform(ParseOptions::default()).unwrap();
    for (index, column) in results[0].iter().enumerate() {
        assert_eq!(waveform.column(0, index), Some(*column));
    }
}
