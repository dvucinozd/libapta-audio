// SPDX-License-Identifier: Apache-2.0
//! Separate executable keeps the allocation counter isolated from parallel tests.
use libapta::pull::{PullBlock, PullRead, PullSession, PullSource, PullState};
use libapta::session::{
    CancellationToken, Session, SessionConfig, WorkBudget, TOTAL_FRAMES_UNKNOWN,
};
use libapta::waveform::NormalizedSample;
use libapta::waveform::PcmView;
use libapta::Error;
use libapta::WaveformColumn;
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

struct Counter;
static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);
unsafe impl GlobalAlloc for Counter {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        // SAFETY: Forwarding the unchanged allocation contract to System.
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: Pointer and layout came from System through this allocator.
        unsafe { System.dealloc(ptr, layout) }
    }
}
#[global_allocator]
static ALLOCATOR: Counter = Counter;

struct Source {
    samples: [f32; 65],
    release: fn(),
}
impl PullSource for Source {
    fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
        if first == 65 {
            return Ok(PullRead::EndOfInput);
        }
        let end = (first as usize + maximum as usize).min(65);
        Ok(PullRead::Data(PullBlock::new(
            first,
            PcmView::F32Interleaved(&self.samples[first as usize..end]),
            &mut self.release,
        )))
    }
}

