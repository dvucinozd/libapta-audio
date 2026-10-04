// SPDX-License-Identifier: Apache-2.0
//! Desktop demonstration, not a replacement for the all-feature C analyzer.
#[path = "wav_to_apta/music.rs"]
mod music;
use libapta::container::{waveform_size, write_waveform, Container, ParseOptions};
use libapta::session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget};
use libapta::wav::Wav;
use libapta::waveform::NormalizedSample;
use libapta::{FeatureState, WaveformColumn, WaveformOverview, WaveformSpan};
use std::{
    error::Error,
    fs::OpenOptions,
    io::{Read, Write},
};

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args_os().collect();
    let musical = args.len() == 4 && args[3] == "--music";
    if args.len() != 3 && !musical {
        return Err(
            "usage: wav_to_apta INPUT.wav OUTPUT.apta [--music] (output must not exist)".into(),
        );
    }
    // Explicit desktop resource policy: reject files larger than 256 MiB.
    const MAX_INPUT: u64 = 256 * 1024 * 1024;
    let mut input = Vec::new();
    std::fs::File::open(&args[1])?
        .take(MAX_INPUT + 1)
        .read_to_end(&mut input)?;
    if input.len() as u64 > MAX_INPUT {
        return Err("WAV exceeds example's 256 MiB input limit".into());
    }
    let wav = Wav::parse(&input)?;
    if wav.frame_count() == 0 {
        return Err("an empty source has no serializable waveform".into());
    }
    if musical {
        let bytes = music::analyze(&wav)?;
        let mut output = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&args[2])?;
        output.write_all(&bytes)?;
        println!(
            "{} frames, default musical analysis, {} bytes",
            wav.frame_count(),
            bytes.len()
        );
        return Ok(());
    }
    // Fixed geometry selected explicitly; this example does not implement C's
    // adaptive overview planner. 32768 is the documented long-track P4 profile.
    const FPC: u32 = 32768;
    let column_count = wav.frame_count().div_ceil(u64::from(FPC));
    let mut columns = vec![WaveformColumn::default(); usize::try_from(column_count)?];
    let mut queue = [NormalizedSample::default(); 256];
    let mut session = Session::new(
        SessionConfig {
            sample_rate: wav.source().sample_rate,
            channel_count: wav.source().channel_count,
            total_frames: wav.frame_count(),
            frames_per_column: FPC,
        },
        &mut queue,
        &mut columns,
    )?;
    let token = CancellationToken::new();
    let budget = WorkBudget {
        maximum_steps: 1,
        maximum_input_frames: 256,
    };
    let mut scratch = [0.0f32; 512];
    let channels = usize::from(wav.source().channel_count);
    let mut first = 0;
    while first < wav.frame_count() {
        let count = wav.read_frames(first, &mut scratch[..256 * channels])?;
        let accepted = session.push_interleaved(&scratch[..count * channels])?;
        if accepted == 0 {
            return Err("unexpected PCM backpressure".into());
        }
        first += accepted as u64;
        session.process(budget, &token)?;
    }
    session.finish_input()?;
    session.process(budget, &token)?;
    if session.state() != SessionState::Complete {
        return Err("incomplete analysis".into());
    }
    let spans = [WaveformSpan {
        first_frame: 0,
        end_frame: wav.frame_count(),
        first_column_index: 0,
        column_count: u32::try_from(column_count)?,
        data_column_offset: 0,
    }];
    let overview = WaveformOverview {
        level_id: 0,
        frames_per_column: FPC,
        origin_frame: 0,
        logical_column_count: u32::try_from(column_count)?,
        state: FeatureState::Final,
        spans: &spans,
        columns: session.columns(),
    };
    let mut bytes = vec![0; waveform_size(&overview)?];
    write_waveform(&wav.source(), &overview, &mut bytes)?;
    Container::parse(&bytes, ParseOptions::default())?.parse_waveform(ParseOptions::default())?;
    let mut output = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&args[2])?;
    output.write_all(&bytes)?;
    println!(
        "{} frames, {} waveform columns, {} bytes",
        wav.frame_count(),
        column_count,
        bytes.len()
    );
    Ok(())
}
