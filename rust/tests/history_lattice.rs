// SPDX-License-Identifier: Apache-2.0
#![cfg(feature = "experimental-s6-history")]
use libapta::{
    analysis::OnsetBin,
    global_analysis::{BIN_CAPACITY, MAX_BEATS},
    global_history::{HistoryView, WindowHistory, WindowRecord, WINDOW_FRAMES},
    history_grid::{BoundMeterSegment, HistoryGrid},
    history_meter::{
        AttackBin, MeterHistory, MeterHistoryView, MeterOutcome, MeterRecord, ATTACK_BIN_CAPACITY,
    },
    history_publication::{publish_into, Publication, PublicationKey, PublicationStorage},
    session::{
        CancellationToken, Session, SessionConfig, SessionState, WorkBudget, TOTAL_FRAMES_UNKNOWN,
    },
    waveform::NormalizedSample,
    Beat, Error, FeatureState, FractionalFrame, FramePeriod, FrameRange, GridSegment, MeterSegment,
    SourceInfo, WaveformColumn,
};

fn empty_segment() -> GridSegment {
    GridSegment {
        applicability_range: FrameRange::default(),
        anchor_position: FractionalFrame::default(),
        anchor_ordinal: 0,
        frames_per_beat: FramePeriod::default(),
        beat_count: 0,
        nominal_tempo_millibpm: 0,
        confidence: 0,
        state: FeatureState::Partial,
        flags: 0,
        segment_id: 0,
        revision: 0,
    }
}
fn empty_meter() -> BoundMeterSegment {
    BoundMeterSegment {
        meter: MeterSegment {
            first_frame: 0,
            end_frame: 0,
            downbeat_frame: 0,
            downbeat_ordinal: 0,
            numerator: 0,
            denominator: 0,
            state: FeatureState::Partial,
            confidence: 0,
            segment_id: 0,
        },
        grid_segment_id: 0,
        grid_revision_id: 0,
        downbeat_position: FractionalFrame::default(),
    }
}

#[derive(Clone)]
struct Captured {
    rate: u32,
    records: Vec<WindowRecord>,
    meter_records: Vec<MeterRecord>,
    total: u64,
    history_end: u64,
    meter_end: u64,
    history_finished: bool,
    meter_finished: bool,
    history_exhausted: bool,
    meter_exhausted: bool,
    #[cfg(feature = "experimental-s6-attack-grid")]
    selections: Vec<libapta::attack_grid::SelectionRecord>,
}
impl Captured {
    fn history(&self) -> HistoryView<'_> {
        HistoryView {
            sample_rate: self.rate,
            records: &self.records,
            observed_frames: self.total,
            inspected_frames: self.history_end,
            capacity_exhausted: self.history_exhausted,
            discontinuous_input: false,
            finished: self.history_finished,
        }
    }
    fn meter(&self) -> MeterHistoryView<'_> {
        MeterHistoryView {
            sample_rate: self.rate,
            records: &self.meter_records,
            observed_frames: self.total,
            inspected_frames: self.meter_end,
            capacity_exhausted: self.meter_exhausted,
            discontinuous_input: false,
            finished: self.meter_finished,
        }
    }
    fn key(&self, generation: u64) -> PublicationKey {
        let revision = 19;
        #[cfg(feature = "experimental-s6-attack-grid")]
        let revision = if self.selections.is_empty() {
            revision
        } else {
            20
        };
        PublicationKey {
            source: SourceInfo {
                sample_rate: self.rate,
                channel_count: 1,
                channel_layout: 1,
                total_frames: Some(self.total),
                fingerprint_kind: 1,
                fingerprint: [7; 32],
            },
            lineage: [1, 2],
            generation,
            grid_revision: revision,
            state: libapta::ResultSessionState::Completed,
        }
    }
}

