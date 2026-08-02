# APTA reference desktop tools 0.1

**Status:** Reference implementation contract  
**Platform:** POSIX  
**Built-in decoder:** RIFF/WAVE PCM and IEEE float  
**API version:** 0.1.0 draft

## 1. Scope

Stage S5 provides a desktop integration layer around the portable APTA core. It does not move filesystem, decoder, thread or application-scheduling ownership into `apta::core`.

The delivered components are:

- `apta::port_posix` — checked read-only POSIX file access;
- `apta::decoder_wav` — reference WAV decoder and pull-source bridge;
- `apta-analyze` — WAV analysis and `.apta` generation;
- `apta-inspect` — human-readable or JSON result inspection;
- `apta-validate` — normal or strict container validation.

## 2. Building

The default POSIX build enables desktop adapters and tools:

```bash
cmake -S . -B build \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=ON \
  -DAPTA_BUILD_TOOLS=ON
cmake --build build --parallel
```

Relevant CMake options:

```text
APTA_BUILD_DESKTOP_ADAPTERS  Build apta::port_posix and apta::decoder_wav
APTA_BUILD_TOOLS             Build apta-analyze, apta-inspect and apta-validate
```

`APTA_BUILD_TOOLS=ON` requires `APTA_BUILD_DESKTOP_ADAPTERS=ON`.

The resulting executables are normally located at:

```text
build/tools/apta-analyze
build/tools/apta-inspect
build/tools/apta-validate
```

## 3. POSIX file adapter

`apta::port_posix` exposes:

```c
apta_posix_file_open_read();
apta_posix_file_get_size();
apta_posix_file_read_at();
apta_posix_file_close();
```

The adapter provides:

- 64-bit file-size and offset handling;
- checked random-access reads;
- explicit end-of-file behavior;
- no global mutable state;
- no implicit ownership transfer;
- source errors mapped to APTA status values.

It is not linked into `apta::core`.

## 4. Decoder boundary

`apta_decoder_t` is a replaceable desktop decoder interface containing:

- `read_frames`;
- `release_frames`;
- `get_total_frames`;
- `destroy`;
- backend-owned `user_data`.

`apta_decoder_make_pcm_source()` maps that interface to the existing public `apta_pcm_source_t` pull contract.

A third-party decoder may implement this interface for FFmpeg, libsndfile, platform media frameworks or application-owned decoders. Those backends are not part of the current reference distribution.

## 5. Reference WAV decoder

`apta_wav_decoder_open_path()` supports:

| Encoding | Supported |
|---|---:|
| PCM signed 16-bit | Yes |
| PCM signed packed 24-bit little-endian | Yes |
| PCM signed 32-bit | Yes |
| IEEE float 32-bit | Yes |
| Mono | Yes |
| Stereo | Yes |
| Standard `fmt ` chunk | Yes |
| `WAVE_FORMAT_EXTENSIBLE` PCM/float subformat | Yes |
| RF64 | No |
| Big-endian RIFX | No |
| Compressed WAV codecs | No |
| More than two channels | No |

The parser validates:

- RIFF/WAVE identity;
- declared RIFF bounds;
- chunk bounds, padding and overflow;
- singleton `fmt ` and `data` chunks;
- format extension size and canonical subformat GUID;
- sample rate and channel limits;
- bits-per-sample compatibility;
- block alignment and byte rate;
- data length divisibility by frame size.

The decoder permits one borrowed PCM block at a time. A matching `release_frames()` call is required before another successful read.

## 6. `apta-analyze`

### 6.1. Syntax

```text
apta-analyze INPUT.wav --output OUTPUT.apta [options]
```

Options:

```text
--profile waveform
--profile performance
--features waveform,detail,bpm,beatgrid,global,dynamic,all
--help
--version
```

### 6.2. Profiles

`waveform` requests:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
```

`performance` requests:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
APTA_FEATURE_BPM
APTA_FEATURE_LOCAL_BEATGRID
APTA_FEATURE_CONFIDENCE
```

The default profile is `performance`.

Each `--features` token adds the features it depends on, so `beatgrid` implies
`bpm`, and `global` implies `beatgrid`:

