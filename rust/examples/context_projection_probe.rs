// SPDX-License-Identifier: Apache-2.0
//! Opt-in P2 generated builder/reader probe; no detector or product dispatch.
#![forbid(unsafe_code)]
use libapta::{builder, container, result, *};
use std::{error::Error, io::Write, str::FromStr};

fn number<T: FromStr>(tokens: &mut std::str::SplitWhitespace<'_>) -> Result<T, Box<dyn Error>> {
    tokens
        .next()
        .ok_or("missing integer")?
        .parse()
        .map_err(|_| "invalid integer".into())
}

fn emit(view: result::ResultView<'_>) -> Result<(), Box<dyn Error>> {
    let grid = view.global_grid.ok_or("missing grid")?;
    let range = grid.applicability_range();
    print!(
        "{{\"range\":[{},{}],\"points\":[",
        range.first_frame, range.end_frame
    );
    let mut points = Vec::new();
    if grid.representation() == GridRepresentation::Explicit {
        for i in 0..grid.beat_count() {
            points.push(grid.beat(i).ok_or("missing beat")?);
        }
    } else {
        for i in 0..grid.segment_count() {
            let s = grid.segment(i).ok_or("missing segment")?;
            for j in 0..s.beat_count {
                points.push(s.beat_at(j)?.ok_or("missing owned beat")?);
            }
        }
    }
    for (i, b) in points.iter().enumerate() {
        if i != 0 {
            print!(",");
        }
        print!(
            "[{},{},{}]",
            b.position.whole_frame, b.position.fraction_q32, b.ordinal
        );
    }
    print!("],\"meter\":");
    if let Some(m) = view.meter {
        let s = m.segment(0).ok_or("missing meter segment")?;
        print!(
            "[{},{},{},{}]",
            s.numerator, s.denominator, s.downbeat_frame, s.downbeat_ordinal
        );
    } else {
        print!("null");
    }
    println!("}}");
    Ok(())
}