/// Generated PCM only. Accent phase is specified independently of the analyzer.
fn run(
    rate: u32,
    numerator: usize,
    phase: usize,
    unknown: bool,
    timed: bool,
    capacity: usize,
) -> Captured {
    run_fixture(
        Fixture {
            rate,
            numerator,
            phase,
            period: if rate == 8000 { 4096 } else { 16384 },
            total: 2 * WINDOW_FRAMES as usize + 17,
            origin: 0,
            jitter: false,
            shape: 0,
        },
        unknown,
        timed,
        capacity,
        false,
    )
}

#[derive(Clone, Copy)]
struct Fixture {
    rate: u32,
    numerator: usize,
    phase: usize,
    period: usize,
    total: usize,
    origin: usize,
    jitter: bool,
    shape: u8,
}
impl Fixture {
    fn sample(self, frame: usize) -> f32 {
        let approximate = frame.saturating_sub(self.origin) / self.period;
        for beat in approximate.saturating_sub(1)..=approximate + 1 {
            let jitter = if self.jitter {
                (((beat as i64 * 73 + 0x52A7) % 129) - 64) * self.period as i64 / (64 * 512)
            } else {
                0
            };
            let start = self.origin as i64 + (beat * self.period) as i64 + jitter;
            let offset = frame as i64 - start;
            if (0..64).contains(&offset) {
                let amplitude = if beat % self.numerator == self.phase {
                    0.875
                } else {
                    0.25
                };
                return if self.shape == 0 {
                    amplitude
                } else {
                    amplitude * (64 - offset) as f32 / 64.0 * (offset as f32 * 0.7).sin()
                };
            }
        }
        0.0
    }
}

fn run_fixture(
    fixture: Fixture,
    unknown: bool,
    timed: bool,
    capacity: usize,
    candidate: bool,
) -> Captured {
    let Fixture { rate, total, .. } = fixture;
    #[cfg(not(feature = "experimental-s6-attack-grid"))]
    assert!(!candidate);
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut beats = vec![Beat::default(); MAX_BEATS];
    let mut attacks = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
    let mut meter_records = vec![MeterRecord::default(); capacity];
    let mut records = vec![WindowRecord::default(); 3];
    #[cfg(feature = "experimental-s6-attack-grid")]
    let mut selections = [libapta::attack_grid::SelectionRecord::default(); 3];
    let mut queue = [NormalizedSample::default(); 4096];
    let mut columns = [WaveformColumn::default(); 17];
    let mut meter = MeterHistory::new(rate, &mut attacks, &mut meter_records).unwrap();
    let mut history = WindowHistory::new(rate, &mut records).unwrap();
    history.enable_meter(&mut meter).unwrap();
    #[cfg(feature = "experimental-s6-attack-grid")]
    if candidate {
        history.enable_attack_grid(&mut selections).unwrap();
    }
    let mut session = Session::new(
        SessionConfig {
            sample_rate: rate,
            channel_count: 1,
            total_frames: if unknown {
                TOTAL_FRAMES_UNKNOWN
            } else {
                total as u64
            },
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
    )
    .unwrap();
    session
        .enable_global_grid(false, &mut bins, &mut flux, &mut beats)
        .unwrap();
    session.enable_global_history(&mut history).unwrap();
    let cancellation = CancellationToken::new();
    let budget = WorkBudget {
        maximum_input_frames: 0,
        maximum_steps: if timed { 1 } else { 0 },
    };
    let mut block = [0.0; 4096];
    let mut accepted = 0;
    let mut retained = None;
    while accepted < total {
        let length = (total - accepted).min(block.len());
        for (offset, value) in block[..length].iter_mut().enumerate() {
            *value = fixture.sample(accepted + offset);
        }
        accepted += session.push_interleaved(&block[..length]).unwrap();
        if timed {
            let mut time = 0;
            let mut clock = || {
                time += 1000;
                time
            };
            let p = session
                .process_with_clock(budget, 1, &mut clock, &cancellation)
                .unwrap();
            assert!(p.completed_steps <= 1);
        } else {
            session.process(budget, &cancellation).unwrap();
        }
        if retained.is_none() {
            retained = session
                .global_history_meter()
                .unwrap()
                .records
                .first()
                .copied();
        }
    }
    session.finish_input().unwrap();
    for _ in 0..10000 {
        session.process(budget, &cancellation).unwrap();
        if session.state() == SessionState::Complete {
            break;
        }
    }
    assert_eq!(session.state(), SessionState::Complete);
    let h = session.global_history().unwrap();
    let m = session.global_history_meter().unwrap();
    if let Some(record) = retained {
        assert_eq!(m.records.first(), Some(&record));
    }
    Captured {
        rate,
        records: h.records.to_vec(),
        meter_records: m.records.to_vec(),
        total: total as u64,
        history_end: h.inspected_frames,
        meter_end: m.inspected_frames,
        history_finished: h.finished,
        meter_finished: m.finished,
        history_exhausted: h.capacity_exhausted,
        meter_exhausted: m.capacity_exhausted,
        #[cfg(feature = "experimental-s6-attack-grid")]
        selections: session.global_history_selection().unwrap_or(&[]).to_vec(),
    }
}

struct Bank {
    segments: [GridSegment; 4],
    ranges: [FrameRange; 4],
    meters: [BoundMeterSegment; 4],
    records: [MeterRecord; 4],
}
impl Bank {
    fn new() -> Self {
        Self {
            segments: [empty_segment(); 4],
            ranges: [FrameRange::default(); 4],
            meters: [empty_meter(); 4],
            records: [MeterRecord::default(); 4],
        }
    }
    fn storage(&mut self) -> PublicationStorage<'_> {
        PublicationStorage {
            segments: &mut self.segments,
            ranges: &mut self.ranges,
            meters: &mut self.meters,
            meter_records: &mut self.records,
        }
    }
    fn publish(
        &mut self,
        captured: &Captured,
        key: PublicationKey,
    ) -> Result<Publication<'_>, Error> {
        publish_into(key, captured.history(), captured.meter(), self.storage())
    }
}

