// SPDX-License-Identifier: Apache-2.0
use libapta::{pull::*, session::*, waveform::PcmView, *};
use libapta_runtime::{OwnedScheduledPullSession, OwnedSparseSession, SparseLimits};
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
fn writer() -> OwnedSparseSession {
    let mut w = OwnedSparseSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 513,
            frames_per_column: 64,
        },
        SparseLimits::default(),
    )
    .unwrap();
    w.enable_three_band().unwrap();
    w.enable_detail().unwrap();
    w
}
#[test]
fn scheduled_would_block_release_clock_and_terminal_completion() {
    let mut src = source();
    src.block_once = true;
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut pull = OwnedScheduledPullSession::new(writer(), src).unwrap();
    let token = CancellationToken::new();
    assert!(
        pull.process(WorkBudget::default(), &token)
            .unwrap()
            .would_block
    );
    let mut ticks = 0;
    pull.process_with_clock(
        WorkBudget::default(),
        1000000,
        &mut || {
            assert_eq!(releases.get(), 1);
            ticks += 1;
            ticks * 1000
        },
        &token,
    )
    .unwrap();
    assert!(ticks > 0);
    assert_eq!(pull.session().session().processed_frames(), 513);
    for _ in 0..3 {
        pull.process(WorkBudget::default(), &token).unwrap();
    }
    assert_eq!(pull.session().session().state(), SessionState::Complete);
    assert_eq!((reads.get(), releases.get()), (2, 1));
}
#[test]
fn scheduled_failed_mirror_drains_accepted_pcm_before_rereading() {
    let src = source();
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut w = writer();
    w.set_result_limits(NativeLimits {
        maximum_storage_bytes: 1,
        ..NativeLimits::default()
    });
    let mut pull = OwnedScheduledPullSession::new(w, src).unwrap();
    let token = CancellationToken::new();
    for _ in 0..2 {
        assert_eq!(
            pull.process(WorkBudget::default(), &token),
            Err(Error::LimitExceeded)
        );
    }
    assert_eq!((reads.get(), releases.get()), (1, 1));
    assert_eq!(pull.session().session().queued_frames(), 513);
    pull.set_result_limits(NativeLimits::default());
    pull.process(WorkBudget::default(), &token).unwrap();
    assert_eq!((reads.get(), releases.get()), (1, 1));
    assert_eq!(pull.session().session().processed_frames(), 513);
    pull.process(WorkBudget::default(), &token).unwrap();
    assert_eq!(pull.session().session().state(), SessionState::Complete);
}
#[test]
fn scheduled_malformed_source_is_terminal_and_cancel_never_reads() {
    let mut src = source();
    src.malformed = true;
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut pull = OwnedScheduledPullSession::new(writer(), src).unwrap();
    for _ in 0..2 {
        assert_eq!(
            pull.process(WorkBudget::default(), &CancellationToken::new()),
            Err(Error::Source)
        );
    }
    assert_eq!((reads.get(), releases.get()), (1, 1));
    let src = source();
    let reads = src.reads.clone();
    let mut pull = OwnedScheduledPullSession::new(writer(), src).unwrap();
    let token = CancellationToken::new();
    token.cancel();
    assert_eq!(
        pull.process(WorkBudget::default(), &token),
        Err(Error::Cancelled)
    );
    assert_eq!(
        pull.process(WorkBudget::default(), &CancellationToken::new()),
        Err(Error::Cancelled)
    );
    assert_eq!(reads.get(), 0);
}
#[test]
fn scheduled_public_replay_demand_does_not_replace_automatic_overview_selection() {
    let mut w = writer();
    w.request_region(RegionRequest {
        range: FrameRange {
            first_frame: 256,
            end_frame: 512,
        },
        feature_mask: result::WAVEFORM_DETAIL,
        priority: 240,
        soft_deadline_monotonic_ns: 0,
        request_id: 0,
    })
    .unwrap();
    let mut pull = OwnedScheduledPullSession::new(w, source()).unwrap();
    assert_eq!(pull.next_pcm_request().unwrap().range.first_frame, 256);
    pull.process(
        WorkBudget {
            maximum_input_frames: 256,
            maximum_steps: 1,
        },
        &CancellationToken::new(),
    )
    .unwrap();
    assert_eq!(
        pull.session().session().accepted_ranges(),
        &[FrameRange {
            first_frame: 0,
            end_frame: 256
        }]
    );
}

