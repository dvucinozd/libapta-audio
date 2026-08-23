# APTA 1.1 development status

- **Branch:** `1.1.0` — the branch ref is authoritative for the current development head; this document intentionally does not mirror a mutable head SHA.
- **Last DSP/serialization qualification baseline:** `d17c3671ff5318d58847643537d7e0da7667e262` (native key/meter CLI integration and MTRD/grid fixes).
- **Current qualification-tooling baseline at this audit:** `2953401f6ee46fcf966f0b384364e510b974bf4e` (privacy-safe corpus runner plus canonical corpus preparation/labeling); its PR exact-head CI, Security, native all-feature smoke, corpus preparation/labeling, tempo and confidence workflows passed.
- **Release status:** development; no `v1.1.0` release claim. `VERSION` remains `1.0.1`.

## Current boundary

The reusable 1.1 infrastructure, native meter/key implementation and complete desktop qualification path are in place. Remaining blockers are evidence-driven rather than missing core feature plumbing: fresh tempo/grid validation, confidence-model fitting and untouched holdout, independent final DJ corpus acceptance, physical ESP32-P4 measurements, and final release freeze.

| Work item | Status | Delivered boundary |
|---|---|---|
| 1. APTA 1.1 result model | Complete | Feature bits, key/meter/quality views, initializers, accessors and 32/64-bit layout evidence |
| 2. External result builder | Complete | Bounded validated deep-copy import, provenance, all current feature setters and immutable finalization |
| 3. Container DJ sections | Complete | Deterministic `MKEY`, `MTRD`, `CONF` read/write, strict validation, golden fixture and frozen-reader compatibility |
| 4. Streaming container I/O | Complete | Output/input callbacks, bounded serialization, selective parsing, caller scratch and buffer equivalence |
| 5. Tempo/grid ensemble | Development candidate | Relation-aware recovery plus confidence-gated close-candidate arbitration and dominant S6 segment-family selection implemented; untouched validation remains open |
| 6. Confidence calibration contract | Complete | Deterministic isotonic fitting/evaluation protocol and data-separation gate; no production calibrated model yet |
| 7. Native meter/downbeat | Development candidate | Bounded 3/4 vs 4/4 plumbing is complete, but a frozen real-ballroom development split exposes failed 3/4 tempo/meter recall and low downbeat accuracy; untouched holdout remains closed |
| 8. Native musical key | Complete implementation | Bounded global major/minor analysis, ranked candidates with strict encoded ordering, immutable snapshots and ESP-IDF packaging |
| 9. Progressive publication | Complete implementation | Provisional -> stable -> final generations with retained-result immutability verified end to end |
| 10. ESP32-P4 CI/capacity | Complete CI/layout evidence | ESP-IDF 6.0.2 `esp32p4` firmware build plus deterministic 30-minute bounded-capacity probe |
| 11. Final DJ acceptance contract | Complete | Frozen fresh-corpus evaluator and thresholds for key, meter, downbeat, grid and high-confidence safety |
| 12. Qualification execution path | Complete infrastructure | Canonical WAV preparation, local labeling, privacy-preserving freeze, anonymous native `--features all` analysis, FINAL-only export and frozen acceptance scoring |

The public development guide is [`../api/APTA-API-1.1-DEVELOPMENT.md`](../api/APTA-API-1.1-DEVELOPMENT.md), the DJ wire contract is [`../../specification/APTA-1.1-DJ-SECTIONS.md`](../../specification/APTA-1.1-DJ-SECTIONS.md), streaming behavior is [`../file-format/APTA-STREAMING-IO-1.1.md`](../file-format/APTA-STREAMING-IO-1.1.md), final corpus scoring is frozen in [`APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`](APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md), and the operational qualification sequence is [`APTA-1.1-QUALIFICATION-RUNBOOK.md`](APTA-1.1-QUALIFICATION-RUNBOOK.md).

## Implemented compatibility guarantees

- Existing `TEMP`, `LGRD`, `GGRD`, `REVN`, waveform and metadata semantics are unchanged by absence of the new optional sections.
- `MKEY`, `MTRD` and `CONF` follow the container-v1 optional-section evolution rule; frozen 1.0 consumers validate common framing and skip them.
- Canonical output is deterministic and reserved bytes are zero.
- Builder/parser enforce range, ordering, count, cross-feature and aggregate-allocation limits before publishing immutable results.
- MTRD integer downbeats bind to the same grid whole-frame component and ordinal. A non-zero Q32 grid remainder is valid because MTRD does not encode fractional frame bits.
- Streaming and buffer writers produce byte-identical canonical output for the same result/options.
- Meter and key use the established immutable-generation lifetime model.

These are development-branch guarantees backed by tests, not yet a tagged stable 1.1 compatibility promise.

## Native DJ qualification path

Native meter/downbeat and musical-key analysis publish through the same immutable result generations as waveform/tempo/grid features. Meter scoring may use quantized onset bins, but publication resolves the selected ordinal through the exact refined S4 grid Q32 period and stores that beat's whole-frame component in MTRD. Builder, writer and reader use the same whole-frame + ordinal contract for explicit, segmented and hybrid grids.

Musical-key ranking is performed at higher precision, then serialized `uint16_t` scores are forced strictly descending so quantization cannot turn a valid native result into an unserializable MKEY candidate list.

