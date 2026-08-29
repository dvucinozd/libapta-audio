# APTA 1.1 development status

- **Branch:** `1.1.0` — the branch ref is authoritative for the current development head; this document intentionally does not mirror a mutable head SHA.
- **Last DSP/serialization qualification baseline:** `d17c3671ff5318d58847643537d7e0da7667e262` (native key/meter CLI integration and MTRD/grid fixes).
- **Current qualification-tooling baseline at this audit:** `2953401f6ee46fcf966f0b384364e510b974bf4e` (privacy-safe corpus runner plus canonical corpus preparation/labeling); its PR exact-head CI, Security, native all-feature smoke, corpus preparation/labeling, tempo and confidence workflows passed.
- **Release status:** development; no `v1.1.0` release claim. `VERSION` remains `1.0.1`.

## Current boundary

The reusable 1.1 infrastructure, native meter/key implementation and complete desktop qualification path are in place. Tempo/grid and confidence acceptance evidence is retained. WP5 closed at `cfb811a96af4202f266d58fc8a74e484b189cf59` as a software-qualified, byte-stable production baseline after rejected experiments were retired, but it is not algorithmically eligible for WP6. The independent 60-track final DJ attempt is formally rejected, leaving a transferable replacement algorithm, physical ESP32-P4 measurements and final release freeze as the active blockers. Formal ASAP/Ballroom holdouts and a new final corpus remain unopened.

| Work item | Status | Delivered boundary |
|---|---|---|
| 1. APTA 1.1 result model | Complete | Feature bits, key/meter/quality views, initializers, accessors and 32/64-bit layout evidence |
| 2. External result builder | Complete | Bounded validated deep-copy import, provenance, all current feature setters and immutable finalization |
| 3. Container DJ sections | Complete | Deterministic `MKEY`, `MTRD`, `CONF` read/write, strict validation, golden fixture and frozen-reader compatibility |
| 4. Streaming container I/O | Complete | Output/input callbacks, bounded serialization, selective parsing, caller scratch and buffer equivalence |
| 5. Tempo/grid ensemble | **Accepted 2026-08-25** | Relation-aware recovery plus confidence-gated close-candidate arbitration and dominant S6 segment-family selection passed all five frozen gates on a formal 48-track owner-supplied fresh set (exact within 1% 25 -> 29, zero broken selections, no safety regressions); historical 188-row regression clean; see [`APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md`](APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md) |
| 6. Confidence calibration contract | Complete | Deterministic isotonic fitting/evaluation protocol and data-separation gate; **production model accepted and integrated 2026-08-25** — `isotonic-pav-clamped-v1` (model ID 1867860160) passed both frozen gates on a 48-row untouched holdout (Brier 0.179 -> 0.152, ECE 0.282 -> 0.198, high-confidence errors preserved at zero) and now publishes an optional BPM quality record; see [`APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md`](APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md) |
| 7. Native meter/downbeat | Development candidate | Bounded 3/4 vs 4/4 plumbing is complete. A conservative opt-in 3-band phase experiment adds four correct downbeats with zero breaks across 140 already-open development tracks, but absolute accuracy remains far below the release gate; both formal holdouts remain closed |
| 8. Native musical key | Complete implementation / WP4 closed without promotion | Bounded global major/minor analysis is complete. KP profiles plus log compression remain the production path at 20/60 on the spent human corpus. The opt-in harmonic projection reaches 22/60 safely but failed to establish independent transfer because both pre-registered ASAP development label derivations were nonviable before analyzer execution; it remains disabled |
| 9. Progressive publication | Complete implementation | Provisional -> stable -> final generations with retained-result immutability verified end to end |
| 10. ESP32-P4 CI/capacity | Complete CI/layout evidence | ESP-IDF 6.0.2 `esp32p4` firmware build plus deterministic 30-minute bounded-capacity probe |
| 11. Final DJ acceptance contract | Complete | Frozen fresh-corpus evaluator and thresholds for key, meter, downbeat, grid and high-confidence safety |
| 12. Qualification execution path | Complete infrastructure | Canonical WAV preparation, local labeling, privacy-preserving freeze, anonymous native `--features all` analysis, FINAL-only export and frozen acceptance scoring |
| 13. WP5 integrated baseline | **Software-qualified / algorithm gate failed** | Exact clean Release 121/121, ASan/UBSan 116/116 and retained-diagnostics focused 8/8 pass; default analyzer bytes and P4 capacity are unchanged, but no WP1-WP4 candidate qualified for promotion or WP6 |

