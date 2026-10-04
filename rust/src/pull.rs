// SPDX-License-Identifier: Apache-2.0
//! Bounded sequential pull adapter with borrowed, exactly-once block release.
//! Each process call makes at most one source callback and processes at most
//! one 256-frame step, even when the supplied budget permits more work.
use crate::session::{
    CancellationToken, Progress, Session, SessionConfig, SessionState, WorkBudget,
};
use crate::waveform::{NormalizedSample, PcmView};
use crate::{Error, WaveformColumn};

/// An acquired source block. Dropping it invokes release exactly once, including
/// malformed-block and cancellation paths. The callback must not panic.
/// PCM and release storage remain borrowed until the block is dropped.
pub struct PullBlock<'a> {
    first_frame: u64,
    pcm: PcmView<'a>,
    release: &'a mut dyn FnMut(),
}
impl<'a> PullBlock<'a> {
    pub fn new(first_frame: u64, pcm: PcmView<'a>, release: &'a mut dyn FnMut()) -> Self {
        Self {
            first_frame,
            pcm,
            release,
        }
    }
    pub(crate) fn first_frame(&self) -> u64 {
        self.first_frame
    }
    pub(crate) fn pcm(&self) -> PcmView<'_> {
        self.pcm
    }
}
impl Drop for PullBlock<'_> {
    fn drop(&mut self) {
        (self.release)();
    }
}
pub enum PullRead<'a> {
    Data(PullBlock<'a>),
    WouldBlock,
    EndOfInput,
}
/// A source supporting reads at absolute frame offsets. A successful data response must
/// start at `first_frame` and contain 1..=maximum_frames complete frames.
/// EOF, WouldBlock and errors transfer no block and require no release call.
/// Callbacks must return promptly; the adapter cannot impose a wall-clock bound.
pub trait PullSource {
    /// Optional length, queried once when attaching a scheduled source.
    /// Sequential adapters use their explicit session configuration.
    fn total_frames(&mut self) -> Option<u64> {
        None
    }
    fn read_frames(&mut self, first_frame: u64, maximum_frames: u32)
        -> Result<PullRead<'_>, Error>;
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PullState {
    Created,
    Running,
    Draining,
    Complete,
    Cancelled,
    Failed,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PullProgress {
    pub processing: Progress,
    /// True only when the source explicitly asks the caller to retry later.
    pub would_block: bool,
}
/// Owns one source for its lifetime; push input and source replacement are not
/// exposed. Processing errors are terminal; WouldBlock is retryable. Source errors are
/// returned unchanged and retained by `failure()`.
pub struct PullSession<'a, S> {
    session: Session<'a>,
    source: S,
    queue_capacity: usize,
    failure: Option<Error>,
}
impl<'a, S: PullSource> PullSession<'a, S> {
    pub fn new(
        config: SessionConfig,
        queue: &'a mut [NormalizedSample],
        output: &'a mut [WaveformColumn],
        source: S,
    ) -> Result<Self, Error> {
        let queue_capacity = queue.len();
        Ok(Self {
            session: Session::new(config, queue, output)?,
            source,
            queue_capacity,
            failure: None,
        })
    }
    pub fn state(&self) -> PullState {
        match self.failure {
            Some(Error::Cancelled) => PullState::Cancelled,
            Some(_) => PullState::Failed,
            None => match self.session.state() {
                SessionState::Created => PullState::Created,
                SessionState::Running => PullState::Running,
                SessionState::Draining => PullState::Draining,
                SessionState::Complete => PullState::Complete,
                SessionState::Cancelled => PullState::Cancelled,
                SessionState::Failed => PullState::Failed,
            },
        }
    }
    pub fn failure(&self) -> Option<Error> {
        self.failure
    }
    /// Attach caller-owned three-band storage before the first source read.
    pub fn enable_three_band(
        &mut self,
        sums: &'a mut [crate::band::BandSums],
    ) -> Result<(), Error> {
        self.session.enable_three_band(sums)
    }

