// SPDX-License-Identifier: Apache-2.0
//! Host-only reference-path feasibility. No detector or production publication.
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

fn native_accepts(
    g: GlobalGrid<'_>,
    rate: u32,
    total: u64,
    tempo: u32,
    meters: &[MeterSegment],
) -> bool {
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
            source_name: "segmented reference witness",
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
        meter: meters.first().map(|s| Meter {
            state: FeatureState::Provisional,
            confidence: 0,
            numerator: s.numerator,
            denominator: s.denominator,
            downbeat_frame: s.downbeat_frame,
            downbeat_ordinal: s.downbeat_ordinal,
            segments: meters,
        }),
        quality: &[],
    };
    native_validation::validate(&input, NativeLimits::default()).is_ok()
}

fn builder_accepts(
    g: GlobalGrid<'_>,
    rate: u32,
    total: u64,
    tempo: u32,
    meters: &[MeterSegment],
) -> bool {
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
        meter: meters.first().map(|s| Meter {
            state: FeatureState::Provisional,
            confidence: 0,
            numerator: s.numerator,
            denominator: s.denominator,
            downbeat_frame: s.downbeat_frame,
            downbeat_ordinal: s.downbeat_ordinal,
            segments: meters,
        }),
        quality: &[],
    };
    libapta::builder::validate(&input, result::Limits::default()).is_ok()
}

#[derive(Clone, Copy)]
struct Reference {
    frame: u64,
    twice_period: u64,
}

fn make_grid<'a>(range: FrameRange, segments: &'a [GridSegment]) -> GlobalGrid<'a> {
    GlobalGrid {
        state: FeatureState::Provisional,
        confidence: 0,
        flags: 3,
        representation: GridRepresentation::Segments,
        requested_range: range,
        evidence_range: range,
        applicability_range: range,
        coverage_range: range,
        segments,
        beats: &[],
    }
}

fn build(
    total: u64,
    references: &[Reference],
    pieces: &[(usize, usize, u32)],
) -> Result<(Vec<GridSegment>, Vec<Beat>), Box<dyn Error>> {
    let mut anchors = Vec::new();
    for &(start, _, _) in pieces {
        anchors.push(references.get(start).ok_or("invalid start ordinal")?.frame);
    }
    build_with_anchors(total, references, pieces, &anchors)
}

fn build_with_anchors(
    total: u64,
    references: &[Reference],
    pieces: &[(usize, usize, u32)],
    anchors: &[u64],
) -> Result<(Vec<GridSegment>, Vec<Beat>), Box<dyn Error>> {
    if total == 0
        || total > 1_440_000
        || references.len() < 2
        || references.len() > 3072
        || pieces.is_empty()
        || pieces.len() > references.len()
        || anchors.len() != pieces.len()
        || anchors.iter().any(|&a| a >= total)
        || references
            .iter()
            .any(|r| r.frame >= total || r.twice_period == 0)
        || references.windows(2).any(|p| p[0].frame >= p[1].frame)
    {
        return Err("invalid bounded reference geometry".into());
    }
    let mut boundaries = vec![references[0].frame];
    boundaries.extend(
        references
            .windows(2)
            .map(|p| (p[0].frame + p[1].frame) / 2 + 1),
    );
    boundaries.push(references.last().ok_or("empty reference")?.frame + 1);
    let mut segments = Vec::new();
    let mut points = Vec::new();
    let mut next = 0;
    for (&(start, end, tempo), &anchor_frame) in pieces.iter().zip(anchors) {
        if start != next
            || end <= start
            || end > references.len()
            || !(40000..=300000).contains(&tempo)
        {
            return Err("pieces changed event partition or tempo range".into());
        }
        let period = ((48000_u128 * 60000) << 32) / u128::from(tempo);
        let anchor = u128::from(anchor_frame) << 32;
        if (anchor >= period && anchor - period >= u128::from(boundaries[start]) << 32)
            || anchor + (end - start) as u128 * period < u128::from(boundaries[end]) << 32
        {
            return Err("declared count hides a natural in-range point".into());
        }
        let segment = GridSegment {
            applicability_range: FrameRange {
                first_frame: boundaries[start],
                end_frame: boundaries[end],
            },
            anchor_position: FractionalFrame {
                whole_frame: anchor_frame,
                fraction_q32: 0,
            },
            anchor_ordinal: start as i64,
            frames_per_beat: FramePeriod {
                whole_frames: (period >> 32) as u64,
                fraction_q32: period as u32,
            },
            beat_count: (end - start) as u32,
            nominal_tempo_millibpm: tempo,
            confidence: 0,
            state: FeatureState::Provisional,
            flags: 3,
            segment_id: segments.len() as u32 + 1,
            revision: 1,
        };
        for i in 0..segment.beat_count {
            points.push(segment.beat_at(i)?.ok_or("missing beat")?);
        }
        segments.push(segment);
        next = end;
    }
    if next != references.len() || points.len() != references.len() {
        return Err("reference tail lost".into());
    }
    for (i, (point, reference)) in points.iter().zip(references).enumerate() {
        let position = (u128::from(point.position.whole_frame) << 32)
            | u128::from(point.position.fraction_q32);
        let target = u128::from(reference.frame) << 32;
        let local_twice = u128::from(reference.twice_period) << 32;
        let segment = segments
            .iter()
            .find(|s| {
                point.ordinal >= s.anchor_ordinal
                    && point.ordinal < s.anchor_ordinal + i64::from(s.beat_count)
            })
            .ok_or("missing ordinal")?;
        let period = (u128::from(segment.frames_per_beat.whole_frames) << 32)
            | u128::from(segment.frames_per_beat.fraction_q32);
        if point.ordinal != i as i64
            || position < u128::from(boundaries[i]) << 32
            || position >= u128::from(boundaries[i + 1]) << 32
            || (i + 1 < references.len()
                && position * 2 > u128::from(reference.frame + references[i + 1].frame) << 32)
            || position.abs_diff(target) * 20 + (20_u128 << 32) > local_twice
            || (period * 2).abs_diff(local_twice) * 100 + (200_u128 << 32) > local_twice
        {
            return Err("reference phase/period/ownership/ordinal guard failed".into());
        }
        if let Some(successor) = points.get(i + 1) {
            let successor = (u128::from(successor.position.whole_frame) << 32)
                | u128::from(successor.position.fraction_q32);
            if successor <= position
                || ((successor - position) * 2).abs_diff(local_twice) * 100 + (200_u128 << 32)
                    > local_twice
            {
                return Err("actual outgoing interval guard failed".into());
            }
        }
    }
    Ok((segments, points))
}

