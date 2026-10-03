// SPDX-License-Identifier: Apache-2.0
use libapta::{
    result::WAVEFORM_OVERVIEW,
    scheduler::{RequestSlot, Scheduler},
    *,
};
fn region(first: u64, priority: u8, deadline: u64) -> RegionRequest {
    RegionRequest {
        range: FrameRange {
            first_frame: first,
            end_frame: first + 1024,
        },
        feature_mask: WAVEFORM_OVERVIEW,
        soft_deadline_monotonic_ns: deadline,
        request_id: 0,
        priority,
    }
}
#[test]
fn deadline_fifo_and_priority_match_c_sequences() {
    let mut slots = [RequestSlot::default(); 16];
    let mut s = Scheduler::new(Some(8192), WAVEFORM_OVERVIEW, &mut slots).unwrap();
    let first = s.request_region(region(0, 96, 0)).unwrap();
    let later = s.request_region(region(2048, 96, 5000)).unwrap();
    let earlier = s.request_region(region(4096, 96, 1000)).unwrap();
    let last = s.request_region(region(6144, 96, 0)).unwrap();
    for id in [earlier, later, first, last] {
        assert_eq!(s.next_pcm_request(&[]).unwrap().request_token, id);
        s.cancel_region_request(id).unwrap();
    }
    assert_eq!(s.next_pcm_request(&[]).unwrap().request_token, 0);
}
#[test]
fn aging_reaches_fifo_tie_after_eight_choices_and_resets() {
    let mut slots = [RequestSlot::default(); 2];
    let mut s = Scheduler::new(Some(8192), WAVEFORM_OVERVIEW, &mut slots).unwrap();
    let background = s.request_region(region(0, 32, 0)).unwrap();
    let normal = s.request_region(region(4096, 96, 0)).unwrap();
    for _ in 0..8 {
        let d = s.next_pcm_request(&[]).unwrap();
        assert_eq!((d.request_token, d.priority), (normal, 96));
    }
    let d = s.next_pcm_request(&[]).unwrap();
    assert_eq!((d.request_token, d.priority), (background, 96));
    s.cancel_region_request(normal).unwrap();
    assert_eq!(s.next_pcm_request(&[]).unwrap().priority, 32);
}
#[test]
fn explicit_request_blocks_focus_fallback_until_cancelled() {
    let mut slots = [RequestSlot::default(); 2];
    let mut s = Scheduler::new(Some(8192), WAVEFORM_OVERVIEW, &mut slots).unwrap();
    s.set_focus(Focus {
        playhead_frame: 4096,
        lookahead_frames: 1024,
        feature_mask: WAVEFORM_OVERVIEW,
        priority: 240,
        ..Default::default()
    })
    .unwrap();
    let id = s.request_region(region(0, 32, 0)).unwrap();
    assert_eq!(s.next_pcm_request(&[]).unwrap().request_token, id);
    let accepted = [FrameRange {
        first_frame: 0,
        end_frame: 1024,
    }];
    assert_eq!(s.next_pcm_request(&accepted), Err(Error::NotAvailable));
    s.cancel_region_request(id).unwrap();
    let d = s.next_pcm_request(&accepted).unwrap();
    assert_eq!(
        (
            d.range.first_frame,
            d.range.end_frame,
            d.priority,
            d.request_token
        ),
        (4096, 5120, 240, 0)
    );
}
#[test]
fn gap_clipping_unknown_background_and_focus_overflow() {
    let mut slots = [];
    let mut s = Scheduler::new(Some(20000), WAVEFORM_OVERVIEW, &mut slots).unwrap();
    assert_eq!(
        s.next_pcm_request(&[]).unwrap().range,
        FrameRange {
            first_frame: 0,
            end_frame: 4096
        }
    );
    let accepted = [
        FrameRange {
            first_frame: 0,
            end_frame: 512,
        },
        FrameRange {
            first_frame: 2048,
            end_frame: 3000,
        },
    ];
    assert_eq!(
        s.next_pcm_request(&accepted).unwrap().range,
        FrameRange {
            first_frame: 512,
            end_frame: 2048
        }
    );
    s.set_focus(Focus {
        playhead_frame: 19900,
        lookahead_frames: u64::MAX,
        feature_mask: WAVEFORM_OVERVIEW,
        priority: 240,
        ..Default::default()
    })
    .unwrap();
    assert_eq!(
        s.next_pcm_request(&[]).unwrap().range,
        FrameRange {
            first_frame: 19900,
            end_frame: 20000
        }
    );
    let mut slots = [];
    let mut s = Scheduler::new(None, WAVEFORM_OVERVIEW, &mut slots).unwrap();
    assert_eq!(
        s.next_pcm_request(&accepted).unwrap().range,
        FrameRange {
            first_frame: 512,
            end_frame: 2048
        }
    );
    s.set_focus(Focus {
        playhead_frame: u64::MAX - 10,
        lookahead_frames: 100,
        feature_mask: WAVEFORM_OVERVIEW,
        ..Default::default()
    })
    .unwrap();
    assert_eq!(s.next_pcm_request(&[]).unwrap().range.end_frame, u64::MAX);
}
#[test]
fn terminal_requests_keep_ids_and_capacity() {
    let mut slots = [RequestSlot::default(); 20];
    let mut s = Scheduler::new(Some(1024), WAVEFORM_OVERVIEW, &mut slots).unwrap();
    for i in 0..16 {
        let request = RegionRequest {
            request_id: 100 + i,
            ..region(0, 96, 0)
        };
        assert_eq!(s.request_region(request).unwrap(), 100 + i);
        s.cancel_region_request(100 + i).unwrap();
        s.cancel_region_request(100 + i).unwrap();
        assert_eq!(
            s.request_progress(100 + i).unwrap().state,
            RequestState::Cancelled
        );
        assert_eq!(s.request_region(request), Err(Error::Conflict));
    }
    assert_eq!(
        s.request_region(region(0, 96, 0)),
        Err(Error::LimitExceeded)
    );
    assert_eq!(s.cancel_region_request(0), Err(Error::InvalidArgument));
    assert_eq!(s.cancel_region_request(50), Err(Error::NotAvailable));
    assert_eq!(s.request_progress(0), Err(Error::InvalidArgument));
    assert_eq!(s.request_progress(50), Err(Error::NotAvailable));
}
#[test]
fn validation_and_requests_beyond_known_eof() {
    let mut slots = [RequestSlot::default(); 3];
    let mut s = Scheduler::new(Some(1024), WAVEFORM_OVERVIEW, &mut slots).unwrap();
    assert_eq!(
        s.request_region(RegionRequest {
            feature_mask: 0,
            ..region(0, 96, 0)
        }),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        s.request_region(RegionRequest {
            feature_mask: 2,
            ..region(0, 96, 0)
        }),
        Err(Error::Unsupported)
    );
    assert_eq!(
        s.set_focus(Focus {
            playhead_frame: 1025,
            ..Default::default()
        }),
        Err(Error::InvalidArgument)
    );
    s.set_focus(Focus {
        playhead_frame: 1024,
        ..Default::default()
    })
    .unwrap();
    let id = s.request_region(region(2048, 96, 0)).unwrap();
    assert_eq!(s.request_progress(id).unwrap().state, RequestState::Queued);
    assert_eq!(s.next_pcm_request(&[]), Err(Error::NotAvailable));
    assert_eq!(
        s.next_pcm_request(&[FrameRange {
            first_frame: 0,
            end_frame: 2048
        }]),
        Err(Error::InvalidArgument)
    );
    let mut slots = [RequestSlot::default(); 1];
    let mut s = Scheduler::new(None, 0, &mut slots).unwrap();
    assert_eq!(s.request_region(region(0, 96, 0)), Err(Error::InvalidState));
    assert_eq!(s.next_pcm_request(&[]), Err(Error::NotAvailable));
}
