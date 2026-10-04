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
        let mut args = vec!["analyze".as_ref(), input.as_os_str(), output.as_os_str()];
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
    for music in [false, true] {
        let output = directory.join(if music { "music.apta" } else { "waveform.apta" });
        let mut args = vec![
            "analyze".as_ref(),
            input.as_os_str(),
            output.as_os_str(),
            "--bands".as_ref(),
            "--detail".as_ref(),
        ];
        if music {
            args.push("--music".as_ref());
        }
        run(&args);
        let data = fs::read(&output).unwrap();
        let parsed = libapta::result::parse(&data, Default::default()).unwrap();
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
