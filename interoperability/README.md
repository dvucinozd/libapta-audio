# APTA 1.0 interoperability evidence

This directory contains the Stage S9 P6 independent interchange suite.

## Evidence directions

1. `tests/fixtures/generate_container_v1_suite.py` is the independent producer.
   It uses only Python's standard library and emits the full-feature
   `v1-all-standard-sections` fixture.
2. `libapta_bridge/` is a public installed-package consumer. It parses the
   independent fixture with strict mode, verifies public semantic views and
   canonical byte reproduction, then writes a binary `.apta` file.
3. `independent_consumer.py` is a separately written standard-library parser.
   It consumes that libapta-written file and validates the fixed header,
   directory, CRC32C, padding, section layouts and every expected semantic
   field from `manifest-v1.json`.
4. `espidf_report.py` records the ESP-IDF firmware-build integration and hashes
   the linked firmware and fixture.

Neither the independent producer nor the independent consumer imports, links or
invokes libapta. The bridge intentionally does link the installed `apta::core`
package and uses public headers only.

## Versioned artifacts

- suite: `APTA-INTERCHANGE-1.0`;
- semantic manifest: `manifest-v1.json`;
- full-feature fixture SHA-256:
  `bf83d543e021087a90b90046868d70eecf25c4ef17f9b2633111df9be65e2d09`;
- native reports: `conformance-reports/apta-interchange-<config>.json`;
- ESP-IDF reports: one report and firmware binary per declared toolchain/target.

## Platform claim boundary

Linux, Windows, ILP32 and ESP-IDF are integrations of the same libapta
implementation. They are not independent DSP implementations.

The hosted ESP-IDF jobs compile and link the strict parse probe into firmware,
but do not execute firmware on physical hardware. Reports therefore identify
that evidence as `firmware-build-only`.

No supported native big-endian target is available in release CI. The
independent consumer instead performs a deterministic byte-swap check for every
multi-byte field and records the absence of native big-endian execution as a
declared APTA 1.0 limitation.
