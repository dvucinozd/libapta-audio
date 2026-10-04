// SPDX-License-Identifier: Apache-2.0
use libapta::{
    band::BandSums,
    detail_analysis::DetailTile,
    owned_result::Storage,
    publication::{PublishedSparseSession, ResultPool},
    result::{WAVEFORM_DETAIL, WAVEFORM_OVERVIEW},
    scheduler::RequestSlot,
    session::{CancellationToken, SessionConfig, WorkBudget},
    sparse::{QueuedBlock, SparseAccumulator, Workspace, NODE_FRAMES},
    waveform::{NormalizedSample, PcmView},
    *,
};

#[test]
fn replay_preserves_overview_bands_and_rejects_invalid_input_before_cache_mutation() {
    let source = SourceInfo {
        sample_rate: 48000,
        channel_count: 1,
        channel_layout: 1,
        total_frames: Some(81920),
        fingerprint_kind: 0,
        fingerprint: [0; 32],
    };
    let config = SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: 81920,
        frames_per_column: 256,
    };
    let mut result_columns = [[WaveformColumn::default(); 320]; 2];
    let mut result_spans = [[WaveformSpan::default(); 160]; 2];
    let mut result_tiles = [[NativeTile {
        level_id: 1,
        tile_index: 0,
        first_frame: 0,
        end_frame: 0,
        first_column_index: 0,
        state: FeatureState::Partial,
        confidence: 0,
        data_column_offset: 0,
        column_count: 0,
    }; 4]; 2];
    let mut result_detail = [[WaveformColumn::default(); 256]; 2];
    let [a, b] = &mut result_columns;
    let [sa, sb] = &mut result_spans;
    let [ta, tb] = &mut result_tiles;
    let [da, db] = &mut result_detail;
    let pool = ResultPool::new(
        source,
        [
            Storage {
                overview_columns: a,
                overview_spans: sa,
                detail_tiles: ta,
                detail_columns: da,
                ..Default::default()
            },
            Storage {
                overview_columns: b,
                overview_spans: sb,
                detail_tiles: tb,
                detail_columns: db,
                ..Default::default()
            },
        ],
        NativeLimits::default(),
    )
    .unwrap();
    let mut accumulators = [SparseAccumulator::default(); 320];
    let mut ranges = [FrameRange::default(); 16];
    let mut nodes = [QueuedBlock::default(); 1];
    let mut pcm = [NormalizedSample::default(); NODE_FRAMES];
    let mut spans = [WaveformSpan::default(); 320];
    let mut columns = [WaveformColumn::default(); 320];
    let mut cache = [DetailTile::default(); 4];
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
    let mut bands = [BandSums::default(); 320];
    let mut slots = [RequestSlot::default(); 16];
    let token = CancellationToken::new();
    let retained = {
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
            &mut slots,
        )
        .unwrap();
        session.enable_three_band(&mut bands).unwrap();
        session
            .enable_detail(&mut cache, &mut detail_tiles, &mut detail_columns)
            .unwrap();
        for tile in 0..5 {
            session
                .push_at(tile * 16384, PcmView::F32Interleaved(&[0.5; 256]))
                .unwrap();
            session.process(WorkBudget::default(), &token).unwrap();
        }
        let before = pool.acquire().unwrap();
        let overview = before.view().overview.unwrap().columns.to_vec();
        drop(before);
        let id = session
            .request_region(RegionRequest {
                range: FrameRange {
                    first_frame: 0,
                    end_frame: 256,
                },
                feature_mask: WAVEFORM_DETAIL,
                priority: 200,
                soft_deadline_monotonic_ns: 0,
                request_id: 0,
            })
            .unwrap();
        assert_eq!(
            session.next_pcm_request().unwrap().range,
            FrameRange {
                first_frame: 0,
                end_frame: 256
            }
        );
        let serial = session.session().detail_mutation_serial();
        assert_eq!(
            session.push_at(0, PcmView::F32Interleaved(&[f32::NAN; 256])),
            Err(Error::InvalidArgument)
        );
        assert_eq!(session.session().detail_mutation_serial(), serial);
        assert_eq!(
            session.push_at(0, PcmView::F32Interleaved(&[0.25; 255])),
            Err(Error::Conflict)
        );
        assert_eq!(session.session().detail_mutation_serial(), serial);
        let processed = session.session().processed_frames();
        session
            .push_at(0, PcmView::F32Interleaved(&[0.25; 256]))
            .unwrap();
        assert_eq!(session.session().queued_frames(), 0);
        assert_eq!(
            session
                .process(WorkBudget::default(), &token)
                .unwrap()
                .consumed_input_frames,
            0
        );
        assert_eq!(session.session().processed_frames(), processed);
        assert_eq!(
            session.request_progress(id).unwrap().state,
            RequestState::Satisfied
        );
        let snapshot = pool.acquire().unwrap();
        assert_eq!(snapshot.view().overview.unwrap().columns, overview);
        assert_eq!(snapshot.changed_features(), WAVEFORM_DETAIL);
        let detail = snapshot.view().detail.unwrap();
        let first = detail
            .tiles
            .iter()
            .find(|tile| tile.tile_index == 0)
            .unwrap();
        assert_eq!(detail.columns[first.data_column_offset].maximum, 8192);
        snapshot
    };
    assert!(
        retained.available_features() & (WAVEFORM_OVERVIEW | WAVEFORM_DETAIL)
            == WAVEFORM_OVERVIEW | WAVEFORM_DETAIL
    );
}
