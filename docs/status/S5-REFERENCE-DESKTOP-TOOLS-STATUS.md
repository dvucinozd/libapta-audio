# Stage S5 reference desktop tools status

**Stage status:** Functionally complete implementation candidate  
**Architecture source:** [`../architecture/APTA-ARCHITECTURE-DRAFT.md`](../architecture/APTA-ARCHITECTURE-DRAFT.md)  
**Adapter merge:** `104ef5824c3f3e85b8e0b75a5fc8952a32b99dae`  
**CLI merge:** `b971be282dea0b7fb168e938d39356152537f4b1`  
**Latest full verification:** GitHub Actions PR CI run `#246`  
**Registered runtime tests:** 63

## 1. Stage requirements

| S5 roadmap item | Status | Implementation evidence |
|---|---|---|
| POSIX source adapter | Complete | `apta::port_posix`, checked 64-bit read-only random access |
| Decoder integration | Complete | replaceable `apta_decoder_t` and `apta_pcm_source_t` bridge |
| Reference decoder | Complete for declared scope | hardened WAV PCM/float mono/stereo backend |
| `apta-analyze` | Complete | pull-mode analysis and atomic canonical `.apta` output |
| `apta-inspect` | Complete | human/JSON inspection with section filtering |
| `apta-validate` | Complete | normal/strict/quiet validation and process exit contract |
| Redistributable integration fixtures | Complete | deterministic WAV generated from Apache-2.0 test source |

## 2. Architecture boundary

The portable core remains independent of desktop ownership:

```text
application / CLI
      │
      ├── apta::port_posix
      ├── apta::decoder_wav
      │       │
      │       └── apta_pcm_source_t
      │
      └── apta::core
```

`apta::core` does not depend on:

- POSIX file APIs;
- WAV parsing;
- command-line argument handling;
- process exit behavior;
- output filenames;
- application threads;
- decoder libraries.

The decoder adapter links to the core public API for initializers and source bridging. The dependency is not reversed.

## 3. POSIX adapter evidence

Implemented and tested behavior:

- read-only open;
- exact 64-bit file size;
- checked offset and size arithmetic;
- bounded random-access reads;
- short read only at valid end-of-file;
- explicit missing-file/source errors;
- no process-global file state.

## 4. WAV decoder evidence

Supported input variants:

1. signed PCM16 mono;
2. signed packed PCM24 stereo;
3. signed PCM32 mono;
4. IEEE float32 stereo;
5. `WAVE_FORMAT_EXTENSIBLE` PCM16 stereo.

Malformed tests cover:

- non-WAV input;
- unsupported big-endian RIFX;
- RIFF size beyond the file;
- invalid block alignment;
- data chunk beyond RIFF bounds;
- unsupported sample representation;
- invalid extensible subformat structure.

The end-to-end decoder test proves random-access ownership and a full WAV → pull-mode S4 → TEMP/LGRD `.apta` round trip.

## 5. CLI behavior evidence

### `apta-analyze`

Verified behavior:

- performance-profile analysis of a generated 125 BPM WAV;
- final overview, tempo and local-grid output;
- producer/backend metadata;
- canonical serialization;
- bounded serialized-size query;
- temporary sibling output;
- flush, file synchronization and atomic rename;
- no final partial file on the handled failure paths.

### `apta-inspect`

Verified behavior:

- hardened parse before display;
- deterministic JSON TEMP output;
- human-readable summary implementation;
- filters for `WOVR`, `WDTL`, `META`, `TEMP` and `LGRD`.

### `apta-validate`

Verified behavior:

- strict validation accepts analyzer output;
- a one-byte-truncated output is rejected;
- quiet mode preserves automation-oriented exit status.

## 6. CI evidence

PR CI run `#246` completed successfully with:

- GCC library, adapter and tool build;
- Clang library, adapter and tool build;
- 63 registered runtime tests;
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- canonical `.apta` seed generation;
- bounded libFuzzer smoke.

The six CLI process tests are fixture ordered:

```text
apta.tools.fixture_wav
apta.tools.analyze
apta.tools.validate_strict
apta.tools.inspect_json
apta.tools.fixture_corrupt
apta.tools.reject_corrupt
```

## 7. Generated audio fixture

The CLI integration WAV is synthesized at test runtime:

- mono;
- PCM16;
- 48 kHz;
- deterministic 125 BPM click pattern;
- 384000 source frames.

No external recording or third-party audio asset is stored in the repository.

## 8. Completion boundary

“Stage S5 complete” means the roadmap's reference desktop adapter, decoder integration, three command-line tools and generated integration fixture exist and have self-tested evidence.

It does not mean:

- built-in FLAC/MP3/AAC decoding;
- Windows desktop support;
- stable CLI output format;
- package installation or signed release artifacts;
- formal interoperability certification;
- stable APTA 1.0 API/ABI;
- global beatgrid or dynamic-tempo support.

Those broader items remain external integrations or later roadmap stages.

## 9. Next stage

The next architecture stage is:

```text
Stage S6 — Global grid and dynamic tempo
```

S6 includes global refinement, multiple tempo/grid segments, explicit beat representation and revision behavior. None of those features are implied by the completed local-grid S4/S5 tooling.
