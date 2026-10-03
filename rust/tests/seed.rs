// SPDX-License-Identifier: Apache-2.0
use libapta::{
    owned_result::{self, Storage},
    publication::{PublishedSparseSession, ResultPool},
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
    waveform::{NormalizedSample, PcmView},
    *,
};
fn source() -> SourceInfo {
    SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(256),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    }
}
fn input<'a>(spans: &'a [WaveformSpan], columns: &'a [WaveformColumn]) -> NativeResultInput<'a> {
    NativeResultInput {
        source: source(),
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
            frames_per_column: 64,
            origin_frame: 0,
            state: FeatureState::Partial,
            confidence: 255,
            spans,
            columns,
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
    }
}
#[test]
fn seed_preflight_failure_is_atomic_and_valid_seed_preserves_created_generation() {
    let span = |index| WaveformSpan {
        first_frame: index as u64 * 64,
        end_frame: (index as u64 + 1) * 64,
        first_column_index: index,
        column_count: 1,
        data_column_offset: 0,
    };
    let spans = [
        span(0),
        WaveformSpan {
            data_column_offset: 1,
            ..span(2)
        },
    ];
    let columns = [WaveformColumn {
        minimum: -10000,
        maximum: 10000,
        rms: 10000,
        flags: 1,
        ..Default::default()
    }; 2];
    let mut owned_spans = [WaveformSpan::default(); 2];
    let mut owned_columns = [WaveformColumn::default(); 2];
    let mut text = [0; 16];
    let mut checkpoint = owned_result::copy(
        &input(&spans, &columns),
        Storage {
            overview_spans: &mut owned_spans,
            overview_columns: &mut owned_columns,
            text_bytes: &mut text,
            ..Default::default()
        },
        NativeLimits::default(),
    )
    .unwrap();
    let mut a = [WaveformColumn::default(); 4];
    let mut b = a;
    let mut sa = [WaveformSpan::default(); 2];
    let mut sb = sa;
    let pool = ResultPool::new(
        source(),
        [
            Storage {
                overview_columns: &mut a,
                overview_spans: &mut sa,
                ..Default::default()
            },
            Storage {
                overview_columns: &mut b,
                overview_spans: &mut sb,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut accum = [SparseAccumulator::default(); 4];
    let mut ranges = [FrameRange::default(); 1];
    let mut nodes = [QueuedBlock::default(); 1];
    let mut pcm = [NormalizedSample::default(); NODE_FRAMES];
    let mut outspans = [WaveformSpan::default(); 4];
    let mut outcols = [WaveformColumn::default(); 4];
    let mut session = PublishedSparseSession::new(
        SessionConfig {
            sample_rate: 48000,
            channel_count: 1,
            total_frames: 256,
            frames_per_column: 64,
        },
        Workspace {
            accumulators: &mut accum,
            ranges: &mut ranges,
            nodes: &mut nodes,
            pcm: &mut pcm,
            snapshot_spans: &mut outspans,
            snapshot_columns: &mut outcols,
        },
        &pool,
    )
    .unwrap();
    assert_eq!(
        session.seed_from_result(&checkpoint, false),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(session.session().complete_columns(), 0);
    assert!(session.session().accepted_ranges().is_empty());
    assert_eq!(pool.generation(), 1);
    let one = input(&spans[..1], &columns[..1]);
    checkpoint.replace(&one, NativeLimits::default()).unwrap();
    assert_eq!(
        session.seed_from_result(&checkpoint, true),
        Err(Error::Conflict)
    );
    session.seed_from_result(&checkpoint, false).unwrap();
    assert_eq!(session.session().state(), SessionState::Created);
    assert_eq!(session.session().complete_columns(), 1);
    assert_eq!(pool.generation(), 1);
    session
        .process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    assert_eq!(pool.generation(), 1);
    assert!(pool.acquire().unwrap().overview().is_none());
    assert_eq!(
        session.push_at(0, PcmView::S16Interleaved(&[0; 64])),
        Err(Error::Conflict)
    );
    // C transitions to Active before detecting the overlapping PCM.
    assert_eq!(pool.generation(), 2);
    assert!(pool.acquire().unwrap().overview().is_some());
    assert_eq!(
        session.seed_from_result(&checkpoint, false),
        Err(Error::InvalidState)
    );
}