The desktop CLI path has an end-to-end Actions smoke test that executes real WAV analysis with `apta-analyze --features all` and requires serializable final `MKEY` and `MTRD` output through `apta-inspect`.

The qualification tooling then provides the full private-corpus path:

1. canonicalize local MP3/FLAC/WAV material to the frozen qualification WAV format;
2. label canonical WAV frame coordinates locally without remote scripts;
3. freeze labels into opaque SHA-256-derived IDs;
4. re-hash and exactly match the frozen manifest before analysis;
5. present only anonymous `track-<hash>.wav` names to `apta-analyze --features all`;
6. bind run metadata to exact source revision, analyzer hash, manifest hash, output hashes and mapping hash;
7. export only completed/FINAL native key, meter and selected beatgrid results;
8. run the pre-registered acceptance evaluator.

This closes the software-path gap between private source audio and the acceptance evaluator. It does **not** provide the required fresh evidence itself. A 60-track private corpus has now been selected, copied locally and canonicalized, and an independent automated pre-review was completed without reading APTA output. Manual verification against the canonical WAV files remains pending, so no official corpus has been frozen and no fresh acceptance result is claimed. The first separately frozen automated diagnostic run exposed long-form completion and TEMP ordering defects; revision `d53ee8d` fixes both, and a from-scratch rerun completed and parsed all 60 outputs. A subsequent exporter audit fixed the scored period to use the same local grid as the native meter/downbeat result when `LGRD` is present. Development-only S6 candidate arbitration and a conservative weak-3/4 policy then improved automated-reference period and meter counts, but downbeat, beatgrid, key and confidence-safety gates still fail. Because those policies were designed after this corpus was inspected, even their improvements are contaminated development evidence. The dated preparation, corrected metrics and candidate deltas are recorded in [`APTA-1.1-FINAL-DJ-CORPUS-STATUS.md`](APTA-1.1-FINAL-DJ-CORPUS-STATUS.md).

A separate targeted protocol now freezes balanced development and untouched
holdout splits from ASAP and the real-audio Ballroom Rhythm Dataset. The first
development run shows that the conservative 4/4 prior does not generalize:
Ballroom 3/4 meter recall is 1/20 while 4/4 is 20/20, and downbeat is 6/40.
The failure is primarily upstream tempo-family selection: period is within 10%
on 17/20 common-time tracks and 0/20 triple-meter tracks. The holdouts remain
unopened. Exact methodology, hashes and the rejected multiband candidate are in
[`APTA-1.1-METER-DOWNBEAT-VALIDATION.md`](APTA-1.1-METER-DOWNBEAT-VALIDATION.md).

## ESP32-P4 qualification boundary

For 48 kHz / 30 minutes (`86,400,000` source frames), deterministic capacity evidence remains:

- overview columns: **2,637** below the 4,096 design ceiling;
- mutable S6 beat capacity: **3,072**;
- bounded immutable result slots: **2**;
- resident explicit beat records: **9,216** across mutable S6 + two result slots;
- minimum static workspace: **932,960 bytes**;
- recommended static workspace: **991,286 bytes**;
- bounded result pool: **531,232 bytes**;
- combined minimum: **1,464,192 bytes**.

This is firmware-build/layout evidence only. Physical-device latency, actual internal/PSRAM placement, allocator fragmentation, thermals, and USB/audio coexistence remain unverified until a real hardware run satisfies the frozen hardware-evidence contract.

## Frozen final DJ acceptance contract

The final evaluator pre-registers these gates:

- at least 48 fresh manually verified tracks;
- exact key accuracy >=75%;
- exact meter accuracy >=95%;
- downbeat phase accuracy >=90%;
- beatgrid accuracy >=90%;
- <=5% high-confidence errors per output family at confidence >=75;
- beat-period tolerance <=1%;
- cyclic downbeat phase tolerance <=0.10 beat.

Do not tune thresholds after examining fresh acceptance results. The old development corpus/holdout is not fresh evidence.

## Remaining blockers before `v1.1.0`

1. Run the relation-aware tempo/grid candidate on genuinely fresh validation evidence and satisfy the frozen evaluation gates.
2. Train calibrated confidence on a separate >=96-row training set and pass an untouched >=48-row disjoint holdout; only then integrate a production `APTA_FEATURE_CALIBRATED_QUALITY` model.
3. Assemble and independently verify the >=48-track final DJ corpus, freeze manifest/labels, run the complete native qualification path and pass every final key/meter/downbeat/grid/high-confidence gate.
4. Collect physical ESP32-P4 memory/timing/USB/audio coexistence evidence on real hardware.
5. Freeze final 1.1 API/ABI/wire documents, deliberately update version metadata, regenerate release/package evidence, rerun the complete exact-candidate matrix, then tag/publish.

Pajoniiir application concerns such as scanning, catalog, playlists, USB transactions, playback scheduling, Rekordbox import wiring and UI remain outside this repository.

## Release discipline

The stable authority remains APTA 1.0 / package 1.0.1. The frozen 1.0 normative manifest and existing tags must not be rewritten. `VERSION` remains `1.0.1`; the `1.1.0` branch name is only a development-line name.

`release/1.1-readiness.json` is fail-closed: while any external evidence blocker is open, version/package/tag state must remain at the development boundary. Closing all blockers makes the candidate only `freeze-eligible`; it does not automatically release 1.1.
