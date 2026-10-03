// SPDX-License-Identifier: Apache-2.0
use libapta::container::{waveform_size, write_waveform, Container, ParseOptions};
use libapta::session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget};
use libapta::waveform::{NormalizedSample, WaveformAccumulator};
use libapta::{FeatureState, SourceInfo, WaveformColumn, WaveformOverview, WaveformSpan};

#[test]
fn budgeted_pcm_to_wire_to_retained_result() {
    // Queue, processing step and column boundaries deliberately do not align.
    let pcm: Vec<f32> = (0..2177)
        .map(|i| ((i * 127 % 2049) as f32 - 1024.) / 1024.)
        .collect();
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(pcm.len() as u64),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let mut output = [WaveformColumn::default(); 3];
    let mut queue = [NormalizedSample::default(); 77];
    let mut retained_columns = [WaveformColumn::default(); 3];
    let mut retained_spans = [WaveformSpan::default(); 1];
    {
        let mut session = Session::new(
            SessionConfig {
                sample_rate: source.sample_rate,
                channel_count: 1,
                total_frames: pcm.len() as u64,
                frames_per_column: 1024,
            },
            &mut queue,
            &mut output,
        )
        .unwrap();
        let token = CancellationToken::new();
        let budget = WorkBudget {
            maximum_input_frames: 31,
            maximum_steps: 1,
        };
        let mut supplied = 0;
        while supplied < pcm.len() {
            supplied += session.push_interleaved(&pcm[supplied..]).unwrap();
            assert!(
                session
                    .process(budget, &token)
                    .unwrap()
                    .consumed_input_frames
                    <= 31
            );
        }
        session.finish_input().unwrap();
        while session.state() != SessionState::Complete {
            session.process(budget, &token).unwrap();
        }
        let spans = [WaveformSpan {
            first_frame: 0,
            end_frame: pcm.len() as u64,
            first_column_index: 0,
            column_count: 3,
            data_column_offset: 0,
        }];
        let overview = WaveformOverview {
            level_id: 0,
            frames_per_column: 1024,
            origin_frame: 0,
            logical_column_count: 3,
            state: FeatureState::Final,
            spans: &spans,
            columns: session.columns(),
        };
        let mut bytes = vec![0; waveform_size(&overview).unwrap()];
        write_waveform(&source, &overview, &mut bytes).unwrap();
        let container = Container::parse(&bytes, ParseOptions::default()).unwrap();
        assert_eq!(container.source, source);
        let parsed = container.parse_waveform(ParseOptions::default()).unwrap();
        for (i, chunk) in pcm.chunks(1024).enumerate() {
            let mut accumulator = WaveformAccumulator::default();
            for sample in chunk {
                accumulator
                    .push_normalized(*sample, sample.abs() >= 1.)
                    .unwrap();
            }
            assert_eq!(parsed.column(0, i), Some(accumulator.column()));
        }
        let copied = parsed
            .copy_into(&mut retained_spans, &mut retained_columns)
            .unwrap();
        assert_eq!(copied.columns, session.columns());
        // Owned copies retain results without a session or serialized buffer.
    }
    assert_eq!(retained_spans[0].end_frame, 2177);
    assert_ne!(retained_columns[2], WaveformColumn::default());
}
