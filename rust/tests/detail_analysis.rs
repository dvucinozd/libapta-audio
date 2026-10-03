// SPDX-License-Identifier: Apache-2.0
use libapta::{
    detail_analysis::*,
    waveform::{normalize_s16, NormalizedSample},
    *,
};
const EMPTY: NativeTile = NativeTile {
    level_id: 0,
    tile_index: 0,
    first_frame: 0,
    end_frame: 0,
    first_column_index: 0,
    state: FeatureState::Partial,
    confidence: 0,
    data_column_offset: 0,
    column_count: 0,
};
fn feed(cache: &mut DetailCache<'_>, first: u64, n: u64, protect: u64) {
    for frame in first..first + n {
        assert!(cache
            .push_normalized(
                frame,
                normalize_s16((((frame * 73) % 65536) as i32 - 32768) as i16),
                |tile| tile < 64 && protect & (1 << tile) != 0
            )
            .unwrap());
    }
}
#[test]
fn completion_pins_first_run_and_only_extends_it() {
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    feed(&mut c, 512, 256, 0);
    feed(&mut c, 1536, 256, 0);
    c.refresh_completed(None).unwrap();
    assert_eq!(c.mutation_serial(), 1);
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        (s.tiles[0].first_column_index, s.tiles[0].column_count),
        (2, 1)
    );
    feed(&mut c, 1792, 512, 0);
    c.refresh_completed(None).unwrap();
    assert_eq!(c.mutation_serial(), 1);
    assert!(c.range_complete(FrameRange {
        first_frame: 1536,
        end_frame: 2304
    }));
    assert!(!c.range_has_output(FrameRange {
        first_frame: 1536,
        end_frame: 2304
    }));
    feed(&mut c, 768, 768, 0);
    c.refresh_completed(None).unwrap();
    assert_eq!(c.mutation_serial(), 2);
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        (s.tiles[0].first_column_index, s.tiles[0].column_count),
        (2, 7)
    );
    assert_eq!(s.tiles[0].state, FeatureState::Partial);
}
#[test]
fn lru_protection_degrades_or_evicts_and_snapshots_are_sorted() {
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    for tile in [3, 1, 2, 0] {
        feed(&mut c, tile * TILE_FRAMES, 256, 0);
        c.refresh_completed(None).unwrap();
    }
    let prior = c.mutation_serial();
    assert!(!c
        .push_normalized(4 * TILE_FRAMES, normalize_s16(123), |i| i < 4)
        .unwrap());
    assert_eq!(c.mutation_serial(), prior);
    feed(&mut c, 4 * TILE_FRAMES, 256, 0b11111);
    c.refresh_completed(None).unwrap();
    assert_eq!(c.mutation_serial(), prior + 2);
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        s.tiles.iter().map(|t| t.tile_index).collect::<Vec<_>>(),
        [0, 1, 2, 4]
    );
    feed(&mut c, 5 * TILE_FRAMES, 256, 0b10101);
    c.refresh_completed(None).unwrap();
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        s.tiles.iter().map(|t| t.tile_index).collect::<Vec<_>>(),
        [0, 2, 4, 5]
    );
}
#[test]
fn eof_completes_tail_and_final_state_geometry() {
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    feed(&mut c, 0, 513, 0);
    c.refresh_completed(None).unwrap();
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        (
            s.tiles[0].column_count,
            s.tiles[0].end_frame,
            s.tiles[0].state
        ),
        (2, 512, FeatureState::Partial)
    );
    c.refresh_completed(Some(513)).unwrap();
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        (
            s.tiles[0].column_count,
            s.tiles[0].end_frame,
            s.tiles[0].state,
            s.tiles[0].confidence
        ),
        (3, 513, FeatureState::Final, 255)
    );
    assert!(c.range_complete(FrameRange {
        first_frame: 0,
        end_frame: 768
    }));
    assert!(!c.range_has_output(FrameRange {
        first_frame: 513,
        end_frame: 768
    }));
    assert_eq!(c.refresh_completed(None), Err(Error::InvalidArgument));
    assert_eq!(
        c.push_normalized(513, normalize_s16(0), |_| false),
        Err(Error::InvalidArgument)
    );
}
#[test]
fn full_tile_stable_before_eof_and_retained_output_independent() {
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    feed(&mut c, 0, TILE_FRAMES, 0);
    c.refresh_completed(None).unwrap();
    let snapshot = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(snapshot.tiles[0].state, FeatureState::Stable);
    let retained = snapshot.columns.to_vec();
    for i in 1..6 {
        feed(&mut c, i * TILE_FRAMES, 256, 0);
        c.refresh_completed(None).unwrap();
    }
    assert_eq!(snapshot.columns, retained);
    assert_eq!(snapshot.tiles[0].tile_index, 0);
}
#[test]
fn invalid_input_capacity_and_extreme_ranges_leave_outputs_unchanged() {
    let mut small = [DetailTile::default(); 3];
    assert!(matches!(
        DetailCache::new(&mut small),
        Err(Error::BufferTooSmall)
    ));
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    for value in [f32::NAN, f32::INFINITY, 1.1] {
        assert_eq!(
            c.push_normalized(
                0,
                NormalizedSample {
                    value,
                    clipped: false
                },
                |_| false
            ),
            Err(Error::InvalidArgument)
        );
    }
    assert_eq!(
        c.push_normalized(u64::MAX, normalize_s16(0), |_| false),
        Err(Error::LimitExceeded)
    );
    assert!(!c.range_complete(FrameRange {
        first_frame: 0,
        end_frame: u64::MAX
    }));
    assert!(!c.column_is_empty(u64::MAX));
    assert_eq!(
        c.refresh_completed(Some(u64::MAX)),
        Err(Error::InvalidArgument)
    );
    assert_eq!(c.mutation_serial(), 0);
    feed(&mut c, 0, 256, 0);
    c.refresh_completed(None).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut none = [];
    assert!(matches!(
        c.snapshot_into(&mut tiles, &mut none),
        Err(Error::BufferTooSmall)
    ));
    assert_eq!(tiles, [EMPTY; 4]);
    assert_eq!(c.refresh_completed(Some(255)), Err(Error::InvalidArgument));
    assert_eq!(c.mutation_serial(), 1);
}
#[test]
fn clipping_matches_c_mixed_sample_not_untrusted_flag() {
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    for frame in 0..256 {
        c.push_normalized(
            frame,
            NormalizedSample {
                value: 0.5,
                clipped: true,
            },
            |_| false,
        )
        .unwrap();
    }
    c.refresh_completed(None).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    let s = c.snapshot_into(&mut tiles, &mut columns).unwrap();
    assert_eq!(
        (
            s.columns[0].minimum,
            s.columns[0].maximum,
            s.columns[0].rms,
            s.columns[0].flags
        ),
        (16384, 16384, 32768, 1)
    );
}

