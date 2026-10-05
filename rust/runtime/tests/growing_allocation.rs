// SPDX-License-Identifier: Apache-2.0
//! Isolated fallible-array injection. Arc/Mutex control allocations use the
//! standard allocator contract and are deliberately excluded from injection.
use libapta::{session::*, waveform::PcmView, Error};
use libapta_runtime::{GrowingLimits, GrowingSession};
use std::{
    alloc::{GlobalAlloc, Layout, System},
    sync::atomic::{AtomicUsize, Ordering},
};
struct Allocator;
static FAIL_AFTER: AtomicUsize = AtomicUsize::new(0);
unsafe impl GlobalAlloc for Allocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let left = FAIL_AFTER.load(Ordering::Relaxed);
        if left != 0 && FAIL_AFTER.fetch_sub(1, Ordering::Relaxed) == 1 {
            return core::ptr::null_mut();
        }
        // SAFETY: Forward the identical allocation layout to System.
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: Every non-null pointer was allocated by System with this layout.
        unsafe { System.dealloc(ptr, layout) }
    }
}
#[global_allocator]
static ALLOCATOR: Allocator = Allocator;
fn config() -> SessionConfig {
    SessionConfig {
        sample_rate: 8000,
        channel_count: 1,
        total_frames: TOTAL_FRAMES_UNKNOWN,
        frames_per_column: 64,
    }
}
#[test]
fn growing_array_failures_preserve_preflight_and_retry_committed_snapshots() {
    // First two allocations are queue/output replacements. Next two are the
    // immutable provenance arrays, before infallible Arc control allocation.
    for failure in 1..=4 {
        let mut s = GrowingSession::new(config(), GrowingLimits::default()).unwrap();
        let old = s.results().acquire().unwrap();
        FAIL_AFTER.store(failure, Ordering::Relaxed);
        let result = s.push_pcm(PcmView::F32Interleaved(&[0.5; 513]));
        let left = FAIL_AFTER.swap(0, Ordering::Relaxed);
        assert_eq!(left, 0, "injection must be consumed");
        assert_eq!(result, Err(Error::LimitExceeded));
        if failure <= 2 {
            assert_eq!(s.session().accepted_frames(), 0);
            assert_eq!(s.session().input_capacity_frames(), 0);
            assert_eq!(s.refresh(), Ok(false));
            assert_eq!(s.push_pcm(PcmView::F32Interleaved(&[0.5; 513])), Ok(513));
        } else {
            assert_eq!(s.session().accepted_frames(), 513);
            assert_eq!(s.session().processed_frames(), 0);
            assert_eq!(s.refresh(), Ok(true));
        }
        assert_eq!(old.info().generation, 1);
        assert!(old.view().overview.is_none());
        s.finish_input().unwrap();
        s.process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(s.session().processed_frames(), 513);
        assert_eq!(s.session().columns().len(), 9);
    }
    // Every musical workspace allocation must fail before attaching any stage.
    for failure in 1..=5 {
        let mut s = GrowingSession::new(config(), GrowingLimits::default()).unwrap();
        FAIL_AFTER.store(failure, Ordering::Relaxed);
        let result = s.enable_default_music();
        let left = FAIL_AFTER.swap(0, Ordering::Relaxed);
        assert_eq!(left, 0);
        assert_eq!(result, Err(Error::LimitExceeded));
        assert_eq!(s.session().publication_serials(), [0; 5]);
        s.enable_default_music().unwrap();
        assert_eq!(s.push_pcm(PcmView::F32Interleaved(&[0.5; 513])), Ok(513));
    }
    // Queue, overview and band growth all preflight before any mutation.
    for failure in 1..=3 {
        let mut s = GrowingSession::new(config(), GrowingLimits::default()).unwrap();
        s.enable_three_band().unwrap();
        s.enable_detail().unwrap();
        FAIL_AFTER.store(failure, Ordering::Relaxed);
        let result = s.push_pcm(PcmView::F32Interleaved(&[0.5; 513]));
        assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
        assert_eq!(result, Err(Error::LimitExceeded));
        assert_eq!(s.session().accepted_frames(), 0);
        assert_eq!(s.session().input_capacity_frames(), 0);
        assert_eq!(s.refresh(), Ok(false));
        assert_eq!(s.push_pcm(PcmView::F32Interleaved(&[0.5; 513])), Ok(513));
    }
    for band in [false, true] {
        let mut c = config();
        c.total_frames = 513;
        let mut s = GrowingSession::new(c, GrowingLimits::default()).unwrap();
        FAIL_AFTER.store(1, Ordering::Relaxed);
        let result = if band {
            s.enable_three_band()
        } else {
            s.enable_detail()
        };
        assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
        assert_eq!(result, Err(Error::LimitExceeded));
        if band {
            s.enable_three_band().unwrap();
        } else {
            s.enable_detail().unwrap();
        }
    }
    // Sparse construction and every attachment allocation fail before commit.
    use libapta_runtime::{OwnedSparseSession, SparseLimits};
    let mut c = config();
    c.total_frames = 513;
    for failure in 1..=9 {
        FAIL_AFTER.store(failure, Ordering::Relaxed);
        let result = OwnedSparseSession::new(c, SparseLimits::default());
        assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
        assert!(matches!(result, Err(Error::LimitExceeded)));
    }
    for failure in 1..=5 {
        let mut s = OwnedSparseSession::new(c, SparseLimits::default()).unwrap();
        let bytes = s.working_bytes();
        FAIL_AFTER.store(failure, Ordering::Relaxed);
        let result = s.enable_default_music();
        assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
        assert_eq!(result, Err(Error::LimitExceeded));
        assert_eq!(s.working_bytes(), bytes);
        assert_eq!(s.session().publication_serials(), [0; 5]);
        s.enable_default_music().unwrap();
    }
    for bands in [false, true] {
        let mut s = OwnedSparseSession::new(c, SparseLimits::default()).unwrap();
        let bytes = s.working_bytes();
        FAIL_AFTER.store(1, Ordering::Relaxed);
        let result = if bands {
            s.enable_three_band()
        } else {
            s.enable_detail()
        };
        assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
        assert_eq!(result, Err(Error::LimitExceeded));
        assert_eq!(s.working_bytes(), bytes);
        if bands {
            s.enable_three_band().unwrap();
        } else {
            s.enable_detail().unwrap();
        }
    }
    // Every replacement allocation fails before any sparse capacity/work commit.
    for failure in 1..=3 {
        let mut s = OwnedSparseSession::new(
            c,
            SparseLimits {
                queue_nodes: 1,
                range_capacity: 1,
                ..SparseLimits::default()
            },
        )
        .unwrap();
        s.push_at(0, PcmView::S16Interleaved(&[100; 256])).unwrap();
        s.process(
            WorkBudget {
                maximum_input_frames: 17,
                maximum_steps: 1,
            },
            &CancellationToken::new(),
        )
        .unwrap();
        let bytes = s.working_bytes();
        let old = s.results().acquire().unwrap();
        FAIL_AFTER.store(failure, Ordering::Relaxed);
        let result = s.reserve_pending(2, 2);
        assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
        assert_eq!(result, Err(Error::LimitExceeded));
        assert_eq!(s.working_bytes(), bytes);
        assert_eq!(s.session().processed_frames(), 17);
        assert_eq!(s.session().queued_frames(), 239);
        assert_eq!(s.results().acquire().unwrap().info(), old.info());
        assert_eq!(s.refresh(), Ok(false));
        s.reserve_pending(2, 2).unwrap();
        assert_eq!(
            s.push_at(256, PcmView::S16Interleaved(&[200; 257])),
            Ok(257)
        );
        s.finish_input().unwrap();
        s.process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(s.session().processed_frames(), 513);
    }
    let mut s = OwnedSparseSession::new(
        c,
        SparseLimits {
            request_capacity: 1,
            ..SparseLimits::default()
        },
    )
    .unwrap();
    let request = libapta::RegionRequest {
        range: libapta::FrameRange {
            first_frame: 0,
            end_frame: 256,
        },
        feature_mask: libapta::result::WAVEFORM_OVERVIEW,
        priority: 96,
        soft_deadline_monotonic_ns: 0,
        request_id: 0,
    };
    let id = s.request_region(request).unwrap();
    let old = s.results().acquire().unwrap();
    let bytes = s.working_bytes();
    FAIL_AFTER.store(1, Ordering::Relaxed);
    let result = s.reserve_requests(2);
    assert_eq!(FAIL_AFTER.swap(0, Ordering::Relaxed), 0);
    assert_eq!(result, Err(Error::LimitExceeded));
    assert_eq!(s.working_bytes(), bytes);
    assert_eq!(s.results().acquire().unwrap().info(), old.info());
    assert_eq!(s.request_region(request), Err(Error::LimitExceeded));
    s.reserve_requests(2).unwrap();
    assert_eq!(s.request_region(request), Ok(id + 1));
}
