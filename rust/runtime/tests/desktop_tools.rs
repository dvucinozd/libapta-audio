// SPDX-License-Identifier: Apache-2.0
use std::{
    fs,
    path::Path,
    process::{Command, Output},
};
fn command(args: &[&std::ffi::OsStr]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_apta-native"))
        .args(args)
        .output()
        .unwrap()
}
fn run(args: &[&std::ffi::OsStr]) -> Output {
    let o = command(args);
    assert!(o.status.success(), "{}", String::from_utf8_lossy(&o.stderr));
    o
}
fn wav(path: &Path) {
    let mut data = vec![];
    for i in 0..320000u32 {
        let phase = i % 4000;
        let v = if phase < 64 {
            (64 - phase) as f32 / 64.0 * 0.75
        } else {
            0.0
        };
        data.extend_from_slice(&v.to_le_bytes());
    }
    let mut bytes = b"RIFF".to_vec();
    bytes.extend_from_slice(&(36 + data.len() as u32).to_le_bytes());
    bytes.extend_from_slice(b"WAVEfmt ");
    bytes.extend_from_slice(&16u32.to_le_bytes());
    for n in [3u16, 1] {
        bytes.extend_from_slice(&n.to_le_bytes());
    }
    bytes.extend_from_slice(&8000u32.to_le_bytes());
    bytes.extend_from_slice(&32000u32.to_le_bytes());
    for n in [4u16, 32] {
        bytes.extend_from_slice(&n.to_le_bytes());
    }
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&(data.len() as u32).to_le_bytes());
    bytes.extend_from_slice(&data);
    fs::write(path, bytes).unwrap();
}
#[test]
fn native_desktop_commands_analyze_inspect_validate_and_preserve_outputs() {
    let directory = std::env::temp_dir().join(format!("apta-native-tools-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let input = directory.join("input.wav");
    let output = directory.join("output.apta");
    wav(&input);
    let version = run(&["version".as_ref()]);
    assert!(String::from_utf8_lossy(&version.stdout).contains("native migration"));
    assert!(!command(&["analyze".as_ref()]).status.success());
    run(&[
        "analyze".as_ref(),
        input.as_os_str(),
        output.as_os_str(),
        "--music".as_ref(),
    ]);
    let bytes = fs::read(&output).unwrap();
    let parsed = libapta::result::parse(&bytes, Default::default()).unwrap();
    assert!(
        parsed.tempo.is_some()
            && parsed.global_grid.is_some()
            && parsed.key.is_some()
            && parsed.meter.is_some()
            && parsed.quality.is_some()
    );
    let inspect = run(&["inspect".as_ref(), output.as_os_str()]);
    let text = String::from_utf8_lossy(&inspect.stdout);
    for expected in [
        "320000",
        "TEMP",
        "GGRD",
        "MKEY",
        "MTRD",
        "CONF",
        "1867860160",
    ] {
        assert!(text.contains(expected), "{expected}");
    }
    run(&["validate".as_ref(), output.as_os_str()]);
    run(&[
        "validate".as_ref(),
        output.as_os_str(),
        "--permissive".as_ref(),
    ]);
    assert!(!command(&[
        "analyze".as_ref(),
        input.as_os_str(),
        output.as_os_str(),
        "--music".as_ref()
    ])
    .status
    .success());
    assert_eq!(fs::read(&output).unwrap(), bytes);
    let corrupt = directory.join("corrupt.apta");
    let mut bad = bytes.clone();
    let end = bad.len() - 1;
    bad[end] ^= 1;
    fs::write(&corrupt, bad).unwrap();
    assert!(!command(&["validate".as_ref(), corrupt.as_os_str()])
        .status
        .success());
    assert!(!command(&["inspect".as_ref(), corrupt.as_os_str()])
        .status
        .success());
    let source_dir = directory.join("corpus");
    fs::create_dir(&source_dir).unwrap();
    fs::copy(&input, source_dir.join("one.wav")).unwrap();
    fs::copy(&input, source_dir.join("two.WAV")).unwrap();
    let target = directory.join("results");
    run(&[
        "corpus".as_ref(),
        source_dir.as_os_str(),
        target.as_os_str(),
        "--music".as_ref(),
    ]);
    assert_eq!(fs::read(target.join("one.apta")).unwrap(), bytes);
    assert_eq!(fs::read(target.join("two.apta")).unwrap(), bytes);
    assert!(!command(&[
        "corpus".as_ref(),
        source_dir.as_os_str(),
        target.as_os_str()
    ])
    .status
    .success());
    fs::write(source_dir.join("bad.wav"), b"invalid").unwrap();
    let partial = directory.join("partial");
    assert!(!command(&[
        "corpus".as_ref(),
        source_dir.as_os_str(),
        partial.as_os_str()
    ])
    .status
    .success());
    assert!(partial.join("one.apta").exists());
    assert!(!partial.join("bad.apta").exists());
    fs::remove_dir_all(&directory).unwrap();
}
#[test]
#[ignore = "requires APTA_C_VALIDATOR"]
fn native_cli_exports_pass_strict_public_c_validation() {
    let directory = std::env::temp_dir().join(format!("apta-native-cli-c-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let input = directory.join("input.wav");
    wav(&input);
    for music in [false, true] {
        let output = directory.join(if music { "music.apta" } else { "waveform.apta" });
        let mut args = vec!["analyze".as_ref(), input.as_os_str(), output.as_os_str(), "--source-identity=sha256:000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f".as_ref()];
        if music {
            args.push("--music".as_ref());
        }
        run(&args);
        let validation = Command::new(std::env::var_os("APTA_C_VALIDATOR").unwrap())
            .arg(output)
            .arg("--strict")
            .output()
            .unwrap();
        assert!(
            validation.status.success(),
            "{}",
            String::from_utf8_lossy(&validation.stderr)
        );
    }
    fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires APTA_C_VALIDATOR"]
fn native_all_feature_output_passes_strict_c_reader() {
    let directory =
        std::env::temp_dir().join(format!("apta-native-all-features-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let input = directory.join("input.wav");
    wav(&input);
    let metadata = directory.join("metadata.cbor");
    fs::write(&metadata, b"\xa1\x07\x64test").unwrap();
    let metadata_flag = format!("--metadata-cbor={}", metadata.display());
    for music in [false, true] {
        let output = directory.join(if music { "music.apta" } else { "waveform.apta" });
        let mut args = vec![
            "analyze".as_ref(),
            input.as_os_str(),
            output.as_os_str(),
            "--hash-source".as_ref(),
            metadata_flag.as_ref(),
            "--bands".as_ref(),
            "--detail".as_ref(),
        ];
        if music {
            args.push("--music".as_ref());
        }
        run(&args);
        let data = fs::read(&output).unwrap();
        let parsed = libapta::result::parse(&data, Default::default()).unwrap();
        assert_eq!(parsed.waveform.metadata.unwrap().comments, Some("test"));
        assert_eq!(parsed.available_features & 7, 7);
        assert_eq!(parsed.waveform.tile_count(), 4);
        let check = Command::new(std::env::var_os("APTA_C_VALIDATOR").unwrap())
            .arg(&output)
            .arg("--strict")
            .output()
            .unwrap();
        assert!(
            check.status.success(),
            "{}",
            String::from_utf8_lossy(&check.stderr)
        );
        assert!(!command(&args).status.success());
        assert_eq!(fs::read(&output).unwrap(), data);
    }
    let invalid = directory.join("invalid.apta");
    assert!(!command(&[
        "analyze".as_ref(),
        input.as_os_str(),
        invalid.as_os_str(),
        "--bands".as_ref(),
        "--bands".as_ref()
    ])
    .status
    .success());
    assert!(!invalid.exists());
    fs::remove_dir_all(directory).unwrap();
}

#[test]
fn native_cli_preserves_supplied_identity_and_rejects_ambiguous_flags() {
    let directory =
        std::env::temp_dir().join(format!("apta-native-identity-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let input = directory.join("input.wav");
    wav(&input);
    let hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    for (kind, name) in [(1, "opaque"), (2, "sha256")] {
        let output = directory.join(format!("{name}.apta"));
        let flag = format!("--source-identity={name}:{hex}");
        run(&[
            "analyze".as_ref(),
            input.as_os_str(),
            output.as_os_str(),
            flag.as_ref(),
            "--bands".as_ref(),
        ]);
        let bytes = fs::read(&output).unwrap();
        let result = libapta::result::parse(&bytes, Default::default()).unwrap();
        assert_eq!(result.source.fingerprint_kind, kind);
        assert_eq!(result.source.fingerprint, core::array::from_fn(|i| i as u8));
        let inspect = run(&["inspect".as_ref(), output.as_os_str()]);
        assert!(String::from_utf8_lossy(&inspect.stdout).contains(&format!("{name}:{hex}")));
        assert!(!command(&[
            "verify-source".as_ref(),
            output.as_os_str(),
            input.as_os_str()
        ])
        .status
        .success());
        let invalid = directory.join("invalid.apta");
        assert!(!command(&[
            "analyze".as_ref(),
            input.as_os_str(),
            invalid.as_os_str(),
            flag.as_ref(),
            flag.as_ref()
        ])
        .status
        .success());
        assert!(!invalid.exists());
        let corpus = directory.join("corpus");
        assert!(!command(&[
            "corpus".as_ref(),
            directory.as_os_str(),
            corpus.as_os_str(),
            flag.as_ref()
        ])
        .status
        .success());
        assert!(!corpus.exists());
    }
    for value in ["sha256:ab", "opaque:💥", "unknown:00"] {
        let flag = format!("--source-identity={value}");
        let output = directory.join("bad.apta");
        assert!(!command(&[
            "analyze".as_ref(),
            input.as_os_str(),
            output.as_os_str(),
            flag.as_ref()
        ])
        .status
        .success());
        assert!(!output.exists());
    }
    fs::remove_dir_all(directory).unwrap();
}

#[test]
fn source_hash_covers_exact_object_and_verification_rejects_edits() {
    let directory = std::env::temp_dir().join(format!("apta-native-hash-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let input = directory.join("input.wav");
    let output = directory.join("hashed.apta");
    wav(&input);
    run(&[
        "analyze".as_ref(),
        input.as_os_str(),
        output.as_os_str(),
        "--hash-source".as_ref(),
    ]);
    let bytes = fs::read(&output).unwrap();
    let r = libapta::result::parse(&bytes, Default::default()).unwrap();
    assert_eq!(r.source.fingerprint_kind, 2);
    // Independently computed with Python hashlib from the generated RIFF object.
    let expected = "255ad3c3469184a6321f4f1531e51bd282eac26fb5027bd3f46572a077d41127";
    let hex: String = r
        .source
        .fingerprint
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();
    assert_eq!(hex, expected);
    run(&[
        "verify-source".as_ref(),
        output.as_os_str(),
        input.as_os_str(),
    ]);
    // A benign RIFF JUNK chunk changes identity while leaving decoded PCM intact.
    let mut edited = fs::read(&input).unwrap();
    edited.extend_from_slice(b"JUNK\x04\0\0\0test");
    let size = (edited.len() - 8) as u32;
    edited[4..8].copy_from_slice(&size.to_le_bytes());
    let other = directory.join("edited.wav");
    fs::write(&other, &edited).unwrap();
    assert!(!command(&[
        "verify-source".as_ref(),
        output.as_os_str(),
        other.as_os_str()
    ])
    .status
    .success());
    let other_result = directory.join("edited.apta");
    run(&[
        "analyze".as_ref(),
        other.as_os_str(),
        other_result.as_os_str(),
        "--hash-source".as_ref(),
    ]);
    let other_bytes = fs::read(&other_result).unwrap();
    let other_parsed = libapta::result::parse(&other_bytes, Default::default()).unwrap();
    assert_ne!(other_parsed.source.fingerprint, r.source.fingerprint);
    assert_eq!(
        other_parsed.waveform.overview.column_count(),
        r.waveform.overview.column_count()
    );
    let inspect = run(&["inspect".as_ref(), output.as_os_str()]);
    assert!(String::from_utf8_lossy(&inspect.stdout).contains(expected));
    for flags in [
        vec!["--hash-source", "--hash-source"],
        vec!["--hash-source", "--source-identity=opaque:0000000000000000000000000000000000000000000000000000000000000000"],
        vec!["--source-identity=opaque:0000000000000000000000000000000000000000000000000000000000000000", "--hash-source"],
    ] {
        let invalid = directory.join("invalid.apta");
        let mut args = vec!["analyze".as_ref(), input.as_os_str(), invalid.as_os_str()];
        args.extend(flags.iter().map(std::ffi::OsStr::new));
        assert!(!command(&args).status.success());
        assert!(!invalid.exists());
    }
    let batch = directory.join("batch");
    run(&[
        "corpus".as_ref(),
        directory.as_os_str(),
        batch.as_os_str(),
        "--hash-source".as_ref(),
    ]);
    assert_eq!(fs::read(batch.join("input.apta")).unwrap(), bytes);
    assert_eq!(fs::read(batch.join("edited.apta")).unwrap(), other_bytes);
    fs::remove_dir_all(directory).unwrap();
}

#[test]
fn metadata_rejects_unknown_malformed_and_duplicate_input_before_output() {
    let directory = std::env::temp_dir().join(format!("apta-native-meta-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let input = directory.join("input.wav");
    wav(&input);
    let meta = directory.join("metadata.cbor");
    let flag = format!("--metadata-cbor={}", meta.display());
    let output = directory.join("out.apta");
    for bytes in [
        &b"invalid"[..],
        &b"\xa1\x08\x01"[..],
        &b"\xa1\x07\x78\x04test"[..],
    ] {
        fs::write(&meta, bytes).unwrap();
        assert!(!command(&[
            "analyze".as_ref(),
            input.as_os_str(),
            output.as_os_str(),
            flag.as_ref()
        ])
        .status
        .success());
        assert!(!output.exists());
    }
    fs::write(&meta, b"\xa1\x07\x64test").unwrap();
    assert!(!command(&[
        "analyze".as_ref(),
        input.as_os_str(),
        output.as_os_str(),
        flag.as_ref(),
        flag.as_ref()
    ])
    .status
    .success());
    assert!(!output.exists());
    run(&[
        "analyze".as_ref(),
        input.as_os_str(),
        output.as_os_str(),
        flag.as_ref(),
    ]);
    let bytes = fs::read(&output).unwrap();
    assert_eq!(
        libapta::result::parse(&bytes, Default::default())
            .unwrap()
            .waveform
            .metadata
            .unwrap()
            .comments,
        Some("test")
    );
    fs::remove_dir_all(directory).unwrap();
}