fn wire_roundtrip(
    g: &GlobalGrid<'_>,
    total: u64,
    expected: &[Beat],
) -> Result<bool, Box<dyn Error>> {
    let Ok(size) = grid::validate_external(g, Some(total), true) else {
        return Ok(false);
    };
    let mut bytes = vec![0; size];
    grid::write_payload(g, true, &mut bytes)?;
    let parsed = grid::GridPayload::parse(&bytes, true, grid::GridOptions::default())?;
    let mut decoded = Vec::new();
    for i in 0..parsed.segment_count() {
        let s = parsed.segment(i).ok_or("missing decoded segment")?;
        for j in 0..s.beat_count {
            decoded.push(s.beat_at(j)?.ok_or("missing decoded beat")?);
        }
    }
    if decoded.len() != expected.len()
        || decoded
            .iter()
            .zip(expected)
            .any(|(a, b)| a.position != b.position || a.ordinal != b.ordinal)
    {
        return Err("wire changed Q32 positions/ordinals".into());
    }
    let mut canonical = vec![0; size];
    parsed.write_canonical(&mut canonical)?;
    if canonical != bytes {
        return Err("noncanonical grid".into());
    }
    Ok(true)
}

fn meter_roundtrip(meters: &[MeterSegment], total: u64, points: &[Beat]) -> bool {
    let Some(first) = meters.first() else {
        return true;
    };
    let m = Meter {
        state: FeatureState::Provisional,
        confidence: 0,
        numerator: first.numerator,
        denominator: first.denominator,
        downbeat_frame: first.downbeat_frame,
        downbeat_ordinal: first.downbeat_ordinal,
        segments: meters,
    };
    let mut bytes = vec![0; 48 + 56 * meters.len()];
    if dj::write_meter(m, Some(total), true, &mut bytes).is_err() {
        return false;
    }
    let Ok(parsed) = dj::parse_meter(&bytes, Some(total), true, meters.len()) else {
        return false;
    };
    if parsed
        .validate_grid(|frame, ordinal| {
            points
                .iter()
                .any(|p| p.ordinal == ordinal && p.position.whole_frame == frame)
        })
        .is_err()
    {
        return false;
    }
    meters
        .iter()
        .enumerate()
        .all(|(i, m)| parsed.segment(i) == Some(*m))
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
    let guided = match tokens.next() {
        Some("SEGMENT_REF_V1") => false,
        Some("SEGMENT_GUIDED_V1") => true,
        _ => return Err("unknown format".into()),
    };
    let total = number::<u64>(&mut tokens)?;
    let count = number::<usize>(&mut tokens)?;
    let pieces = number::<usize>(&mut tokens)?;
    let meter_count = number::<usize>(&mut tokens)?;
    if count > 3072 || pieces > count || meter_count > 8 {
        return Err("request cap".into());
    }
    let mut references = Vec::new();
    for _ in 0..count {
        references.push(Reference {
            frame: number(&mut tokens)?,
            twice_period: number(&mut tokens)?,
        });
    }
    let mut partition = Vec::new();
    let mut anchors = Vec::new();
    for _ in 0..pieces {
        partition.push((
            number(&mut tokens)?,
            number(&mut tokens)?,
            number(&mut tokens)?,
        ));
        if guided {
            anchors.push(number::<u64>(&mut tokens)?);
        }
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
    let (segments, points) = if guided {
        build_with_anchors(total, &references, &partition, &anchors)?
    } else {
        build(total, &references, &partition)?
    };
    let range = FrameRange {
        first_frame: references[0].frame,
        end_frame: references.last().ok_or("empty reference")?.frame + 1,
    };
    let g = make_grid(range, &segments);
    let tempo = segments[0].nominal_tempo_millibpm;
    let native = native_accepts(g, 48000, total, tempo, &meters);
    let builder = builder_accepts(g, 48000, total, tempo, &meters);
    let wire = wire_roundtrip(&g, total, &points)?;
    let meter = meter_roundtrip(&meters, total, &points);
    print!("{{\"native_accepts\":{native},\"builder_accepts\":{builder},\"wire_roundtrip\":{wire},\"meter_roundtrip\":{meter},\"meter_records\":{},\"points\":[",meters.len());
    for (i, p) in points.iter().enumerate() {
        if i != 0 {
            print!(",");
        }
        print!(
            "[{},{},{}]",
            p.position.whole_frame, p.position.fraction_q32, p.ordinal
        );
    }
    println!("]}}");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn references(count: usize) -> Vec<Reference> {
        (0..count)
            .map(|i| Reference {
                frame: i as u64 * 24000,
                twice_period: 48000,
            })
            .collect()
    }
    #[test]
    fn explicit_integer_anchor_preserves_fractional_grid_and_meter() {
        let refs = references(12);
        let (s, p) = build_with_anchors(300000, &refs, &[(0, 12, 120001)], &[1]).unwrap();
        assert_eq!(p[0].position.whole_frame, 1);
        assert_ne!(p[4].position.fraction_q32, 0);
        let g = make_grid(
            FrameRange {
                first_frame: 0,
                end_frame: 264001,
            },
            &s,
        );
        let meters = [MeterSegment {
            first_frame: 0,
            end_frame: 264001,
            downbeat_frame: 1,
            downbeat_ordinal: 0,
            numerator: 4,
            denominator: 4,
            state: FeatureState::Provisional,
            confidence: 0,
            segment_id: 1,
        }];
        assert!(native_accepts(g, 48000, 300000, 120001, &meters));
        assert!(builder_accepts(g, 48000, 300000, 120001, &meters));
        assert!(wire_roundtrip(&g, 300000, &p).unwrap());
        assert!(meter_roundtrip(&meters, 300000, &p));
    }
    #[test]
    fn explicit_anchor_cannot_escape_reference_ownership_or_source() {
        let refs = references(12);
        for anchors in [&[][..], &[24000][..], &[300000][..]] {
            assert!(build_with_anchors(300000, &refs, &[(0, 12, 120000)], anchors).is_err());
        }
    }
    #[test]
    fn guided_junction_checks_actual_interval_not_only_own_period() {
        let refs = references(16);
        assert!(build_with_anchors(
            400000,
            &refs,
            &[(0, 8, 120000), (8, 16, 120000)],
            &[0, 191500]
        )
        .is_err());
    }
    #[test]
    fn explicit_reference_anchors_reproduce_legacy_points() {
        let refs = references(12);
        let partition = [(0, 6, 120000), (6, 12, 120000)];
        let (old_s, old_p) = build(300000, &refs, &partition).unwrap();
        let (new_s, new_p) = build_with_anchors(300000, &refs, &partition, &[0, 144000]).unwrap();
        assert_eq!(old_s.len(), new_s.len());
        assert!(old_p
            .iter()
            .zip(&new_p)
            .all(|(a, b)| a.position == b.position && a.ordinal == b.ordinal));
    }
    #[test]
    fn complete_native_builder_wire_and_meter_positive_control() {
        let refs = references(12);
        let (segments, points) = build(300000, &refs, &[(0, 12, 120000)]).unwrap();
        let g = make_grid(
            FrameRange {
                first_frame: 0,
                end_frame: 264001,
            },
            &segments,
        );
        let meters = [MeterSegment {
            first_frame: 0,
            end_frame: 264001,
            downbeat_frame: 0,
            downbeat_ordinal: 0,
            numerator: 4,
            denominator: 4,
            state: FeatureState::Provisional,
            confidence: 0,
            segment_id: 1,
        }];
        assert!(native_accepts(g, 48000, 300000, 120000, &meters));
        assert!(builder_accepts(g, 48000, 300000, 120000, &meters));
        assert!(wire_roundtrip(&g, 300000, &points).unwrap());
        assert!(meter_roundtrip(&meters, 300000, &points));
    }
    #[test]
    fn changing_periods_preserve_junction_and_original_ordinals() {
        let mut refs = references(8);
        refs.extend((0..8).map(|i| Reference {
            frame: 192000 + i * 30000,
            twice_period: 60000,
        }));
        let (s, p) = build(500000, &refs, &[(0, 8, 120000), (8, 16, 96000)]).unwrap();
        let g = make_grid(
            FrameRange {
                first_frame: 0,
                end_frame: 402001,
            },
            &s,
        );
        assert!(native_accepts(g, 48000, 500000, 120000, &[]));
        assert!(builder_accepts(g, 48000, 500000, 120000, &[]));
        assert!(wire_roundtrip(&g, 500000, &p).unwrap());
        assert_eq!(p[8].ordinal, 8);
    }
    #[test]
    fn nine_segments_are_native_only_without_prefix_truncation() {
        let refs = references(9);
        let partition: Vec<_> = (0..9).map(|i| (i, i + 1, 120000)).collect();
        let (s, p) = build(300000, &refs, &partition).unwrap();
        let g = make_grid(
            FrameRange {
                first_frame: 0,
                end_frame: 192001,
            },
            &s,
        );
        assert_eq!(p.len(), 9);
        assert!(native_accepts(g, 48000, 300000, 120000, &[]));
        assert!(!builder_accepts(g, 48000, 300000, 120000, &[]));
        assert!(!wire_roundtrip(&g, 300000, &p).unwrap());
    }
    #[test]
    fn wrong_same_grid_meter_coordinate_and_ordinal_reject_complete_graph() {
        let refs = references(12);
        let (s, p) = build(300000, &refs, &[(0, 12, 120000)]).unwrap();
        let g = make_grid(
            FrameRange {
                first_frame: 0,
                end_frame: 264001,
            },
            &s,
        );
        for (frame, ordinal) in [(1, 0), (0, 1)] {
            let meters = [MeterSegment {
                first_frame: 0,
                end_frame: 264001,
                downbeat_frame: frame,
                downbeat_ordinal: ordinal,
                numerator: 4,
                denominator: 4,
                state: FeatureState::Provisional,
                confidence: 0,
                segment_id: 1,
            }];
            assert!(!native_accepts(g, 48000, 300000, 120000, &meters));
            assert!(!builder_accepts(g, 48000, 300000, 120000, &meters));
            assert!(!meter_roundtrip(&meters, 300000, &p));
        }
    }
    #[test]
    fn lost_or_duplicated_reference_partition_rejects() {
        let refs = references(12);
        assert!(build(300000, &refs, &[(0, 6, 120000)]).is_err());
        assert!(build(300000, &refs, &[(0, 6, 120000), (5, 12, 120000)]).is_err());
    }
    #[test]
    fn unsupported_tempo_and_wrong_reference_period_reject_without_clamping() {
        let refs = references(12);
        assert!(build(300000, &refs, &[(0, 12, 39999)]).is_err());
        assert!(build(300000, &refs, &[(0, 12, 121000)]).is_ok());
        assert!(build(300000, &refs, &[(0, 12, 122000)]).is_err());
    }

    #[test]
    fn fractional_q32_meter_binding_passes_complete_native_and_wire_validation() {
        let period = ((48000_u128 * 60000) << 32) / 123457;
        let refs: Vec<_> = (0..16)
            .map(|i| Reference {
                frame: 10000 + ((i * period + (1 << 31)) >> 32) as u64,
                twice_period: 46656,
            })
            .collect();
        let (s, p) = build(400000, &refs, &[(0, 16, 123457)]).unwrap();
        assert_ne!(p[4].position.fraction_q32, 0);
        let range = FrameRange {
            first_frame: 10000,
            end_frame: refs[15].frame + 1,
        };
        let g = make_grid(range, &s);
        let meters = [MeterSegment {
            first_frame: range.first_frame,
            end_frame: range.end_frame,
            downbeat_frame: p[4].position.whole_frame,
            downbeat_ordinal: 4,
            numerator: 4,
            denominator: 4,
            state: FeatureState::Provisional,
            confidence: 0,
            segment_id: 1,
        }];
        assert!(native_accepts(g, 48000, 400000, 123457, &meters));
        assert!(builder_accepts(g, 48000, 400000, 123457, &meters));
        assert!(wire_roundtrip(&g, 400000, &p).unwrap());
        assert!(meter_roundtrip(&meters, 400000, &p));
    }
}