#[test]
#[ignore = "requires APTA_C_SPARSE_PULL_ORACLE"]
fn owning_scheduled_source_and_waveform_traces_match_public_c() {
    use std::fmt::Write;
    struct OracleSource {
        id: u32,
        pcm: [i16; 4096],
        reads: u32,
        first: u64,
        maximum: u32,
        releases: Rc<Cell<u32>>,
        release: Box<dyn FnMut()>,
    }
    impl PullSource for OracleSource {
        fn total_frames(&mut self) -> Option<u64> {
            Some(4096)
        }
        fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
            self.reads += 1;
            self.first = first;
            self.maximum = maximum;
            if self.id == 1 && self.reads == 1 {
                return Ok(PullRead::WouldBlock);
            }
            let n = if self.id == 1 {
                maximum.min(300)
            } else {
                maximum
            } as usize;
            for (i, v) in self.pcm[..n].iter_mut().enumerate() {
                *v = (1000 + (first + i as u64) / 1024 * 1000) as i16;
            }
            Ok(PullRead::Data(PullBlock::new(
                first,
                PcmView::S16Interleaved(&self.pcm[..n]),
                &mut self.release,
            )))
        }
    }
    fn record(
        out: &mut String,
        step: u32,
        status: i32,
        p: &OwnedScheduledPullSession<OracleSource>,
    ) {
        let src = p.source();
        let r = p.results().acquire().unwrap();
        let w = r.view().overview;
        let state = match p.session().session().state() {
            SessionState::Created => 0,
            SessionState::Running => 1,
            SessionState::Draining => 2,
            SessionState::Complete => 3,
            _ => panic!("unexpected terminal state"),
        };
        writeln!(
            out,
            "H {step} {status} {} {} {} {} {state} {} {} {} {} {}",
            src.reads,
            src.releases.get(),
            src.first,
            src.maximum,
            r.info().session_state as u8,
            r.available_features(),
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
    for id in [0, 1, 2, 11, 12] {
        let releases = Rc::new(Cell::new(0));
        let count = releases.clone();
        let src = OracleSource {
            id,
            pcm: [0; 4096],
            reads: 0,
            first: 0,
            maximum: 0,
            releases,
            release: Box::new(move || count.set(count.get() + 1)),
        };
        let w = OwnedSparseSession::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 4096,
                frames_per_column: 1024,
            },
            SparseLimits::default(),
        )
        .unwrap();
        let mut p = OwnedScheduledPullSession::new(w, src).unwrap();
        let token = CancellationToken::new();
        let mut focus = Focus {
            playhead_frame: 2048,
            lookahead_frames: 1024,
            priority: 240,
            feature_mask: 1,
            ..Focus::default()
        };
        if id == 0 || id == 12 {
            p.set_focus(focus).unwrap();
        }
        if id == 11 {
            for (first, priority) in [(2048, 32), (0, 96)] {
                p.request_region(RegionRequest {
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
        let mut actual = String::new();
        record(&mut actual, 0, 0, &p);
        for step in 1..=20 {
            let budget = WorkBudget {
                maximum_input_frames: if id == 2 { 4096 } else { 1024 },
                maximum_steps: if id == 2 { 1 } else { 4 },
            };
            let result = p.process(budget, &token).unwrap();
            let status = if p.session().session().state() == SessionState::Complete {
                3
            } else if p.session().session().queued_frames() != 0 {
                1
            } else if result.processing.consumed_input_frames == 0 {
                2
            } else {
                0
            };
            record(&mut actual, step, status, &p);
            if status == 3 {
                p.process(budget, &token).unwrap();
                record(&mut actual, step + 100, 3, &p);
                break;
            }
            if id == 0 && step <= 2 {
                focus.playhead_frame = 0;
                if step == 2 {
                    focus.feature_mask = 0;
                }
                p.set_focus(focus).unwrap();
            }
        }
        let output =
            std::process::Command::new(std::env::var_os("APTA_C_SPARSE_PULL_ORACLE").unwrap())
                .arg(id.to_string())
                .output()
                .unwrap();
        assert!(output.status.success());
        // Native heap generations differ; compare every callback coordinate,
        // status, state, available mask, span and quantized column exactly.
        let expected = String::from_utf8(output.stdout)
            .unwrap()
            .lines()
            .map(|line| {
                if line.starts_with("H ") {
                    line.split_whitespace()
                        .enumerate()
                        .filter(|(i, _)| *i != 8 && *i != 11)
                        .map(|(_, s)| s)
                        .collect::<Vec<_>>()
                        .join(" ")
                } else {
                    line.to_owned()
                }
            })
            .collect::<Vec<_>>()
            .join("\n")
            + "\n";
        assert_eq!(actual, expected, "scenario {id}");
    }
}

#[test]
fn scheduled_range_backpressure_releases_and_recovers_after_reservation() {
    let w = OwnedSparseSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 513,
            frames_per_column: 64,
        },
        SparseLimits {
            queue_nodes: 1,
            range_capacity: 1,
            ..SparseLimits::default()
        },
    )
    .unwrap();
    let src = source();
    let reads = src.reads.clone();
    let releases = src.releases.clone();
    let mut pull = OwnedScheduledPullSession::new(w, src).unwrap();
    let budget = WorkBudget {
        maximum_input_frames: 128,
        maximum_steps: 1,
    };
    let token = CancellationToken::new();
    pull.process(budget, &token).unwrap();
    assert_eq!(pull.session().session().processed_frames(), 128);
    assert_eq!(pull.process(budget, &token), Err(Error::BufferTooSmall));
    assert_eq!((reads.get(), releases.get()), (2, 2));
    assert_eq!(pull.failure(), None);
    assert_eq!(pull.session().session().processed_frames(), 128);
    pull.reserve_pending(1, 2).unwrap();
    for _ in 0..8 {
        pull.process(budget, &token).unwrap();
    }
    assert_eq!(pull.session().session().state(), SessionState::Complete);
    assert_eq!(pull.session().session().processed_frames(), 513);
    assert_eq!((reads.get(), releases.get()), (6, 6));
}
