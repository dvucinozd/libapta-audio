// SPDX-License-Identifier: Apache-2.0
use libapta::{session::*, waveform::PcmView, *};
use libapta_runtime::*;
fn config() -> SessionConfig {
    SessionConfig {
        sample_rate: 8000,
        channel_count: 1,
        total_frames: 512,
        frames_per_column: 256,
    }
}
fn identity(kind: u32, variant: u8) -> SourceIdentity {
    SourceIdentity::new(
        kind,
        core::array::from_fn(|i| if kind == 0 { 0 } else { i as u8 + variant }),
    )
    .unwrap()
}
#[test]
#[ignore = "requires APTA_C_SOURCE_IDENTITY_ORACLE"]
fn actual_checkpoint_identity_and_resumed_wire_match_public_c() {
    for unknown in [false, true] {
        for from in 0..=2 {
            let mut cfg = config();
            if unknown {
                cfg.total_frames = TOTAL_FRAMES_UNKNOWN;
            }
            let mut writer =
                GrowingSession::new_with_identity(cfg, GrowingLimits::default(), identity(from, 0))
                    .unwrap();
            writer
                .push_pcm(PcmView::S16Interleaved(&[12000; 256]))
                .unwrap();
            writer
                .process(WorkBudget::default(), &CancellationToken::new())
                .unwrap();
            let seed = writer.results().acquire().unwrap();
            drop(writer);
            for to in 0..=2 {
                for variant in 0..=1 {
                    for required in [false, true] {
                        let mut command = std::process::Command::new(
                            std::env::var_os("APTA_C_SOURCE_IDENTITY_ORACLE").unwrap(),
                        );
                        command.args([
                            from.to_string(),
                            to.to_string(),
                            variant.to_string(),
                            u8::from(required).to_string(),
                        ]);
                        if unknown {
                            command.arg("unknown-seed");
                        }
                        let c = command.output().unwrap();
                        assert!(c.status.success(), "{}", String::from_utf8_lossy(&c.stderr));
                        let newline = c.stdout.iter().position(|b| *b == b'\n').unwrap();
                        let status: i32 = std::str::from_utf8(&c.stdout[..newline])
                            .unwrap()
                            .parse()
                            .unwrap();
                        let mut s = OwnedSparseSession::new_with_identity(
                            config(),
                            SparseLimits::default(),
                            identity(to, variant),
                        )
                        .unwrap();
                        let old = s.results().acquire().unwrap();
                        let seeded = s.seed_from_result(&seed, required);
                        assert_eq!(
                            seeded.map_or_else(
                                |e| {
                                    assert_eq!(e, Error::Conflict);
                                    -10
                                },
                                |_| 0
                            ),
                            status,
                            "{from} {to} {variant} {required}"
                        );
                        assert_eq!(s.results().acquire().unwrap().info(), old.info());
                        if seeded.is_err() {
                            assert!(s.session().accepted_ranges().is_empty());
                            continue;
                        }
                        s.push_at(256, PcmView::S16Interleaved(&[-6000; 256]))
                            .unwrap();
                        s.process(WorkBudget::default(), &CancellationToken::new())
                            .unwrap();
                        s.finish_input().unwrap();
                        s.process(WorkBudget::default(), &CancellationToken::new())
                            .unwrap();
                        let snapshot = s.snapshot().unwrap();
                        let mut tiles = [];
                        let view = result::from_session_snapshot(
                            &snapshot,
                            &mut tiles,
                            NativeLimits::default(),
                        )
                        .unwrap();
                        let mut bytes = vec![0; result::serialized_size(&view).unwrap()];
                        result::write(&view, &mut bytes, Default::default()).unwrap();
                        assert_eq!(
                            bytes,
                            c.stdout[newline + 1..],
                            "{from} {to} {variant} {required}"
                        );
                        assert!(old.view().overview.is_none());
                    }
                }
            }
        }
    }
}

#[test]
fn identity_validation_and_context_retention() {
    assert_eq!(SourceIdentity::new(3, [0; 32]), Err(Error::InvalidArgument));
    assert_eq!(SourceIdentity::new(0, [1; 32]), Err(Error::InvalidArgument));
    assert!(SourceIdentity::new(2, [0; 32]).is_ok());
    let ctx = RuntimeContext::new(ContextLimits::default());
    let mut s = ctx
        .create_sparse_session_with_identity(config(), SparseLimits::default(), identity(2, 0))
        .unwrap();
    let old = s.results().acquire().unwrap();
    assert_eq!(old.source().fingerprint_kind, 2);
    s.push_at(0, PcmView::S16Interleaved(&[0; 256])).unwrap();
    s.process(WorkBudget::default(), &CancellationToken::new())
        .unwrap();
    let current = s.results().acquire().unwrap();
    assert_eq!(current.source(), old.source());
    drop(s);
    assert_eq!(ctx.close(), Err(Error::Busy));
    drop(current);
    drop(old);
    assert_eq!(ctx.close(), Ok(()));
}