fn verify_binding(grid: HistoryGrid<'_>, meters: &[BoundMeterSegment]) {
    for bound in meters {
        let segment = grid
            .segments
            .iter()
            .find(|s| s.segment_id == bound.grid_segment_id)
            .unwrap();
        let index = u32::try_from(bound.meter.downbeat_ordinal - segment.anchor_ordinal).unwrap();
        let beat = segment.beat_at(index).unwrap().unwrap();
        assert_eq!(beat.position, bound.downbeat_position);
        assert_eq!(beat.position.whole_frame, bound.meter.downbeat_frame);
        assert_eq!(bound.grid_revision_id, grid.revision_id);
        assert_eq!(bound.meter.state, FeatureState::Provisional);
        assert!(bound.meter.confidence < 50);
    }
}

#[test]
fn actual_pcm_lattice_path_is_coherent_but_musical_probe_is_rejected() {
    let mut verdicts = Vec::new();
    for (rate, numerator, phase) in [(8000, 4, 1), (8000, 3, 2), (48000, 4, 1)] {
        let baseline = run(rate, numerator, phase, false, false, 3);
        println!(
            "rate={rate} numerator={numerator} phase={phase} {:?}",
            baseline.meter_records
        );
        assert!(baseline.history_finished && baseline.meter_finished);
        let mut bank = Bank::new();
        let published = bank.publish(&baseline, baseline.key(1)).unwrap();
        if rate == 48000 {
            assert!(published.meters().is_empty());
            assert!(baseline.meter_records[..2]
                .iter()
                .all(|r| r.outcome == MeterOutcome::Ambiguous));
        } else {
            assert_eq!(published.meters().len(), 2);
        }
        verify_binding(published.grid(), published.meters());
        let mut correct = !published.meters().is_empty();
        for bound in published.meters() {
            correct &= usize::from(bound.meter.numerator) == numerator;
            let period = if rate == 8000 { 4096 } else { 16384 };
            // Independent PCM accent coordinate, allowing only the grid's own
            // tiny millibpm quantization error, not a nearest-beat reassignment.
            let frame = bound.meter.downbeat_frame;
            let beat = (frame + period / 2) / period;
            correct &= beat as usize % numerator == phase;
            correct &= frame.abs_diff(beat * period) < 8;
        }
        println!("musical diagnostic correct={correct}; not an accuracy PASS");
        verdicts.push(correct);
        let scheduled = run(rate, numerator, phase, true, true, 3);
        assert_eq!(scheduled.records, baseline.records);
        // One-step input with an always-expired analysis clock starves capture
        // until EOF. The smaller attack ring honestly loses the first window.
        assert_eq!(
            scheduled.meter_records[0].outcome,
            MeterOutcome::Unavailable
        );
        assert_eq!(&scheduled.meter_records[1..], &baseline.meter_records[1..]);
        assert!(scheduled.history_finished && scheduled.meter_finished);
    }
    // Freeze the actual failed experiment, rather than tune the grid/refinement
    // or meter thresholds to make these development fixtures pass.
    assert_eq!(verdicts, [false, false, false]);
}

