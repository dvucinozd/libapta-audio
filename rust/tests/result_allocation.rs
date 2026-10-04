// SPDX-License-Identifier: Apache-2.0
//! One test in its own executable isolates allocation measurements.
use libapta::{
    builder,
    result::*,
    stream::{Input, StoredSection, StreamOptions},
    *,
};
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};
struct Counter;
static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);
unsafe impl GlobalAlloc for Counter {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        // SAFETY: Forward the unchanged allocation contract to System.
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: Pointer and layout came from System through this allocator.
        unsafe { System.dealloc(ptr, layout) }
    }
}
#[global_allocator]
static ALLOCATOR: Counter = Counter;
fn fixture(name: &str) -> Vec<u8> {
    let dir = if name.starts_with("dj-") {
        "../tests/fixtures"
    } else {
        "../conformance/fixtures/container-v1-suite"
    };
    let text = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join(dir)
            .join(name),
    )
    .unwrap();
    let h: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    (0..h.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
        .collect()
}
fn with_input<T>(bytes: &[u8], f: impl FnOnce(ResultInput<'_>) -> T) -> T {
    let p = parse(bytes, Limits::default()).unwrap();
    let w = p.waveform;
    let mut spans = vec![WaveformSpan::default(); w.overview.span_count()];
    let mut columns = vec![WaveformColumn::default(); w.overview.column_count()];
    let overview = w.overview.copy_into(&mut spans, &mut columns).unwrap();
    let tc: Vec<Vec<_>> = (0..w.tile_count())
        .map(|i| {
            let t = w.tile(i).unwrap();
            (0..t.column_count())
                .map(|j| t.column(j).unwrap())
                .collect()
        })
        .collect();
    let tiles: Vec<_> = tc
        .iter()
        .enumerate()
        .map(|(i, c)| {
            let t = w.tile(i).unwrap();
            WaveformTile {
                level_id: t.level_id,
                tile_index: t.tile_index,
                first_frame: t.first_frame,
                end_frame: t.end_frame,
                first_column_index: t.first_column_index,
                state: t.state,
                confidence: t.confidence,
                columns: c,
            }
        })
        .collect();
    let candidates: Vec<_> = p
        .tempo
        .map(|t| {
            (0..t.candidate_count())
                .map(|i| t.candidate(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let tempo = p.tempo.map(|t| TempoView {
        selected: t.selected(),
        candidates: &candidates,
    });
    let mut gs: Vec<_> = p
        .global_grid
        .map(|g| {
            (0..g.segment_count())
                .map(|i| g.segment(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let mut gb: Vec<_> = p
        .global_grid
        .map(|g| (0..g.beat_count()).map(|i| g.beat(i).unwrap()).collect())
        .unwrap_or_default();
    let global_grid = p
        .global_grid
        .map(|g| g.copy_into(&mut gs, &mut gb).unwrap());
    let mut kc: Vec<_> = p
        .key
        .map(|k| {
            (0..k.candidate_count())
                .map(|i| k.candidate(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let key = p.key.map(|k| k.copy_into(&mut kc).unwrap());
    let mut ms: Vec<_> = p
        .meter
        .map(|m| {
            (0..m.segment_count())
                .map(|i| m.segment(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    let meter = p.meter.map(|m| m.copy_into(&mut ms).unwrap());
    let qr: Vec<_> = p
        .quality
        .map(|q| {
            (0..q.record_count())
                .map(|i| q.record(i).unwrap())
                .collect()
        })
        .unwrap_or_default();
    f(ResultInput {
        source: p.source,
        overview,
        tiles: &tiles,
        metadata: w.metadata,
        tempo,
        local_grid: p.local_grid,
        global_grid,
        revision: p.revision,
        key,
        meter,
        quality: &qr,
    })
}

#[test]
fn parse_copy_write_finalize_allocate_nothing_and_output_outlives_inputs() {
    publication_allocates_nothing();
    for name in [
        "v1-all-standard-sections.apta.hex",
        "dj-sections-v1-combined.apta.hex",
    ] {
        let bytes = fixture(name);
        let mut output = vec![0; bytes.len()];
        let mut retained = vec![0; bytes.len()];
        let mut streamed_output = vec![0; bytes.len()];
        with_input(&bytes, |r| {
            native_copy_allocates_nothing(r);
            // Every allocation for fixture decoding and caller storage is outside
            // the measured path; the native operations below must allocate zero.
            let mut spans = r.overview.spans.to_vec();
            let mut columns = r.overview.columns.to_vec();
            let mut tile_columns: Vec<Vec<_>> =
                r.tiles.iter().map(|t| t.columns.to_vec()).collect();
            let mut tempo = r.tempo.map(|t| t.candidates.to_vec()).unwrap_or_default();
            let mut gs = r
                .global_grid
                .map(|g| g.segments.to_vec())
                .unwrap_or_default();
            let mut gb = r.global_grid.map(|g| g.beats.to_vec()).unwrap_or_default();
            let mut kc = r.key.map(|k| k.candidates.to_vec()).unwrap_or_default();
            let mut ms = r.meter.map(|m| m.segments.to_vec()).unwrap_or_default();
            let mut qr = r.quality.to_vec();
            let before = ALLOCATIONS.load(Ordering::Relaxed);
            let p = parse(&bytes, Limits::default()).unwrap();
            let overview = p
                .waveform
                .overview
                .copy_into(&mut spans, &mut columns)
                .unwrap();
            assert_eq!(overview, r.overview);
            for (i, cols) in tile_columns.iter_mut().enumerate() {
                let tile = p.waveform.tile(i).unwrap();
                for (j, col) in cols.iter_mut().enumerate() {
                    *col = tile.column(j).unwrap();
                }
                assert_eq!(cols, r.tiles[i].columns);
            }
            if let Some(t) = p.tempo {
                t.copy_candidates(&mut tempo).unwrap();
            }
            if let Some(g) = p.global_grid {
                assert_eq!(
                    g.copy_into(&mut gs, &mut gb).unwrap(),
                    r.global_grid.unwrap()
                );
            }
            if let Some(k) = p.key {
                assert_eq!(k.copy_into(&mut kc).unwrap(), r.key.unwrap());
            }
            if let Some(m) = p.meter {
                assert_eq!(m.copy_into(&mut ms).unwrap(), r.meter.unwrap());
            }
            if let Some(q) = p.quality {
                assert_eq!(q.copy_into(&mut qr).unwrap(), r.quality);
            }
            assert_eq!(
                write(&r, &mut output, Limits::default()).unwrap(),
                bytes.len()
            );
            let mut sink = Sink {
                bytes: &mut streamed_output,
                position: 0,
            };
            assert_eq!(
                libapta::stream_write::write(&r, &mut sink, Limits::default()).unwrap(),
                bytes.len()
            );
            let generation = builder::finalize(&r, &mut retained, Limits::default()).unwrap();
            assert_eq!(generation.available_features, p.available_features);
            assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
        });
        assert_eq!(output, bytes);
        assert_eq!(streamed_output, bytes);
        assert_eq!(retained, bytes);
        let mut scratch = [0; 7];
        let mut descriptors = [StoredSection::default(); 16];
        let mut arena = vec![0; bytes.len()];
        let before = ALLOCATIONS.load(Ordering::Relaxed);
        let streamed = read_from_stream(
            &mut Reader(&bytes),
            StreamOptions::default(),
            &mut scratch,
            &mut descriptors,
            &mut arena,
        )
        .unwrap();
        assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
        drop(bytes); // Result storage must not borrow the parsed fixture or native arrays.
        assert!(streamed.available_features & WAVEFORM_OVERVIEW != 0);
        assert!(streamed.waveform.overview.unwrap().column(0, 0).is_some());
        output.fill(0xa5); // Mutating another result cannot alter this generation.
        let before = ALLOCATIONS.load(Ordering::Relaxed);
        let retained_view = parse(&retained, Limits::default()).unwrap();
        assert!(retained_view.available_features & WAVEFORM_OVERVIEW != 0);
        assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
    }
}

struct Reader<'a>(&'a [u8]);
impl Input for Reader<'_> {
    fn size(&mut self) -> Result<u64, Error> {
        Ok(self.0.len() as u64)
    }
    fn read_at(&mut self, offset: u64, out: &mut [u8]) -> Result<usize, Error> {
        let start = usize::try_from(offset).map_err(|_| Error::Source)?;
        let remaining = self.0.get(start..).ok_or(Error::Source)?;
        let n = out.len().min(remaining.len()).min(3);
        out[..n].copy_from_slice(&remaining[..n]);
        Ok(n)
    }
}

struct Sink<'a> {
    bytes: &'a mut [u8],
    position: usize,
}
impl libapta::stream::Output for Sink<'_> {
    fn write(&mut self, b: &[u8]) -> Result<usize, Error> {
        let n = b.len().min(3);
        self.bytes[self.position..self.position + n].copy_from_slice(&b[..n]);
        self.position += n;
        Ok(n)
    }
    fn seek(&mut self, p: u64) -> Result<(), Error> {
        self.position = usize::try_from(p).map_err(|_| Error::Source)?;
        Ok(())
    }
    fn flush(&mut self) -> Result<(), Error> {
        Ok(())
    }
}

fn native_copy_allocates_nothing(r: ResultInput<'_>) {
    use libapta::owned_result::{self, GridStorage, Storage};
    let local_coverage: Vec<_> = r.local_grid.map(|g| vec![g.coverage]).unwrap_or_default();
    let local_segments: Vec<_> = r.local_grid.map(|g| vec![g.segment]).unwrap_or_default();
    let global_coverage: Vec<_> = r
        .global_grid
        .map(|g| vec![g.coverage_range])
        .unwrap_or_default();
    let mut offset = 0;
    let tiles: Vec<_> = r
        .tiles
        .iter()
        .map(|t| {
            let tile = NativeTile {
                level_id: t.level_id,
                tile_index: t.tile_index,
                first_frame: t.first_frame,
                end_frame: t.end_frame,
                first_column_index: t.first_column_index,
                state: t.state,
                confidence: t.confidence,
                data_column_offset: offset,
                column_count: t.columns.len(),
            };
            offset += t.columns.len();
            tile
        })
        .collect();
    let columns: Vec<_> = r
        .tiles
        .iter()
        .flat_map(|t| t.columns.iter().copied())
        .collect();
    let input = NativeResultInput {
        source: r.source,
        info: NativeResultInfo::default(),
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "Allocation proof",
            source_version: "1",
        },
        overview: Some(NativeOverview {
            frames_per_column: r.overview.frames_per_column,
            origin_frame: r.overview.origin_frame,
            state: r.overview.state,
            confidence: 255,
            spans: r.overview.spans,
            columns: r.overview.columns,
        }),
        detail: (!tiles.is_empty()).then_some(NativeDetail {
            tiles: &tiles,
            columns: &columns,
        }),
        metadata: r.metadata,
        tempo: r.tempo,
        local_grid: r.local_grid.map(|g| NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: GridRepresentation::Segments,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: &local_coverage,
            segments: &local_segments,
            beats: &[],
        }),
        global_grid: r.global_grid.map(|g| NativeGrid {
            state: g.state,
            confidence: g.confidence,
            flags: g.flags,
            representation: g.representation,
            requested_range: g.requested_range,
            evidence_range: g.evidence_range,
            applicability_range: g.applicability_range,
            coverage_ranges: &global_coverage,
            segments: g.segments,
            beats: g.beats,
        }),
        revision: r.revision,
        key: r.key,
        meter: r.meter,
        quality: r.quality,
    };
    let req = owned_result::requirements(&input, NativeLimits::default()).unwrap();
    let mut spans = input.overview.unwrap().spans.to_vec();
    let mut overview_columns = input.overview.unwrap().columns.to_vec();
    let mut dest_tiles = tiles.clone();
    let mut detail_columns = columns.clone();
    let mut tempo = input
        .tempo
        .map(|t| t.candidates.to_vec())
        .unwrap_or_default();
    let mut lc = local_coverage.clone();
    let mut ls = local_segments.clone();
    let mut lb = [];
    let mut gc = global_coverage.clone();
    let mut gs = input
        .global_grid
        .map(|g| g.segments.to_vec())
        .unwrap_or_default();
    let mut gb = input
        .global_grid
        .map(|g| g.beats.to_vec())
        .unwrap_or_default();
    let mut keys = input.key.map(|k| k.candidates.to_vec()).unwrap_or_default();
    let mut meter = input.meter.map(|m| m.segments.to_vec()).unwrap_or_default();
    let mut quality = input.quality.to_vec();
    let mut text = vec![0; req.text_bytes];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    assert_eq!(
        owned_result::requirements(&input, NativeLimits::default()).unwrap(),
        req
    );
    let owner = owned_result::copy(
        &input,
        Storage {
            overview_spans: &mut spans,
            overview_columns: &mut overview_columns,
            detail_tiles: &mut dest_tiles,
            detail_columns: &mut detail_columns,
            tempo_candidates: &mut tempo,
            local_grid: GridStorage {
                coverage_ranges: &mut lc,
                segments: &mut ls,
                beats: &mut lb,
            },
            global_grid: GridStorage {
                coverage_ranges: &mut gc,
                segments: &mut gs,
                beats: &mut gb,
            },
            key_candidates: &mut keys,
            meter_segments: &mut meter,
            quality: &mut quality,
            text_bytes: &mut text,
        },
        NativeLimits::default(),
    )
    .unwrap();
    assert_eq!(owner.metadata(), input.metadata);
    assert_eq!(owner.requirements(), req);
    assert_eq!(owner.view().provenance, input.provenance);
    let storage = owner.into_storage();
    let mut reused = owned_result::copy(&input, storage, NativeLimits::default()).unwrap();
    reused.replace(&input, NativeLimits::default()).unwrap();
    assert_eq!(reused.requirements(), req);
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}

fn publication_allocates_nothing() {
    sparse_publication_allocates_nothing();
    detail_publication_allocates_nothing();
    detail_replay_allocates_nothing(false);
    detail_replay_allocates_nothing(true);
    seeded_scheduled_pull_allocates_nothing();
    use libapta::{
        owned_result::Storage,
        publication::{PublishedSession, ResultPool},
        session::{CancellationToken, SessionConfig, WorkBudget},
        waveform::{NormalizedSample, PcmView},
    };
    let mut spans0 = [WaveformSpan::default(); 1];
    let mut spans1 = spans0;
    let mut columns0 = [WaveformColumn::default(); 2];
    let mut columns1 = columns0;
    let mut queue = [NormalizedSample::default(); 128];
    let mut work_columns = [WaveformColumn::default(); 2];
    let tile = NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 0,
        data_column_offset: 0,
        column_count: 0,
    };
    let mut dt0 = [tile; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let mut cache = [libapta::detail_analysis::DetailTile::default(); 4];
    let mut dt = [tile; 4];
    let mut dc = [WaveformColumn::default(); 256];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
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
                overview_spans: &mut spans0,
                overview_columns: &mut columns0,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_spans: &mut spans1,
                overview_columns: &mut columns1,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let (retained, cloned) = {
        let mut session = PublishedSession::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 128,
                frames_per_column: 64,
            },
            &mut queue,
            &mut work_columns,
            &pool,
        )
        .unwrap();
        session.enable_detail(&mut cache, &mut dt, &mut dc).unwrap();
        session
            .push_pcm(PcmView::F32Interleaved(&[0.25; 128]))
            .unwrap();
        session.finish_input().unwrap();
        let token = CancellationToken::new();
        session.process(WorkBudget::default(), &token).unwrap();
        let retained = pool.acquire().unwrap();
        let cloned = retained.clone();
        assert_eq!(retained.overview().unwrap().columns.len(), 2);
        assert_eq!(retained.detail().unwrap().tiles[0].end_frame, 128);
        assert_eq!(cloned.info().generation, retained.info().generation);
        (retained, cloned)
    };
    assert_eq!(cloned.info().session_state, ResultSessionState::Completed);
    assert_eq!(cloned.detail().unwrap().columns.len(), 1);
    drop(retained);
    drop(cloned);
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}

fn sparse_publication_allocates_nothing() {
    use libapta::{
        owned_result::Storage,
        publication::{PublishedSparseSession, ResultPool},
        scheduler::RequestSlot,
        session::{CancellationToken, SessionConfig, WorkBudget},
        sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
        waveform::{NormalizedSample, PcmView},
    };
    let mut spans0 = [WaveformSpan::default(); 3];
    let mut spans1 = spans0;
    let mut columns0 = [WaveformColumn::default(); 3];
    let mut columns1 = columns0;
    let mut accumulators = [SparseAccumulator::default(); 3];
    let mut ranges = [FrameRange::default(); 4];
    let mut nodes = [QueuedBlock::default(); 3];
    let mut pcm = [NormalizedSample::default(); 3 * NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 3];
    let mut columns = [WaveformColumn::default(); 3];
    let mut requests = [RequestSlot::default(); 2];
    let mut bands = [libapta::band::BandSums::default(); 3];
    let mut cache = [libapta::detail_analysis::DetailTile::default(); 4];
    let mut detail_tiles = [NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 0,
        data_column_offset: 0,
        column_count: 0,
    }; 4];
    let mut detail_columns = [WaveformColumn::default(); 256];
    let mut detail_tiles0 = [NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 0,
        data_column_offset: 0,
        column_count: 0,
    }; 4];
    let mut detail_tiles1 = detail_tiles0;
    let mut detail_columns0 = [WaveformColumn::default(); 256];
    let mut detail_columns1 = detail_columns0;
    let config = SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: 192,
        frames_per_column: 64,
    };
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let pool = ResultPool::new(
        SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(192),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        [
            Storage {
                overview_spans: &mut spans0,
                overview_columns: &mut columns0,
                detail_tiles: &mut detail_tiles0,
                detail_columns: &mut detail_columns0,
                ..Default::default()
            },
            Storage {
                overview_spans: &mut spans1,
                overview_columns: &mut columns1,
                detail_tiles: &mut detail_tiles1,
                detail_columns: &mut detail_columns1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let (retained, clone) = {
        let mut session = PublishedSparseSession::new_scheduled(
            config,
            Workspace {
                accumulators: &mut accumulators,
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
        session.enable_three_band(&mut bands).unwrap();
        session
            .enable_detail(&mut cache, &mut detail_tiles, &mut detail_columns)
            .unwrap();
        let request = session
            .request_region(RegionRequest {
                range: FrameRange {
                    first_frame: 64,
                    end_frame: 128,
                },
                feature_mask: WAVEFORM_OVERVIEW,
                soft_deadline_monotonic_ns: 10,
                request_id: 0,
                priority: 200,
            })
            .unwrap();
        session
            .set_focus(Focus {
                playhead_frame: 0,
                lookahead_frames: 192,
                feature_mask: WAVEFORM_OVERVIEW,
                priority: 32,
                ..Default::default()
            })
            .unwrap();
        assert_eq!(
            session.next_pcm_request().unwrap().range,
            FrameRange {
                first_frame: 64,
                end_frame: 128
            }
        );
        session
            .push_at(64, PcmView::S16Interleaved(&[100; 64]))
            .unwrap();
        let token = CancellationToken::new();
        session.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(
            session.request_progress(request).unwrap().state,
            RequestState::Satisfied
        );
        let old = pool.acquire().unwrap();
        let old_clone = old.clone();
        let old_column = old.overview().unwrap().columns[0];
        session
            .push_at(0, PcmView::S16Interleaved(&[-200; 64]))
            .unwrap();
        session.process(WorkBudget::default(), &token).unwrap();
        session
            .push_at(128, PcmView::S16Interleaved(&[300; 64]))
            .unwrap();
        assert_eq!(
            session.process(WorkBudget::default(), &token),
            Err(Error::ResultSlotsExhausted)
        );
        assert_eq!(old.overview().unwrap().columns, &[old_column]);
        assert_eq!(old_clone.overview().unwrap().spans[0].first_frame, 64);
        drop(old);
        drop(old_clone);
        session.process(WorkBudget::default(), &token).unwrap();
        session.cancel_region_request(request).unwrap();
        session.finish_input().unwrap();
        session.process(WorkBudget::default(), &token).unwrap();
        let retained = pool.acquire().unwrap();
        let clone = retained.clone();
        assert_eq!(retained.overview().unwrap().columns.len(), 3);
        assert!(retained
            .overview()
            .unwrap()
            .columns
            .iter()
            .all(|c| c.flags & 8 != 0));
        assert_eq!(retained.view().detail.unwrap().tiles.len(), 1);
        assert_eq!(retained.view().detail.unwrap().tiles[0].column_count, 1);
        (retained, clone)
    };
    assert_eq!(retained.info().session_state, ResultSessionState::Completed);
    assert_eq!(clone.overview().unwrap().state, FeatureState::Final);
    drop(retained);
    drop(clone);
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}

fn seeded_scheduled_pull_allocates_nothing() {
    use libapta::{
        owned_result::Storage,
        publication::{PublishedSparseSession, ResultPool},
        pull::{PullBlock, PullRead, PullSource},
        scheduler::RequestSlot,
        session::{CancellationToken, SessionConfig, WorkBudget},
        sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
        sparse_pull::ScheduledPullSession,
        waveform::{NormalizedSample, PcmView},
    };
    use std::cell::Cell;
    struct Source<'a, F> {
        samples: &'a [i16],
        reads: &'a Cell<usize>,
        release: F,
    }
    impl<F: FnMut()> PullSource for Source<'_, F> {
        fn total_frames(&mut self) -> Option<u64> {
            Some(512)
        }
        fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
            assert_eq!(first, 256);
            self.reads.set(self.reads.get() + 1);
            let count = self.samples.len().min(maximum as usize);
            Ok(PullRead::Data(PullBlock::new(
                first,
                PcmView::S16Interleaved(&self.samples[..count]),
                &mut self.release,
            )))
        }
    }
    let source_info = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(512),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let seed_spans = [WaveformSpan {
        first_frame: 0,
        end_frame: 256,
        first_column_index: 0,
        column_count: 1,
        data_column_offset: 0,
    }];
    let seed_columns = [WaveformColumn {
        minimum: -1000,
        maximum: 1000,
        rms: 1000,
        flags: 9,
        low: 12,
        mid: 23,
        high: 34,
    }];
    let input = NativeResultInput {
        source: source_info,
        info: NativeResultInfo {
            session_state: ResultSessionState::AcceptingInput,
            ..Default::default()
        },
        provenance: Provenance {
            origin: ProvenanceOrigin::ExternalImport,
            source_name: "checkpoint",
            source_version: "",
        },
        overview: Some(NativeOverview {
            frames_per_column: 256,
            origin_frame: 0,
            state: FeatureState::Partial,
            confidence: 255,
            spans: &seed_spans,
            columns: &seed_columns,
        }),
        detail: None,
        metadata: None,
        tempo: None,
        local_grid: None,
        global_grid: None,
        revision: None,
        key: None,
        meter: None,
        quality: &[],
    };
    let mut seed_owned_spans = [WaveformSpan::default(); 1];
    let mut seed_owned_columns = [WaveformColumn::default(); 1];
    let mut seed_text = [0; 16];
    let checkpoint = owned_result::copy(
        &input,
        Storage {
            overview_spans: &mut seed_owned_spans,
            overview_columns: &mut seed_owned_columns,
            text_bytes: &mut seed_text,
            ..Default::default()
        },
        NativeLimits::default(),
    )
    .unwrap();
    let mut spans0 = [WaveformSpan::default(); 2];
    let mut spans1 = spans0;
    let mut columns0 = [WaveformColumn::default(); 2];
    let mut columns1 = columns0;
    let mut acc = [SparseAccumulator::default(); 2];
    let mut ranges = [FrameRange::default(); 3];
    let mut nodes = [QueuedBlock::default(); 1];
    let mut pcm = [NormalizedSample::default(); NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 2];
    let mut columns = [WaveformColumn::default(); 2];
    let mut requests = [RequestSlot::default(); 2];
    let reads = Cell::new(0);
    let releases = Cell::new(0);
    let samples = [123i16; 256];
    let tile = NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 255,
        data_column_offset: 0,
        column_count: 0,
    };
    let mut dt0 = [tile; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let mut cache = [detail_analysis::DetailTile::default(); 4];
    let mut dt = [tile; 4];
    let mut dc = [WaveformColumn::default(); 256];
    let mut bands = [band::BandSums::default(); 2];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let pool = ResultPool::new(
        source_info,
        [
            Storage {
                overview_spans: &mut spans0,
                overview_columns: &mut columns0,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_spans: &mut spans1,
                overview_columns: &mut columns1,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let retained = {
        let mut session = PublishedSparseSession::new_scheduled(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 512,
                frames_per_column: 256,
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
        session.enable_three_band(&mut bands).unwrap();
        session.enable_detail(&mut cache, &mut dt, &mut dc).unwrap();
        session.seed_from_result(&checkpoint, false).unwrap();
        assert_eq!(pool.generation(), 1);
        assert_eq!(
            session.session().accepted_ranges(),
            &[FrameRange {
                first_frame: 0,
                end_frame: 256
            }]
        );
        session
            .request_region(RegionRequest {
                range: FrameRange {
                    first_frame: 0,
                    end_frame: 256,
                },
                feature_mask: WAVEFORM_DETAIL,
                priority: 200,
                request_id: 0,
                soft_deadline_monotonic_ns: 0,
            })
            .unwrap();
        assert_eq!(session.next_pcm_request().unwrap().range.first_frame, 0);
        let source = Source {
            samples: &samples,
            reads: &reads,
            release: || releases.set(releases.get() + 1),
        };
        let mut pull = ScheduledPullSession::new(session, source).unwrap();
        let token = CancellationToken::new();
        assert_eq!(
            pull.process(WorkBudget::default(), &token)
                .unwrap()
                .processing
                .consumed_input_frames,
            256
        );
        assert_eq!(reads.get(), 1);
        assert_eq!(releases.get(), 1);
        pull.process(WorkBudget::default(), &token).unwrap();
        assert_eq!(reads.get(), 1);
        assert_eq!(releases.get(), 1);
        let retained = pool.acquire().unwrap();
        assert_eq!(retained.overview().unwrap().columns.len(), 2);
        assert_eq!(retained.detail().unwrap().columns.len(), 1);
        assert_eq!(
            retained.detail().unwrap().tiles[0].state,
            FeatureState::Partial
        );
        let _source = pull.into_source();
        retained
    };
    assert_eq!(retained.info().session_state, ResultSessionState::Completed);
    assert_eq!(retained.overview().unwrap().state, FeatureState::Final);
    drop(retained);
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}

fn detail_publication_allocates_nothing() {
    use libapta::{
        detail_analysis::DetailTile,
        owned_result::Storage,
        publication::{PublishedSparseSession, ResultPool},
        session::{CancellationToken, SessionConfig, WorkBudget},
        sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
        waveform::{NormalizedSample, PcmView},
    };
    let tile = NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 255,
        data_column_offset: 0,
        column_count: 0,
    };
    let mut dt0 = [tile; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let mut os0 = [WaveformSpan::default(); 9];
    let mut os1 = os0;
    let mut oc0 = [WaveformColumn::default(); 9];
    let mut oc1 = oc0;
    let mut accumulators = [SparseAccumulator::default(); 9];
    let mut ranges = [FrameRange::default(); 4];
    let mut nodes = [QueuedBlock::default(); 2];
    let mut pcm = [NormalizedSample::default(); 2 * NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 9];
    let mut columns = [WaveformColumn::default(); 9];
    let mut cache = [DetailTile::default(); 4];
    let mut dt = [tile; 4];
    let mut dc = [WaveformColumn::default(); 256];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let pool = ResultPool::new(
        SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(513),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        [
            Storage {
                overview_spans: &mut os0,
                overview_columns: &mut oc0,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_spans: &mut os1,
                overview_columns: &mut oc1,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let retained = {
        let mut session = PublishedSparseSession::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 513,
                frames_per_column: 64,
            },
            Workspace {
                accumulators: &mut accumulators,
                ranges: &mut ranges,
                nodes: &mut nodes,
                pcm: &mut pcm,
                snapshot_spans: &mut spans,
                snapshot_columns: &mut columns,
            },
            &pool,
        )
        .unwrap();
        session.enable_detail(&mut cache, &mut dt, &mut dc).unwrap();
        session
            .push_at(256, PcmView::S16Interleaved(&[123; 257]))
            .unwrap();
        // Eager detail acceptance is independent of overview's frame budget.
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1,
                },
                &CancellationToken::new(),
            )
            .unwrap();
        let first = pool.acquire().unwrap();
        assert!(first.overview().is_none());
        assert_eq!(first.detail().unwrap().tiles[0].first_frame, 256);
        assert_eq!(first.changed_features(), WAVEFORM_DETAIL);
        drop(first);
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        let completed = pool.acquire().unwrap();
        assert_eq!(
            completed.info().session_state,
            ResultSessionState::Completed
        );
        assert_eq!(
            completed.detail().unwrap().tiles[0].state,
            FeatureState::Partial
        );
        assert_eq!(completed.detail().unwrap().tiles[0].end_frame, 513);
        assert_eq!(completed.detail().unwrap().columns.len(), 2);
        completed
    };
    assert_eq!(retained.detail().unwrap().tiles[0].end_frame, 513);
    drop(retained);
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}

fn detail_replay_allocates_nothing(seed: bool) {
    use libapta::{
        detail_analysis::DetailTile,
        owned_result::Storage,
        publication::{PublishedSparseSession, ResultPool},
        session::{CancellationToken, SessionConfig, WorkBudget},
        sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
        waveform::{NormalizedSample, PcmView},
    };
    let tile = NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 255,
        data_column_offset: 0,
        column_count: 0,
    };
    let mut dt0 = [tile; 4];
    let mut dt1 = dt0;
    let mut dc0 = [WaveformColumn::default(); 256];
    let mut dc1 = dc0;
    let mut os0 = [WaveformSpan::default(); 385];
    let mut os1 = os0;
    let mut oc0 = [WaveformColumn::default(); 385];
    let mut oc1 = oc0;
    let mut accumulators = [SparseAccumulator::default(); 385];
    let mut ranges = [FrameRange::default(); 8];
    let mut nodes = [QueuedBlock::default(); 2];
    let mut pcm = [NormalizedSample::default(); 2 * NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 385];
    let mut columns = [WaveformColumn::default(); 385];
    let mut cache = [DetailTile::default(); 4];
    let mut dt = [tile; 4];
    let mut dc = [WaveformColumn::default(); 256];
    let mut requests = [libapta::scheduler::RequestSlot::default(); 2];
    let mut bands = [libapta::band::BandSums::default(); 385];
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let pool = ResultPool::new(
        SourceInfo {
            sample_rate: 48000,
            channel_count: 1,
            channel_layout: 1,
            total_frames: Some(98305),
            fingerprint_kind: 0,
            fingerprint: [0; 32],
        },
        [
            Storage {
                overview_spans: &mut os0,
                overview_columns: &mut oc0,
                detail_tiles: &mut dt0,
                detail_columns: &mut dc0,
                ..Default::default()
            },
            Storage {
                overview_spans: &mut os1,
                overview_columns: &mut oc1,
                detail_tiles: &mut dt1,
                detail_columns: &mut dc1,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    {
        let mut session = PublishedSparseSession::new_scheduled(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: 98305,
                frames_per_column: 256,
            },
            Workspace {
                accumulators: &mut accumulators,
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
        session.enable_detail(&mut cache, &mut dt, &mut dc).unwrap();
        session.enable_three_band(&mut bands).unwrap();
        let cancel = CancellationToken::new();
        let seed_spans = [WaveformSpan {
            first_frame: 0,
            end_frame: 256,
            first_column_index: 0,
            column_count: 1,
            data_column_offset: 0,
        }];
        let seed_columns = [WaveformColumn {
            minimum: -1000,
            maximum: 1000,
            rms: 1000,
            low: 12,
            mid: 23,
            high: 34,
            flags: 9,
        }];
        let input = NativeResultInput {
            source: SourceInfo {
                sample_rate: 48000,
                channel_count: 1,
                channel_layout: 1,
                total_frames: Some(98305),
                fingerprint_kind: 0,
                fingerprint: [0; 32],
            },
            info: NativeResultInfo {
                session_state: ResultSessionState::AcceptingInput,
                ..Default::default()
            },
            provenance: Provenance {
                origin: ProvenanceOrigin::ExternalImport,
                source_name: "seed",
                source_version: "",
            },
            overview: Some(NativeOverview {
                frames_per_column: 256,
                origin_frame: 0,
                state: FeatureState::Partial,
                confidence: 255,
                spans: &seed_spans,
                columns: &seed_columns,
            }),
            detail: None,
            metadata: None,
            tempo: None,
            local_grid: None,
            global_grid: None,
            revision: None,
            key: None,
            meter: None,
            quality: &[],
        };
        let mut os = seed_spans;
        let mut oc = seed_columns;
        let mut text = [0; 4];
        let checkpoint = owned_result::copy(
            &input,
            Storage {
                overview_spans: &mut os,
                overview_columns: &mut oc,
                text_bytes: &mut text,
                ..Default::default()
            },
            NativeLimits::default(),
        )
        .unwrap();
        if seed {
            session.seed_from_result(&checkpoint, false).unwrap();
        }
        assert_eq!(pool.generation(), 1);
        for tile in if seed { 1 } else { 0 }..5 {
            assert_eq!(
                session.push_at(tile * 16384, PcmView::S16Interleaved(&[123; 256])),
                Ok(256)
            );
            session.process(WorkBudget::default(), &cancel).unwrap();
        }
        let processed = session.session().processed_frames();
        let mut prior = [WaveformColumn::default(); 5];
        {
            let result = pool.acquire().unwrap();
            prior.copy_from_slice(result.overview().unwrap().columns);
            assert!(!result
                .detail()
                .unwrap()
                .tiles
                .iter()
                .any(|t| t.tile_index == 0));
        }
        let request = session
            .request_region(RegionRequest {
                range: FrameRange {
                    first_frame: 0,
                    end_frame: 256,
                },
                feature_mask: WAVEFORM_DETAIL,
                priority: 200,
                request_id: 0,
                soft_deadline_monotonic_ns: 0,
            })
            .unwrap();
        session
            .set_focus(Focus {
                feature_mask: WAVEFORM_DETAIL,
                playhead_frame: 0,
                lookahead_frames: 256,
                priority: 100,
                ..Default::default()
            })
            .unwrap();
        let demand = session.next_pcm_request().unwrap();
        assert_eq!(
            demand.range,
            FrameRange {
                first_frame: 0,
                end_frame: 256
            }
        );
        assert_eq!(demand.feature_mask, WAVEFORM_DETAIL);
        assert_eq!(
            session.push_at(0, PcmView::S16Interleaved(&[123; 256])),
            Ok(256)
        );
        session.process(WorkBudget::default(), &cancel).unwrap();
        assert_eq!(session.session().processed_frames(), processed);
        assert_eq!(session.session().queued_frames(), 0);
        assert_eq!(
            session.request_progress(request).unwrap().state,
            RequestState::Satisfied
        );
        let result = pool.acquire().unwrap();
        assert_eq!(result.overview().unwrap().columns, prior);
        assert!(result
            .detail()
            .unwrap()
            .tiles
            .iter()
            .any(|t| t.tile_index == 0));
        assert_eq!(result.changed_features(), WAVEFORM_DETAIL);
    }
    {
        use libapta::session::{Session, SessionConfig, TOTAL_FRAMES_UNKNOWN};
        use libapta::waveform::NormalizedSample;
        let cancel = libapta::session::CancellationToken::new();
        let mut queue = [NormalizedSample::default(); 60];
        let mut replacement_queue = [NormalizedSample::default(); 120];
        let mut columns = [WaveformColumn::default(); 4];
        let mut replacement_columns = [WaveformColumn::default(); 8];
        let mut bands = [libapta::band::BandSums::default(); 8];
        let mut s = Session::new(
            SessionConfig {
                sample_rate: 48000,
                channel_count: 1,
                total_frames: TOTAL_FRAMES_UNKNOWN,
                frames_per_column: 64,
            },
            &mut queue,
            &mut columns,
        )
        .unwrap();
        s.enable_three_band(&mut bands).unwrap();
        s.push_interleaved(&[0.5; 60]).unwrap();
        s.process(
            WorkBudget {
                maximum_input_frames: 50,
                maximum_steps: 0,
            },
            &cancel,
        )
        .unwrap();
        s.push_interleaved(&[0.25; 50]).unwrap();
        let _old_queue = s.replace_queue(&mut replacement_queue).unwrap();
        let _old_columns = s.replace_output(&mut replacement_columns).unwrap();
        s.finish_input().unwrap();
        s.process(WorkBudget::default(), &cancel).unwrap();
        let snapshot = s.snapshot(2).unwrap();
        let mut spans = [WaveformSpan::default(); 1];
        let mut output = [WaveformColumn::default(); 2];
        let mut text = [0u8; 64];
        let owned = snapshot
            .copy_to(
                owned_result::Storage {
                    overview_spans: &mut spans,
                    overview_columns: &mut output,
                    text_bytes: &mut text,
                    ..owned_result::Storage::default()
                },
                NativeLimits::default(),
                WAVEFORM_OVERVIEW,
            )
            .unwrap();
        assert_eq!(owned.overview().unwrap().columns, s.columns());
        assert_eq!(s.processed_frames(), 110);
    }
    assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), before);
}
