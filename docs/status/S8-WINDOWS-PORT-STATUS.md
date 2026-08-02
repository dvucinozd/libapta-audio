# Stage S8 — Windows platform integration status

**Status:** complete self-tested and CI-verified implementation candidate
**Source baseline:** `d21572a`
**Selected platform:** native Windows desktop/runtime

## 1. Scope and independence

Stage S8 selects Windows as the second platform integration after ESP-IDF. The
Windows port consumes the public APTA API and `.apta` data model and has no
dependency on ESP-IDF headers, allocation policy, clock binding or DSP helper.

The delivered integration consists of:

- `apta::port_windows`, also exposed to shared desktop code as
  `apta::port_native`;
- UTF-8 to UTF-16 path conversion with invalid UTF-8 rejection;
- checked 64-bit read-only Win32 file access;
- sibling-temporary flushed writes and replacement through Win32 APIs;
- the existing replaceable WAV decoder compiled over the native adapter;
- `apta-analyze`, `apta-inspect` and `apta-validate` Windows executables;
- native adapter, decoder, pull-analysis, independent-fixture and generated CLI
  runtime tests;
- a Windows CI job that now enables adapters and tools with warnings as errors.

The core still does not own filesystems or codecs. The Windows code is confined
to `ports/windows`; shared desktop sources depend only on
`apta/desktop/apta_file.h`.

## 2. File contract

The platform-neutral desktop file API provides:

```c
apta_file_open_read();
apta_file_get_size();
apta_file_read_at();
apta_file_close();
apta_file_write_atomic();
```

Paths are UTF-8 on every platform. Windows converts them with
`MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)` and uses wide Win32
file APIs. File sizes and offsets are 64-bit. Atomic output is written to an
exclusively created sibling file, flushed, closed and moved over the destination
with replacement and write-through flags. Failed writes remove the temporary
file and leave no partially written destination.

The older `apta_posix_file_*` entry points remain source- and ABI-compatible on
POSIX and delegate to the same native contract.

## 3. Runtime and interchange evidence

Validation at the source baseline:

| Configuration | Result |
|---|---:|
| Linux GCC release, warnings as errors | 85/85 CTest tests |
| Linux core-only release, warnings as errors | 72/72 CTest tests |
| Linux Clang ASan + UBSan debug | 85/85 CTest tests |
| Windows x64 MinGW cross-build, warnings as errors | Complete core, adapter, decoder, tools and test inventory built |
| Native Windows adapter/decoder/pull tests | Passed |
| Native Windows generated CLI flow | WAV analyzed; strict validation and TEMP JSON inspection passed |
| Independent Python fixture consumed on Windows | Passed strict validation |
| Linux-produced `.apta` consumed on Windows | Passed strict validation and TEMP inspection |
| Windows-produced `.apta` consumed on Linux | Passed strict validation |
| GitHub Windows/MSVC release, warnings as errors | 84/84 CTest tests; run `30765409478` |
| GitHub ESP-IDF supported-build matrix | All three configurations passed; run `30765409488` |

The generated click-track result was identical at the semantic boundary on
both platforms: 125,000 millibpm, final state and confidence 98.

The committed `reference-wovr-meta.apta.hex` is produced by a standalone Python
implementation. `apta.interchange.external_fixture` now consumes and
byte-reproduces it in the regular CTest inventory, including the Windows CI job.

## 4. Acceptance boundary

The implementation satisfies the Stage S8 functional scope and completion
gate:

- a platform adapter exists and is independent of ESP-IDF;
- the platform executes the public pull-analysis API over real file I/O;
- it generates, validates and inspects canonical `.apta` output;
- both directions of Linux/Windows interchange have passed;
- an independently produced fixture is accepted and reproduced;
- malformed WAV and corrupt `.apta` paths remain covered;
- the updated GitHub Windows/MSVC job builds the adapters and tools and passes
  the entire 84-test Windows inventory with warnings as errors.

This is a platform integration claim, not an independent DSP implementation,
formal Windows certification or APTA 1.0 conformance claim. Long-path policy,
network-share replacement semantics, ARM64 Windows and application-specific
decoder integrations remain outside this stage.
