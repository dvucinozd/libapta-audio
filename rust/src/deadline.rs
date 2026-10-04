// SPDX-License-Identifier: Apache-2.0
//! One shared cooperative deadline for a process call and its analysis stages.

pub(crate) struct Deadline<'a> {
    clock: Option<&'a mut dyn FnMut() -> u64>,
    end_ns: u64,
}

impl<'a> Deadline<'a> {
    pub(crate) fn disabled() -> Self {
        Self {
            clock: None,
            end_ns: 0,
        }
    }

    pub(crate) fn new(soft_us: u32, clock: &'a mut dyn FnMut() -> u64) -> Self {
        if soft_us == 0 {
            return Self::disabled();
        }
        let now = clock();
        if now == 0 {
            return Self::disabled();
        }
        Self {
            clock: Some(clock),
            end_ns: now.saturating_add(u64::from(soft_us) * 1000),
        }
    }

    pub(crate) fn expired(&mut self) -> bool {
        self.clock
            .as_mut()
            .is_some_and(|clock| clock() >= self.end_ns)
    }

    /// The effective C runtime checks S4, S6, key, and meter in this order,
    /// even when only overview is enabled or a previous check has expired.
    /// Future analysis stages must use these same checks and this deadline.
    pub(crate) fn analysis_boundaries(&mut self) {
        for _ in 0..4 {
            let _ = self.expired();
        }
    }
}
