// SPDX-License-Identifier: Apache-2.0
//! Safe std context lifetime and retained-graph accounting. This is not a C
//! custom allocator, allocation-class implementation, or physical heap budget.
use crate::{GrowingLimits, GrowingSession};
use libapta::{session::SessionConfig, Error};
use std::sync::{Arc, Mutex};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ContextLimits {
    pub maximum_sessions: usize,
    pub maximum_results: usize,
    pub maximum_retained_bytes: usize,
}
impl Default for ContextLimits {
    fn default() -> Self {
        Self {
            maximum_sessions: usize::MAX,
            maximum_results: usize::MAX,
            maximum_retained_bytes: usize::MAX,
        }
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ContextUsage {
    pub sessions: usize,
    pub results: usize,
    /// Actual retained HeapResult array capacities and header bytes. Arc/lock
    /// control allocation and mutable workspaces are excluded explicitly.
    pub retained_bytes: usize,
    pub closed: bool,
}
struct State {
    limits: ContextLimits,
    usage: ContextUsage,
}
/// Clones identify the same context. Logical close is serialized with resource
/// registration and returns Busy until writers, current graphs and retained
/// readers have all released their resources. Closed handles cannot reopen.
#[derive(Clone)]
pub struct RuntimeContext {
    state: Arc<Mutex<State>>,
}
impl RuntimeContext {
    pub fn new(limits: ContextLimits) -> Self {
        Self {
            state: Arc::new(Mutex::new(State {
                limits,
                usage: ContextUsage::default(),
            })),
        }
    }
    pub fn usage(&self) -> Result<ContextUsage, Error> {
        self.state
            .lock()
            .map(|s| s.usage)
            .map_err(|_| Error::Internal)
    }
    pub fn close(&self) -> Result<(), Error> {
        let mut state = self.state.lock().map_err(|_| Error::Internal)?;
        if state.usage.closed {
            return Err(Error::InvalidState);
        }
        if state.usage.sessions != 0 || state.usage.results != 0 || state.usage.retained_bytes != 0
        {
            return Err(Error::Busy);
        }
        state.usage.closed = true;
        Ok(())
    }
    pub fn create_session(
        &self,
        config: SessionConfig,
        limits: GrowingLimits,
    ) -> Result<GrowingSession, Error> {
        let session = self.register(true, 0)?;
        GrowingSession::new_in_context(config, limits, self.clone(), session)
    }
    pub fn create_sparse_session(
        &self,
        config: SessionConfig,
        limits: crate::SparseLimits,
    ) -> Result<crate::OwnedSparseSession, Error> {
        let resource = self.register(true, 0)?;
        crate::OwnedSparseSession::new_in_context(config, limits, self.clone(), resource)
    }
    pub(crate) fn result(&self, bytes: usize) -> Result<Resource, Error> {
        self.register(false, bytes)
    }
    fn register(&self, session: bool, bytes: usize) -> Result<Resource, Error> {
        let mut s = self.state.lock().map_err(|_| Error::Internal)?;
        if s.usage.closed {
            return Err(Error::InvalidState);
        }
        let mut next = s.usage;
        if session {
            next.sessions = next.sessions.checked_add(1).ok_or(Error::LimitExceeded)?;
        } else {
            next.results = next.results.checked_add(1).ok_or(Error::LimitExceeded)?;
            next.retained_bytes = next
                .retained_bytes
                .checked_add(bytes)
                .ok_or(Error::LimitExceeded)?;
        }
        if next.sessions > s.limits.maximum_sessions
            || next.results > s.limits.maximum_results
            || next.retained_bytes > s.limits.maximum_retained_bytes
        {
            return Err(Error::LimitExceeded);
        }
        s.usage = next;
        Ok(Resource {
            context: self.clone(),
            session,
            bytes,
        })
    }
}
pub(crate) struct Resource {
    context: RuntimeContext,
    session: bool,
    bytes: usize,
}
impl Drop for Resource {
    fn drop(&mut self) {
        // Recover poison on release so resource accounting cannot leak after a
        // panic. No user callback executes while the context lock is held.
        let mut state = self.context.state.lock().unwrap_or_else(|p| p.into_inner());
        if self.session {
            state.usage.sessions -= 1;
        } else {
            state.usage.results -= 1;
            state.usage.retained_bytes -= self.bytes;
        }
    }
}
