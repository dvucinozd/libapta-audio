# Task 2 third correction report

## Scope and outcome

This correction addresses the single remaining blocker from
`task-2-final-review.md`: an imported FINAL overview can no longer be accepted
when the source length is absent or unknown. The builder now matches the strict
WOVR parser invariant while retaining canonical non-FINAL unknown-length
imports.

## TDD evidence

The regression was added and run before production changes against
`335bdbd329fcdd6851a1fed6c32ce59073ab995d`:

```powershell
$env:Path="$env:Path;C:\msys64\ucrt64\bin;C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1"
$env:CMAKE_GENERATOR='Ninja'
cmake --build build-task2-full --target apta_result_builder_review_regressions
.\build-task2-full\tests\apta_result_builder_review_regressions.exe
```

The executable exited 1 with these expected RED assertions:

- `overview-unknown/final-rejected`
- `overview-unknown/partial-preserved`
- `overview-unknown/strict-parse`

This established both failures: the invalid FINAL input was accepted and
replaced the prior PARTIAL value, and the resulting serialized container was
rejected by strict parsing.

After the minimal validation guard, the same executable exited 0 with no RED
output. The complete focused command was:

```powershell
ctest --test-dir build-task2-full -R '^apta\.(api\.result_builder|serialization\.wovr)' --output-on-failure
```

Result: 10/10 passed.

## Implementation

- Overview validation rejects `APTA_FEATURE_FINAL` unless source information
  is already present and `total_frames` is known.
- Rejection occurs before setter allocation/copy, so a failed replacement is
  transactional and preserves the preceding valid overview.
- The regression first stores a canonical PARTIAL prefix for an unknown-length
  source, attempts the invalid FINAL replacement, finalizes the preserved
  PARTIAL result, then verifies serialize -> strict parse succeeds.
- Public builder documentation now states that FINAL requires known length and
  complete canonical coverage, while non-FINAL imports may be canonical
  prefixes or sparse spans.
- The documentation-only builder header hash was updated in the additive 1.1
  header-delta manifest. No symbol, structure, or layout changed.

## Verification

- Focused builder/WOVR suite: 10/10 passed.
- Full monolithic Ninja suite: 102/102 passed, 0 failed (40.42 seconds).
- Shared `apta_core` build: passed.
- Shared public-symbol gate: 1/1 passed.
- Compile-only ABI probes for `i686-pc-linux-gnu`,
  `i686-pc-windows-msvc`, and `riscv32-esp-unknown-elf`: all passed.
- Public header snapshot, layout, old-header/new-library, package/install,
  conformance, and versioned interchange gates are included in the 102/102
  run.
- `git diff --check`: passed.

The authoritative correction hash is reported from `git rev-parse HEAD` after
the commit in the controller handoff.
