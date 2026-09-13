# Implementation plan for Shome review findings — 2026-09-13

Status: PLAN ONLY. User requested saving this plan and deferring implementation
to the next session. No implementation, release, firmware or corpus work here.

Active checkout: `D:\AI\LIBAPTA\libapta-audio-dsp-20260904`.
Branch: `agent/dsp-takeover-20260904`.
Planning baseline: `f4acaa31070f5520e2b220d9dc3c27fd3889cdf0`.
Review input: `E:\Downloads\REVIEW.md`, reviewed source revision `e9ea781`.
All eight affected areas are unchanged between those revisions. Findings 3 and
7 were independently reproduced with repository tests; other findings were
checked against current code, without the reviewer's external probes/fixtures.
Recreate those reproductions as repository regression tests before fixing them.

## Scope and execution rules

At next session first refresh remote refs and inspect status/worktrees, local
instructions and this plan. Preserve unrelated untracked `output/`. Revalidate
findings if the branch moved; do not overwrite other work. Prior narrow
commit/push authorization persists. No parallel agents are required by this plan.

Fix production correctness before resuming DSP candidate development. Preserve
E1/E2/N1/A1 instruments, frozen evidence, algorithm thresholds, build defaults
and VERSION 1.0.1. These fixes do not confer music or physical P4 qualification.
Do not change public ABI or wire format without first documenting why an
existing-compatible solution cannot satisfy the contract.

For each step: add a minimal reproducer that fails on the baseline, implement
the smallest correction, run focused tests, inspect the staged diff and commit
the completed step. Keep findings in separate commits where practical. Record
exact commands/results, remaining limitations and local/remote SHA on push.

## 1. P1: independent PCM analyzers under detail-cache pressure

Files: `src/waveform/apta_waveform_input_detail.c`,
`src/waveform/apta_waveform_detail_mvp.c`, relevant detail/onset/grid tests.

- Reproduce overview+detail+BPM with all four resident detail tiles protected,
  then accept PCM outside the focus. Compare accepted frames and S4 evidence
  against a session receiving identical PCM with detail disabled. Include S6
  when requested; measure actual consumption rather than infer it from S4.
- Separate analyzer progress/failure handling in the shared sample loop. A
  best-effort detail-cache miss must not skip S4/S6 or the block remainder.
  Audit whether an S4 failure similarly starves S6. Preserve real error/state
  semantics and bounded storage; do not ignore every failure indiscriminately.
- Cover cache eviction/protection, discontinuities, replay/duplicate input and
  enabled-feature combinations. Confirm no double consumption on retry.

Done: accepted PCM reaches every eligible independent analyzer exactly once,
cache pressure preserves protected tiles, and public acceptance remains honest.

## 2. P1: musical-key applicability under out-of-order input

Files: `src/key/apta_key.c`, internal key state as necessary, key/persistence tests.

- Reproduce `[96000,144000)` followed by `[0,48000)` at 48 kHz, and the opposite
  order, using overview+key. Check returned range and serialization round-trip.
- Define range bookkeeping for evidence actually accumulated into completed
  windows. Track pending-window bounds separately if necessary; a discarded
  partial window must not contaminate retained evidence bounds.
- Maintain the enclosing source range of retained evidence regardless of
  processing order. An enclosing range can contain gaps; do not claim continuous
  coverage where the API only supplies a single applicability interval.
- Test backward/forward jumps, sparse regions, an abandoned partial window,
  short input with no completed window, and persistence after each publication.

Done: available key results always have valid bounds describing retained
evidence and persist successfully for every supported input ordering.

## 3. P1: standalone ESP-IDF package construction

Files: `ports/espidf/package_component.py`, `ports/espidf/test_package_component.py`,
`ports/espidf/CMakeLists.txt`, cooperative example CMake files if needed.

- Replace the obsolete literal `REQUIRES espidf esp_timer heap log` adaptation
  with an explicit, unambiguous adaptation of the actual CMake dependency list.
  Retain conditional dependencies and reject unexpected source structure.
- Include `src/confidence` and audit the entire component source inventory.
  Validate every referenced packaged source/header, not only the newly missing file.
- Extend packaging tests to require successful archive construction, deterministic
  contents, correct standalone dependency names and no monorepo-only paths.
- Compile the extracted standalone example using the supported ESP-IDF environment.
  If that toolchain is unavailable, record the build gate as open; a package test
  does not establish firmware build success. Do not flash hardware in this step.

Done: package tests pass, standalone references resolve, archive is reproducible,
and extracted-example compilation passes or is explicitly left unverified.

## 4. P2: immutable workflow action references

Files: `.github/workflows/dj-candidate-comparison-1.1.yml`,
`key-validation-1.1.yml`, `meter-validation-1.1.yml`.

- Resolve the five current action references to verified upstream full commit
  SHAs while preserving intended versions. Keep readable version comments.