The public development guide is [`../api/APTA-API-1.1-DEVELOPMENT.md`](../api/APTA-API-1.1-DEVELOPMENT.md), the DJ wire contract is [`../../specification/APTA-1.1-DJ-SECTIONS.md`](../../specification/APTA-1.1-DJ-SECTIONS.md), streaming behavior is [`../file-format/APTA-STREAMING-IO-1.1.md`](../file-format/APTA-STREAMING-IO-1.1.md), final corpus scoring is frozen in [`APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`](APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md), and the operational qualification sequence is [`APTA-1.1-QUALIFICATION-RUNBOOK.md`](APTA-1.1-QUALIFICATION-RUNBOOK.md).

The ordered engineering sequence for closing the remaining algorithm,
acceptance, hardware and freeze blockers is
[`APTA-1.1-ALGORITHM-IMPLEMENTATION-PLAN.md`](APTA-1.1-ALGORITHM-IMPLEMENTATION-PLAN.md).

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

This closes the software-path gap between private source audio and the acceptance evaluator. A 60-track private corpus was selected, canonicalized and later verified in full by an independent musician without APTA output in the listening workbench. The recovered 60-row export deterministically reproduces the frozen labels hash. Evaluation of the retained exact-corpus analyzer results formally rejects the candidate: meter passes, while key, downbeat, beatgrid and key/grid confidence-safety gates fail. The dated preparation, reproducible hashes, corrected metrics and candidate deltas are recorded in [`APTA-1.1-FINAL-DJ-CORPUS-STATUS.md`](APTA-1.1-FINAL-DJ-CORPUS-STATUS.md).

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
- minimum static workspace: **941,216 bytes**;
- recommended static workspace: **1,000,058 bytes**;
- bounded result pool: **537,104 bytes**;
- combined minimum: **1,478,320 bytes**.

At remediation revision `0fe1c22e44e759db3675a289e859b14a085c31e0`, the
corrected 12-feature capacity probe preserves every value above and the exact
ESP-IDF 6.0.2 build produces a 235,024-byte ESP32-P4 v3.1-v3.99 image with
PSRAM and the required 32,768-frame overview profile. Validator unit tests pass
8/8. Revision `18ade2ed13da23585d9ee10826056c83e3ded9a1` corrects the P4
revision profile and produces a normally flashable 233,056-byte v1.0-v1.99
image. Its diagnostic boot on the physical v1.3 board passed the 32 MiB PSRAM
test and full eight-second feature sweep with zero heap delta. This remains
diagnostic rather than qualifying evidence. The real 48 kHz USB/audio path,
1,800-second counters, thermals and final exact-candidate rerun remain open
under the frozen hardware-evidence contract.

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

1. ~~Run the relation-aware tempo/grid candidate on genuinely fresh validation evidence and satisfy the frozen evaluation gates.~~ **Closed 2026-08-25** — accepted on a formal 48-track owner-supplied fresh set; all five frozen gates passed (exact within 1% 25 -> 29, zero broken selections, no safety regressions). See [`APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md`](APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md).
2. ~~Train calibrated confidence on a separate >=96-row training set and pass an untouched >=48-row disjoint holdout; only then integrate a production `APTA_FEATURE_CALIBRATED_QUALITY` model.~~ **Closed 2026-08-25** — the accepted model and privacy-safe holdout summary are retained under `evidence/1.1`.
3. Produce a transferable replacement candidate that first satisfies the open-development WP1-WP4 gates and one-shot WP6 holdouts, then passes every final key/meter/downbeat/grid/high-confidence gate on a newly verified >=48-track WP7 corpus. WP5 is software-clean but does not authorize either evidence set. The first independent 60-track attempt is frozen and formally rejected; it cannot be reused as fresh acceptance evidence after candidate tuning.
4. Collect physical ESP32-P4 memory/timing/USB/audio coexistence evidence on real hardware.
5. Freeze final 1.1 API/ABI/wire documents, deliberately update version metadata, regenerate release/package evidence, rerun the complete exact-candidate matrix, then tag/publish.

Pajoniiir application concerns such as scanning, catalog, playlists, USB transactions, playback scheduling, Rekordbox import wiring and UI remain outside this repository.

## Release discipline

The stable authority remains APTA 1.0 / package 1.0.1. The frozen 1.0 normative manifest and existing tags must not be rewritten. `VERSION` remains `1.0.1`; the `1.1.0` branch name is only a development-line name.

`release/1.1-readiness.json` is fail-closed: while any external evidence blocker is open, version/package/tag state must remain at the development boundary. Closing all blockers makes the candidate only `freeze-eligible`; it does not automatically release 1.1.