#[test]
fn lifecycle_identity_and_every_output_capacity_are_checked_before_writes() {
    let captured = run(8000, 4, 1, false, false, 3);
    let mut bank = Bank::new();
    for capacity in 0..4 {
        let before = (bank.segments, bank.ranges, bank.meters, bank.records);
        let mut storage = bank.storage();
        match capacity {
            0 => storage.segments = &mut [],
            1 => storage.ranges = &mut [],
            2 => storage.meters = &mut [],
            _ => storage.meter_records = &mut [],
        }
        assert!(matches!(
            publish_into(
                captured.key(1),
                captured.history(),
                captured.meter(),
                storage
            ),
            Err(Error::BufferTooSmall)
        ));
        assert_eq!(
            (bank.segments, bank.ranges, bank.meters, bank.records),
            before
        );
    }
    for invalid in 0..6 {
        let mut key = captured.key(1);
        match invalid {
            0 => key.source.fingerprint_kind = 0,
            1 => key.source.sample_rate = 48000,
            2 => key.generation = 0,
            3 => key.lineage = [0; 2],
            4 => key.state = libapta::ResultSessionState::Created,
            _ => key.source.total_frames = Some(captured.total - 1),
        }
        assert!(matches!(
            bank.publish(&captured, key),
            Err(Error::InvalidArgument)
        ));
        assert_eq!(bank.segments, [empty_segment(); 4]);
    }
    let mut cancelled_key = captured.key(1);
    cancelled_key.state = libapta::ResultSessionState::Cancelled;
    let mut cancelled_bank = Bank::new();
    let mut cancelled = cancelled_bank.publish(&captured, cancelled_key).unwrap();
    let completed = bank.publish(&captured, captured.key(2)).unwrap();
    assert!(matches!(
        cancelled.upgrade(completed),
        Err(Error::InvalidState)
    ));
    assert_eq!(cancelled.key(), cancelled_key);

    let mut changed = captured.clone();
    let MeterOutcome::Estimated(ref mut estimate) = changed.meter_records[0].outcome else {
        panic!()
    };
    estimate.downbeat_index = 1;
    let mut changed_bank = Bank::new();
    let incompatible = changed_bank.publish(&changed, changed.key(3)).unwrap();
    let mut active = completed;
    assert!(matches!(active.upgrade(incompatible), Err(Error::Conflict)));
    assert_eq!(active.key().generation, 2);
    assert_eq!(bank.segments[2..], [empty_segment(); 2]);
    assert_eq!(bank.meters[2..], [empty_meter(); 2]);
    assert_eq!(bank.records[3], MeterRecord::default());
}

