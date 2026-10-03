// SPDX-License-Identifier: Apache-2.0
use core::cell::Cell;
use libapta::pull::{PullBlock, PullRead, PullSession, PullSource, PullState};
use libapta::session::{CancellationToken, SessionConfig, WorkBudget, TOTAL_FRAMES_UNKNOWN};
use libapta::waveform::{NormalizedSample, PcmView};
use libapta::{Error, WaveformColumn};

struct Source<'a, F> {
    samples: &'a [f32],
    release: F,
    reads: &'a Cell<usize>,
    block_once: bool,
    error: Option<Error>,
    wrong_offset: bool,
    oversized: bool,
    empty: bool,
    cancel: Option<&'a CancellationToken>,
}
impl<F: FnMut()> PullSource for Source<'_, F> {
    fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
        self.reads.set(self.reads.get() + 1);
        assert!((1..=256).contains(&maximum));
        if let Some(token) = self.cancel {
            token.cancel();
        }
        if let Some(error) = self.error {
            return Err(error);
        }
        if self.block_once {
            self.block_once = false;
            return Ok(PullRead::WouldBlock);
        }
        if first as usize >= self.samples.len() && !self.empty {
            return Ok(PullRead::EndOfInput);
        }
        let start = first as usize;
        let count = if self.empty {
            0
        } else if self.oversized {
            self.samples.len() - start
        } else {
            maximum as usize
        };
        let end = (start + count).min(self.samples.len());
        Ok(PullRead::Data(PullBlock::new(
            first + u64::from(self.wrong_offset),
            PcmView::F32Interleaved(&self.samples[start..end]),
            &mut self.release,
        )))
    }
}
fn cfg(total_frames: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames,
        frames_per_column: 64,
    }
}
fn source<'a, F: FnMut()>(samples: &'a [f32], reads: &'a Cell<usize>, release: F) -> Source<'a, F> {
    Source {
        samples,
        reads,
        release,
        block_once: false,
        error: None,
        wrong_offset: false,
        oversized: false,
        empty: false,
        cancel: None,
    }
}
#[test]
fn bounded_pull_known_and_unknown_exact_columns_and_owned_snapshot() {
    let pcm: [f32; 321] = core::array::from_fn(|i| (i as f32 - 160.0) / 160.0);
    let mut results = [[WaveformColumn::default(); 6]; 2];
    for (total, result) in [321, TOTAL_FRAMES_UNKNOWN].into_iter().zip(&mut results) {
        let reads = Cell::new(0);
        let releases = Cell::new(0);
        let input = source(&pcm, &reads, || releases.set(releases.get() + 1));
        let mut queue = [NormalizedSample::default(); 77];
        let mut output = [WaveformColumn::default(); 6];
        let mut pull = PullSession::new(cfg(total), &mut queue, &mut output, input).unwrap();
        let token = CancellationToken::new();
        while pull.state() != PullState::Complete {
            let before = reads.get();
            let p = pull
                .process(
                    WorkBudget {
                        maximum_input_frames: 7,
                        maximum_steps: 1,
                    },
                    &token,
                )
                .unwrap();
            assert!(reads.get() - before <= 1);
            assert!(p.processing.consumed_input_frames <= 7);
            assert!(p.processing.completed_steps <= 1);
            assert!(!p.would_block);
        }
        assert_eq!(pull.total_frames(), Some(321));
        assert_eq!(pull.processed_frames(), 321);
        assert_eq!(releases.get(), 46);
        assert_eq!(reads.get(), 46 + usize::from(total == TOTAL_FRAMES_UNKNOWN));
        pull.copy_snapshot_into(result).unwrap();
        token.cancel();
        assert_eq!(
            pull.process(WorkBudget::default(), &token)
                .unwrap()
                .processing
                .completed_steps,
            0
        );
        let _returned = pull.into_inner();
        assert_eq!(releases.get(), 46);
    }
    assert_eq!(results[0], results[1]);
}
#[test]
fn would_block_retries_without_release_and_default_budget_is_bounded() {
    let reads = Cell::new(0);
    let releases = Cell::new(0);
    let pcm = [0.25; 300];
    let mut input = source(&pcm, &reads, || releases.set(releases.get() + 1));
    input.block_once = true;
    let mut queue = [NormalizedSample::default(); 300];
    let mut output = [WaveformColumn::default(); 5];
    let mut pull = PullSession::new(cfg(300), &mut queue, &mut output, input).unwrap();
    let token = CancellationToken::new();
    assert!(
        pull.process(WorkBudget::default(), &token)
            .unwrap()
            .would_block
    );
    assert_eq!(releases.get(), 0);
    let p = pull.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(p.processing.consumed_input_frames, 256);
    assert_eq!(p.processing.completed_steps, 1);
    assert_eq!(pull.state(), PullState::Running);
    assert_eq!(releases.get(), 1);
    pull.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(pull.state(), PullState::Complete);
    assert_eq!(releases.get(), 2);
}
#[test]
fn malformed_blocks_release_once_and_failure_is_terminal() {
    for mode in 0..5 {
        let reads = Cell::new(0);
        let releases = Cell::new(0);
        let mut pcm = [0.25; 65];
        if mode == 3 {
            pcm[0] = f32::NAN;
        }
        let mut input = source(&pcm, &reads, || releases.set(releases.get() + 1));
        input.wrong_offset = mode == 0;
        input.oversized = mode == 1;
        input.empty = mode == 2;
        let mut config = cfg(64);
        if mode == 4 {
            config.channel_count = 2;
        }
        let mut queue = [NormalizedSample::default(); 64];
        let mut output = [WaveformColumn::default(); 1];
        let mut pull = PullSession::new(config, &mut queue, &mut output, input).unwrap();
        let token = CancellationToken::new();
        let budget = WorkBudget {
            maximum_input_frames: 3,
            maximum_steps: 1,
        };
        assert_eq!(
            pull.process(budget, &token),
            Err(Error::InvalidArgument),
            "mode {mode}"
        );
        assert_eq!(pull.state(), PullState::Failed);
        assert_eq!(pull.accepted_frames(), 0);
        assert_eq!(releases.get(), 1);
        assert_eq!(pull.process(budget, &token), Err(Error::InvalidArgument));
        assert_eq!(reads.get(), 1);
        assert_eq!(releases.get(), 1);
    }
}
#[test]
fn cancellation_before_and_after_read_releases_only_acquired_blocks() {
    for after in [false, true] {
        let token = CancellationToken::new();
        let reads = Cell::new(0);
        let releases = Cell::new(0);
        let pcm = [0.25; 64];
        let mut input = source(&pcm, &reads, || releases.set(releases.get() + 1));
        if after {
            input.cancel = Some(&token);
        } else {
            token.cancel();
        }
        let mut queue = [NormalizedSample::default(); 64];
        let mut output = [WaveformColumn::default(); 1];
        let mut pull = PullSession::new(cfg(64), &mut queue, &mut output, input).unwrap();
        assert_eq!(
            pull.process(WorkBudget::default(), &token),
            Err(Error::Cancelled)
        );
        assert_eq!(pull.state(), PullState::Cancelled);
        assert_eq!(pull.accepted_frames(), 0);
        assert_eq!(reads.get(), usize::from(after));
        assert_eq!(releases.get(), usize::from(after));
        assert_eq!(
            pull.process(WorkBudget::default(), &token),
            Err(Error::Cancelled)
        );
    }
}
#[test]
fn errors_and_early_eof_are_terminal_without_release() {
    for source_error in [None, Some(Error::NotAvailable)] {
        let reads = Cell::new(0);
        let releases = Cell::new(0);
        let mut input = source(&[], &reads, || releases.set(releases.get() + 1));
        input.error = source_error;
        let mut queue = [NormalizedSample::default(); 1];
        let mut output = [WaveformColumn::default(); 1];
        let mut pull = PullSession::new(cfg(1), &mut queue, &mut output, input).unwrap();
        let expected = source_error.unwrap_or(Error::InvalidArgument);
        assert_eq!(
            pull.process(WorkBudget::default(), &CancellationToken::new()),
            Err(expected)
        );
        assert_eq!(pull.failure(), Some(expected));
        assert_eq!(releases.get(), 0);
    }
}
#[test]
fn unknown_capacity_probe_releases_overflow_and_completes_exact_fit() {
    for length in [64, 65] {
        let reads = Cell::new(0);
        let releases = Cell::new(0);
        let pcm = [0.25; 65];
        let input = source(&pcm[..length], &reads, || releases.set(releases.get() + 1));
        let mut queue = [NormalizedSample::default(); 64];
        let mut output = [WaveformColumn::default(); 1];
        let mut pull =
            PullSession::new(cfg(TOTAL_FRAMES_UNKNOWN), &mut queue, &mut output, input).unwrap();
        let token = CancellationToken::new();
        pull.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(pull.accepted_frames(), 64);
        assert_eq!(releases.get(), 1);
        let result = pull.process(WorkBudget::default(), &token);
        if length == 64 {
            assert!(result.is_ok());
            assert_eq!(pull.state(), PullState::Complete);
            assert_eq!(releases.get(), 1);
        } else {
            assert_eq!(result, Err(Error::BufferTooSmall));
            assert_eq!(pull.state(), PullState::Failed);
            assert_eq!(releases.get(), 2);
        }
        assert_eq!(pull.accepted_frames(), 64);
        assert_eq!(reads.get(), 2);
    }
}
#[test]
fn empty_sources_and_zero_capacity() {
    for total in [0, TOTAL_FRAMES_UNKNOWN] {
        let reads = Cell::new(0);
        let releases = Cell::new(0);
        let input = source(&[], &reads, || releases.set(releases.get() + 1));
        let mut queue = [NormalizedSample::default(); 1];
        let mut pull = PullSession::new(cfg(total), &mut queue, &mut [], input).unwrap();
        pull.process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        assert_eq!(pull.state(), PullState::Complete);
        assert_eq!(pull.total_frames(), Some(0));
        assert_eq!(reads.get(), usize::from(total == TOTAL_FRAMES_UNKNOWN));
        assert_eq!(releases.get(), 0);
    }
}

#[test]
fn cancellation_in_release_stops_processing_after_copy() {
    let token = CancellationToken::new();
    let reads = Cell::new(0);
    let releases = Cell::new(0);
    let pcm = [0.25; 64];
    let input = source(&pcm, &reads, || {
        releases.set(releases.get() + 1);
        token.cancel();
    });
    let mut queue = [NormalizedSample::default(); 64];
    let mut output = [WaveformColumn::default(); 1];
    let mut pull = PullSession::new(cfg(64), &mut queue, &mut output, input).unwrap();
    assert_eq!(
        pull.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(pull.state(), PullState::Cancelled);
    assert_eq!(pull.accepted_frames(), 64);
    assert_eq!(pull.processed_frames(), 0);
    assert_eq!(releases.get(), 1);
    assert!(pull.columns().is_empty());
}
