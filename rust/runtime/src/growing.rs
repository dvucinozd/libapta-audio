// SPDX-License-Identifier: Apache-2.0
//! Owning, fallibly growing sequential waveform/key sessions. Processing is the
//! existing portable Session; allocation and concurrent retention live here.
use crate::{HeapResult, HeapResults};
use libapta::{
    session::{
        CancellationToken, Progress, Session, SessionConfig, SessionState, WorkBudget,
        TOTAL_FRAMES_UNKNOWN,
    },
    waveform::{NormalizedSample, PcmView},
    *,
};

#[derive(Clone, Copy, Debug)]
pub struct GrowingLimits {
    pub maximum_queue_frames: usize,
    pub maximum_columns: usize,
    /// Queue and mutable column Vec capacities, excluding controls and snapshots.
    pub maximum_working_bytes: usize,
    pub results: NativeLimits,
}
impl Default for GrowingLimits {
    fn default() -> Self {
        Self {
            maximum_queue_frames: 32768,
            maximum_columns: 1_048_576,
            maximum_working_bytes: 64 * 1024 * 1024,
            results: NativeLimits::default(),
        }
    }
}
fn array<T: Default + Clone>(count: usize) -> Result<Vec<T>, Error> {
    let mut v = Vec::new();
    v.try_reserve_exact(count)
        .map_err(|_| Error::LimitExceeded)?;
    v.resize(count, T::default());
    Ok(v)
}
fn working_bytes(queue: usize, columns: usize) -> Result<usize, Error> {
    queue
        .checked_mul(core::mem::size_of::<NormalizedSample>())
        .and_then(|n| {
            columns
                .checked_mul(core::mem::size_of::<WaveformColumn>())
                .and_then(|c| n.checked_add(c))
        })
        .ok_or(Error::LimitExceeded)
}

pub type OwningSession = Session<
    'static,
    Vec<NormalizedSample>,
    Vec<WaveformColumn>,
    Vec<libapta::analysis::OnsetBin>,
    Vec<f32>,
    Vec<libapta::analysis::OnsetBin>,
    Vec<f32>,
    Vec<Beat>,
>;

