// SPDX-License-Identifier: Apache-2.0
//! Provisional history diagnostics only. No .apta export or FINAL promotion.
#![forbid(unsafe_code)]
use libapta::{
    analysis::OnsetBin,
    attack_grid::{SelectionOutcome, SelectionRecord},
    global_analysis::{BIN_CAPACITY, MAX_BEATS},
    global_history::{WindowHistory, WindowOutcome, WindowRecord, WINDOW_FRAMES},
    history_meter::{AttackBin, MeterHistory, MeterOutcome, MeterRecord, ATTACK_BIN_CAPACITY},
    session::{CancellationToken, Session, SessionConfig, SessionState, WorkBudget},
    waveform::NormalizedSample,
    Beat, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment, WaveformColumn,
};
use sha2::{Digest, Sha256};
use std::{
    error::Error,
    fmt::Write as _,
    fs::{File, OpenOptions},
    io::{Read, Write as _},
    path::Path,
};

const MAX_BYTES: u64 = 256 * 1024 * 1024;
const USAGE: &str =
    "usage: apta-history-diagnostic INPUT.wav OUTPUT.json --mode=coarse|attack|novelty|novelty-trace|novelty-dense --frames=N";

fn empty_segment() -> GridSegment {
    GridSegment {
        applicability_range: FrameRange::default(),
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod::default(),
        beat_count: 0,
        nominal_tempo_millibpm: 0,
        confidence: 0,
        state: FeatureState::Provisional,
        flags: 1,
        segment_id: 0,
        revision: 0,
    }
}
fn window_json(outcome: WindowOutcome) -> String {
    match outcome {
        WindowOutcome::Accepted(e) => format!(
            "{{\"kind\":\"accepted\",\"anchor_frame\":{},\"tempo_millibpm\":{},\"confidence\":{}}}",
            e.anchor_frame, e.tempo_millibpm, e.confidence
        ),
        WindowOutcome::Rejected => "{\"kind\":\"rejected\"}".into(),
        WindowOutcome::Unavailable => "{\"kind\":\"unavailable\"}".into(),
        WindowOutcome::TooShort => "{\"kind\":\"too_short\"}".into(),
    }
}
fn selection_name(outcome: SelectionOutcome) -> &'static str {
    match outcome {
        SelectionOutcome::Accepted(_) => "accepted",
        SelectionOutcome::Unavailable => "unavailable",
        SelectionOutcome::InsufficientEvents => "insufficient_events",
        SelectionOutcome::EventLimit => "event_limit",
        SelectionOutcome::Irregular => "irregular",
        SelectionOutcome::TempoOutOfRange => "tempo_out_of_range",
    }
}

