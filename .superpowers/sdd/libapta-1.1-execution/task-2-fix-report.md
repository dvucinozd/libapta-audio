# Task 2 correction report

## Scope and outcome

This correction addresses all five blocking findings in
`task-2-review.md` without changing frozen 1.0 artifacts or Task 1 layouts.

## TDD evidence

Before production changes, `result_builder_review_regressions.c` was added and
run against `7da7658`:

```powershell
cmake --build build-task2-full --target apta_result_builder_review_regressions
.\build-task2-full\tests\apta_result_builder_review_regressions.exe
```

The executable failed and printed RED expectations for: missing global
revision, omitted S6 allocation, invalid overview/detail geometry, invalid
detail columns, missing waveform CONFIDENCE mask, session/feature-state
contradiction, grid without BPM, duplicate tempo, selected-key mismatch, and
decreasing meter ordinals.

Final focused command:

```powershell
cmake --build build-task2-full --target apta_result_builder_roundtrip apta_result_builder_validation apta_result_builder_allocation_failure apta_result_builder_review_regressions
ctest --test-dir build-task2-full -R '^apta.api.result_builder' --output-on-failure
```

Result: 4/4 passed.

## Finding 1: honest global-grid revision

- Added `apta_result_builder_set_grid_revision()` using the existing public
  `apta_grid_revision_view_t`.
- A global grid cannot finalize without a supplied revision.
- Validation requires a nonzero ID, PENDING/APPLIED state, valid confidence and
  affected range, matching representation/counts, matching segment/beat
  revision IDs, and consistent dynamic-tempo flags.
- Replacing the global grid clears its prior revision.
- Finalization copies the supplied revision into S6 storage; it never invents a
  NONE revision.
- The realistic fixture now queries revision and completes
  builder -> serialize -> parse -> revision/grid verification.

## Finding 2: canonical waveform geometry

- Overview spans now validate origin plus column-index geometry, ordered column
  intervals, known-length logical column count, overflow, final truncation,
  VALID columns, and 3-band flag/value consistency.
- Detail tiles now enforce level 1, 64-column tile boundaries, 256 frames per
  column, canonical source ranges, only final-track truncation, FINAL requiring
  known length, VALID columns, and zero bands without HAS_3BAND.
- The committed fixture was corrected from `[65536,131072)` to the canonical
  `[65536,66048)` range for columns 256 and 257.
- Adversarial overview/detail geometry, flags, bands, and unknown-duration tests
  are included. The valid fixture serializes and parses WOVR/WDTL successfully.

## Finding 3: masks and modifiers

- CONFIDENCE is derived for waveform-only and other imported views carrying a
  numeric confidence, independently of BPM.
- LOCAL rejects DYNAMIC_TEMPO; GLOBAL rejects LOCKED.
- View modifier flags must equal the aggregate modifier flags present on their
  segments/beats. Available and changed masks are derived from this complete,
  validated representation.
- Tests cover selector violations and aggregate contradictions.

## Finding 4: cross-validation

- Local/global grids require BPM.
- Grid periods/nominal tempos are checked against source rate and selected
  tempo; dynamic grids must still contain the selected tempo.
- Hybrid explicit beats must agree with segment anchor/period/ordinal data.
- Meter downbeats are matched to imported explicit or segment-derived beats;
  meter ordinals increase and segment IDs are unique.
- Tempo/key candidates have strict score ordering, reject duplicate values, and
  include the selected value when supplied. Selected-only zero-count tempo/key
  views remain valid and are documented.
- Grid segment IDs are nonzero/unique. COMPLETED requires FINAL imported data;
  CREATED cannot publish feature data.
- Tests cover duplicate IDs/values, selected membership, hybrid disagreement,
  meter/grid mismatch, and session-state contradictions.

## Finding 5: complete allocation cap

- Finalize computes the complete result-owned graph before allocating or
  publishing, including `apta_result_t`, copied payloads, and
  `apta_internal_s6_result_state_t`.
- Public documentation defines `maximum_allocation_bytes` as the finalized
  result graph bound, excluding builder-retained setter copies.
- An internal-size boundary test proves cap-minus-S6 fails and the exact bound
  passes.
- Allocation-failure iteration now retries finalize after an injected
  finalize-time OOM, proving rollback leaves the builder reusable and all
  allocations balanced.

## Additional ownership/lifetime coverage

Tests now cover successful replacement, failed replacement preserving the last
valid value, finalize twice, reuse after finalize, independent earlier results,
and multiple distinct calibrated-quality targets.

## ABI and verification

The new revision setter is present in the 1.1 ELF map, Windows DEF, public
symbol list, and MSVC pragma exports. The builder header hash was updated; no
new public structure was introduced, so the approved layout manifests remain
unchanged.

Verification results:

- Full monolithic Ninja suite: 102/102 passed, 0 failed (41.19 seconds).
- Shared `apta_core` build: passed.
- Shared public-symbol gate: 1/1 passed.
- Compile-only ABI probes: i686 Linux, i686 Windows/MSVC ABI, and RISC-V32 all
  exited 0.
- Header snapshot, public layouts, old-header/new-library, package/install,
  public conformance, and interchange gates are included in the 102/102 run.
- `git diff --check`: passed.

The exact correction commit hash is supplied in the controller handoff because
a commit cannot contain its own content hash.
