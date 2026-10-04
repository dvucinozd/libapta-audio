// SPDX-License-Identifier: Apache-2.0
use libapta::{
    detail_analysis::DetailTile,
    session::*,
    waveform::{NormalizedSample, PcmView},
    *,
};
const EMPTY: NativeTile = NativeTile {
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
fn config(total: u64) -> SessionConfig {
    SessionConfig {
        sample_rate: 48000,
        channel_count: 1,
        total_frames: total,
        frames_per_column: 64,
    }
}
#[test]
fn eager_push_eof_unknown_and_retained_copy() {
    for total in [513, TOTAL_FRAMES_UNKNOWN] {
        let mut queue = [NormalizedSample::default(); 513];
        let mut overview = [WaveformColumn::default(); 9];
        let mut cache = [DetailTile::default(); 4];
        let mut tiles = [EMPTY; 4];
        let mut columns = [WaveformColumn::default(); 256];
        {
            let mut session = Session::new(config(total), &mut queue, &mut overview).unwrap();
            session.enable_detail(&mut cache).unwrap();
            session
                .push_pcm(PcmView::S16Interleaved(&[123; 513]))
                .unwrap();
            assert_eq!(session.processed_frames(), 0);
            assert_eq!(
                session
                    .copy_detail_into(&mut tiles, &mut columns)
                    .unwrap()
                    .unwrap()
                    .columns
                    .len(),
                2
            );
            session
                .process(
                    WorkBudget {
                        maximum_input_frames: 1,
                        maximum_steps: 1,
                    },
                    &CancellationToken::new(),
                )
                .unwrap();
            session.finish_input().unwrap();
            let detail = session
                .copy_detail_into(&mut tiles, &mut columns)
                .unwrap()
                .unwrap();
            assert_eq!(detail.columns.len(), 3);
            assert_eq!(detail.tiles[0].end_frame, 513);
            assert_eq!(detail.tiles[0].state, FeatureState::Final);
        }
        assert_eq!(tiles[0].end_frame, 513);
        assert!(columns[..3].iter().all(|c| c.minimum > 0));
    }
}
#[test]
fn invalid_pcm_and_capacity_do_not_change_detail() {
    let mut queue = [NormalizedSample::default(); 512];
    let mut overview = [WaveformColumn::default(); 4];
    let mut cache = [DetailTile::default(); 4];
    let mut session =
        Session::new(config(TOTAL_FRAMES_UNKNOWN), &mut queue, &mut overview).unwrap();
    session.enable_detail(&mut cache).unwrap();
    let mut bad = [0.5; 256];
    bad[255] = f32::NAN;
    assert_eq!(session.push_interleaved(&bad), Err(Error::InvalidArgument));
    assert_eq!(session.accepted_frames(), 0);
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    assert!(session
        .copy_detail_into(&mut tiles, &mut columns)
        .unwrap()
        .is_none());
    assert_eq!(session.push_interleaved(&[0.25; 300]).unwrap(), 256);
    let before = session
        .copy_detail_into(&mut tiles, &mut columns)
        .unwrap()
        .unwrap()
        .columns[0];
    assert_eq!(session.push_interleaved(&[1.0]), Err(Error::BufferTooSmall));
    assert_eq!(
        session
            .copy_detail_into(&mut tiles, &mut columns)
            .unwrap()
            .unwrap()
            .columns[0],
        before
    );
    let mut short: [WaveformColumn; 0] = [];
    assert_eq!(
        session.copy_detail_into(&mut tiles, &mut short),
        Err(Error::BufferTooSmall)
    );
    assert_eq!(tiles[0].column_count, 1);
}
#[test]
fn planar_stereo_mixing_matches_c_clipping_rule() {
    let mut queue = [NormalizedSample::default(); 256];
    let mut overview = [WaveformColumn::default(); 4];
    let mut cache = [DetailTile::default(); 4];
    let mut cfg = config(256);
    cfg.channel_count = 2;
    let mut session = Session::new(cfg, &mut queue, &mut overview).unwrap();
    session.enable_detail(&mut cache).unwrap();
    let left = [1.0; 256];
    let right = [-1.0; 256];
    session
        .push_pcm(PcmView::F32Planar(&[&left, &right]))
        .unwrap();
    session.finish_input().unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    let c = session
        .copy_detail_into(&mut tiles, &mut columns)
        .unwrap()
        .unwrap()
        .columns[0];
    assert_eq!((c.minimum, c.maximum, c.rms, c.flags & 2), (0, 0, 0, 0));
}
fn trace_detail(output: &mut String, detail: NativeDetail<'_>) {
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
fn sequential_known_and_unknown_match_public_c_detail() {
    let oracle = std::env::var_os("APTA_C_DETAIL_SESSION_ORACLE").unwrap();
    for (mode, total) in [("sequential", 513), ("unknown", TOTAL_FRAMES_UNKNOWN)] {
        let mut queue = [NormalizedSample::default(); 513];
        let mut overview = [WaveformColumn::default(); 9];
        let mut cache = [DetailTile::default(); 4];
        let mut tiles = [EMPTY; 4];
        let mut columns = [WaveformColumn::default(); 256];
        let mut session = Session::new(config(total), &mut queue, &mut overview).unwrap();
        session.enable_detail(&mut cache).unwrap();
        session
            .push_pcm(PcmView::S16Interleaved(&[123; 513]))
            .unwrap();
        session
            .process(
                WorkBudget {
                    maximum_input_frames: 1,
                    maximum_steps: 1,
                },
                &CancellationToken::new(),
            )
            .unwrap();
        let mut trace = String::new();
        trace_detail(
            &mut trace,
            session
                .copy_detail_into(&mut tiles, &mut columns)
                .unwrap()
                .unwrap(),
        );
        session.finish_input().unwrap();
        session
            .process(WorkBudget::default(), &CancellationToken::new())
            .unwrap();
        trace_detail(
            &mut trace,
            session
                .copy_detail_into(&mut tiles, &mut columns)
                .unwrap()
                .unwrap(),
        );
        let c = std::process::Command::new(&oracle)
            .arg(mode)
            .output()
            .unwrap();
        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
        assert_eq!(trace, String::from_utf8(c.stdout).unwrap(), "{mode}");
    }
}
#[test]
fn sequential_pull_unknown_eof_release_and_retained_detail() {
    use libapta::pull::*;
    use std::cell::Cell;
    struct Source<'a> {
        samples: [i16; 513],
        release: &'a mut dyn FnMut(),
    }
    impl PullSource for Source<'_> {
        fn read_frames(&mut self, first: u64, maximum: u32) -> Result<PullRead<'_>, Error> {
            if first == 513 {
                return Ok(PullRead::EndOfInput);
            }
            let n = (513 - first).min(u64::from(maximum)) as usize;
            Ok(PullRead::Data(PullBlock::new(
                first,
                PcmView::S16Interleaved(&self.samples[..n]),
                self.release,
            )))
        }
    }
    let releases = Cell::new(0);
    let mut release = || releases.set(releases.get() + 1);
    let mut queue = [NormalizedSample::default(); 256];
    let mut overview = [WaveformColumn::default(); 9];
    let mut cache = [DetailTile::default(); 4];
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    {
        let mut pull = PullSession::new(
            config(TOTAL_FRAMES_UNKNOWN),
            &mut queue,
            &mut overview,
            Source {
                samples: [123; 513],
                release: &mut release,
            },
        )
        .unwrap();
        pull.enable_detail(&mut cache).unwrap();
        let token = CancellationToken::new();
        while pull.state() != PullState::Complete {
            pull.process(WorkBudget::default(), &token).unwrap();
        }
        assert_eq!(releases.get(), 3);
        let detail = pull
            .copy_detail_into(&mut tiles, &mut columns)
            .unwrap()
            .unwrap();
        assert_eq!(detail.columns.len(), 3);
        assert_eq!(detail.tiles[0].end_frame, 513);
    }
    assert_eq!(tiles[0].state, FeatureState::Final);
}