#[cfg(feature = "experimental-s6-attack-grid")]
fn diagnostic_verdict(captured: &Captured, fixture: Fixture) -> (bool, bool, bool) {
    let mut bank = Bank::new();
    let publication = bank.publish(captured, captured.key(1)).unwrap();
    verify_binding(publication.grid(), publication.meters());
    let mut grid_correct = !publication.grid().segments.is_empty();
    for segment in publication.grid().segments {
        let period_q32 = (u128::from(segment.frames_per_beat.whole_frames) << 32)
            + u128::from(segment.frames_per_beat.fraction_q32);
        let truth_q32 = (fixture.period as u128) << 32;
        grid_correct &= period_q32.abs_diff(truth_q32) * 100 <= truth_q32;
        let anchor = segment.anchor_position.whole_frame as i128 - fixture.origin as i128;
        let phase = anchor.rem_euclid(fixture.period as i128) as u128;
        grid_correct &= phase.min(fixture.period as u128 - phase) * 10 <= fixture.period as u128;
    }
    let meter_correct = !publication.meters().is_empty()
        && publication.meters().iter().all(|bound| {
            let position = bound.meter.downbeat_frame as i128 - fixture.origin as i128;
            let beat = (position + fixture.period as i128 / 2).div_euclid(fixture.period as i128);
            let error = (position - beat * fixture.period as i128).unsigned_abs();
            usize::from(bound.meter.numerator) == fixture.numerator
                && beat.rem_euclid(fixture.numerator as i128) as usize == fixture.phase
                && error * 10 <= fixture.period as u128
        });
    (
        grid_correct,
        meter_correct,
        !publication.meters().is_empty(),
    )
}

#[cfg(feature = "experimental-s6-attack-grid")]
#[test]
fn attack_grid_preserves_failed_baselines_and_measures_actual_pcm() {
    for (rate, numerator, phase) in [(8000, 4, 1), (8000, 3, 2), (48000, 4, 1)] {
        let fixture = Fixture {
            rate,
            numerator,
            phase,
            period: if rate == 8000 { 4096 } else { 16384 },
            total: 2 * WINDOW_FRAMES as usize + 17,
            origin: 0,
            jitter: false,
            shape: 0,
        };
        let baseline = run_fixture(fixture, false, false, 3, false);
        let candidate = run_fixture(fixture, true, false, 3, true);
        assert_eq!(candidate.selections.len(), baseline.records.len());
        for (selection, original) in candidate.selections.iter().zip(&baseline.records) {
            assert_eq!(selection.range, original.range);
            assert_eq!(selection.baseline, original.outcome);
        }
        let verdict = diagnostic_verdict(&candidate, fixture);
        println!(
            "REPRO rate={rate} meter={numerator} candidate={verdict:?} records={:?}",
            candidate.meter_records
        );
        assert_eq!(verdict, (true, true, true));
        let late = run_fixture(fixture, true, true, 3, true);
        assert_eq!(
            late.records[0].outcome,
            libapta::global_history::WindowOutcome::Unavailable
        );
        assert_eq!(&late.records[1..], &candidate.records[1..]);
        assert_eq!(&late.meter_records[1..], &candidate.meter_records[1..]);
    }
}

