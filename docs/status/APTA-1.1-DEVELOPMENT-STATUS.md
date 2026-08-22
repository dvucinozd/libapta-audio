# APTA 1.1 development status

- **Branch:** `1.1.0`
- **Current implementation head:** `d17c3671ff5318d58847643537d7e0da7667e262`
- **Release status:** development; no `v1.1.0` tag or release claim

## Current boundary

The reusable 1.1 infrastructure, native meter/key implementation and the complete
desktop qualification path are now in place. The remaining blockers are
evidence-driven rather than missing core feature plumbing: fresh corpus
qualification, confidence-model fitting/holdout, physical ESP32-P4 measurements
and the final release freeze.

| Work item | Status | Delivered boundary |
|---|---|---|
| 1. APTA 1.1 result model | Complete | Feature bits, key/meter/quality views, initializers, accessors and 32/64-bit layout evidence |
| 2. External result builder | Complete | Bounded validated deep-copy import, provenance, all current feature setters and immutable finalization |
| 3. Container DJ sections | Complete | Deterministic `MKEY`, `MTRD`, `CONF` read/write, strict validation, golden fixture and frozen-reader compatibility |
| 4. Streaming container I/O | Complete | Output/input callbacks, bounded serialization, selective parsing, caller scratch and buffer equivalence |
| 5. Tempo/grid ensemble implementation | Candidate complete | Relation-aware S6 proposal + S4 refinement/grid-fit gate implemented; fresh-corpus acceptance remains open |
| 6. Confidence calibration contract | Complete | Deterministic isotonic fitting/evaluation protocol and data-separation gate; no production calibrated model yet |
| 7. Native meter/downbeat | Complete implementation | Bounded 3/4 vs 4/4 meter/downbeat analysis, exact refined-grid downbeat binding, immutable snapshots and cooperative scheduler integration |
| 8. Native musical key | Complete implementation | Bounded global major/minor key analysis, ranked candidates with strict encoded ordering, immutable snapshots and ESP-IDF packaging |
| 9. Progressive publication | Complete implementation | Provisional -> stable -> final generations with retained-result immutability verified end to end |
| 10. ESP32-P4 CI/capacity profile | Complete CI/layout evidence | ESP-IDF 6.0.2 `esp32p4` firmware build plus deterministic 30-minute bounded-capacity probe |
| 11. Final DJ acceptance contract | Complete | Frozen fresh-corpus evaluator and thresholds for key, meter, downbeat, grid and high-confidence safety |
| 12. Qualification execution path | Complete infrastructure | Privacy-preserving corpus freeze, native `apta-analyze --features all`, FINAL-only result export and frozen acceptance scoring |

The public development guide is
[`../api/APTA-API-1.1-DEVELOPMENT.md`](../api/APTA-API-1.1-DEVELOPMENT.md), the
DJ wire contract is
[`../../specification/APTA-1.1-DJ-SECTIONS.md`](../../specification/APTA-1.1-DJ-SECTIONS.md),
streaming behavior is
[`../file-format/APTA-STREAMING-IO-1.1.md`](../file-format/APTA-STREAMING-IO-1.1.md),
and final corpus scoring is frozen in
[`APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`](APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md).
The operational qualification sequence is documented in
[`APTA-1.1-QUALIFICATION-RUNBOOK.md`](APTA-1.1-QUALIFICATION-RUNBOOK.md).

## Implemented compatibility guarantees

- Existing `TEMP`, `LGRD`, `GGRD`, `REVN`, waveform and metadata semantics are
  unchanged by absence of the new optional sections.
- `MKEY`, `MTRD` and `CONF` use the existing container-v1 optional-section
  evolution rule. A frozen 1.0 consumer validates common framing and skips them.
- Canonical output is deterministic and reserved bytes are zero.
- Builder and parser enforce range, ordering, count, cross-feature and aggregate
  allocation limits before publishing an immutable result.
- MTRD integer downbeats bind to the same grid **whole-frame component and
  ordinal**. A non-zero Q32 grid remainder is valid because MTRD does not encode
  fractional frame bits.
- Result pointers retain the established result-lifetime ownership model.
- Streaming and buffer writers produce byte-identical canonical output for the
  same result and options.
- Meter and key publication use the existing immutable-generation contract; a
  retained earlier result is not rewritten when later evidence matures.

These are development-branch guarantees backed by tests, not yet a tagged
stable 1.1 compatibility promise.

## Tempo/grid ensemble boundary

The earlier Phase-5 threshold-only candidate remains rejected. Its old holdout
is not fresh evidence and must not be reused for threshold tuning.

The current candidate is structurally different: S6 chooses a known metrical
region, S4 re-estimates/refines the exact tempo on fine onset evidence, the
proposal must retain the existing endorsement score floor, and the proposed
fine grid must fit strictly better before promotion. A complete ensemble
evaluation is charged as one cooperative scheduler step and remains bounded by
the S4 evidence capacity.

Acceptance remains governed by
[`APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md`](APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md).
No corpus-level improvement is claimed until a genuinely fresh validation set
passes that protocol.

## Native meter, key and progressive publication

Native meter/downbeat and musical-key analysis are implemented without adding a
second ownership model or unbounded result storage. Both publish through the
same immutable result generations as the established waveform/tempo/grid
features.