#[test]
fn entire_session_path_allocates_nothing() {
    let bytes = b"RIFF\x28\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\xbb\0\0\0\x77\x01\0\x02\0\x10\0data\x04\0\0\0\0\0\0\0";
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let mut scanner = libapta::wav::WavScanner::new(bytes.len() as u64);
    for byte in bytes.chunks(1) {
        scanner.push(byte).unwrap();
    }
    let layout = scanner.finish().unwrap();
    assert_eq!(layout.decode(&bytes[44..], &mut [0.; 2]), Ok(2));
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);

    let mut first = [libapta::scheduler::RequestSlot::default(); 1];
    let mut replacement = [libapta::scheduler::RequestSlot::default(); 16];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let mut scheduler = libapta::scheduler::Scheduler::new(Some(1024), 1, &mut first).unwrap();
    let id = scheduler
        .request_region(libapta::RegionRequest {
            range: libapta::FrameRange {
                first_frame: 0,
                end_frame: 256,
            },
            feature_mask: 1,
            priority: 96,
            soft_deadline_monotonic_ns: 0,
            request_id: 0,
        })
        .unwrap();
    scheduler.replace_request_storage(&mut replacement).unwrap();
    assert_eq!(scheduler.next_pcm_request(&[]).unwrap().request_token, id);
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
    let mut queue = [NormalizedSample {
        value: 0.0,
        clipped: false,
    }; 65];
    let mut columns = [WaveformColumn::default(); 2];
    let mut snapshot = [WaveformColumn::default(); 2];
    for total_frames in [65, TOTAL_FRAMES_UNKNOWN] {
        let mut bands = [libapta::band::BandSums::default(); 2];
        let mut cache = [libapta::detail_analysis::DetailTile::default(); 4];
        let mut detail_tiles = [libapta::NativeTile {
            level_id: 1,
            tile_index: 0,
            first_frame: 0,
            end_frame: 0,
            first_column_index: 0,
            state: libapta::FeatureState::Partial,
            confidence: 0,
            data_column_offset: 0,
            column_count: 0,
        }; 4];
        let mut detail_columns = [WaveformColumn::default(); 256];
        let before = ALLOCATIONS.load(Ordering::Relaxed);
        let mut session = Session::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames,
                frames_per_column: 64,
            },
            &mut queue,
            &mut columns,
        )
        .unwrap();
        session.enable_three_band(&mut bands).unwrap();
        session.enable_detail(&mut cache).unwrap();
        session.push_interleaved(&[0.2; 65]).unwrap();
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        session.copy_snapshot_into(&mut snapshot).unwrap();
        assert_eq!(
            session
                .copy_detail_into(&mut detail_tiles, &mut detail_columns)
                .unwrap()
                .unwrap()
                .tiles[0]
                .end_frame,
            65
        );
        assert!(snapshot.iter().all(|column| column.flags & 8 != 0));
        assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
    }
    for total_frames in [65, TOTAL_FRAMES_UNKNOWN] {
        let mut queue = [NormalizedSample::default(); 17];
        let mut columns = [WaveformColumn::default(); 2];
        let mut snapshot = [WaveformColumn::default(); 2];
        let mut bands = [libapta::band::BandSums::default(); 2];
        let mut cache = [libapta::detail_analysis::DetailTile::default(); 4];
        let mut detail_tiles = [libapta::NativeTile {
            level_id: 1,
            tile_index: 0,
            first_frame: 0,
            end_frame: 0,
            first_column_index: 0,
            state: libapta::FeatureState::Partial,
            confidence: 0,
            data_column_offset: 0,
            column_count: 0,
        }; 4];
        let mut detail_columns = [WaveformColumn::default(); 256];
        let before = ALLOCATIONS.load(Ordering::Relaxed);
        let input = Source {
            samples: [0.25; 65],
            release: || {},
        };
        let mut pull = PullSession::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames,
                frames_per_column: 64,
            },
            &mut queue,
            &mut columns,
            input,
        )
        .unwrap();
        pull.enable_three_band(&mut bands).unwrap();
        pull.enable_detail(&mut cache).unwrap();
        while pull.state() != PullState::Complete {
            pull.process(WorkBudget::default(), &CancellationToken::new())
                .unwrap();
        }
        pull.copy_snapshot_into(&mut snapshot).unwrap();
        assert_eq!(
            pull.copy_detail_into(&mut detail_tiles, &mut detail_columns)
                .unwrap()
                .unwrap()
                .tiles[0]
                .end_frame,
            65
        );
        let _source = pull.into_inner();
        assert!(snapshot.iter().all(|column| column.flags & 8 != 0));
        assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
    }
    let mut bins = vec![libapta::analysis::OnsetBin::default(); libapta::analysis::BIN_CAPACITY];
    let mut flux = vec![0.0; bins.len()];
    let mut global_bins =
        vec![libapta::analysis::OnsetBin::default(); libapta::global_analysis::BIN_CAPACITY];
    let mut global_flux = vec![0.0; global_bins.len()];
    let mut beats = vec![libapta::Beat::default(); libapta::global_analysis::MAX_BEATS];
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); 10];
    let samples: Vec<f32> = (0..320000)
        .map(|i| if i % 4000 < 64 { 0.75 } else { 0.0 })
        .collect();
    #[cfg(feature = "experimental-s6-history")]
    let mut history_records = [libapta::global_history::WindowRecord::default(); 2];
    #[cfg(feature = "experimental-s6-history")]
    let mut attack_bins = vec![
        libapta::history_meter::AttackBin::default();
        libapta::history_meter::ATTACK_BIN_CAPACITY
    ];
    #[cfg(feature = "experimental-s6-history")]
    let mut meter_records = [libapta::history_meter::MeterRecord::default(); 2];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    #[cfg(feature = "experimental-s6-history")]
    let mut history_meter =
        libapta::history_meter::MeterHistory::new(8000, &mut attack_bins, &mut meter_records)
            .unwrap();
    #[cfg(feature = "experimental-s6-history")]
    let mut history =
        libapta::global_history::WindowHistory::new(8000, &mut history_records).unwrap();
    #[cfg(feature = "experimental-s6-history")]
    history.enable_meter(&mut history_meter).unwrap();
    let mut session = Session::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: 320000,
            frames_per_column: 32768,
        },
        &mut queue,
        &mut columns,
    )
    .unwrap();
    session.enable_tempo(&mut bins, &mut flux).unwrap();
    session
        .enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats)
        .unwrap();
    #[cfg(feature = "experimental-s6-history")]
    session.enable_global_history(&mut history).unwrap();
    session.enable_key().unwrap();
    session.enable_meter().unwrap();
    session.enable_calibrated_quality().unwrap();
    for block in samples.chunks(4096) {
        session.push_interleaved(block).unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
    }
    session.finish_input().unwrap();
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert!(session.tempo().is_some());
    assert!(session.global_grid().is_some());
    assert!(session.meter().is_some());
    assert!(session.key().is_some());
    assert!(session.bpm_quality().is_some());
    #[cfg(feature = "experimental-s6-history")]
    {
        let view = session.global_history().unwrap();
        assert!(view.finished);
        assert_eq!(view.records.len(), 2);
        let template = session.global_grid().unwrap().segments[0];
        let mut converted_segments = [template; 2];
        let mut converted_ranges = [libapta::FrameRange::default(); 2];
        let converted = libapta::history_grid::convert_into(
            view,
            1,
            &mut converted_segments,
            &mut converted_ranges,
        )
        .unwrap();
        assert_eq!(converted.segments.len(), 1);
        assert_eq!(converted.unsupported_windows, 1);
        assert!(converted.segments[0].beat_at(0).unwrap().is_some());
        let beat = converted.segments[0].beat_at(3).unwrap().unwrap();
        // Synthetic label exercises successful coordinate binding; this is not
        // a claim that the classifier selected this beat as a downbeat.
        let synthetic = libapta::MeterSegment {
            first_frame: converted.segments[0].applicability_range.first_frame,
            end_frame: converted.segments[0].applicability_range.end_frame,
            downbeat_frame: beat.position.whole_frame,
            downbeat_ordinal: beat.ordinal,
            numerator: 4,
            denominator: 4,
            state: libapta::FeatureState::Provisional,
            confidence: 80,
            segment_id: 1,
        };
        let mut bindings = [libapta::history_grid::BoundMeterSegment {
            meter: synthetic,
            grid_segment_id: 0,
            grid_revision_id: 0,
            downbeat_position: libapta::FractionalFrame::default(),
        }];
        libapta::history_grid::bind_meter_into(converted, 1, &[synthetic], &mut bindings).unwrap();
        let retained = bindings;
        // The actual local-meter range extends beyond this supported global
        // window. Preserve that real rejection rather than clipping or rebasing.
        assert!(matches!(
            libapta::history_grid::bind_meter_into(
                converted,
                1,
                session.meter().unwrap().segments,
                &mut bindings,
            ),
            Err(libapta::Error::Conflict)
        ));
        assert_eq!(bindings, retained);
        let mut published_segments = [template; 2];
        let mut published_ranges = [libapta::FrameRange::default(); 2];
        let mut published_meters = [retained[0]; 2];
        let mut published_records = [libapta::history_meter::MeterRecord::default(); 2];
        let publication = libapta::history_publication::publish_into(
            libapta::history_publication::PublicationKey {
                source: libapta::SourceInfo {
                    sample_rate: 8000,
                    channel_count: 1,
                    channel_layout: 1,
                    total_frames: Some(320000),
                    fingerprint_kind: 1,
                    fingerprint: [9; 32],
                },
                lineage: [1, 2],
                generation: 1,
                grid_revision: 1,
                state: libapta::ResultSessionState::Completed,
            },
            view,
            session.global_history_meter().unwrap(),
            libapta::history_publication::PublicationStorage {
                segments: &mut published_segments,
                ranges: &mut published_ranges,
                meters: &mut published_meters,
                meter_records: &mut published_records,
            },
        )
        .unwrap();
        assert_eq!(publication.meter_records().len(), 2);
        assert!(publication.meter_finished());
        assert_eq!(publication.grid().unsupported_windows, 1);
    }
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
    #[cfg(feature = "experimental-s6-attack-grid")]
    attack_selection_allocates_nothing(false);
    #[cfg(feature = "experimental-s6-onset-grid")]
    attack_selection_allocates_nothing(true);
}