#[cfg(feature = "experimental-s6-attack-grid")]
#[test]
fn frozen_sparse_attack_challenge_reports_fixes_breaks_and_abstentions() {
    let mut id = 0;
    let (mut fixes, mut breaks, mut abstentions, mut baseline_correct, mut candidate_correct) =
        (0, 0, 0, 0, 0);
    for rate in [8000u32, 44100, 48000] {
        for bpm in [90u32, 123, 168] {
            for numerator in [3usize, 4] {
                for shape in [0u8, 1] {
                    let fixture = Fixture {
                        rate,
                        period: ((rate * 60 + bpm / 2) / bpm) as usize,
                        numerator,
                        phase: (id * 5 + 1) % numerator,
                        shape,
                        origin: 37 + (id * 149) % 503,
                        jitter: true,
                        total: WINDOW_FRAMES as usize,
                    };
                    let baseline = run_fixture(fixture, false, false, 3, false);
                    let candidate = run_fixture(fixture, true, false, 3, true);
                    assert_eq!(
                        candidate.selections[0].baseline,
                        baseline.records[0].outcome
                    );
                    let old = diagnostic_verdict(&baseline, fixture);
                    let new = diagnostic_verdict(&candidate, fixture);
                    let old_correct = old.0 && old.1;
                    let new_correct = new.0 && new.1;
                    baseline_correct += usize::from(old_correct);
                    candidate_correct += usize::from(new_correct);
                    fixes += usize::from(!old_correct && new_correct);
                    breaks += usize::from(old_correct && !new_correct);
                    abstentions += usize::from(!new.2);
                    println!("CHALLENGE id={id} rate={rate} bpm={bpm} meter={numerator} phase={} shape={shape} origin={} period={} baseline={old:?} candidate={new:?} selection={:?}", fixture.phase, fixture.origin, fixture.period, candidate.selections[0].outcome);
                    id += 1;
                }
            }
        }
    }
    println!("CHALLENGE_SUMMARY count={id} baseline_correct={baseline_correct} candidate_correct={candidate_correct} fixes={fixes} breaks={breaks} abstentions={abstentions}");
    assert_eq!(id, 36);
    // Frozen promotion veto, not a claim of real-music acceptance.
    assert_eq!(
        breaks, 0,
        "promotion veto: do not tune after this diagnostic"
    );
    assert_eq!(
        candidate_correct, 36,
        "candidate assumptions failed; retain and report rejection"
    );
}

#[test]
fn publication_retains_independent_outputs_and_rejects_stale_or_foreign_upgrade() {
    let captured = run(8000, 4, 1, true, false, 3);
    let mut first_bank = Bank::new();
    let mut next_bank = Bank::new();
    let mut foreign_bank = Bank::new();
    let first = first_bank.publish(&captured, captured.key(1)).unwrap();
    let mut active = first;
    let next = next_bank.publish(&captured, captured.key(2)).unwrap();
    active.upgrade(next).unwrap();
    assert_eq!(active.key().generation, 2);
    assert_eq!(first.key().generation, 1);
    assert_eq!(active.grid().segments, first.grid().segments);
    assert!(matches!(active.upgrade(first), Err(Error::Conflict)));
    let mut foreign_key = captured.key(3);
    foreign_key.source.fingerprint = [8; 32];
    let foreign = foreign_bank.publish(&captured, foreign_key).unwrap();
    assert!(matches!(active.upgrade(foreign), Err(Error::Conflict)));
    assert_eq!(active.key().generation, 2);
    drop(captured);
    verify_binding(first.grid(), first.meters());
    verify_binding(active.grid(), active.meters());
}

#[test]
fn capacity_and_malformed_publication_are_explicit_and_transactional() {
    let captured = run(8000, 4, 1, false, false, 1);
    assert!(captured.history_finished);
    assert!(!captured.meter_finished && captured.meter_exhausted);
    let mut bank = Bank::new();
    let published = bank.publish(&captured, captured.key(1)).unwrap();
    assert!(published.meter_capacity_exhausted());
    assert!(!published.meter_finished());
    assert_eq!(published.meter_inspected_frames(), WINDOW_FRAMES);
    verify_binding(published.grid(), published.meters());
    let before = (bank.segments, bank.ranges, bank.meters, bank.records);
    let mut bad = captured.clone();
    bad.meter_records[0].window.range.end_frame -= 1;
    assert!(matches!(
        bank.publish(&bad, bad.key(2)),
        Err(Error::Conflict)
    ));
    assert_eq!(
        (bank.segments, bank.ranges, bank.meters, bank.records),
        before
    );
    let mut storage = bank.storage();
    storage.meters = &mut [];
    assert!(matches!(
        publish_into(
            captured.key(2),
            captured.history(),
            captured.meter(),
            storage
        ),
        Err(Error::BufferTooSmall)
    ));
    assert_eq!(
        (bank.segments, bank.ranges, bank.meters, bank.records),
        before
    );
}
