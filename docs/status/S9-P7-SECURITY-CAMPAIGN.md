# Stage S9 P7 — security and fuzz release campaign

**Status:** validated release candidate. CI #419 and its retained P7 artifact
passed and were inspected for PR #67. The phase becomes complete when the PR
is merged.

## Scope

P7 proves the frozen APTA 1.0 parser, serializer and public state-machine
surface under sanitizers. It does not add analysis features and does not alter
the public API, ABI or container wire format.

## Frozen target inventory

`tests/fuzz/campaign-v1.json` is the authoritative P7 inventory:

| Target | Required surface |
|---|---|
| `container-parser` | complete strict and permissive container parser |
| `header-directory` | fixed header, directory, offsets, sizes and CRC routing |
| `meta-reader` | META reader and public metadata view |
| `wovr-wdtl-readers` | WOVR/WDTL readers and waveform views |
| `temp-lgrd-readers` | TEMP/LGRD readers and tempo/local-grid views |
| `ggrd-revn-readers` | GGRD/REVN readers and revision view |
| `serializer-roundtrip` | parse, canonical serialize and strict reparse |
| `pcm-validation` | PCM formats, block/range validation and bounded process |
| `state-transitions` | focus, requests, cancellation, EOI and revision calls |

The campaign harness consumes only installed public APTA headers and the
`libapta` archive. It does not call private parser entry points.

## Approved reproducible threshold

The P7 policy is approved by merging this phase; individual CI runs may not
silently reduce it.

- engine: Clang libFuzzer;
- sanitizers: AddressSanitizer and UndefinedBehaviorSanitizer;
- minimum executions: **8,192 per target**;
- minimum aggregate executions: **73,728**;
- per-input timeout: **5 seconds**;
- RSS limit: **512 MiB**;
- single-allocation limit: **128 MiB**;
- target-specific maximum input lengths: 2 KiB through 64 KiB;
- required result: zero crashes, hangs, sanitizer findings and retained crash
  artifacts.

Complete command lines, compiler version, binary hashes, corpus hashes,
execution counts and source revision are written to
`fuzz-campaign.json`.

## Versioned corpora

`tests/fuzz/corpus-v1.json` freezes the corpus inputs:

- canonical final and partial WOVR containers;
- WDTL and META containers;
- TEMP/LGRD and GGRD/REVN containers;
- the independent full-feature seven-section fixture;
- deterministic PCM and state-transition byte sequences.

Corpora are deduplicated by SHA-256 when materialized. Any future discovered
crash input must be minimized with libFuzzer and retained as a regression
input. A zero-finding campaign does not fabricate a crash corpus.

## Security review

`security/review/invariants-v1.json` freezes the reviewed source set and
critical invariants.

### Container arithmetic and allocations

The review covers fixed-header and directory arithmetic, offset-plus-size
bounds, CRC dispatch, per-section count limits, logical allocation budgets and
canonical serializer sizing. Dynamic GGRD/REVN allocations are admitted only
through `apta_internal_result_allocation_fits()` and parser-configured limits.

### PCM, request and revision state

The campaign varies sample format, channel count, data/plane combinations,
frame ranges, discontinuities, EOI, work budgets, focus, region requests,
request cancellation, result acquisition and revision calls. Every operation
is bounded by a small fixed harness buffer and operation count.

### Atomic file replacement

POSIX uses a same-directory `mkstemp`, flushes and `fsync`s the file, closes it,
then renames it; all failure paths unlink the temporary file. The parent
directory is not `fsync`ed after rename. P7 accepts this as a documented
power-loss durability limitation, not a parser memory-safety defect.

Windows rejects invalid UTF-8 with `MB_ERR_INVALID_CHARS`, checks `INT_MAX` and
allocation multiplication bounds, creates a unique temporary file with
`CREATE_NEW`, flushes it, replaces with `MOVEFILE_WRITE_THROUGH` and deletes
the temporary path on failure.

### Security policy

`SECURITY.md` names the current development line, the active 1.0 release
candidate and the upcoming maintained 1.x line, while retaining the private
reporting contact and coordinated-disclosure process.

## Validated campaign evidence

CI **#419** tested PR head
`58359cac7ad2523fd1f85dde0a4ba14dbd2bc4a9` through GitHub's pull-request
merge revision `d220022d244cf99eddea5b93709b088d5480f5a4`.

All four jobs passed:

- Linux static/shared build, API, ABI, conformance and interchange;
- Windows static/shared build, API, ABI, conformance and interchange;
- ILP32 build and tests;
- P7 sanitized runtime, invariant review and release campaign.

The P7 campaign used Ubuntu Clang 18.1.3 and produced:

- **9/9** passing targets;
- **8,192** executions for every target;
- **73,728** aggregate executions;
- zero ASan or UBSan markers;
- zero timeout, crash or OOM artifacts;
- zero unresolved findings;
- **17/17** reviewed security-invariant files passing;
- seven versioned initial seeds for every parser target;
- three versioned initial seeds for each PCM/state target.

Retained artifact:

- name: `apta-security-campaign-attempt-1`;
- artifact id: `8883539335`;
- ZIP SHA-256:
  `d9657d60efc0dd04764bf26174705e9557b01f7e8637c1c9bc2b412095e0a4c8`;
- `fuzz-campaign.json` SHA-256:
  `50df72429d5f8d9b7f8eea7cd42d0192a830fd8ad9933b75dbde3aab8712a5c9`;
- `security-invariants.json` SHA-256:
  `33a6ddbe73eb5f75ccb351be0af3ff447f4e358ea563ca86c7c6f399537d3a17`;
- `corpus-report.json` SHA-256:
  `7d66ad444791d6dfaf83c0ac976498114d512b7fb131681c4be400e091fa1351`.

The campaign policy SHA-256 recorded independently in both the campaign and
corpus reports is
`d74b1c3de537f5e2e3814b1857bb918cf1a8dbc946576562e47e19c9daa34b9a`.
The corpus manifest SHA-256 is
`a24720111ce675e6bd14bc109e3e24782ab462b62203f3c41167e62d14188bc2`.

No production source, public API/ABI, normative specification or container
wire-format file changed in P7.

## Exit gate

P7 is complete only when:

- all nine targets meet the fixed threshold;
- ASan/UBSan runtime tests and the campaign report pass;
- no crash, timeout or sanitizer artifact is retained;
- the security invariant report passes;
- every known deviation is resolved or explicitly accepted;
- final evidence names the exact source revision and workflow run;
- no production behavior changed unless a failed gate required a fix.
