// SPDX-License-Identifier: Apache-2.0
//! Native desktop interfaces. Names/options are additive and do not replace the
//! published C tools or the frozen privacy/qualification corpus workflow.
#![forbid(unsafe_code)]
use libapta::{container, result, session::*, wav::Wav, waveform::PcmView, NativeLimits};
use libapta_runtime::{ContextLimits, GrowingLimits, RuntimeContext};
use sha2::{Digest, Sha256};
use std::{
    error::Error,
    ffi::OsString,
    fs::{self, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};
const MAX_INPUT: u64 = 256 * 1024 * 1024;
const USAGE: &str = "usage: apta-native analyze INPUT.wav OUTPUT.apta [--music] [--bands] [--detail] [--source-identity=KIND:HEX | --hash-source] [--metadata-cbor=FILE]\n       apta-native inspect INPUT.apta [--json]\n       apta-native verify-source INPUT.apta SOURCE_OBJECT\n       apta-native validate INPUT.apta [--permissive]\n       apta-native corpus INPUT_DIRECTORY OUTPUT_DIRECTORY [--music] [--bands] [--detail] [--hash-source]\n       apta-native version\nIdentity KIND is opaque or sha256; HEX is a host-supplied 64-digit hexadecimal identity.\n--hash-source computes SHA-256 of every input byte, including WAV headers.\nOutput files/directories must not exist. Corpus is local WAV batch conversion, not frozen qualification.";
fn read(path: &Path) -> Result<Vec<u8>, Box<dyn Error>> {
    let mut input = Vec::new();
    fs::File::open(path)?
        .take(MAX_INPUT + 1)
        .read_to_end(&mut input)?;
    if input.len() as u64 > MAX_INPUT {
        return Err("input exceeds 256 MiB limit".into());
    }
    Ok(input)
}
#[derive(Clone, Default)]
struct Features {
    music: bool,
    bands: bool,
    detail: bool,
    identity: Option<SourceIdentity>,
    hash_source: bool,
    metadata: Option<PathBuf>,
}
fn features(flags: &[OsString], allow_identity: bool) -> Result<Features, Box<dyn Error>> {
    let mut out = Features::default();
    for flag in flags {
        if let Some(value) = flag
            .to_str()
            .and_then(|s| s.strip_prefix("--metadata-cbor="))
        {
            if !allow_identity || out.metadata.is_some() || value.is_empty() {
                return Err(USAGE.into());
            }
            out.metadata = Some(PathBuf::from(value));
            continue;
        }
        if let Some(value) = flag
            .to_str()
            .and_then(|s| s.strip_prefix("--source-identity="))
        {
            if !allow_identity || out.identity.is_some() || out.hash_source {
                return Err(USAGE.into());
            }
            let (kind, hex) = value.split_once(':').ok_or(USAGE)?;
            let kind = match kind {
                "opaque" => 1,
                "sha256" => 2,
                _ => return Err(USAGE.into()),
            };
            if hex.len() != 64 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
                return Err("identity needs exactly 64 hexadecimal digits".into());
            }
            let mut bytes = [0; 32];
            for (index, byte) in bytes.iter_mut().enumerate() {
                *byte = u8::from_str_radix(&hex[index * 2..index * 2 + 2], 16)?;
            }
            out.identity = Some(SourceIdentity::new(kind, bytes)?);
            continue;
        }
        let selected = match flag.to_str() {
            Some("--hash-source") if out.identity.is_none() => &mut out.hash_source,
            Some("--music") => &mut out.music,
            Some("--bands") => &mut out.bands,
            Some("--detail") => &mut out.detail,
            _ => return Err(USAGE.into()),
        };
        if *selected {
            return Err(USAGE.into());
        }
        *selected = true;
    }
    Ok(out)
}
fn analyze(input: &Path, output: &Path, features: &Features) -> Result<(), Box<dyn Error>> {
    let metadata_bytes = if let Some(path) = &features.metadata {
        let mut bytes = Vec::new();
        fs::File::open(path)?.take(8193).read_to_end(&mut bytes)?;
        if bytes.len() > 8192 {
            return Err("metadata exceeds 8192 bytes".into());
        }
        Some(bytes)
    } else {
        None
    };
    let metadata = metadata_bytes
        .as_deref()
        .map(libapta::meta::parse)
        .transpose()?;
    if let Some(metadata) = &metadata {
        let mut canonical = vec![0; libapta::meta::serialized_size(metadata)?];
        libapta::meta::write(metadata, &mut canonical)?;
        if Some(canonical.as_slice()) != metadata_bytes.as_deref() {
            return Err(
                "metadata must use canonical supported fields; unknown fields would be lost".into(),
            );
        }
    }
    let bytes = read(input)?;
    let wav = Wav::parse(&bytes)?;
    if wav.frame_count() == 0 {
        return Err("empty WAV has no serializable overview".into());
    }
    let source = wav.source();
    let context = RuntimeContext::new(ContextLimits::default());
    let mut s = context.create_session_with_identity(
        SessionConfig {
            sample_rate: source.sample_rate,
            channel_count: source.channel_count,
            total_frames: wav.frame_count(),
            frames_per_column: 32768,
        },
        GrowingLimits::default(),
        if features.hash_source {
            SourceIdentity::new(2, Sha256::digest(&bytes).into())?
        } else {
            features.identity.unwrap_or_default()
        },
    )?;
    if features.bands {
        s.enable_three_band()?;
    }
    if features.detail {
        s.enable_detail()?;
    }
    if features.music {
        s.enable_default_music()?;
    }
    let mut scratch = vec![0.0; 4096 * usize::from(source.channel_count)];
    let token = CancellationToken::new();
    let mut first = 0;
    while first < wav.frame_count() {
        let frames = wav.read_frames(first, &mut scratch)?;
        let accepted = s.push_pcm(PcmView::F32Interleaved(
            &scratch[..frames * usize::from(source.channel_count)],
        ))?;
        if accepted == 0 {
            return Err("unexpected PCM backpressure".into());
        }
        first += accepted as u64;
        s.process(WorkBudget::default(), &token)?;
    }
    s.finish_input()?;
    while s.session().state() != SessionState::Complete {
        s.process(WorkBudget::default(), &token)?;
    }
    let data = {
        let snapshot = s.session().snapshot(1)?;
        let mut tiles = [libapta::WaveformTile {
            level_id: 1,
            tile_index: 0,
            first_frame: 0,
            end_frame: 0,
            first_column_index: 0,
            state: libapta::FeatureState::Partial,
            confidence: 0,
            columns: &[],
        }; libapta::detail_analysis::TILE_COUNT];
        let mut wire =
            result::from_session_snapshot(&snapshot, &mut tiles, NativeLimits::default())?;
        wire.metadata = metadata;
        let size = result::serialized_size(&wire)?;
        let mut data = Vec::new();
        data.try_reserve_exact(size)?;
        data.resize(size, 0);
        result::write(&wire, &mut data, result::Limits::default())?;
        result::parse(&data, result::Limits::default())?;
        data
    };
    drop(s);
    context.close()?;
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(output)?;
    file.write_all(&data)?;
    file.flush()?;
    println!("{} frames, {} bytes", wav.frame_count(), data.len());
    Ok(())
}
/// Versioned numeric summary; exact frame coordinates stay JSON integers.
fn inspect_json(path: &Path) -> Result<(), Box<dyn Error>> {
    let data = read(path)?;
    let r = result::parse(&data, result::Limits::default())?;
    let c = container::Container::parse(&data, container::ParseOptions::default())?;
    let total = r
        .source
        .total_frames
        .map_or_else(|| "null".to_owned(), |n| n.to_string());
    let fingerprint: String = r
        .source
        .fingerprint
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();
    print!("{{\"schema_version\":1,\"source\":{{\"sample_rate\":{},\"channel_count\":{},\"total_frames\":{},\"fingerprint_kind\":{},\"fingerprint_hex\":\"{}\"}},\"available_features\":{},\"partial\":{},\"sections\":[",
        r.source.sample_rate, r.source.channel_count, total, r.source.fingerprint_kind, fingerprint, r.available_features, c.flags & 1 != 0);
    for i in 0..c.section_count() {
        let section = c.section(i).unwrap();
        if i != 0 {
            print!(",");
        }
        // Numeric bytes avoid treating an untrusted FourCC as JSON text.
        print!(
            "{{\"fourcc_bytes\":{:?},\"version\":{},\"payload_bytes\":{}}}",
            section.fourcc,
            section.version,
            section.payload.len()
        );
    }
    let overview = r.waveform.overview;
    print!("],\"overview\":{{\"frames_per_column\":{},\"spans\":{},\"columns\":{}}},\"detail_tiles\":{},\"tempo_millibpm\":",
        overview.frames_per_column, overview.span_count(), overview.column_count(), r.waveform.tile_count());
    if let Some(t) = r.tempo {
        print!("{}", t.selected().tempo_millibpm);
    } else {
        print!("null");
    }
    print!(",\"key\":");
    if let Some(k) = r.key {
        let mut candidates = [libapta::KeyCandidate::default(); 24];
        let key = k.copy_into(&mut candidates)?;
        print!("{{\"tonic\":{},\"mode\":{},\"confidence\":{},\"state\":{},\"first_frame\":{},\"end_frame\":{},\"candidates\":[", key.tonic, key.mode, key.confidence, key.state as u8, key.first_frame, key.end_frame);
        for (i, candidate) in key.candidates.iter().enumerate() {
            if i != 0 {
                print!(",");
            }
            print!(
                "{{\"tonic\":{},\"mode\":{},\"score\":{},\"confidence\":{}}}",
                candidate.tonic, candidate.mode, candidate.score, candidate.confidence
            );
        }
        print!("]}}");
    } else {
        print!("null");
    }
    println!("}}");
    Ok(())
}
fn inspect(path: &Path) -> Result<(), Box<dyn Error>> {
    let data = read(path)?;
    let r = result::parse(&data, result::Limits::default())?;
    let c = container::Container::parse(&data, container::ParseOptions::default())?;
    println!(
        "source: {} Hz, {} channels, {:?} frames",
        r.source.sample_rate, r.source.channel_count, r.source.total_frames
    );
    if r.source.fingerprint_kind != 0 {
        print!(
            "source identity: {}:",
            if r.source.fingerprint_kind == 1 {
                "opaque"
            } else {
                "sha256"
            }
        );
        for byte in r.source.fingerprint {
            print!("{byte:02x}");
        }
        println!();
    }
    println!(
        "features: {:#x}, partial: {}",
        r.available_features,
        c.flags & 1 != 0
    );
    for i in 0..c.section_count() {
        let section = c.section(i).unwrap();
        println!(
            "section: {}, version {}, {} bytes",
            String::from_utf8_lossy(&section.fourcc),
            section.version,
            section.payload.len()
        );
    }
    let overview = r.waveform.overview;
    println!(
        "overview: {} frames/column, {} spans, {} columns",
        overview.frames_per_column,
        overview.span_count(),
        overview.column_count()
    );
    println!("detail: {} tiles", r.waveform.tile_count());
    if let Some(t) = r.tempo {
        println!("tempo: {:?}", t.selected());
        for i in 0..t.candidate_count() {
            println!("tempo candidate: {:?}", t.candidate(i).unwrap());
        }
    }
    if let Some(g) = r.local_grid {
        println!("local grid: {:?}", g);
    }
    if let Some(g) = r.global_grid {
        println!(
            "global grid: {:?}, flags {:#x}, requested {:?}, evidence {:?}, applicability {:?}",
            g.state(),
            g.flags(),
            g.requested_range(),
            g.evidence_range(),
            g.applicability_range()
        );
        for i in 0..g.segment_count() {
            println!("grid segment: {:?}", g.segment(i).unwrap());
        }
        println!("global beats: {}", g.beat_count());
    }
    if let Some(revision) = r.revision {
        println!("revision: {revision:?}");
    }
    if let Some(k) = r.key {
        let mut candidates = vec![libapta::KeyCandidate::default(); k.candidate_count()];
        let key = k.copy_into(&mut candidates)?;
        println!("key: {key:?}");
    }
    if let Some(m) = r.meter {
        for i in 0..m.segment_count() {
            println!("meter segment: {:?}", m.segment(i).unwrap());
        }
    }
    if let Some(q) = r.quality {
        for i in 0..q.record_count() {
            println!("quality: {:?}", q.record(i).unwrap());
        }
    }
    if let Some(meta) = r.waveform.metadata {
        println!("metadata: {meta:?}");
    }
    Ok(())
}
/// Compare the exact source object, without decoding or normalizing its PCM.
fn verify_source(result_path: &Path, source_path: &Path) -> Result<(), Box<dyn Error>> {
    let bytes = read(result_path)?;
    let r = result::parse(&bytes, result::Limits::default())?;
    if r.source.fingerprint_kind != 2 {
        return Err("result does not carry a SHA-256 source-object identity".into());
    }
    let actual: [u8; 32] = Sha256::digest(read(source_path)?).into();
    if actual != r.source.fingerprint {
        return Err("source-object SHA-256 mismatch".into());
    }
    println!("verified SHA-256 source-object identity");
    Ok(())
}
fn corpus(input: &Path, output: &Path, features: Features) -> Result<(), Box<dyn Error>> {
    let mut files: Vec<PathBuf> = fs::read_dir(input)?
        .map(|e| e.map(|e| e.path()))
        .collect::<Result<_, _>>()?;
    files.retain(|p| p.is_file() && p.extension().is_some_and(|e| e.eq_ignore_ascii_case("wav")));
    files.sort();
    if files.is_empty() {
        return Err("input directory contains no WAV files".into());
    }
    fs::create_dir(output)?;
    let mut failures = 0;
    for path in &files {
        let name = path.file_name().ok_or("missing WAV filename")?;
        let target = output.join(name).with_extension("apta");
        if let Err(e) = analyze(path, &target, &features) {
            eprintln!("{}: {e}", path.display());
            failures += 1;
        }
    }
    println!("{} converted, {failures} failed", files.len() - failures);
    if failures != 0 {
        return Err("corpus conversion incomplete; successful outputs retained".into());
    }
    Ok(())
}
fn run(args: &[OsString]) -> Result<(), Box<dyn Error>> {
    match args {
        [command] if command == "version" || command == "--version" => {
            println!(
                "apta-native {} (container 1; native migration)",
                env!("CARGO_PKG_VERSION")
            );
            Ok(())
        }
        [command] if command == "--help" || command == "help" => {
            println!("{USAGE}");
            Ok(())
        }
        [command, input, source] if command == "verify-source" => {
            verify_source(Path::new(input), Path::new(source))
        }
        [command, input, flag] if command == "inspect" && flag == "--json" => {
            inspect_json(Path::new(input))
        }
        [command, input] if command == "inspect" => inspect(Path::new(input)),
        [command, input] if command == "validate" => {
            result::parse(&read(Path::new(input))?, result::Limits::default())?;
            println!("valid (strict)");
            Ok(())
        }
        [command, input, flag] if command == "validate" && flag == "--permissive" => {
            let mut limits = result::Limits::default();
            limits.container.strict = false;
            result::parse(&read(Path::new(input))?, limits)?;
            println!("valid (permissive)");
            Ok(())
        }
        [command, input, output, flags @ ..] if command == "analyze" => {
            analyze(Path::new(input), Path::new(output), &features(flags, true)?)
        }
        [command, input, output, flags @ ..] if command == "corpus" => {
            corpus(Path::new(input), Path::new(output), features(flags, false)?)
        }
        _ => Err(USAGE.into()),
    }
}
fn main() {
    if let Err(e) = run(&std::env::args_os().skip(1).collect::<Vec<_>>()) {
        eprintln!("apta-native: {e}");
        std::process::exit(1);
    }
}