#[cfg(feature = "experimental-s6-attack-grid")]
fn attack_selection_allocates_nothing(novelty: bool) {
    use libapta::{
        analysis::OnsetBin,
        attack_grid::SelectionRecord,
        global_analysis::{BIN_CAPACITY, MAX_BEATS},
        global_history::{WindowHistory, WindowRecord, WINDOW_FRAMES},
        history_meter::{AttackBin, MeterHistory, MeterRecord, ATTACK_BIN_CAPACITY},
    };
    let mut bins = vec![OnsetBin::default(); BIN_CAPACITY];
    let mut flux = vec![0.0; BIN_CAPACITY];
    let mut beats = vec![libapta::Beat::default(); MAX_BEATS];
    let mut attack_bins = vec![AttackBin::default(); ATTACK_BIN_CAPACITY];
    let mut records = [WindowRecord::default(); 2];
    let mut meter_records = [MeterRecord::default(); 2];
    let mut selections = [SelectionRecord::default(); 2];
    let mut queue = [NormalizedSample::default(); 4096];
    let mut columns = [WaveformColumn::default(); 9];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let mut meter = MeterHistory::new(8000, &mut attack_bins, &mut meter_records).unwrap();
    let mut history = WindowHistory::new(8000, &mut records).unwrap();
    history.enable_meter(&mut meter).unwrap();
    if novelty {
        #[cfg(feature = "experimental-s6-onset-grid")]
        history.enable_onset_grid(&mut selections).unwrap();
    } else {
        history.enable_attack_grid(&mut selections).unwrap();
    }
    let total = WINDOW_FRAMES + 17;
    let mut session = libapta::session::Session::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: total,
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
    let mut block = [0.0f32; 4096];
    let token = CancellationToken::new();
    let mut frame = 0;
    while frame < total {
        let count = (total - frame).min(block.len() as u64) as usize;
        for (n, sample) in block[..count].iter_mut().enumerate() {
            let position = frame + n as u64;
            let origin = if novelty { 768 } else { 0 };
            let active = position >= origin && (position - origin) % 4096 < 64;
            *sample = if active {
                if position / 4096 % 4 == 1 {
                    0.875
                } else {
                    0.25
                }
            } else if novelty {
                0.05
            } else {
                0.0
            };
            if novelty {
                *sample *= [0.0, 0.5, 1.0, 0.5, 0.0, -0.5, -1.0, -0.5][position as usize % 8];
            }
        }
        frame += session.push_interleaved(&block[..count]).unwrap() as u64;
        session.process(WorkBudget::default(), &token).unwrap();
    }
    session.finish_input().unwrap();
    session.process(WorkBudget::default(), &token).unwrap();
    let template = session.global_grid().unwrap().segments[0];
    let history = session.global_history().unwrap();
    let mut grid_segments = [template; 2];
    let mut ranges = [libapta::FrameRange::default(); 2];
    let converted =
        libapta::history_grid::convert_into(history, 2, &mut grid_segments, &mut ranges).unwrap();
    assert_eq!(converted.segments[0].nominal_tempo_millibpm, 117188);
    assert_eq!(converted.unsupported_windows, 1);
    assert_eq!(session.global_history_selection().unwrap().len(), 2);
    #[cfg(feature = "experimental-s6-onset-trace")]
    if novelty {
        let trace = &session.global_history_selection().unwrap()[0].onsets;
        assert!(trace.complete);
        assert_eq!(trace.count, 64);
        assert_eq!(trace.events[0].frame, 768);
    }
    #[cfg(feature = "experimental-s6-temporal-trace")]
    if novelty {
        let dense = &session.global_history_selection().unwrap()[0].dense;
        assert!(dense.complete);
        assert_eq!(dense.count, 1024);
        assert_eq!(dense.startup_bins, 3);
    }
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}
