// SPDX-License-Identifier: Apache-2.0
//! Desktop synchronization boundary. Core sessions remain single-writer and
//! allocator-free. Arc control blocks use the standard allocator; result graphs
//! occupy explicit independent caller storage or independently owned heap arrays. No session or core pool is shared
//! across threads, and no core result lease survives a synchronization call.
#![forbid(unsafe_code)]
mod context;
mod growing;
pub use context::{ContextLimits, ContextUsage, RuntimeContext};
mod heap;
pub use growing::{GrowingLimits, GrowingSession, OwningSession};
pub use heap::{HeapResult, HeapResults};
use libapta::{
    owned_result::{OwnedResult, Storage},
    publication::{PublishedSession, PublishedSparseSession},
    session::{CancellationToken, Progress, WorkBudget},
    Error,
};
use std::sync::{Arc, RwLock};

/// Concurrent acquisition of immutable generations, with no fixed retained-result
/// count. One writer publishes complete independent copies. Reader acquisition
/// clones an Arc while holding a short read lock; retained readers never lock
/// processing or require an available core publication slot. Caller graph storage
/// must outlive its retained generation, as enforced by Rust's borrow checker.
#[derive(Clone)]
pub struct ConcurrentResults<'a> {
    latest: Arc<RwLock<Arc<OwnedResult<'a>>>>,
}
impl<'a> ConcurrentResults<'a> {
    pub fn new(initial: OwnedResult<'a>) -> Self {
        Self {
            latest: Arc::new(RwLock::new(Arc::new(initial))),
        }
    }
    pub fn acquire(&self) -> Result<Arc<OwnedResult<'a>>, Error> {
        self.latest
            .read()
            .map(|r| Arc::clone(&r))
            .map_err(|_| Error::Internal)
    }
    /// Publish a newer snapshot of the same source. A duration may resolve from
    /// unknown to known; it cannot change afterward. Failure leaves latest intact.
    pub fn publish(&self, result: OwnedResult<'a>) -> Result<(), Error> {
        let mut latest = self.latest.write().map_err(|_| Error::Internal)?;
        let mut source = result.source();
        if latest.source().total_frames.is_none() {
            source.total_frames = None;
        }
        if source != latest.source() || result.info().generation <= latest.info().generation {
            return Err(Error::Conflict);
        }
        *latest = Arc::new(result);
        Ok(())
    }
    fn refresh(&self, result: &OwnedResult<'_>, storage: Storage<'a>) -> Result<bool, Error> {
        let current = self.acquire()?;
        let mut source = result.source();
        if current.source().total_frames.is_none() {
            source.total_frames = None;
        }
        if source != current.source() {
            return Err(Error::Conflict);
        }
        if current.info().generation == result.info().generation {
            return Ok(false);
        }
        drop(current);
        self.publish(result.copy_to(storage)?)?;
        Ok(true)
    }
    /// Mirror the current native sequential generation. Short storage is retryable
    /// without rerunning processing, via another `refresh_session` call.
    pub fn refresh_session(
        &self,
        session: &PublishedSession<'_, '_, '_>,
        storage: Storage<'a>,
    ) -> Result<bool, Error> {
        let result = session.acquire_result()?;
        self.refresh(&result, storage)
    }
    pub fn refresh_sparse(
        &self,
        session: &PublishedSparseSession<'_, '_, '_>,
        storage: Storage<'a>,
    ) -> Result<bool, Error> {
        let result = session.acquire_result()?;
        self.refresh(&result, storage)
    }
    /// Process actual native PCM and mirror publication even on a native error
    /// after a committed intermediate generation. A copy failure takes precedence;
    /// inspect the native session's processed counts and retry only the mirror.
    pub fn process(
        &self,
        session: &mut PublishedSession<'_, '_, '_>,
        budget: WorkBudget,
        cancel: &CancellationToken,
        storage: Storage<'a>,
    ) -> Result<Progress, Error> {
        let work = session.process(budget, cancel);
        self.refresh_session(session, storage)?;
        work
    }
    pub fn process_sparse(
        &self,
        session: &mut PublishedSparseSession<'_, '_, '_>,
        budget: WorkBudget,
        cancel: &CancellationToken,
        storage: Storage<'a>,
    ) -> Result<Progress, Error> {
        let work = session.process(budget, cancel);
        self.refresh_sparse(session, storage)?;
        work
    }
}

mod growing_pull;
pub use growing_pull::GrowingPullSession;

mod owned_sparse;
pub use owned_sparse::{OwnedSparseSession, OwningSparseSession, SparseLimits};

mod owned_sparse_pull;
pub use owned_sparse_pull::OwnedScheduledPullSession;