| Token | Adds |
|---|---|
| `waveform` | `WAVEFORM_OVERVIEW` |
| `detail` | `+ WAVEFORM_DETAIL` |
| `bpm` | `+ BPM`, `CONFIDENCE` |
| `beatgrid` | `+ LOCAL_BEATGRID` |
| `global` | `+ GLOBAL_BEATGRID` |
| `dynamic` | `+ DYNAMIC_TEMPO` |
| `all` | every feature above |

`global` and `dynamic` were previously unreachable: no token requested them and
`all` stopped at the local grid, so no shipped tool could run S6 over real
audio.

### 6.3. Output behavior

The analyzer:

1. opens the WAV decoder;
2. creates a pull-mode session;
3. attaches deterministic producer/backend metadata;
4. processes cooperatively until final end-of-input;
5. serializes a canonical `.apta` result;
6. writes a temporary file in the destination directory;
7. flushes and synchronizes the temporary file;
8. atomically renames it to the requested output path.

A failed analysis or write does not leave a partially written final output path created by the current invocation.

The default maximum serialized output is 1 GiB.

### 6.4. Examples

```bash
build/tools/apta-analyze track.wav --output track.apta
build/tools/apta-analyze track.wav --output track.apta --profile waveform
build/tools/apta-analyze track.wav --output track.apta \
  --features waveform,bpm,beatgrid
```

The built-in executable accepts WAV input. FLAC, MP3, AAC and other codecs require another decoder backend or an application-side decode-to-PCM path.

## 7. `apta-inspect`

### 7.1. Syntax

```text
apta-inspect INPUT.apta [--json] [--section SECTION]
```

Recognized section filters:

```text
WOVR
WDTL
META
TEMP
LGRD
```

### 7.2. Human-readable mode

Default output includes:

- file and container version;
- specification version;
- source frames, sample rate and channels;
- result generation and session state;
- available feature mask;
- overview geometry and coverage;
- metadata fields;
- selected tempo and candidate count;
- local-grid segment and period summary.

### 7.3. JSON mode

`--json` emits one deterministic JSON object to standard output. Text fields are escaped for JSON control characters, quotation marks and backslashes.

Examples:

```bash
build/tools/apta-inspect track.apta
build/tools/apta-inspect track.apta --json
build/tools/apta-inspect track.apta --json --section TEMP
build/tools/apta-inspect track.apta --section LGRD
```

Inspection first parses the complete input through the hardened library parser. Header fields are displayed only after that parse succeeds.

## 8. `apta-validate`

### 8.1. Syntax

```text
apta-validate INPUT.apta [--strict] [--quiet]
```

`--strict` enables `APTA_PARSE_STRICT`.

`--quiet` suppresses successful output and validation diagnostics intended for interactive use. The process exit status remains authoritative.

### 8.2. Exit behavior

```text
0  valid container
1  input, allocation, parser or validation failure
2  command-line usage error
```

Examples:

```bash
build/tools/apta-validate track.apta
build/tools/apta-validate track.apta --strict
build/tools/apta-validate track.apta --strict --quiet
```

The default maximum input size for inspection and validation is 256 MiB.

## 9. Generated integration fixtures

The test suite generates a deterministic mono PCM16 WAV click track at runtime. No third-party or copyrighted audio recording is required or committed.

The end-to-end test performs:

```text
generated WAV
  → apta-analyze
  → canonical .apta
  → apta-validate --strict
  → apta-inspect --json --section TEMP
  → one-byte truncation
  → strict rejection
```

The same workflow runs in ordinary GCC CI and in the Clang AddressSanitizer/UndefinedBehaviorSanitizer job.

## 10. Non-goals and limitations

Stage S5 does not claim:

- built-in FLAC, MP3, AAC or streaming decoder support;
- Windows file adapter support;
- RF64 support;
- recursive directory/batch analysis;
- stable command-line output compatibility;
- stable APTA API or ABI;
- package-manager installation;
- Stage S6 global-grid/revision analysis or `GGRD`/`REVN` inspection through
  the S5 command-line interfaces. Those features exist in the library API and
  container layer but are not selectable/displayed by the current tools.