- Run `python security/automation/check_workflow_pins.py` over the repository.
  Inspect current Actions runs before any dispatch; avoid redundant runs.

Done: zero pin-validator violations and no unrelated action upgrades.

## 5. P2: streaming MTRD/grid consistency

Files: `src/serialization/apta_streaming_io.c`, grid-matching helpers,
`src/serialization/apta_dj_reader.c` as reference, streaming/buffer reader tests.

- Create a CRC-valid combined grid/MTRD fixture, then shift a downbeat by one
  frame in both summary and segment while retaining its ordinal. Confirm the
  current buffer rejection and streaming acceptance before the correction.
- Apply identical encoded-grid binding rules to streaming input, including
  meter-only selective loading when a grid is encoded but not materialized.
- Preserve bounded allocation and monotonic O(M+G) validation; avoid rescanning
  the whole grid per meter record or forcing unrelated features into output.
- Cover local/global grids, EXPLICIT/SEGMENTS/HYBRID, frame and ordinal mismatch,
  valid sparse grids, absent grids, and section order supported by the format.

Done: both APIs agree on valid/invalid relationships with all-feature and
selective loading, without resource-bound or complexity regressions.

## 6. P2: streaming WOVR framing and lifecycle

Files: `src/serialization/apta_streaming_io.c`, strict buffer overview parser,
shared validation helpers if appropriate, reader conformance tests.

- Recreate separate CRC-valid mutations: partial WOVR in a final container;
  inconsistent declared logical-column count with unchanged spans/payload.
- Validate encoded logical extent against spans using the buffer reader's exact
  rule; do not assume logical extent always equals packed count for sparse spans.
- Enforce container/section lifecycle compatibility before materialization.
  Preserve permitted partial containers and sparse overview behavior.
- Run the same positive/negative fixtures through buffer and streaming APIs,
  including truncation, limit boundaries and invalid span/column indexing.

Done: framing and lifecycle verdicts agree; no new memory-safety claim is inferred
from the original validation discrepancies.

## 7. P2: terminal region-request capacity

Files: `src/core/apta_session_scheduler.c`, scheduler internals/policy,
`specification/progressive-scheduling.md`, public request documentation and tests.

- Reproduce >16 request/cancel cycles and separately >16 satisfied requests in
  one session. Distinguish active capacity from retained terminal history.
- Document compatible reclamation semantics before coding: prefer reusing the
  oldest terminal slot when capacity is needed, keeping terminal progress queryable
  until retirement. After retirement, old IDs return NOT_AVAILABLE and must not
  refer to a different request. Check existing API guarantees before choosing this.
- Audit cancellation cleanup, pending scheduler work, automatic-ID allocation,
  caller-supplied IDs, collision detection and wraparound. Do not simply zero a
  slot while other state still references it, or silently change ID semantics.
- Test long-running mixed active/cancelled/satisfied requests, all-active exhaustion,
  terminal queries before/after retirement, and cancellation during pending work.

Done: terminal history does not permanently exhaust the session; active limits
remain bounded, stale IDs are safe, and documented query behavior is preserved.

## 8. P2: resumable release publication

Files: `.github/workflows/release.yml`, an isolated publication helper/harness if useful.

- Separate immutable tag existence from verified release completion. If a tag
  exists, verify it points to the intended commit; reject mismatches without
  force-updating the tag.
- Resume tag-present/release-absent and incomplete-assets states from the exact
  tagged revision. Do not package a newer HEAD under an older existing tag.
- Define expected asset names/hashes from the release manifest. Verify existing
  assets, upload missing assets and fail explicitly on conflicting contents.
  Do not silently overwrite immutable artifacts or treat names alone as integrity.
- Test without real publication: no tag, tag only, interrupted release creation,
  missing assets, conflicting tag/asset, already-complete release, repeated and
  concurrent invocation. Inspect event/job conditions so retries actually reach
  recovery. Preserve release eligibility and authorization gates.

Done: interrupted publication can finish idempotently while tags and existing
verified assets remain immutable. No real release/tag is created by these tests.

## Final validation and next-session entry point

After focused tests, run the relevant current native Werror Release and Debug
ASan/UBSan suites. Include shared-library/public-symbol tests if API/build
composition is affected, parser negative/selective-loading suites, package tests,
workflow pins and isolated release-recovery tests. Run security invariants and
the expected-blocked readiness check; passing readiness must not become a claim
that musical/hardware gates closed. Record environments not exercised.

Do not rerun frozen DSP banks merely for unrelated core/CI edits. If a fix changes
their dependencies, identify and justify the relevant compatibility check first.
Close each finding with test evidence and update the development status/handoff.

FIRST NEXT ACTION: refresh branch/status and implement the failing regression
test for step 1 (protected detail cache starving S4/S6). Complete step 1 before
moving on. This plan schedules no background work; implementation is paused.
