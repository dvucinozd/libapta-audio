// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::Storage,
    publication::{PublishedSparseSession, ResultPool},
    pull::{PullBlock, PullRead, PullSource},
    scheduler::RequestSlot,
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
    sparse_pull::ScheduledPullSession,
    waveform::{NormalizedSample, PcmView},
    *,
};
use std::{cell::Cell, rc::Rc};
#[derive(Clone, Copy)]
enum Mode {
    Normal,
    WouldBlock,
    BadFirst,
    Empty,
    TooLong,
    Error,
    ErrorAfterFirst,
    Eof,
    Cancel,
}
struct Source {
    mode: Mode,
    data: [i16; 129],
    reads: usize,
    release: Box<dyn FnMut()>,
    released: Rc<Cell<usize>>,
    cancel: Rc<CancellationToken>,
    total: Option<u64>,
}
impl Source {
    fn new(mode: Mode) -> Self {
        let released = Rc::new(Cell::new(0));
        let copy = released.clone();
        Self {
            mode,
            data: [123; 129],
            reads: 0,
            release: Box::new(move || copy.set(copy.get() + 1)),
            released,
            cancel: Rc::new(CancellationToken::new()),
            total: Some(128),
        }
    }
}
impl PullSource for Source {
    fn total_frames(&mut self) -> Option<u64> {
        self.total
    }
    fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
        self.reads += 1;
        match self.mode {
            Mode::WouldBlock if self.reads == 1 => return Ok(PullRead::WouldBlock),
            Mode::Error => return Err(Error::Corrupt),
            Mode::ErrorAfterFirst if self.reads > 1 => return Err(Error::Corrupt),
            Mode::Eof => return Ok(PullRead::EndOfInput),
            Mode::Cancel => self.cancel.cancel(),
            _ => {}
        }
        let n = match self.mode {
            Mode::Empty => 0,
            Mode::TooLong => maximum as usize + 1,
            _ => maximum as usize,
        };
        let offset = if matches!(self.mode, Mode::BadFirst) {
            first + 1
        } else {
            first
        };
        Ok(PullRead::Data(PullBlock::new(
            offset,
            PcmView::S16Interleaved(&self.data[..n]),
            &mut self.release,
        )))
    }
}
fn run(source: Source, f: impl FnOnce(ScheduledPullSession<'_, '_, '_, Source>, &ResultPool<'_>)) {
    let mut ca = [WaveformColumn::default(); 2];
    let mut cb = ca;
    let mut sa = [WaveformSpan::default(); 2];
    let mut sb = sa;
    let pool = ResultPool::new(
        SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(128),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        [
            Storage {
                overview_columns: &mut ca,
                overview_spans: &mut sa,
                ..Default::default()
            },
            Storage {
                overview_columns: &mut cb,
                overview_spans: &mut sb,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut acc = [SparseAccumulator::default(); 2];
    let mut ranges = [FrameRange::default(); 4];
    let mut nodes = [QueuedBlock::default(); 2];
    let mut pcm = [NormalizedSample::default(); 2 * NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 2];
    let mut cols = [WaveformColumn::default(); 2];
    let mut requests = [RequestSlot::default(); 2];
    let session = PublishedSparseSession::new_scheduled(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 128,
            frames_per_column: 64,
        },
        Workspace {
            accumulators: &mut acc,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut cols,
        },
        &pool,
        &mut requests,
    )
    .unwrap();
    f(ScheduledPullSession::new(session, source).unwrap(), &pool);
}
#[test]
fn sparse_requested_range_then_background_and_eof() {
    run(Source::new(Mode::Normal), |mut s, pool| {
        let token = CancellationToken::new();
        s.request_region(RegionRequest {
            range: FrameRange {
                first_frame: 64,
                end_frame: 128,
            },
            feature_mask: 1,
            priority: 200,
            soft_deadline_monotonic_ns: 0,
            request_id: 0,
        })
        .unwrap();
        assert_eq!(
            s.process(WorkBudget::default(), &token)
                .unwrap()
                .processing
                .consumed_input_frames,
            64
        );
        assert_eq!(
            pool.acquire().unwrap().overview().unwrap().spans[0].first_frame,
            64
        );
        assert_eq!(
            s.process(WorkBudget::default(), &token)
                .unwrap()
                .processing
                .consumed_input_frames,
            64
        );
        assert_eq!(s.session().session().state(), SessionState::Running);
        s.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(s.session().session().state(), SessionState::Complete);
        assert_eq!(
            pool.acquire().unwrap().overview().unwrap().state,
            FeatureState::Final
        );
        let source = s.into_source();
        assert_eq!(source.reads, 2);
        assert_eq!(source.released.get(), 2);
    });
}
#[test]
fn would_block_has_no_release_then_retries() {
    let source = Source::new(Mode::WouldBlock);
    let released = source.released.clone();
    run(source, |mut s, _| {
        let token = CancellationToken::new();
        assert!(
            s.process(WorkBudget::default(), &token)
                .unwrap()
                .would_block
        );
        assert_eq!(released.get(), 0);
        s.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(released.get(), 1);
        assert_eq!(s.into_source().reads, 2);
    });
}
#[test]
fn invalid_blocks_release_and_fail_source() {
    for mode in [
        Mode::BadFirst,
        Mode::Empty,
        Mode::TooLong,
        Mode::Error,
        Mode::Eof,
    ] {
        let source = Source::new(mode);
        let released = source.released.clone();
        run(source, |mut s, pool| {
            let token = CancellationToken::new();
            assert_eq!(s.process(WorkBudget::default(), &token), Err(Error::Source));
            assert_eq!(
                pool.acquire().unwrap().info().session_state,
                ResultSessionState::Failed
            );
            assert_eq!(
                released.get(),
                if matches!(mode, Mode::Error | Mode::Eof) {
                    0
                } else {
                    1
                }
            );
            assert_eq!(s.process(WorkBudget::default(), &token), Err(Error::Source));
            assert_eq!(s.into_source().reads, 1);
        });
    }
}
#[test]
fn callback_cancellation_accepts_then_releases_before_cancel() {
    let source = Source::new(Mode::Cancel);
    let cancel = source.cancel.clone();
    let released = source.released.clone();
    run(source, |mut s, pool| {
        assert_eq!(
            s.process(WorkBudget::default(), &cancel),
            Err(Error::Cancelled)
        );
        assert_eq!(released.get(), 1);
        assert_eq!(s.session().session().queued_frames(), 128);
        assert_eq!(s.session().session().processed_frames(), 0);
        assert_eq!(
            pool.acquire().unwrap().info().session_state,
            ResultSessionState::Cancelled
        );
    });
}
#[test]
fn failed_publication_is_retryable_and_rereads_source() {
    let source = Source::new(Mode::ErrorAfterFirst);
    let released = source.released.clone();
    run(source, |mut s, pool| {
        let held = pool.acquire().unwrap();
        let token = CancellationToken::new();
        s.process(
            WorkBudget {
                maximum_input_frames: 32,
                maximum_steps: 1,
            },
            &token,
        )
        .unwrap();
        assert_eq!(
            s.process(WorkBudget::default(), &token),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(released.get(), 1);
        assert_eq!(held.info().session_state, ResultSessionState::Created);
        assert_eq!(s.session().session().state(), SessionState::Running);
        drop(held);
        assert_eq!(s.process(WorkBudget::default(), &token), Err(Error::Source));
        assert_eq!(
            pool.acquire().unwrap().info().session_state,
            ResultSessionState::Failed
        );
        assert_eq!(s.into_source().reads, 3);
    });
}
