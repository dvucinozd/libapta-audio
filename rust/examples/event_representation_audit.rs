// SPDX-License-Identifier: Apache-2.0
//! Host-only saved-event representation diagnostic. No detector or publication.
#![forbid(unsafe_code)]
use libapta::{grid, result, *};
use std::{error::Error, io::Read, str::FromStr};

fn number<T: FromStr>(tokens: &mut std::str::SplitWhitespace<'_>) -> Result<T, Box<dyn Error>> {
    tokens
        .next()
        .ok_or("missing field")?
        .parse()
        .map_err(|_| "invalid integer".into())
}

fn segments(beats: &[Beat], rate: u32) -> Vec<GridSegment> {
    let mut result = Vec::new();
    let mut start = 0;
    while start + 1 < beats.len() {
        let period = beats[start + 1].position.whole_frame - beats[start].position.whole_frame;
        let mut end = start + 1;
        while end + 1 < beats.len()
            && beats[end + 1].position.whole_frame - beats[end].position.whole_frame == period
        {
            end += 1;
        }
        let final_run = end + 1 == beats.len();
        result.push(GridSegment {
            applicability_range: FrameRange {
                first_frame: beats[start].position.whole_frame,
                end_frame: beats[end].position.whole_frame + u64::from(final_run),
            },
            anchor_position: beats[start].position,
            anchor_ordinal: start as i64,
            frames_per_beat: FramePeriod {
                whole_frames: period,
                fraction_q32: 0,
            },
            beat_count: (end - start + usize::from(final_run)) as u32,
            nominal_tempo_millibpm: ((u64::from(rate) * 60000 + period / 2) / period) as u32,
            confidence: 0,
            state: FeatureState::Provisional,
            flags: 3,
            segment_id: result.len() as u32 + 1,
            revision: 1,
        });
        start = end;
    }
    result
}

fn grid<'a>(range: FrameRange, segments: &'a [GridSegment], beats: &'a [Beat]) -> GlobalGrid<'a> {
    GlobalGrid {
        state: FeatureState::Provisional,
        confidence: 0,
        flags: 3,
        representation: if segments.is_empty() {
            GridRepresentation::Explicit
        } else {
            GridRepresentation::Segments
        },
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_range: range,
        segments,
        beats,
    }
}

fn roundtrip(g: &GlobalGrid<'_>, total: u64, expected: &[Beat]) -> Result<bool, Box<dyn Error>> {
    let Ok(size) = grid::validate_external(g, Some(total), true) else {
        return Ok(false);
    };
    let mut bytes = vec![0; size];
    grid::write_payload(g, true, &mut bytes)?;
    let payload = grid::GridPayload::parse(&bytes, true, grid::GridOptions::default())?;
    let mut decoded = Vec::new();
    for i in 0..payload.segment_count() {
        let segment = payload.segment(i).ok_or("segment disappeared")?;
        for j in 0..segment.beat_count {
            decoded.push(segment.beat_at(j)?.ok_or("segment beat disappeared")?);
        }
    }
    for i in 0..payload.beat_count() {
        decoded.push(payload.beat(i).ok_or("beat disappeared")?);
    }
    if decoded.len() != expected.len()
        || decoded
            .iter()
            .zip(expected)
            .any(|(a, b)| a.position != b.position || a.ordinal != b.ordinal)
    {
        return Err("roundtrip changed event coordinates/ordinals".into());
    }
    let mut canonical = vec![0; size];
    payload.write_canonical(&mut canonical)?;
    if canonical != bytes {
        return Err("noncanonical roundtrip".into());
    }
    Ok(true)
}

fn native_accepts(g: GlobalGrid<'_>, rate: u32, total: u64, tempo: u32) -> bool {
    let coverage = [g.coverage_range];
    let candidates = [TempoCandidate {
        tempo_millibpm: tempo,
        score: 1,
        confidence: 0,
        relation_to_selected: 0,
        flags: 0,
    }];
    let input = NativeResultInput {
        source: SourceInfo {
            sample_rate: rate,
            channel_count: 2,
            channel_layout: 2,
            total_frames: Some(total),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        info: NativeResultInfo {
            session_state: ResultSessionState::AcceptingInput,
            ..Default::default()
        },
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "event audit",
            source_version: "1",
        },
        overview: None,
        detail: None,
        metadata: None,
        tempo: Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Provisional,
                confidence: 0,
                flags: 0,
                tempo_millibpm: tempo,
                candidate_set_id: 0,
                evidence_range: g.evidence_range,
                applicability_range: g.applicability_range,
            },
            candidates: &candidates,
        }),
        local_grid: None,
        global_grid: Some(NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: g.representation,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: &coverage,
            segments: g.segments,
            beats: g.beats,
        }),
        revision: Some(GridRevision {
            state: RevisionState::Pending,
            confidence: 0,
            flags: 2,
            revision_id: 1,
            previous_revision_id: 0,
            proposed_representation: g.representation,
            proposed_segment_count: g.segments.len() as u32,
            proposed_beat_count: g.beats.len() as u32,
            affected_range: g.applicability_range,
        }),
        key: None,
        meter: None,
        quality: &[],
    };
    native_validation::validate(&input, NativeLimits::default()).is_ok()
}

