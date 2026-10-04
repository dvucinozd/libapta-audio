// SPDX-License-Identifier: Apache-2.0
//! Gates established against public C before implementing a compatibility boundary.
//! These tests intentionally do not assert native allocator/ABI equivalence.
#[test]
#[ignore = "requires APTA_C_ALLOCATION_CONTRACT_ORACLE"]
fn public_c_allocation_failure_layout_and_retained_lifetime_contract() {
    let output =
        std::process::Command::new(std::env::var_os("APTA_C_ALLOCATION_CONTRACT_ORACLE").unwrap())
            .output()
            .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let trace = String::from_utf8(output.stdout).unwrap();
    println!("{trace}");
    assert!(trace.contains("workspace exact_minimum; invalid_allocator before_allocation; retained_release other_thread"));
    let points: usize = trace
        .lines()
        .find_map(|s| s.strip_prefix("failure_points "))
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(points, 49);
    assert!(trace.starts_with("classes 6\n"));
    for point in 1..=49 {
        let expected = if [11, 12, 24].contains(&point) { 3 } else { -2 };
        assert!(trace
            .lines()
            .any(|line| line == format!("failure {point} {expected}")));
    }
}

#[test]
#[ignore = "requires APTA_C_UNKNOWN_SPARSE_ORACLE"]
fn public_c_unknown_sparse_eof_holes_and_retention_contract() {
    for holes in 0..2 {
        let output =
            std::process::Command::new(std::env::var_os("APTA_C_UNKNOWN_SPARSE_ORACLE").unwrap())
                .arg(holes.to_string())
                .output()
                .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let r = libapta::result::parse(&output.stdout, Default::default()).unwrap();
        assert_eq!(r.source.total_frames, Some(320));
        let overview = r.waveform.overview;
        assert_eq!(overview.frames_per_column, 64);
        assert_eq!(overview.column_count(), if holes == 0 { 5 } else { 3 });
        assert_eq!(overview.span_count(), if holes == 0 { 1 } else { 3 });
    }
}
