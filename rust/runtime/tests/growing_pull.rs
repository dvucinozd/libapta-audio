// SPDX-License-Identifier: Apache-2.0
use libapta::{pull::*, session::*, waveform::PcmView, *};
use libapta_runtime::{GrowingLimits, GrowingPullSession, GrowingSession};
use std::{cell::Cell, rc::Rc};
struct Source {
    pcm: Vec<f32>,
    reads: Rc<Cell<usize>>,
    releases: Rc<Cell<usize>>,
    release: Box<dyn FnMut()>,
    block_once: bool,
    malformed: bool,
}
impl PullSource for Source {
    fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
        self.reads.set(self.reads.get() + 1);
        if self.block_once {
            self.block_once = false;
            return Ok(PullRead::WouldBlock);
        }
        let first = first as usize;
        if first == self.pcm.len() {
            return Ok(PullRead::EndOfInput);
        }
        let end = (first + maximum as usize).min(self.pcm.len());
        Ok(PullRead::Data(PullBlock::new(
            (first + usize::from(self.malformed)) as u64,
            PcmView::F32Interleaved(&self.pcm[first..end]),
            &mut self.release,
        )))
    }
}
fn source() -> Source {
    let releases = Rc::new(Cell::new(0));
    let count = releases.clone();
    Source {
        pcm: vec![0.5; 513],
        reads: Rc::new(Cell::new(0)),
        releases,
        release: Box::new(move || count.set(count.get() + 1)),
        block_once: false,
        malformed: false,
    }
}
fn writer(known: bool) -> GrowingSession {
    let mut w = GrowingSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: if known { 513 } else { TOTAL_FRAMES_UNKNOWN },
            frames_per_column: 64,
        },
        GrowingLimits::default(),
    )
    .unwrap();
    w.enable_three_band().unwrap();
    w.enable_detail().unwrap();
    w
}
#[test]
fn known_unknown_would_block_release_and_retained_results() {
    for known in [false, true] {
        let mut src = source();
        src.block_once = true;
        let reads = src.reads.clone();
        let releases = src.releases.clone();
        let mut pull = GrowingPullSession::new(writer(known), src).unwrap();
        let token = CancellationToken::new();
        assert!(
            pull.process(WorkBudget::default(), &token)
                .unwrap()
                .would_block
        );
        assert_eq!(releases.get(), 0);
        let initial = pull.results().acquire().unwrap();
        while pull.state() != PullState::Complete {
            pull.process(WorkBudget::default(), &token).unwrap();
        }
        assert_eq!(reads.get(), if known { 4 } else { 5 });
        assert_eq!(releases.get(), 3);
        let final_result = pull.results().acquire().unwrap();
        assert_eq!(final_result.view().overview.unwrap().columns.len(), 9);
        assert_eq!(final_result.view().detail.unwrap().columns.len(), 3);
        for _ in 0..3 {
            pull.process(WorkBudget::default(), &token).unwrap();
        }
        assert_eq!(reads.get(), if known { 4 } else { 5 });
        drop(pull.into_inner());
        assert!(initial.view().overview.is_none());
        assert_eq!(final_result.source().total_frames, Some(513));
    }
}
#[test]
fn mirror_failure_releases_once_and_retries_without_rereading() {
    let src = source();
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut w = writer(false);
    w.set_result_limits(NativeLimits {
        maximum_storage_bytes: 1,
        ..NativeLimits::default()
    });
    let mut pull = GrowingPullSession::new(w, src).unwrap();
    let token = CancellationToken::new();
    assert_eq!(
        pull.process(WorkBudget::default(), &token),
        Err(Error::LimitExceeded)
    );
    assert_eq!(pull.session().session().accepted_frames(), 256);
    assert_eq!(pull.session().session().processed_frames(), 0);
    assert_eq!((reads.get(), releases.get()), (1, 1));
    assert_eq!(
        pull.process(WorkBudget::default(), &token),
        Err(Error::LimitExceeded)
    );
    assert_eq!((reads.get(), releases.get()), (1, 1));
    pull.set_result_limits(NativeLimits::default());
    pull.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(pull.session().session().processed_frames(), 256);
    assert_eq!((reads.get(), releases.get()), (1, 1));
    while pull.state() != PullState::Complete {
        pull.process(WorkBudget::default(), &token).unwrap();
    }
    assert_eq!((reads.get(), releases.get()), (4, 3));
}
#[test]
fn malformed_source_and_cancellation_are_terminal() {
    let mut src = source();
    src.malformed = true;
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut pull = GrowingPullSession::new(writer(false), src).unwrap();
    for _ in 0..2 {
        assert_eq!(
            pull.process(WorkBudget::default(), &CancellationToken::new()),
            Err(Error::InvalidArgument)
        );
    }
    assert_eq!((reads.get(), releases.get()), (1, 1));
    let src = source();
    let reads = src.reads.clone();
    let mut pull = GrowingPullSession::new(writer(false), src).unwrap();
    let token = CancellationToken::new();
    token.cancel();
    assert_eq!(
        pull.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(pull.state(), PullState::Cancelled);
    assert_eq!(reads.get(), 0);
}

#[test]
fn clock_starts_after_release_and_musical_drain_never_reads_source() {
    let mut src = source();
    src.pcm = vec![0.0; 70001];
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut w = GrowingSession::new(
        SessionConfig {
            sample_rate: 8000,
            channel_count: 1,
            total_frames: TOTAL_FRAMES_UNKNOWN,
            frames_per_column: 64,
        },
        GrowingLimits::default(),
    )
    .unwrap();
    w.enable_default_music().unwrap();
    w.enable_three_band().unwrap();
    w.enable_detail().unwrap();
    let mut pull = GrowingPullSession::new(w, src).unwrap();
    let token = CancellationToken::new();
    let mut ticks = 1;
    pull.process_with_clock(
        WorkBudget::default(),
        1,
        &mut || {
            assert_eq!(releases.get(), 1);
            ticks += 1000;
            ticks
        },
        &token,
    )
    .unwrap();
    assert!(ticks > 1);
    while !matches!(pull.state(), PullState::Draining | PullState::Complete) {
        pull.process(WorkBudget::default(), &token).unwrap();
    }
    let count = reads.get();
    while pull.state() != PullState::Complete {
        pull.process(WorkBudget::default(), &token).unwrap();
    }
    assert_eq!(reads.get(), count);
    let r = pull.results().acquire().unwrap();
    assert_eq!(r.source().total_frames, Some(70001));
    // Silence intentionally produces no selected key.
    assert!(r.view().key.is_none());
    assert!(r.view().detail.is_some());
}