fn kernel_trace(commands: &str) -> String {
    use std::fmt::Write;
    let mut storage = [DetailTile::default(); 4];
    let mut c = DetailCache::new(&mut storage).unwrap();
    let mut tiles = [EMPTY; 4];
    let mut columns = [WaveformColumn::default(); 256];
    let mut protected = 0u64;
    let mut out = String::new();
    for line in commands.lines() {
        let fields: Vec<_> = line.split_whitespace().collect();
        let a: u64 = fields[1].parse().unwrap();
        let b: u64 = fields[2].parse().unwrap();
        match fields[0] {
            "P" => {
                let mut status = 0;
                let mut count = 0;
                for frame in a..a + b {
                    let raw = (((frame * 73) % 65536) as i32 - 32768) as i16;
                    match c.push_normalized(frame, normalize_s16(raw), |tile| {
                        tile < 64 && protected & (1 << tile) != 0
                    }) {
                        Ok(true) => count += 1,
                        Ok(false) => {
                            status = 4;
                            break;
                        }
                        Err(Error::LimitExceeded) => {
                            status = -11;
                            break;
                        }
                        Err(e) => panic!("unexpected{e:?}"),
                    }
                }
                writeln!(out, "P {status} {count}").unwrap();
            }
            "R" => {
                c.refresh_completed(if a == u64::MAX { None } else { Some(a) })
                    .unwrap();
                let d = c.snapshot_into(&mut tiles, &mut columns).unwrap();
                writeln!(out, "R {} {}", c.mutation_serial(), d.tiles.len()).unwrap();
                for t in d.tiles {
                    writeln!(
                        out,
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
                    for c in &d.columns[t.data_column_offset..t.data_column_offset + t.column_count]
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
            "T" => protected = a,
            "Q" => {
                let r = FrameRange {
                    first_frame: a,
                    end_frame: b,
                };
                writeln!(
                    out,
                    "Q {} {}",
                    u8::from(c.range_complete(r)),
                    u8::from(c.range_has_output(r))
                )
                .unwrap();
            }
            op => panic!("{op}"),
        }
    }
    out
}
#[test]
#[ignore = "requires APTA_C_DETAIL_ANALYSIS_ORACLE"]
fn detail_kernel_exact_quantization_runs_protection_eviction_and_eof_match_c() {
    use std::io::Write;
    let oracle = std::env::var_os("APTA_C_DETAIL_ANALYSIS_ORACLE")
        .expect("set APTA_C_DETAIL_ANALYSIS_ORACLE");
    let none = u64::MAX;
    let scenarios=[
 format!("P 0 16384\nR {none} 0\nR 16384 0\nQ 0 16384\n"),
 format!("P 0 513\nR {none} 0\nR 513 0\nQ 0 768\nQ 513 768\n"),
 format!("P 512 256\nP 1536 256\nR {none} 0\nP 1792 512\nR {none} 0\nQ 1536 2304\nP 768 768\nR {none} 0\nQ 512 2304\n"),
 format!("P 49152 256\nR {none} 0\nP 16384 256\nR {none} 0\nP 32768 256\nR {none} 0\nP 0 256\nR {none} 0\nT 15 0\nP 65536 256\nR {none} 0\nT 31 0\nP 65536 256\nR {none} 0\nT 21 0\nP 81920 256\nR {none} 0\n"),
 // Accessing a resident tile updates LRU even before its new column completes.
 format!("P 0 256\nP 16384 256\nP 32768 256\nP 49152 256\nR {none} 0\nP 256 1\nP 65536 256\nR {none} 0\nQ 16384 16640\n"),
 ];
    for (i, commands) in scenarios.iter().enumerate() {
        let mut child = std::process::Command::new(&oracle)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
            .unwrap();
        child
            .stdin
            .take()
            .unwrap()
            .write_all(commands.as_bytes())
            .unwrap();
        let out = child.wait_with_output().unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        assert_eq!(
            kernel_trace(commands),
            String::from_utf8(out.stdout).unwrap(),
            "scenario{i}"
        );
    }
}
