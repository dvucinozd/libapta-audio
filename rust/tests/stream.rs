// SPDX-License-Identifier: Apache-2.0
use libapta::{container::crc32c, result::*, stream::*, Error};
struct Reader {
    bytes: Vec<u8>,
    maximum: usize,
    request_limit: usize,
    reads: usize,
    fail_after: Option<usize>,
    oversized: bool,
}
impl Input for Reader {
    fn size(&mut self) -> Result<u64, Error> {
        Ok(self.bytes.len() as u64)
    }
    fn read_at(&mut self, p: u64, out: &mut [u8]) -> Result<usize, Error> {
        assert!(out.len() <= self.request_limit);
        self.reads += 1;
        if self.fail_after.is_some_and(|n| self.reads >= n) {
            return Err(Error::Cancelled);
        }
        if self.oversized {
            return Ok(out.len() + 1);
        }
        let p = p as usize;
        let n = out
            .len()
            .min(self.maximum)
            .min(self.bytes.len().saturating_sub(p));
        out[..n].copy_from_slice(&self.bytes[p..p + n]);
        Ok(n)
    }
}
fn fixture(name: &str) -> Vec<u8> {
    let path = if name.starts_with("dj-") {
        format!("../tests/fixtures/{name}")
    } else {
        format!("../conformance/fixtures/container-v1-suite/{name}")
    };
    let h = std::fs::read_to_string(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(path))
        .unwrap();
    let h: String = h.chars().filter(|c| !c.is_whitespace()).collect();
    (0..h.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
        .collect()
}
fn reader(b: Vec<u8>) -> Reader {
    Reader {
        bytes: b,
        maximum: 3,
        request_limit: 17,
        reads: 0,
        fail_after: None,
        oversized: false,
    }
}
fn get64(b: &[u8], p: usize) -> u64 {
    u64::from_le_bytes(b[p..p + 8].try_into().unwrap())
}
fn put32(b: &mut [u8], p: usize, v: u32) {
    b[p..p + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(b: &mut [u8], p: usize, v: u64) {
    b[p..p + 8].copy_from_slice(&v.to_le_bytes());
}
#[test]
fn selected_key_and_quality_are_owned_and_filtered() {
    let mut r = reader(fixture("dj-sections-v1-combined.apta.hex"));
    let mut scratch = [0; 17];
    let mut d = [StoredSection::default(); 4];
    let mut arena = [0; 512];
    let v = read_from_stream(
        &mut r,
        StreamOptions {
            requested_features: MUSICAL_KEY | CALIBRATED_QUALITY,
            ..Default::default()
        },
        &mut scratch,
        &mut d,
        &mut arena,
    )
    .unwrap();
    assert!(v.waveform.overview.is_none());
    assert!(v.meter.is_none());
    assert_eq!(v.available_features, MUSICAL_KEY | CALIBRATED_QUALITY);
    let q = v.quality.unwrap();
    assert_eq!(q.record_count(), 1);
    assert_eq!(q.record(0).unwrap().feature, MUSICAL_KEY);
    assert!(q.record(1).is_none());
    drop(r);
    assert_eq!(v.key.unwrap().candidate(0).unwrap().score, 62000);
}
#[test]
fn malformed_unselected_payload_is_crc_checked_but_not_materialized() {
    let mut b = fixture("dj-sections-v1-combined.apta.hex");
    let p = get64(&b, 104) as usize;
    let n = get64(&b, 112) as usize;
    b[p] = 255;
    let crc = crc32c(&b[p..p + n]);
    put32(&mut b, 128, crc);
    let mut scratch = [0; 17];
    let mut d = [StoredSection::default(); 4];
    let mut arena = [0; 512];
    let options = StreamOptions {
        requested_features: MUSICAL_KEY,
        ..Default::default()
    };
    assert!(read_from_stream(
        &mut reader(b.clone()),
        options,
        &mut scratch,
        &mut d,
        &mut arena
    )
    .is_ok());
    b[p] ^= 1;
    assert_eq!(
        read_from_stream(&mut reader(b), options, &mut scratch, &mut d, &mut arena).unwrap_err(),
        Error::Corrupt
    );
}
#[test]
fn dependency_grid_is_validated_then_hidden() {
    let mut r = reader(fixture("v1-wovr-temp-ggrd-revn.apta.hex"));
    let mut scratch = [0; 17];
    let mut d = [StoredSection::default(); 4];
    let mut arena = [0; 1024];
    let v = read_from_stream(
        &mut r,
        StreamOptions {
            requested_features: GLOBAL_BEATGRID,
            ..Default::default()
        },
        &mut scratch,
        &mut d,
        &mut arena,
    )
    .unwrap();
    assert!(v.tempo.is_none());
    assert!(v.global_grid.is_some());
    assert!(v.waveform.overview.is_none());
    assert_eq!(v.available_features, GLOBAL_BEATGRID);
}
#[test]
fn callback_failures_propagate_and_oversized_progress_is_rejected() {
    let mut scratch = [0; 17];
    let mut d = [StoredSection::default(); 4];
    let mut arena = [0; 1024];
    let mut r = reader(fixture("dj-sections-v1-combined.apta.hex"));
    r.fail_after = Some(4);
    assert_eq!(
        read_from_stream(
            &mut r,
            StreamOptions::default(),
            &mut scratch,
            &mut d,
            &mut arena
        )
        .unwrap_err(),
        Error::Cancelled
    );
    r.fail_after = None;
    r.oversized = true;
    assert_eq!(
        read_from_stream(
            &mut r,
            StreamOptions::default(),
            &mut scratch,
            &mut d,
            &mut arena
        )
        .unwrap_err(),
        Error::Source
    );
}
#[test]
fn large_skipped_payload_needs_only_small_scratch_and_no_retained_arena() {
    let old = fixture("v1-wovr-only.apta.hex");
    let mut b = vec![0; old.len() + 40];
    b[..136].copy_from_slice(&old[..136]);
    b[176..].copy_from_slice(&old[136..]);
    put64(&mut b, 104, get64(&old, 104) + 40);
    put32(&mut b, 20, 2);
    while b.len() % 8 != 0 {
        b.push(0)
    }
    let start = b.len();
    b.resize(start + 1024 * 1024, 0x5a);
    b[136..140].copy_from_slice(b"JUNK");
    b[140] = 1;
    put64(&mut b, 144, start as u64);
    put64(&mut b, 152, 1024 * 1024);
    put64(&mut b, 160, 1024 * 1024);
    let crc = crc32c(&b[start..]);
    put32(&mut b, 168, crc);
    let size = b.len() as u64;
    put64(&mut b, 32, size);
    let crc = crc32c(&b[..92]);
    put32(&mut b, 92, crc);
    let mut r = reader(b);
    r.maximum = 17;
    let mut scratch = [0; 17];
    let mut d = [StoredSection::default(); 2];
    let v = read_from_stream(
        &mut r,
        StreamOptions {
            requested_features: 0,
            maximum_retained_bytes: 0,
            ..Default::default()
        },
        &mut scratch,
        &mut d,
        &mut [],
    )
    .unwrap();
    assert_eq!(v.available_features, 0);
    assert!(v.waveform.overview.is_none());
    assert!(r.reads > 60000);
}
struct Writer {
    bytes: Vec<u8>,
    position: usize,
    stall: bool,
    fail_seek: bool,
    fail_flush: bool,
    flushed: bool,
}
impl Output for Writer {
    fn write(&mut self, b: &[u8]) -> Result<usize, Error> {
        assert!(b.len() <= 7);
        if self.stall {
            return Ok(0);
        }
        let n = b.len().min(3);
        self.bytes.resize(self.position + n, 0);
        self.bytes[self.position..self.position + n].copy_from_slice(&b[..n]);
        self.position += n;
        Ok(n)
    }
    fn seek(&mut self, p: u64) -> Result<(), Error> {
        if self.fail_seek {
            return Err(Error::Cancelled);
        }
        self.position = p as usize;
        Ok(())
    }
    fn flush(&mut self) -> Result<(), Error> {
        if self.fail_flush {
            return Err(Error::Cancelled);
        }
        self.flushed = true;
        Ok(())
    }
}
fn writer() -> Writer {
    Writer {
        bytes: vec![],
        position: 0,
        stall: false,
        fail_seek: false,
        fail_flush: false,
        flushed: false,
    }
}
#[test]
fn output_transport_exact_bytes_partial_progress_and_failures() {
    let b = fixture("dj-sections-v1-combined.apta.hex");
    let mut w = writer();
    assert_eq!(
        write_bytes(&mut w, &b, Limits::default(), 7),
        Ok(b.len() as u64)
    );
    assert_eq!(w.bytes, b);
    assert!(w.flushed);
    let mut w = writer();
    w.stall = true;
    assert_eq!(
        write_bytes(&mut w, &b, Limits::default(), 7),
        Err(Error::Source)
    );
    assert!(!w.flushed);
    for flush in [false, true] {
        let mut w = writer();
        w.fail_seek = !flush;
        w.fail_flush = flush;
        assert_eq!(
            write_bytes(&mut w, &b, Limits::default(), 7),
            Err(Error::Cancelled)
        );
    }
    let mut bad = b.clone();
    bad[0] = 0;
    let mut w = writer();
    assert!(write_bytes(&mut w, &bad, Limits::default(), 7).is_err());
    assert!(w.bytes.is_empty());
}
#[test]
#[ignore = "requires APTA_C_STREAM_ORACLE"]
fn c_stream_selection_feature_parity() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    let oracle = std::env::var_os("APTA_C_STREAM_ORACLE").expect("set APTA_C_STREAM_ORACLE");
    for name in [
        "v1-wovr-only.apta.hex",
        "v1-wovr-meta.apta.hex",
        "v1-wovr-wdtl.apta.hex",
        "v1-wovr-temp.apta.hex",
        "v1-wovr-temp-lgrd.apta.hex",
        "v1-wovr-temp-ggrd-revn.apta.hex",
        "v1-all-standard-sections.apta.hex",
        "dj-sections-v1-combined.apta.hex",
    ] {
        for mask in [
            0,
            WAVEFORM_OVERVIEW,
            BPM,
            LOCAL_BEATGRID,
            GLOBAL_BEATGRID,
            MUSICAL_KEY,
            METER_DOWNBEAT,
            MUSICAL_KEY | CALIBRATED_QUALITY,
            CONFIDENCE,
            CONFIDENCE | METER_DOWNBEAT,
            CONFIDENCE | MUSICAL_KEY,
            ALL_FEATURES,
        ] {
            let b = fixture(name);
            let mut child = Command::new(&oracle)
                .arg(mask.to_string())
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .stderr(Stdio::piped())
                .spawn()
                .unwrap();
            child.stdin.take().unwrap().write_all(&b).unwrap();
            let out = child.wait_with_output().unwrap();
            assert!(
                out.status.success(),
                "C streaming failure {name} mask={mask}: {}",
                String::from_utf8_lossy(&out.stderr)
            );
            let expected: u64 = std::str::from_utf8(&out.stdout)
                .unwrap()
                .trim()
                .parse()
                .unwrap();
            let mut input = reader(b);
            let mut scratch = [0; 17];
            let mut descriptors = [StoredSection::default(); 16];
            let mut arena = vec![0; 8192];
            let r = read_from_stream(
                &mut input,
                StreamOptions {
                    requested_features: mask,
                    ..Default::default()
                },
                &mut scratch,
                &mut descriptors,
                &mut arena,
            )
            .unwrap_or_else(|e| panic!("Rust streaming failure {name} mask={mask}: {e}"));
            assert_eq!(r.available_features, expected, "{name} mask={mask}");
        }
    }
}
