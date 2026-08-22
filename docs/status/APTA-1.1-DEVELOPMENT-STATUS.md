# APTA 1.1 development status

- **Branch:** `1.1.0`
- **Implementation baseline:** `0e524bee53fb5a528dd6e3fd138c4b4777def8f0`
- **Release status:** development; no `v1.1.0` tag or release claim

## Current boundary

The first four implementation tasks are complete. Task 5 is now in progress: a
relation-aware tempo/grid ensemble gate has landed, while corpus qualification
and acceptance remain deliberately open.

| Task | Status | Delivered boundary |
|---|---|---|
| 1. APTA 1.1 result model | Complete | Feature bits, key/meter/quality views, initializers, accessors and 32/64-bit layout evidence |
| 2. External result builder | Complete | Bounded validated deep-copy import, provenance, all current feature setters and immutable finalization |
| 3. Container DJ sections | Complete | Deterministic `MKEY`, `MTRD`, `CONF` read/write, strict validation, golden fixture and frozen-reader compatibility |
| 4. Streaming container I/O | Complete | Output/input callbacks, bounded serialization, selective parsing, caller scratch and buffer equivalence |
| 5. Tempo/grid ensemble | In progress | S6 metrical proposals require independent S4 score support and a strictly better fine-grid fit before promotion; corpus acceptance is still open |

The completed task-1-through-task-4 implementation is present in commits
`d787925` through `0e524be` on the development line. The public guide is
[`../api/APTA-API-1.1-DEVELOPMENT.md`](../api/APTA-API-1.1-DEVELOPMENT.md), the
DJ wire contract is
[`../../specification/APTA-1.1-DJ-SECTIONS.md`](../../specification/APTA-1.1-DJ-SECTIONS.md)
and streaming behavior is
[`../file-format/APTA-STREAMING-IO-1.1.md`](../file-format/APTA-STREAMING-IO-1.1.md).

## Implemented compatibility guarantees

- Existing `TEMP`, `LGRD`, `GGRD`, `REVN`, waveform and metadata semantics are
  unchanged by absence of the new optional sections.
- `MKEY`, `MTRD` and `CONF` use the existing container-v1 optional-section
  evolution rule. A frozen 1.0 consumer validates common framing and skips them.
- Canonical output is deterministic and reserved bytes are zero.
- Builder and parser enforce range, ordering, count, cross-feature and aggregate
  allocation limits before publishing an immutable result.
- Result pointers retain the established result-lifetime ownership model.
- Streaming and buffer writers produce byte-identical canonical output for the
  same result and options.

These are development-branch guarantees backed by tests, not yet a tagged
stable 1.1 compatibility promise.

## Verification at the task-1-through-task-4 baseline

| Gate | Result |
|---|---:|
| Configured static host suite | 99/99 passed |
| Shared ABI/export suite | 4/4 passed |
| Focused post-commit task-1-through-task-4 suite | 8/8 passed |
| ILP32 public-header/layout compile | Passed |
| RISC-V 32-bit public-header compile | Passed |
| `git diff --check` | Clean |

Coverage includes result ranges and storage, builder round trips and invalid
input, allocation-failure paths, malformed and scale DJ sections, independent
golden bytes, frozen APTA 1.0 consumption, streaming equivalence, selective
loading and scratch limits.

## Task 5 checkpoint — tempo/grid ensemble

The earlier Phase-5 threshold-only candidate remains rejected. Its hold-out
failure is still authoritative and those 48 hold-out rows must not be reused as
fresh tuning data.

The first APTA 1.1 ensemble checkpoint adds genuinely different evidence rather
than another fitted threshold:

- S6 may propose a metrical family member even when that tempo did not survive
  into S4's public three-candidate list;
- S6 chooses the metrical region, while S4 fine-bin evidence re-estimates and
  sub-bin refines the BPM before it can be published;
- the proposal must be one of the existing half/double, two-thirds/three-half,
  third/triple or quarter/quadruple relations;
- the proposal must retain at least the already-existing S4 endorsement score
  floor on the same fine onset evidence;
- the S4 fine grid at the proposed period must fit that evidence strictly better
  than the currently selected fine grid;
- if promoted, the local grid is re-phased at the promoted period rather than
  keeping the phase of the displaced S4 winner;
- a complete ensemble evaluation is charged as one cooperative scheduler step
  and remains bounded by the existing S4 evidence capacity;
- no new public API, ABI, container field or release-version claim is introduced
  by this checkpoint.

The acceptance boundary is frozen in
[`APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md`](APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md).
This checkpoint is an implementation candidate, not an accuracy claim. It must
pass native regression gates and the pre-registered fresh-corpus evaluation
before Task 5 can be marked complete.

## Work that is not complete

The branch is not yet a complete DJ analyzer. The following planned work remains:

1. tempo/grid ensemble corpus qualification for half, double, third and related
   metrical errors, including regression limits and a non-reused validation set;
2. calibrated confidence/quality models trained and frozen against a fully
   separate holdout;
3. native meter/downbeat detection;
4. native global musical-key detection;
5. progressive quick-pass/full-pass publication through immutable generations;
6. the ESP32-P4 DJ memory profile, including 30-minute bounded-capacity probes,
   9,216 explicit beats, 4,096 overview columns and the PSRAM/internal/scratch
   budgets;
7. mandatory ESP-IDF 6.0.2/P4 CI and hardware memory/workspace evidence;
8. the independent manually verified corpus and the tempo, beatgrid, downbeat
   and key acceptance thresholds;
9. final API/ABI freeze, version metadata, package evidence, tag and release.

Pajoniiir application work such as scanning, tags, catalog, playlists, USB
transactions, playback scheduling, Rekordbox import wiring and UI remains
outside this repository and outside the first four libapta tasks.

## Release discipline

The stable authority remains APTA 1.0 / package 1.0.1. The frozen 1.0 normative
manifest and existing tags must not be rewritten. The `1.1.0` branch name is a
development-line name; it is not evidence that package version, encoded API
version, conformance profiles or release artifacts have been promoted.

Before `v1.1.0`, the project must complete the remaining algorithms and P4
profile, rerun the complete native/shared/ILP32/ESP-IDF/fuzz/container evidence,
freeze the final API and wire documents, update version metadata deliberately
and publish a release-specific verification record.