    /// Attach caller-owned eager detail storage before the first source read.
    pub fn enable_detail(
        &mut self,
        tiles: &'a mut [crate::detail_analysis::DetailTile],
    ) -> Result<(), Error> {
        self.session.enable_detail(tiles)
    }
    pub fn copy_detail_into<'b>(
        &self,
        tiles: &'b mut [crate::NativeTile],
        columns: &'b mut [WaveformColumn],
    ) -> Result<Option<crate::NativeDetail<'b>>, Error> {
        self.session.copy_detail_into(tiles, columns)
    }
    pub fn config(&self) -> SessionConfig {
        self.session.config()
    }
    pub fn total_frames(&self) -> Option<u64> {
        self.session.total_frames()
    }
    pub fn accepted_frames(&self) -> u64 {
        self.session.accepted_frames()
    }
    pub fn processed_frames(&self) -> u64 {
        self.session.processed_frames()
    }
    pub fn columns(&self) -> &[WaveformColumn] {
        self.session.columns()
    }
    pub fn copy_snapshot_into(&self, output: &mut [WaveformColumn]) -> Result<usize, Error> {
        self.session.copy_snapshot_into(output)
    }
    /// Consume the adapter to recover the source. No source block is retained
    /// between process calls, so this never needs an additional release.
    pub fn into_inner(self) -> S {
        self.source
    }
    /// Requests at most 256 frames and obeys the frame budget; zero fields mean
    /// unlimited. A call performs at most one processing step and one read.
    /// At unknown-duration output capacity, a one-frame EOF probe distinguishes
    /// exact-fit input from overflow. Extra data is released without acceptance
    /// and fails with BufferTooSmall. This also permits an empty zero-capacity source.
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
    ) -> Result<PullProgress, Error> {
        self.process_clock(budget, cancellation, None)
    }
    /// Exclude source read/release latency from the injected soft deadline.
    pub fn process_with_clock(
        &mut self,
        budget: WorkBudget,
        soft_us: u32,
        clock: &mut dyn FnMut() -> u64,
        cancellation: &CancellationToken,
    ) -> Result<PullProgress, Error> {
        self.process_clock(budget, cancellation, Some((soft_us, clock)))
    }
    fn process_clock(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<PullProgress, Error> {
        if let Some(error) = self.failure {
            return Err(error);
        }
        let result = self.step(budget, cancellation, clock);
        if let Err(error) = result {
            self.failure = Some(error);
        }
        result
    }
    fn step(
        &mut self,
        budget: WorkBudget,
        cancellation: &CancellationToken,
        clock: Option<(u32, &mut dyn FnMut() -> u64)>,
    ) -> Result<PullProgress, Error> {
        let process = |session: &mut Session<'_>, budget, clock| match clock {
            Some((soft_us, clock)) => {
                session.process_with_clock(budget, soft_us, clock, cancellation)
            }
            None => session.process(budget, cancellation),
        };
        if self.session.state() == SessionState::Complete {
            return Ok(PullProgress {
                processing: process(&mut self.session, budget, clock)?,
                would_block: false,
            });
        }
        if cancellation.is_cancelled() {
            process(&mut self.session, budget, clock)?;
            return Err(Error::Cancelled);
        }
        let first = self.session.accepted_frames();
        if self.session.total_frames() == Some(first) {
            self.session.finish_input()?;
            return Ok(PullProgress {
                processing: process(&mut self.session, budget, clock)?,
                would_block: false,
            });
        }
        let remaining = self.session.input_capacity_frames() - first;
        let probing = remaining == 0;
        let frame_budget = if budget.maximum_input_frames == 0 {
            u32::MAX
        } else {
            budget.maximum_input_frames
        };
        let maximum = (self.queue_capacity.min(256) as u32)
            .min(frame_budget)
            .min(if probing {
                1
            } else {
                remaining.min(u64::from(u32::MAX)) as u32
            });
        let response = self.source.read_frames(first, maximum);
        if cancellation.is_cancelled() {
            drop(response);
            process(&mut self.session, budget, clock)?;
            return Err(Error::Cancelled);
        }
        match response? {
            PullRead::WouldBlock => Ok(PullProgress {
                processing: process(&mut self.session, budget, clock)?,
                would_block: true,
            }),
            PullRead::EndOfInput => {
                // Known-length early EOF fails through the existing finish contract.
                if self.session.total_frames().is_some() {
                    return Err(Error::InvalidArgument);
                }
                self.session.finish_input()?;
                Ok(PullProgress {
                    processing: process(&mut self.session, budget, clock)?,
                    would_block: false,
                })
            }
            PullRead::Data(block) => {
                let frames = block.pcm.frame_count(self.session.config().channel_count)?;
                if block.first_frame != first || frames == 0 || frames > maximum as usize {
                    return Err(Error::InvalidArgument);
                }
                if probing {
                    return Err(Error::BufferTooSmall);
                }
                let accepted = self.session.push_pcm(block.pcm)?;
                if accepted != frames {
                    return Err(Error::InvalidState);
                }
                drop(block);
                if self.session.total_frames() == Some(self.session.accepted_frames()) {
                    self.session.finish_input()?;
                }
                Ok(PullProgress {
                    processing: process(
                        &mut self.session,
                        WorkBudget {
                            maximum_input_frames: maximum,
                            maximum_steps: 1,
                        },
                        clock,
                    )?,
                    would_block: false,
                })
            }
        }
    }
}
