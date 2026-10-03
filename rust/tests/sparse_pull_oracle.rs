// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::{OwnedResult, Storage},
    publication::{PublishedSparseSession, ResultPool},
    pull::{PullBlock, PullRead, PullSource},
    scheduler::RequestSlot,
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace},
    sparse_pull::ScheduledPullSession,
    waveform::{NormalizedSample, PcmView},
    *,
};
use std::{cell::Cell, fmt::Write};
#[derive(Default)]
struct Counts {
    reads: Cell<u32>,
    releases: Cell<u32>,
    first: Cell<u64>,
    maximum: Cell<u32>,
}
struct Source<'a> {
    scenario: u32,
    counts: &'a Counts,
    cancel: &'a CancellationToken,
    pcm: [i16; 4097],
    release: &'a mut dyn FnMut(),
}
impl PullSource for Source<'_> {
    fn total_frames(&mut self) -> Option<u64> {
        Some(4096)
    }
    fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
        let reads = self.counts.reads.get() + 1;
        self.counts.reads.set(reads);
        self.counts.first.set(first);
        self.counts.maximum.set(maximum);
        if self.scenario == 13 {
            self.cancel.cancel();
            return Ok(PullRead::WouldBlock);
        }
        if self.scenario == 1 && reads == 1 {
            return Ok(PullRead::WouldBlock);
        }
        if self.scenario == 4 || self.scenario == 14 || (self.scenario == 6 && reads >= 2) {
            return Err(Error::Corrupt);
        }
        if self.scenario == 5 {
            return Ok(PullRead::EndOfInput);
        }
        let mut n = maximum as usize;
        if self.scenario == 1 {
            n = n.min(300)
        }
        if self.scenario == 8 {
            n = 0
        }
        if self.scenario == 9 {
            n += 1
        }
        for (i, v) in self.pcm[..n].iter_mut().enumerate() {
            *v = (1000 + ((first + i as u64) / 1024) * 1000) as i16;
        }
        if self.scenario == 7 {
            self.cancel.cancel();
        }
        Ok(PullRead::Data(PullBlock::new(
            first + u64::from(self.scenario == 3),
            PcmView::S16Interleaved(&self.pcm[..n]),
            self.release,
        )))
    }
}
fn state(s: SessionState) -> u8 {
    match s {
        SessionState::Created => 0,
        SessionState::Running => 1,
        SessionState::Draining => 2,
        SessionState::Complete => 3,
        SessionState::Cancelled => 4,
        SessionState::Failed => 5,
    }
}
fn snapshot(
    out: &mut String,
    step: u32,
    status: i32,
    c: &Counts,
    s: SessionState,
    r: &OwnedResult<'_>,
) {
    let w = r.overview();
    writeln!(
        out,
        "H {step} {status} {} {} {} {} {} {} {} {} {} {} {} {}",
        c.reads.get(),
        c.releases.get(),
        c.first.get(),
        c.maximum.get(),
        state(s),
        r.info().generation,
        r.info().session_state as u8,
        r.available_features(),
        r.changed_features(),
        w.map_or(0, |w| w.state as u8),
        w.map_or(0, |w| w.spans.len()),
        w.map_or(0, |w| w.confidence)
    )
    .unwrap();
    if let Some(w) = w {
        for span in w.spans {
            writeln!(
                out,
                "S {} {} {}",
                span.first_frame, span.end_frame, span.column_count
            )
            .unwrap();
            for c in &w.columns[span.data_column_offset as usize
                ..(span.data_column_offset + span.column_count) as usize]
            {
                writeln!(
                    out,
                    "C {} {} {} {} {} {} {}",
                    c.minimum, c.maximum, c.rms, c.low, c.mid, c.high, c.flags
                )
                .unwrap();
            }
        }
    }
}
fn native(id: u32) -> String {
    let counts = Counts::default();
    let cancel = CancellationToken::new();
    let mut release = || counts.releases.set(counts.releases.get() + 1);
    let source = Source {
        scenario: id,
        counts: &counts,
        cancel: &cancel,
        pcm: [0; 4097],
        release: &mut release,
    };
    let info = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(4096),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let mut ca = [WaveformColumn::default(); 4];
    let mut cb = ca;
    let mut sa = [WaveformSpan::default(); 4];
    let mut sb = sa;
    let pool = ResultPool::new(
        info,
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
    let mut acc = [SparseAccumulator::default(); 4];
    let mut ranges = [FrameRange::default(); 16];
    let mut nodes = [QueuedBlock::default(); 4];
    let mut pcm = [NormalizedSample::default(); 4 * 4096];
    let mut spans = [WaveformSpan::default(); 4];
    let mut columns = [WaveformColumn::default(); 4];
    let mut requests = [RequestSlot::default(); 16];
    let session = PublishedSparseSession::new_scheduled(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 4096,
            frames_per_column: 1024,
        },
        Workspace {
            accumulators: &mut acc,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut spans,
            snapshot_columns: &mut columns,
        },
        &pool,
        &mut requests,
    )
    .unwrap();
    let mut s = ScheduledPullSession::new(session, source).unwrap();
    let mut initial = if id == 6 {
        Some(pool.acquire().unwrap())
    } else {
        None
    };
    let mut focus = Focus {
        playhead_frame: 2048,
        lookahead_frames: 1024,
        priority: 240,
        feature_mask: 1,
        ..Default::default()
    };
    if id == 0 || id == 12 {
        s.set_focus(focus).unwrap();
    }
    if id == 10 {
        cancel.cancel();
    }
    if id == 11 {
        for (first, priority) in [(2048, 32), (0, 96)] {
            s.request_region(RegionRequest {
                range: FrameRange {
                    first_frame: first,
                    end_frame: first + 1024,
                },
                feature_mask: 1,
                priority,
                request_id: 0,
                soft_deadline_monotonic_ns: 0,
            })
            .unwrap();
        }
    }
    let mut out = String::new();
    snapshot(
        &mut out,
        0,
        0,
        &counts,
        s.session().session().state(),
        &pool.acquire().unwrap(),
    );
    for step in 1..=20 {
        let budget = WorkBudget {
            maximum_input_frames: if id == 2 {
                4096
            } else if id == 6 {
                256
            } else {
                1024
            },
            maximum_steps: if id == 2 { 1 } else { 4 },
        };
        let mut perform = |step, out: &mut String| {
            let result = s.process(budget, &cancel);
            let status = match result {
                Err(Error::Source) => -5,
                Err(Error::InvalidState) => -12,
                Err(Error::Cancelled) => -7,
                Err(Error::ResultSlotsExhausted) => -14,
                Err(e) => panic!("unexpected {e:?}"),
                Ok(p) => {
                    if s.session().session().state() == SessionState::Complete {
                        3
                    } else if s.session().session().queued_frames() != 0 {
                        1
                    } else if p.processing.consumed_input_frames == 0 {
                        2
                    } else {
                        0
                    }
                }
            };
            snapshot(
                out,
                step,
                status,
                &counts,
                s.session().session().state(),
                &pool.acquire().unwrap(),
            );
            status
        };
        let status = perform(step, &mut out);
        if (status < 0 && status != -14) || status == 3 {
            if id == 14 || id == 15 {
                cancel.cancel();
            }
            perform(step + 100, &mut out);
            break;
        }
        if id == 0 && step <= 2 {
            focus.playhead_frame = 0;
            if step == 2 {
                focus.feature_mask = 0;
            }
            s.set_focus(focus).unwrap();
        }
        if id == 6 && step == 2 {
            assert_eq!(status, -14);
            drop(initial.take());
        }
    }
    assert!(matches!(
        s.session().session().state(),
        SessionState::Complete | SessionState::Cancelled | SessionState::Failed
    ));
    out
}
#[test]
#[ignore = "requires APTA_C_SPARSE_PULL_ORACLE"]
fn scheduled_pull_callbacks_publication_and_failures_match_c() {
    let oracle =
        std::env::var_os("APTA_C_SPARSE_PULL_ORACLE").expect("set APTA_C_SPARSE_PULL_ORACLE");
    for id in 0..=15 {
        let out = std::process::Command::new(&oracle)
            .arg(id.to_string())
            .output()
            .unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        assert_eq!(
            native(id),
            String::from_utf8(out.stdout).unwrap(),
            "scenario{id}"
        );
    }
}
