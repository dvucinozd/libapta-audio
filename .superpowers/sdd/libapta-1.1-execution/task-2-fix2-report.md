# Task 2 second correction report

## Scope and outcome

This correction addresses the two remaining blockers from
`task-2-rereview.md`: honest EXPLICIT local/global grid import and
state-sensitive overview coverage geometry. No new public symbol or structure
was added, and the previously approved 1.0 and Task 1 ABI layouts remain
unchanged.

## TDD evidence

Before production changes, focused cases were added to
`result_builder_review_regressions.c` and run against `8a6cf23`:

```powershell
$env:Path="$env:Path;C:\msys64\ucrt64\bin;C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1"
$env:CMAKE_GENERATOR='Ninja'
cmake --build build-task2-full --target apta_result_builder_review_regressions
.\build-task2-full\tests\apta_result_builder_review_regressions.exe
```

The executable exited 1 and printed these RED expectations:

- `explicit-local/finalize`
- `explicit-global/finalize`
- `overview-final-gap`
- `overview-partial-prefix/set`
- consequent `overview-partial-prefix/parse`

Final focused command:

```powershell
ctest --test-dir build-task2-full -R '^apta\.(api\.result_builder|serialization\.(s6|wovr))' --output-on-failure
```

Result: 12/12 passed. A final narrower rerun after adding the malformed S6
shape matrix passed 2/2 for the consolidated builder regression and S6
serialization test.

## Blocker 1: honest EXPLICIT grids

- Finalize now validates an EXPLICIT grid from adjacent beat intervals rather
  than searching a nonexistent segment array. Every interval is compared to
  the selected BPM using its ordinal delta.
- A single EXPLICIT beat is deliberately accepted as phase-only evidence; it
  cannot establish a period. This conservative rule is documented on the
  public builder setter.
- S6 writer and reader now accept the already-public EXPLICIT wire shape:
  zero segments, nonzero beats, and an APPLIED revision whose representation
  and counts match. No segment is synthesized and EXPLICIT is not relabeled.
- The reader skips the zero-length segment allocation and keeps the public
  segment pointer NULL.
- The regression covers local finalize/getter and global
  finalize/revision/serialize/parse/getter. Parsed global EXPLICIT bytes
  reserialize byte-identically.
- Malformed tests reject EXPLICIT with segments, HYBRID without segments, and
  SEGMENTS without segments in both strict and permissive modes.
- Existing SEGMENTS/HYBRID fixtures and the versioned interchange gate still
  pass. Therefore existing 1.0 containers remain readable; EXPLICIT is only an
  additive accepted interpretation of the existing GGRD/REVN fields.

## Blocker 2: overview state and coverage

- FINAL overview imports must begin at logical column zero and the declared
  origin, continue with adjacent column indices and source ranges without
  gaps, and reach the known logical end.
- PARTIAL/non-FINAL imports retain the existing per-span index/range mapping,
  ordering, non-overlap, truncation, overflow, and source-bound checks, but are
  no longer forced to cover the known end of the track.
- Tests reject a gapped FINAL overview and accept a valid known-length PARTIAL
  prefix through finalize, serialize, parse, and getter verification.
- All WOVR writer, partial-writer, roundtrip, malformed, hardening, and
  allocation-failure tests pass.

## ABI and verification

The documentation-only builder-header delta hash was updated. There are no new
exports or public layouts in this correction.

Verification results:

- Focused builder/S6/WOVR suite: 12/12 passed.
- Final monolithic Ninja suite: 102/102 passed, 0 failed (37.61 seconds).
- Shared `apta_core` build: passed.
- Shared public-symbol gate: 1/1 passed using Espressif LLVM 20
  `llvm-readobj`.
- Compile-only ABI probes for `i686-pc-linux-gnu`,
  `i686-pc-windows-msvc`, and `riscv32-esp-unknown-elf`: all passed.
- Package/install, public header snapshot/layout, old-header/new-library,
  public conformance, and versioned interchange gates are included in the
  102/102 run.
- `git diff --check`: passed.

The exact correction commit hash is supplied in the controller handoff because
a commit cannot contain its own content hash.