fn run() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() < 3 {
        return Err("read/build/payload arguments required".into());
    }
    if args[1] == "read" || args[1] == "payload" {
        let bytes = std::fs::read(&args[2])?;
        let view = result::parse(&bytes, result::Limits::default())?;
        if args[1] == "read" {
            return emit(view);
        }
        let container = container::Container::parse(&bytes, Default::default())?;
        print!("{{");
        let mut first = true;
        for i in 0..container.section_count() {
            let s = container.section(i).ok_or("missing section")?;
            if ![b"GGRD", b"REVN", b"MTRD"].contains(&&s.fourcc) {
                continue;
            }
            if !first {
                print!(",");
            }
            first = false;
            print!("\"{}\":\"", std::str::from_utf8(&s.fourcc)?);
            for b in s.payload {
                print!("{b:02x}");
            }
            print!("\"");
        }
        println!("}}");
        return Ok(());
    }
    if args[1] != "build" || args.len() != 4 {
        return Err("invalid mode".into());
    }
    let text = std::fs::read_to_string(&args[2])?;
    let mut tokens = text.split_whitespace();
    let total: u64 = number(&mut tokens)?;
    let first = number(&mut tokens)?;
    let end = number(&mut tokens)?;
    let anchor = number(&mut tokens)?;
    let ordinal = number(&mut tokens)?;
    let tempo = number(&mut tokens)?;
    let whole = number(&mut tokens)?;
    let fraction = number(&mut tokens)?;
    let count: u32 = number(&mut tokens)?;
    let hasmeter: u32 = number(&mut tokens)?;
    let numerator = number(&mut tokens)?;
    let mut dbframe = number(&mut tokens)?;
    let mut dbordinal = number(&mut tokens)?;
    let repr: u32 = number(&mut tokens)?;
    let defect: u32 = number(&mut tokens)?;
    if total > 1440000 || !(first < end && end <= total) || !(1..=3072).contains(&count) {
        return Err("fixture capacity/geometry".into());
    }
    let representation = match repr {
        1 => GridRepresentation::Segments,
        2 => GridRepresentation::Explicit,
        3 => GridRepresentation::Hybrid,
        _ => return Err("invalid representation".into()),
    };
    let mut beats = Vec::new();
    for _ in 0..count {
        beats.push(Beat {
            position: FractionalFrame {
                whole_frame: number(&mut tokens)?,
                fraction_q32: number(&mut tokens)?,
            },
            ordinal: number(&mut tokens)?,
            revision: 1,
            flags: 0,
            confidence: 0,
        });
    }
    if tokens.next().is_some() {
        return Err("trailing fixture token".into());
    }
    if defect == 4 {
        beats[0].position.fraction_q32 ^= 1;
    }
    if defect == 1 {
        dbframe += 1;
    }
    if defect == 2 {
        dbordinal += 1;
    }
    if defect == 3 {
        dbframe = anchor;
        dbordinal = ordinal;
    }
    let range = FrameRange {
        first_frame: first,
        end_frame: end,
    };
    let segments = [GridSegment {
        applicability_range: range,
        anchor_position: FractionalFrame {
            whole_frame: anchor,
            fraction_q32: 0,
        },
        anchor_ordinal: ordinal,
        frames_per_beat: FramePeriod {
            whole_frames: whole,
            fraction_q32: fraction,
        },
        beat_count: count,
        nominal_tempo_millibpm: tempo,
        confidence: 0,
        state: FeatureState::Final,
        flags: 0,
        segment_id: 1,
        revision: 1,
    }];
    let grid = GlobalGrid {
        state: FeatureState::Final,
        confidence: 0,
        flags: 0,
        representation,
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_range: range,
        segments: if repr == 2 { &[] } else { &segments },
        beats: if repr == 1 { &[] } else { &beats },
    };
    let ms = [MeterSegment {
        first_frame: first,
        end_frame: end,
        downbeat_frame: dbframe,
        downbeat_ordinal: dbordinal,
        numerator,
        denominator: 4,
        state: FeatureState::Final,
        confidence: 0,
        segment_id: 1,
    }];
    let meter = (hasmeter != 0).then_some(Meter {
        state: FeatureState::Final,
        confidence: 0,
        numerator,
        denominator: 4,
        downbeat_frame: dbframe,
        downbeat_ordinal: dbordinal,
        segments: &ms,
    });
    let candidates = [TempoCandidate {
        tempo_millibpm: tempo,
        score: 1,
        confidence: 0,
        relation_to_selected: 0,
        flags: 0,
    }];
    let spans = [WaveformSpan {
        first_frame: 0,
        end_frame: total,
        first_column_index: 0,
        column_count: 1,
        data_column_offset: 0,
    }];
    let columns = [WaveformColumn {
        flags: 1,
        ..Default::default()
    }];
    let input = result::ResultInput {
        source: SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(total),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        overview: WaveformOverview {
            level_id: 0,
            frames_per_column: total as u32,
            origin_frame: 0,
            logical_column_count: 1,
            state: FeatureState::Final,
            spans: &spans,
            columns: &columns,
        },
        tiles: &[],
        metadata: None,
        tempo: Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Final,
                confidence: 0,
                flags: 0,
                tempo_millibpm: tempo,
                candidate_set_id: 0,
                evidence_range: range,
                applicability_range: range,
            },
            candidates: &candidates,
        }),
        local_grid: None,
        global_grid: Some(grid),
        revision: Some(GridRevision {
            state: RevisionState::Applied,
            confidence: 0,
            flags: 0,
            revision_id: 1,
            previous_revision_id: 0,
            proposed_representation: representation,
            proposed_segment_count: grid.segments.len() as u32,
            proposed_beat_count: grid.beats.len() as u32,
            affected_range: range,
        }),
        key: None,
        meter,
        quality: &[],
    };
    let size = match builder::validate(&input, Default::default()) {
        Ok(n) => n,
        Err(error) => {
            println!("{{\"rejected\":true,\"error\":\"{error:?}\"}}");
            return Ok(());
        }
    };
    let mut bytes = vec![0; size];
    let view = builder::finalize(&input, &mut bytes, Default::default())?;
    emit(view)?;
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&args[3])?
        .write_all(&bytes)?;
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    run()
}
