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
    let before = ALLOCATIONS.load(Ordering::Relaxed);
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
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}