fn diagnose(input: &Path, mode: &str, requested: u64) -> Result<String, Box<dyn Error>> {
    let trace_requested = mode == "novelty-trace";
    let dense_requested = mode == "novelty-dense";
    #[cfg(not(feature = "temporal-trace-diagnostics"))]
    if dense_requested {
        return Err("novelty-dense requires temporal-trace-diagnostics build feature".into());
    }
    #[cfg(not(feature = "onset-trace-diagnostics"))]
    if trace_requested {
        return Err("novelty-trace requires onset-trace-diagnostics build feature".into());
    }
    let mode = if trace_requested || dense_requested {
        "novelty"
    } else {
        mode
    };
    let attack = mode != "coarse";
    let mut bytes = Vec::new();
    File::open(input)?
        .take(MAX_BYTES + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() as u64 > MAX_BYTES {
        return Err("WAV exceeds 256 MiB input cap".into());
    }
    let mut hash = String::new();
    for byte in Sha256::digest(&bytes) {
        write!(&mut hash, "{byte:02x}")?;
    }
    let wav = libapta::wav::Wav::parse(&bytes)?;
    let source = wav.source();
    let count = wav.frame_count().min(requested);
    if count == 0 {
        return Err("nonempty input and positive prefix required".into());
    }
    let capacity = usize::try_from(count.div_ceil(WINDOW_FRAMES))?;
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut beats = vec![Beat::default(); MAX_BEATS];
    let mut attacks = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
    let mut records = vec![WindowRecord::default(); capacity];
    let mut meters = vec![MeterRecord::default(); capacity];
    let mut trace = vec![SelectionRecord::default(); capacity];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); usize::try_from(count.div_ceil(32768))?];
    let mut meter = MeterHistory::new(source.sample_rate, &mut attacks, &mut meters)?;
    let mut history = WindowHistory::new(source.sample_rate, &mut records)?;
    history.enable_meter(&mut meter)?;
    if mode == "novelty" {
        #[cfg(feature = "onset-diagnostics")]
        history.enable_onset_grid(&mut trace)?;
        #[cfg(not(feature = "onset-diagnostics"))]
        return Err("novelty mode requires onset-diagnostics build feature".into());
    } else if attack {
        history.enable_attack_grid(&mut trace)?;
    }
    let mut session = Session::new(
        SessionConfig {
            sample_rate: source.sample_rate,
            channel_count: source.channel_count,
            total_frames: count,
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
    )?;
    session.enable_global_grid(false, &mut bins, &mut flux, &mut beats)?;
    session.enable_global_history(&mut history)?;
    let channels = usize::from(source.channel_count);
    let mut decoded = vec![0.0; 4096 * channels];
    let token = CancellationToken::new();
    let mut first = 0;
    while first < count {
        let maximum = usize::try_from((count - first).min(4096))?;
        let read = wav.read_frames(first, &mut decoded[..maximum * channels])?;
        if read != maximum {
            return Err("unexpected short WAV decode".into());
        }
        let accepted = session.push_interleaved(&decoded[..read * channels])?;
        if accepted != read {
            return Err("unexpected partial diagnostic input".into());
        }
        first += read as u64;
        session.process(WorkBudget::default(), &token)?;
    }
    session.finish_input()?;
    for _ in 0..8192 {
        session.process(WorkBudget::default(), &token)?;
        if session.state() == SessionState::Complete {
            break;
        }
    }
    if session.state() != SessionState::Complete {
        return Err("diagnostic drain did not complete".into());
    }
    let view = session.global_history().ok_or("missing history")?;
    let meter_view = session
        .global_history_meter()
        .ok_or("missing meter history")?;
    if !view.finished
        || view.capacity_exhausted
        || view.discontinuous_input
        || !meter_view.finished
        || meter_view.capacity_exhausted
        || meter_view.discontinuous_input
        || view.records.len() != capacity
        || meter_view.records.len() != capacity
    {
        return Err("incomplete or exhausted diagnostic evidence".into());
    }
    let revision = if mode == "novelty" {
        3
    } else if attack {
        2
    } else {
        1
    };
    let needed = libapta::history_grid::requirements(view, revision)?;
    let mut segments = vec![empty_segment(); needed.segments];
    let mut ranges = vec![FrameRange::default(); needed.supported_ranges];
    let grid = libapta::history_grid::convert_into(view, revision, &mut segments, &mut ranges)?;
    let selections = session.global_history_selection();
    let mut output = format!("{{\"format\":\"libapta-history-diagnostic-1\",\"mode\":\"{mode}\",\"input_sha256\":\"{hash}\",\"sample_rate\":{},\"channel_count\":{},\"source_total_frames\":{},\"requested_frames\":{requested},\"analyzed_first_frame\":0,\"analyzed_frames\":{count},\"finished\":true,\"capacity_exhausted\":false,\"provisional\":true,\"windows\":[", source.sample_rate, source.channel_count, wav.frame_count());
    for (index, (record, meter)) in view.records.iter().zip(meter_view.records).enumerate() {
        if index != 0 {
            output.push(',');
        }
        let (baseline, reason) = if let Some(trace) = selections {
            let selection = trace.get(index).ok_or("missing selection record")?;
            if selection.range != record.range {
                return Err("selection coordinate mismatch".into());
            }
            (selection.baseline, selection_name(selection.outcome))
        } else {
            (record.outcome, "not_enabled")
        };
        write!(&mut output, "{{\"first_frame\":{},\"end_frame\":{},\"baseline\":{},\"selected\":{},\"selection_reason\":\"{reason}\",\"meter\":", record.range.first_frame, record.range.end_frame, window_json(baseline), window_json(record.outcome))?;
        if meter.window != *record {
            return Err("meter grid-evidence mismatch".into());
        }
        match meter.outcome {
            MeterOutcome::Estimated(e) => {
                let segment = grid
                    .segments
                    .iter()
                    .find(|s| s.applicability_range == record.range)
                    .ok_or("missing selected segment")?;
                let beat = segment
                    .beat_at(e.downbeat_index)?
                    .ok_or("missing meter beat")?;
                write!(&mut output, "{{\"kind\":\"estimated\",\"numerator\":{},\"denominator\":4,\"confidence\":{},\"downbeat_frame\":{},\"downbeat_fraction_q32\":{},\"downbeat_ordinal\":{}}}", e.numerator, e.confidence, beat.position.whole_frame, beat.position.fraction_q32, beat.ordinal)?;
            }
            other => write!(
                &mut output,
                "{{\"kind\":\"{}\"}}",
                match other {
                    MeterOutcome::GridUnsupported => "grid_unsupported",
                    MeterOutcome::InsufficientBeats => "insufficient_beats",
                    MeterOutcome::BeatLimit => "beat_limit",
                    MeterOutcome::Ambiguous => "ambiguous",
                    _ => "unavailable",
                }
            )?,
        }
        #[cfg(feature = "onset-trace-diagnostics")]
        if trace_requested {
            let capture = &selections.ok_or("missing onset trace")?[index].onsets;
            write!(&mut output, ",\"onset_trace\":{{\"format\":\"libapta-onset-events-1\",\"complete\":{},\"events\":[", capture.complete)?;
            for (event_index, event) in capture.events[..usize::from(capture.count)]
                .iter()
                .enumerate()
            {
                if event_index != 0 {
                    output.push(',');
                }
                write!(&mut output, "[{},{}]", event.frame, event.strength)?;
            }
            output.push_str("]}");
        }
        #[cfg(feature = "temporal-trace-diagnostics")]
        if dense_requested {
            let capture = &selections.ok_or("missing dense trace")?[index].dense;
            let bins = record.range.end_frame.div_ceil(256) - record.range.first_frame / 256;
            if !capture.complete || u64::from(capture.count) != bins {
                return Err("incomplete dense trace".into());
            }
            write!(&mut output, ",\"dense_trace\":{{\"format\":\"libapta-dense-novelty-1\",\"complete\":true,\"count\":{},\"first_bin\":{},\"bin_frames\":256,\"last_bin_frames\":{},\"startup_bins\":{},\"values\":[", capture.count, record.range.first_frame / 256, (record.range.end_frame - 1) % 256 + 1, capture.startup_bins)?;
            for (bin, value) in capture.values[..usize::from(capture.count)]
                .iter()
                .enumerate()
            {
                if !value.is_finite() || !(0.0..=1.0).contains(value) {
                    return Err("invalid dense novelty value".into());
                }
                if bin != 0 {
                    output.push(',');
                }
                // Serialize the exact f32 as f64, so a host JSON decoder does
                // not interpret a shortest-f32 decimal as a different f64.
                write!(&mut output, "{}", f64::from(*value))?;
            }
            output.push_str("]}");
        }
        output.push('}');
    }
    output.push_str("]}\n");
    Ok(output)
}

fn run() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 4 {
        return Err(USAGE.into());
    }
    let mode = match args[2].to_str() {
        Some("--mode=coarse") => "coarse",
        Some("--mode=attack") => "attack",
        Some("--mode=novelty") => "novelty",
        Some("--mode=novelty-trace") => "novelty-trace",
        Some("--mode=novelty-dense") => "novelty-dense",
        _ => return Err(USAGE.into()),
    };
    let frames: u64 = args[3]
        .to_str()
        .and_then(|s| s.strip_prefix("--frames="))
        .ok_or(USAGE)?
        .parse()?;
    if frames == 0 {
        return Err(USAGE.into());
    }
    let output = diagnose(Path::new(&args[0]), mode, frames)?;
    OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&args[1])?
        .write_all(output.as_bytes())?;
    Ok(())
}
fn main() {
    if let Err(error) = run() {
        eprintln!("history diagnostic failed: {error}");
        std::process::exit(1);
    }
}
