# Stage S9 P6 — independent interchange and platform evidence

**Status:** implementation candidate; final status is established only by the
PR validation runs named below after merge.

## Scope

P6 adds bidirectional container-version-1 evidence without changing the public
API, ABI, DSP implementation or wire format.

### Independent producer → libapta

The existing standard-library Python producer emits a canonical 1032-byte
fixture containing:

```text
WOVR WDTL META TEMP LGRD GGRD REVN
```

The installed-package bridge:

- links only `apta::core`;
- parses the fixture in strict mode;
- asserts public source, waveform, metadata, tempo, grid and revision values;
- writes canonical output;
- requires byte identity with the independent fixture.

### libapta output → independent consumer

The Python independent consumer:

- uses only the standard library;
- validates the 96-byte header and all 40-byte directory entries;
- validates bounds, canonical alignment and zero padding;
- recomputes header and section CRC32C;
- decodes every version-1 section independently;
- compares each field with `interoperability/manifest-v1.json`;
- emits precise field paths on failure;
- performs deterministic byte-swap evidence for every multi-byte field.

### Platform evidence

The same installed-package exchange test runs through CTest in:

- native Linux static and shared builds;
- Windows static and shared builds;
- the ILP32 build.

The ESP-IDF matrix compiles and links a strict full-feature fixture probe into:

- ESP-IDF 5.5.4 / ESP32 / scalar;
- ESP-IDF 6.0.2 / ESP32 / scalar;
- ESP-IDF 6.0.2 / ESP32-S3 / ESP-DSP.

Each embedded job retains the firmware binary and a deterministic report with
firmware, fixture and source hashes. Hosted CI does not execute the firmware on
physical hardware, so that evidence is explicitly `firmware-build-only`.

## Claim boundary

POSIX, Windows and ESP-IDF are independent platform integrations of libapta,
not independent DSP implementations.

No supported native big-endian release target exists. P6 retains deterministic
byte-swap evidence and records native big-endian execution as an APTA 1.0
limitation rather than overclaiming coverage.

## Exit evidence

The P6 PR may be marked complete only when:

- the installed bridge and independent consumer pass on Linux, Windows and
  ILP32;
- static and shared reports are retained by CI;
- all three ESP-IDF firmware builds pass and upload reports plus firmware;
- fixture, semantic manifest, producer source and output hashes are present;
- no production source, public API, ABI or normative wire layout changed.
