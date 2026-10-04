// SPDX-License-Identifier: Apache-2.0
use libapta::{result::WAVEFORM_DETAIL, *};
fn write_detail(output: &mut String, detail: NativeDetail<'_>) {
    use std::fmt::Write;
    writeln!(output, "R {}", detail.tiles.len()).unwrap();
    for t in detail.tiles {
        writeln!(
            output,
            "T {} {} {} {} {} {} {} {}",
            t.level_id,
            t.tile_index,
            t.first_frame,
            t.end_frame,
            t.first_column_index,
            t.column_count,
            t.state as u8,
            t.confidence
        )
        .unwrap();
        for c in &detail.columns[t.data_column_offset..t.data_column_offset + t.column_count] {
            writeln!(
                output,
                "C {} {} {} {} {} {} {}",
                c.minimum, c.maximum, c.rms, c.low, c.mid, c.high, c.flags
            )
            .unwrap();
        }
    }
}
#[test]
#[ignore = "requires APTA_C_DETAIL_SESSION_ORACLE"]
fn eager_accepted_pcm_and_partial_eof_publication_match_public_c() {
    let oracle =
        std::env::var_os("APTA_C_DETAIL_SESSION_ORACLE").expect("set APTA_C_DETAIL_SESSION_ORACLE");
    let c = std::process::Command::new(oracle).output().unwrap();
    assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
    assert_eq!(native_trace(), String::from_utf8(c.stdout).unwrap());
}
fn native_trace() -> String {
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
    let mut output = String::new();
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
        write_detail(&mut output, first.detail().unwrap());
        assert!(first.overview().is_none());
        assert_eq!(first.detail().unwrap().tiles[0].first_frame, 256);
        assert_eq!(first.changed_features(), WAVEFORM_DETAIL);
        drop(first);
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        let completed = pool.acquire().unwrap();
        write_detail(&mut output, completed.detail().unwrap());
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
    output
}