The native meter estimator may use quantized onset bins when scoring candidate
meter phases, but publication resolves the selected ordinal through the exact
refined S4 grid Q32 period and stores that beat's whole-frame component in
MTRD. Builder, writer and reader cross-validation use the same whole-frame +
ordinal contract for explicit, segmented and hybrid grids.

Musical-key candidate ranking is performed at higher precision, then its
serialized `uint16_t` scores are made strictly descending so quantization cannot
turn a valid native result into an unserializable MKEY candidate list.

Musical key and meter passed Linux static/shared, Windows shared-ABI/interchange,
ILP32, sanitizer/fuzz/security, ESP-IDF 5.5.4/6.0.2, standalone ESP-IDF component
and ESP32-P4 qualification before the desktop `--features all` path was
integrated. The progressive publication regression retains a PROVISIONAL result
while later analysis publishes STABLE and FINAL generations, then verifies all
retained generations remain readable and unchanged even after session
destruction.

These tests establish lifecycle, serialization and boundedness semantics. They
are not a claim that key or downbeat accuracy has passed the final corpus
thresholds.

## Qualification execution path

The repository now contains all deterministic tooling needed to execute the
fresh-corpus gates without committing track titles, artists or local paths:

1. freeze local staging data into opaque track IDs and a SHA-256-bound labels
   manifest;
2. analyze each source with the native desktop analyzer using all supported DJ
   features;
3. inspect the generated `.apta` containers and export the exact frozen
   acceptance CSV schema;
4. reject any export whose session is not `completed` or whose key, meter or
   selected beatgrid snapshot is not `final`;
5. run the pre-registered DJ acceptance evaluator against the frozen manifest
   and labels hash.

This closes the software-path gap between real audio input and the acceptance
evaluator. It does **not** supply the fresh labelled audio evidence itself and
no fresh corpus has been scored yet.

## ESP32-P4 qualification evidence

The P4 CI profile builds the cooperative example with ESP-IDF 6.0.2 for
`esp32p4` and runs a deterministic 30-minute capacity calculation.

For 48 kHz / 30 minutes (`86,400,000` source frames), the profile selected
32,768 frames per overview column and measured:

- overview columns: **2,637** (below the 4,096 design ceiling);
- mutable S6 beat capacity: **3,072**;
- bounded immutable result slots: **2**;
- resident explicit beat records: **9,216** total across mutable S6 + two result slots;
- minimum static workspace: **932,960 bytes**;
- recommended static workspace: **991,286 bytes**;
- bounded result pool: **531,232 bytes**;
- combined minimum: **1,464,192 bytes**.

This is firmware-build and deterministic layout evidence only. Physical-device
latency, PSRAM bandwidth, allocator fragmentation, thermals, USB/audio
coexistence and real hardware memory placement remain unverified until hardware
is available.

## Frozen final DJ acceptance contract

`tools/apta_1_1_dj_acceptance_eval.py` and
[`APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`](APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md)
pre-register the final independent-corpus gates before fresh results are
inspected:

- at least 48 fresh manually verified tracks;
- exact key accuracy >=75%;
- exact meter accuracy >=95%;
- downbeat phase accuracy >=90%;
- beatgrid accuracy >=90%;
- <=5% high-confidence errors per output family at confidence >=75;
- beat-period tolerance <=1%;
- cyclic downbeat phase tolerance <=0.10 beat.

The evaluator requires exact opaque-ID matching and SHA-256 binding of the label
file. A smaller corpus is diagnostic-only. Its positive/negative self-test and
the full host/ABI/security matrix passed before the evaluator was integrated.
This freezes the measurement machinery; no fresh corpus has been scored yet.

## Remaining blockers before `v1.1.0`

1. Run the relation-aware tempo/grid candidate on genuinely fresh validation
   evidence and satisfy its pre-registered regression/acceptance gates.
2. Train the calibrated confidence model on a separate training set and pass an
   untouched holdout under the frozen calibration protocol; only then integrate
   a production `APTA_FEATURE_CALIBRATED_QUALITY` model.
3. Assemble and manually verify the independent >=48-track final DJ corpus,
   freeze its manifest/labels hash, run the now-complete native qualification
   path and pass every final key/meter/downbeat/grid/high-confidence gate.
4. Collect physical ESP32-P4 memory/timing evidence when hardware is available,
   including actual internal/PSRAM placement and workload interaction relevant
   to the intended device.
5. Freeze the final 1.1 API/ABI/wire documents, deliberately update version
   metadata, generate package/release evidence, tag and publish.

Pajoniiir application work such as scanning, tags, catalog, playlists, USB
transactions, playback scheduling, Rekordbox import wiring and UI remains
outside this repository.

## Release discipline

The stable authority remains APTA 1.0 / package 1.0.1. The frozen 1.0 normative
manifest and existing tags must not be rewritten. `VERSION` remains `1.0.1` and
the `1.1.0` branch name is only a development-line name.

No `v1.1.0` release should be cut until the evidence blockers above are closed
and the complete native/shared/ILP32/ESP-IDF/P4/fuzz/container/package matrix is
rerun against the exact release candidate.