/// One owning writer with independently retained heap generations. Supports
/// waveform, key and all default musical stages. Owning band/detail workspaces
/// remain a separate gate. This does not emulate C's bounded-slot generations.
/// After snapshot failure, retry `refresh` before further mutations. PCM already
/// processed remains committed and old acquired generations remain unchanged.
pub struct GrowingSession {
    session: OwningSession,
    limits: GrowingLimits,
    queue_capacity: usize,
    column_capacity: usize,
    allocated_queue: usize,
    allocated_columns: usize,
    results: HeapResults,
    dirty: bool,
    music_bytes: usize,
    music_enabled: bool,
    key_enabled: bool,
    context: Option<crate::RuntimeContext>,
    _context_resource: Option<crate::context::Resource>,
}
impl GrowingSession {
    pub fn new(config: SessionConfig, limits: GrowingLimits) -> Result<Self, Error> {
        Self::build(config, limits, None, None)
    }
    fn build(
        config: SessionConfig,
        limits: GrowingLimits,
        context: Option<crate::RuntimeContext>,
        context_resource: Option<crate::context::Resource>,
    ) -> Result<Self, Error> {
        if limits.maximum_queue_frames == 0 || config.frames_per_column == 0 {
            return Err(Error::InvalidArgument);
        }
        let columns = if config.total_frames == TOTAL_FRAMES_UNKNOWN {
            0
        } else {
            usize::try_from(
                config
                    .total_frames
                    .div_ceil(u64::from(config.frames_per_column)),
            )
            .map_err(|_| Error::LimitExceeded)?
        };
        let queue = limits.maximum_queue_frames.min(256);
        if columns > limits.maximum_columns
            || working_bytes(queue, columns)? > limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let q = array(queue)?;
        let o = array(columns)?;
        if working_bytes(q.capacity(), o.capacity())? > limits.maximum_working_bytes {
            return Err(Error::LimitExceeded);
        }
        let allocated_queue = q.capacity();
        let allocated_columns = o.capacity();
        let session = Session::with_storage(config, q, o)?;
        let mut initial = Self::snapshot(&session, 1, limits.results)?;
        if let Some(context) = &context {
            initial.attach_context(context)?;
        }
        Ok(Self {
            session,
            limits,
            queue_capacity: queue,
            column_capacity: columns,
            allocated_queue,
            allocated_columns,
            results: HeapResults::new(initial),
            dirty: false,
            music_bytes: 0,
            music_enabled: false,
            key_enabled: false,
            context,
            _context_resource: context_resource,
        })
    }
    pub(crate) fn new_in_context(
        config: SessionConfig,
        limits: GrowingLimits,
        context: crate::RuntimeContext,
        resource: crate::context::Resource,
    ) -> Result<Self, Error> {
        Self::build(config, limits, Some(context), Some(resource))
    }
    pub fn session(&self) -> &OwningSession {
        &self.session
    }
    pub fn results(&self) -> HeapResults {
        self.results.clone()
    }
    pub fn enable_key(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        self.session
            .enable_key_with_math(libapta::key_analysis::KeyMath {
                cos: f32::cos,
                log: f32::ln,
                sqrt: f32::sqrt,
            })?;
        self.key_enabled = true;
        Ok(())
    }
    /// Own all default musical workspaces before PCM. Every allocation and
    /// aggregate byte limit is preflighted before any analysis stage is attached.
    pub fn enable_default_music(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        if self.music_enabled || self.key_enabled || self.session.state() != SessionState::Created {
            return Err(Error::InvalidState);
        }
        use libapta::{analysis, global_analysis};
        let minimum = working_bytes(self.allocated_queue, self.allocated_columns)?;
        let bytes = (analysis::BIN_CAPACITY + global_analysis::BIN_CAPACITY)
            .checked_mul(core::mem::size_of::<analysis::OnsetBin>() + core::mem::size_of::<f32>())
            .and_then(|n| n.checked_add(global_analysis::MAX_BEATS * core::mem::size_of::<Beat>()))
            .ok_or(Error::LimitExceeded)?;
        if minimum.checked_add(bytes).ok_or(Error::LimitExceeded)?
            > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let bins = array::<analysis::OnsetBin>(analysis::BIN_CAPACITY)?;
        let flux = array::<f32>(analysis::BIN_CAPACITY)?;
        let global_bins = array::<analysis::OnsetBin>(global_analysis::BIN_CAPACITY)?;
        let global_flux = array::<f32>(global_analysis::BIN_CAPACITY)?;
        let beats = array::<Beat>(global_analysis::MAX_BEATS)?;
        let actual = (bins.capacity() + global_bins.capacity())
            .checked_mul(core::mem::size_of::<analysis::OnsetBin>())
            .and_then(|n| {
                (flux.capacity() + global_flux.capacity())
                    .checked_mul(core::mem::size_of::<f32>())
                    .and_then(|f| n.checked_add(f))
            })
            .and_then(|n| {
                beats
                    .capacity()
                    .checked_mul(core::mem::size_of::<Beat>())
                    .and_then(|b| n.checked_add(b))
            })
            .ok_or(Error::LimitExceeded)?;
        if minimum.checked_add(actual).ok_or(Error::LimitExceeded)?
            > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        self.session.enable_tempo(bins, flux)?;
        self.session
            .enable_global_grid(true, global_bins, global_flux, beats)?;
        self.session.enable_meter()?;
        self.session
            .enable_key_with_math(libapta::key_analysis::KeyMath {
                cos: f32::cos,
                log: f32::ln,
                sqrt: f32::sqrt,
            })?;
        self.session.enable_calibrated_quality()?;
        self.music_bytes = actual;
        self.music_enabled = true;
        self.key_enabled = true;
        Ok(())
    }
    fn ensure_clean(&self) -> Result<(), Error> {
        if self.dirty {
            Err(Error::InvalidState)
        } else {
            Ok(())
        }
    }
    /// Reserve queue/output together before committing either replacement.
    /// Growth keeps queued FIFO samples, a partial column and key history intact.
    fn reserve(&mut self, queue: usize, columns: usize) -> Result<(), Error> {
        let queue = queue.max(self.queue_capacity);
        let columns = columns.max(self.column_capacity);
        if queue > self.limits.maximum_queue_frames
            || columns > self.limits.maximum_columns
            || working_bytes(queue, columns)?
                .checked_add(self.music_bytes)
                .ok_or(Error::LimitExceeded)?
                > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        let q = if queue > self.queue_capacity {
            Some(array::<NormalizedSample>(queue)?)
        } else {
            None
        };
        let o = if columns > self.column_capacity {
            Some(array::<WaveformColumn>(columns)?)
        } else {
            None
        };
        let actual_q = q.as_ref().map_or(self.allocated_queue, Vec::capacity);
        let actual_o = o.as_ref().map_or(self.allocated_columns, Vec::capacity);
        if working_bytes(actual_q, actual_o)?
            .checked_add(self.music_bytes)
            .ok_or(Error::LimitExceeded)?
            > self.limits.maximum_working_bytes
        {
            return Err(Error::LimitExceeded);
        }
        self.allocated_queue = actual_q;
        self.allocated_columns = actual_o;
        if let Some(o) = o {
            self.session.replace_output(o)?;
            self.column_capacity = columns;
        }
        if let Some(q) = q {
            self.session.replace_queue(q)?;
            self.queue_capacity = queue;
        }
        Ok(())
    }
    pub fn push_pcm(&mut self, pcm: PcmView<'_>) -> Result<usize, Error> {
        self.ensure_clean()?;
        if !matches!(
            self.session.state(),
            SessionState::Created | SessionState::Running
        ) {
            return Err(Error::InvalidState);
        }
        let config = self.session.config();
        let supplied = pcm.frame_count(config.channel_count)?;
        let n = supplied
            .min(self.limits.maximum_queue_frames - self.session.queued_frames())
            .min(
                usize::try_from(
                    self.session
                        .total_frames()
                        .map_or(u64::MAX, |t| t - self.session.accepted_frames()),
                )
                .unwrap_or(usize::MAX),
            );
        for i in 0..n {
            pcm.sample_frame(i, config.channel_count)?;
        }
        let end = self
            .session
            .accepted_frames()
            .checked_add(n as u64)
            .ok_or(Error::LimitExceeded)?;
        let columns = usize::try_from(end.div_ceil(u64::from(config.frames_per_column)))
            .map_err(|_| Error::LimitExceeded)?;
        // Geometric output growth amortizes copying, with a hard caller ceiling.
        let columns = if columns > self.column_capacity {
            columns
                .max(self.column_capacity.saturating_mul(2))
                .min(self.limits.maximum_columns)
        } else {
            self.column_capacity
        };
        if end.div_ceil(u64::from(config.frames_per_column)) > columns as u64 {
            return Err(Error::LimitExceeded);
        }
        self.reserve(self.session.queued_frames() + n, columns)?;
        let accepted = self.session.push_pcm(pcm)?;
        if accepted != 0 {
            self.dirty = true;
            self.refresh()?;
        }
        Ok(accepted)
    }
    pub fn finish_input(&mut self) -> Result<(), Error> {
        self.ensure_clean()?;
        self.session.finish_input()?;
        self.dirty = true;
        self.refresh()?;
        Ok(())
    }
    pub fn process(
        &mut self,
        budget: WorkBudget,
        cancel: &CancellationToken,
    ) -> Result<Progress, Error> {
        self.ensure_clean()?;
        let before = (
            self.session.state(),
            self.session.processed_frames(),
            self.session.publication_serials(),
        );
        let work = self.session.process(budget, cancel);
        if before
            != (
                self.session.state(),
                self.session.processed_frames(),
                self.session.publication_serials(),
            )
        {
            self.dirty = true;
            self.refresh()?;
        }
        work
    }
    /// Retry only an unmirrored publication; no PCM or analysis step is repeated.
    pub fn refresh(&mut self) -> Result<bool, Error> {
        if !self.dirty {
            return Ok(false);
        }
        let generation = self
            .results
            .acquire()?
            .info()
            .generation
            .checked_add(1)
            .ok_or(Error::LimitExceeded)?;
        let mut result = Self::snapshot(&self.session, generation, self.limits.results)?;
        if let Some(context) = &self.context {
            result.attach_context(context)?;
        }
        self.results.publish(result)?;
        self.dirty = false;
        Ok(true)
    }
    /// Adjust result limits to retry a rejected snapshot. Working limits stay fixed.
    pub fn set_result_limits(&mut self, limits: NativeLimits) {
        self.limits.results = limits;
    }
    fn snapshot(
        session: &OwningSession,
        generation: u64,
        limits: NativeLimits,
    ) -> Result<HeapResult, Error> {
        HeapResult::copy_snapshot(&session.snapshot(generation)?, limits)
    }
}