fn builder_accepts(g: GlobalGrid<'_>, rate: u32, total: u64, tempo: u32) -> bool {
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
    let candidates = [TempoCandidate {
        tempo_millibpm: tempo,
        score: 1,
        confidence: 0,
        relation_to_selected: 0,
        flags: 0,
    }];
    let input = result::ResultInput {
        source: SourceInfo {
            sample_rate: rate,
            channel_count: 2,
            channel_layout: 2,
            total_frames: Some(total),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        overview: WaveformOverview {
            level_id: 0,
            frames_per_column: total as u32,
            origin_frame: 0,
            logical_column_count: 1,
            state: FeatureState::Provisional,
            spans: &spans,
            columns: &columns,
        },
        tiles: &[],
        metadata: None,
        tempo: Some(TempoView {
            selected: TempoValue {
                state: FeatureState::Provisional,
                confidence: 0,
                flags: 0,
                tempo_millibpm: tempo,
                candidate_set_id: 0,
                evidence_range: g.evidence_range,
                applicability_range: g.applicability_range,
            },
            candidates: &candidates,
        }),
        local_grid: None,
        global_grid: Some(g),
        revision: Some(GridRevision {
            state: RevisionState::Pending,
            confidence: 0,
            flags: 2,
            revision_id: 1,
            previous_revision_id: 0,
            proposed_representation: g.representation,
            proposed_segment_count: g.segments.len() as u32,
            proposed_beat_count: g.beats.len() as u32,
            affected_range: g.applicability_range,
        }),
        key: None,
        meter: None,
        quality: &[],
    };
    libapta::builder::validate(&input, result::Limits::default()).is_ok()
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut text = String::new();
    std::io::stdin()
        .take(1024 * 1024 + 1)
        .read_to_string(&mut text)?;
    if text.len() > 1024 * 1024 {
        return Err("request too large".into());
    }
    let mut tokens = text.split_whitespace();
    if tokens.next() != Some("EVENT_REPR_V1") {
        return Err("unknown format".into());
    }
    let rate: u32 = number(&mut tokens)?;
    let total: u64 = number(&mut tokens)?;
    let tempo: u32 = number(&mut tokens)?;
    let count: usize = number(&mut tokens)?;
    let downbeats: usize = number(&mut tokens)?;
    let meter_count: usize = number(&mut tokens)?;
    if rate != 48000
        || total == 0
        || total > u64::from(u32::MAX)
        || count == 0
        || count > grid::MAX_BEATS
        || downbeats > count
        || meter_count > 6
    {
        return Err("unsupported geometry/count".into());
    }
    let mut beats = Vec::with_capacity(count);
    for i in 0..count {
        let frame: u64 = number(&mut tokens)?;
        if frame >= total
            || beats
                .last()
                .is_some_and(|b: &Beat| b.position.whole_frame >= frame)
        {
            return Err("invalid event order".into());
        }
        beats.push(Beat {
            position: FractionalFrame {
                whole_frame: frame,
                fraction_q32: 0,
            },
            ordinal: i as i64,
            revision: 1,
            flags: 3,
            confidence: 0,
        });
    }
    let mut db = Vec::new();
    for _ in 0..downbeats {
        db.push((number::<i64>(&mut tokens)?, number::<u64>(&mut tokens)?));
    }
    let mut meters = Vec::new();
    for i in 0..meter_count {
        meters.push(MeterSegment {
            first_frame: number(&mut tokens)?,
            end_frame: number(&mut tokens)?,
            downbeat_frame: number(&mut tokens)?,
            downbeat_ordinal: number(&mut tokens)?,
            numerator: number(&mut tokens)?,
            denominator: 4,
            state: FeatureState::Provisional,
            confidence: 0,
            segment_id: i as u32 + 1,
        });
    }
    if tokens.next().is_some() {
        return Err("trailing fields".into());
    }
    let range = FrameRange {
        first_frame: beats[0].position.whole_frame,
        end_frame: beats[count - 1].position.whole_frame + 1,
    };
    let explicit = grid(range, &[], &beats);
    let segments = segments(&beats, rate);
    if beats.len() >= 2 {
        let mut expanded = Vec::new();
        for segment in &segments {
            for i in 0..segment.beat_count {
                expanded.push(segment.beat_at(i)?.ok_or("missing compressed event")?);
            }
        }
        if expanded.len() != beats.len()
            || expanded
                .iter()
                .zip(&beats)
                .any(|(a, b)| a.position != b.position || a.ordinal != b.ordinal)
        {
            return Err("unbounded exact compression changed coordinates".into());
        }
    }
    let unsupported = segments
        .iter()
        .filter(|s| !(40000..=300000).contains(&s.nominal_tempo_millibpm))
        .count();
    let segmented = grid(range, &segments, &[]);
    let explicit_wire = roundtrip(&explicit, total, &beats)?;
    let segmented_wire = if segments.is_empty() || unsupported != 0 {
        false
    } else {
        roundtrip(&segmented, total, &beats)?
    };
    let mut bytes = vec![0; grid::validate(&explicit, true)?];
    grid::write_payload(&explicit, true, &mut bytes)?;
    let payload = grid::GridPayload::parse(&bytes, true, grid::GridOptions::default())?;
    if db
        .iter()
        .any(|(ordinal, frame)| !payload.matches_downbeat(*frame, *ordinal))
    {
        return Err("foreign downbeat".into());
    }
    if meters
        .iter()
        .any(|m| !payload.matches_downbeat(m.downbeat_frame, m.downbeat_ordinal))
    {
        return Err("meter moved off the event grid".into());
    }
    if let Some(first) = meters.first() {
        let meter = Meter {
            state: FeatureState::Provisional,
            confidence: 0,
            numerator: first.numerator,
            denominator: 4,
            downbeat_frame: first.downbeat_frame,
            downbeat_ordinal: first.downbeat_ordinal,
            segments: &meters,
        };
        let mut out = vec![0; 48 + 56 * meters.len()];
        libapta::dj::write_meter(meter, Some(total), true, &mut out)?;
        let decoded = libapta::dj::parse_meter(&out, Some(total), true, meters.len())?;
        decoded.validate_grid(|frame, ordinal| payload.matches_downbeat(frame, ordinal))?;
        for (index, expected) in meters.iter().enumerate() {
            if decoded.segment(index) != Some(*expected) {
                return Err("meter roundtrip changed source binding".into());
            }
        }
    }
    let positions: Vec<u64> = beats.iter().map(|b| b.position.whole_frame).collect();
    println!("{{\"explicit_events\":{positions:?},\"required_segments\":{},\"unsupported_segment_periods\":{unsupported},\"explicit_wire_roundtrip\":{explicit_wire},\"segmented_wire_roundtrip\":{segmented_wire},\"explicit_native_accepts\":{},\"segmented_native_accepts\":{},\"explicit_builder_accepts\":{},\"segmented_builder_accepts\":{},\"meter_lookups\":{}}}",
        segments.len(),native_accepts(explicit,rate,total,tempo),unsupported == 0 && native_accepts(segmented,rate,total,tempo),
        builder_accepts(explicit,rate,total,tempo),unsupported == 0 && builder_accepts(segmented,rate,total,tempo),meters.len());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn beats(points: &[u64]) -> Vec<Beat> {
        points
            .iter()
            .enumerate()
            .map(|(i, p)| Beat {
                position: FractionalFrame {
                    whole_frame: *p,
                    fraction_q32: 0,
                },
                ordinal: i as i64,
                revision: 1,
                flags: 3,
                confidence: 0,
            })
            .collect()
    }
    #[test]
    fn exact_segments_do_not_duplicate_boundaries() {
        let b = beats(&[100, 24100, 48100, 73100, 98100]);
        let s = segments(&b, 48000);
        assert_eq!(s.len(), 2);
        assert_eq!(s[0].beat_count, 2);
        assert_eq!(s[1].beat_count, 3);
        assert!(roundtrip(
            &grid(
                FrameRange {
                    first_frame: 100,
                    end_frame: 98101
                },
                &s,
                &[]
            ),
            100000,
            &b
        )
        .unwrap());
    }
    #[test]
    fn ninth_segment_rejects_without_truncating_explicit_events() {
        let mut p = vec![0];
        for i in 0..10 {
            p.push(p.last().unwrap() + 24000 + (i % 2) * 960);
        }
        let b = beats(&p);
        let s = segments(&b, 48000);
        let r = FrameRange {
            first_frame: 0,
            end_frame: p.last().unwrap() + 1,
        };
        assert!(s.len() > 8);
        assert!(!roundtrip(&grid(r, &s, &[]), r.end_frame, &b).unwrap());
        assert!(roundtrip(&grid(r, &[], &b), r.end_frame, &b).unwrap());
    }
    #[test]
    fn dynamic_explicit_payload_and_native_validation_are_distinct() {
        let b = beats(&[0, 24000, 48960, 72960]);
        let r = FrameRange {
            first_frame: 0,
            end_frame: 72961,
        };
        let g = grid(r, &[], &b);
        assert!(roundtrip(&g, 100000, &b).unwrap());
        assert!(!native_accepts(g, 48000, 100000, 120000));
        assert!(!builder_accepts(g, 48000, 100000, 120000));
    }
    #[test]
    fn constant_control_passes_complete_native_and_builder_validation() {
        let b = beats(&[0, 24000, 48000, 72000]);
        let r = FrameRange {
            first_frame: 0,
            end_frame: 72001,
        };
        let g = grid(r, &[], &b);
        assert!(native_accepts(g, 48000, 100000, 120000));
        assert!(builder_accepts(g, 48000, 100000, 120000));
    }
}
