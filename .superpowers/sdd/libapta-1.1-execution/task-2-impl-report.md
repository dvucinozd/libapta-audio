# Task 2 implementation report

## Outcome

Task 2 adds a reusable opaque `apta_result_builder_t` that imports externally
computed result data, validates it, owns setter inputs immediately, and
finalizes a second deep copy as a normal immutable `apta_result_t`. Finalized
results use the existing getters and `apta_result_release`. The builder can be
reset, reused, finalized repeatedly, or destroyed independently of its results.

The additive public contract is in `include/apta/apta_result_builder.h`. It
includes fixed-width extensible options, info, provenance, and waveform-detail
input structures and initializers. Provenance explicitly identifies an external
import; the builder rejects the native-analysis provenance value rather than
pretending analysis ran.

## TDD evidence

Initial RED configure/build:

```powershell
$env:Path="$env:Path;C:\msys64\ucrt64\bin;C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1"
$env:CMAKE_GENERATOR='Ninja'
cmake -S . -B build-task2-red -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF -DAPTA_BUILD_TOOLS=OFF
cmake --build build-task2-red --target apta_result_builder_roundtrip
```

The compile failed as intended with unknown
`apta_result_builder_t`/provenance/input types and undeclared builder functions.

A later overflow regression was also captured RED:

```powershell
cmake --build build-task2-red --target apta_result_builder_validation
.\build-task2-red\tests\apta_result_builder_validation.exe
```

It exited 1 at the `UINT32_MAX` tempo-candidate assertion before the validator
was changed to reject an array exceeding its allocation bound prior to walking
the caller pointer.

Focused GREEN:

```powershell
cmake --build build-task2-red --target apta_result_builder_validation apta_result_builder_roundtrip apta_result_builder_allocation_failure
ctest --test-dir build-task2-red -R '^apta.api.result_builder' --output-on-failure
```

Result: 3/3 passed. The custom-allocator fault loop covers allocation indices
2 through 11, accepts each expected OOM path, checks unpublished outputs, and
requires zero outstanding allocations after cleanup. Context allocation failure
is accounted for separately by the existing context tests.

## Validation and ownership

Validation covers ABI prefixes/reserved fields, unsupported options and
representations, source bounds, pointer/count pairs, feature selectors,
waveform/span/tile ordering, array arithmetic and allocation bounds, tempo and
confidence ranges, local/global grid representation and monotonicity, key tonic
and tuning, meter/downbeat values and ranges, calibrated-quality targets and
duplicates, and finalize-time source/feature consistency.

All builder and result storage uses the library context allocator. Setters are
transactional deep copies; finalization publishes only after all allocations
and copies succeed. Failure cleanup releases all temporary result storage.

## ABI and verification

The new symbols are present in the 1.1 ELF map, Windows DEF, symbol manifest,
and MSVC pragma-export list. New public structures are covered by initializer,
compile-layout, and LP64/ILP32/p32a64 layout manifests. The 1.1 header-delta
manifest includes `apta.h` and `apta_result_builder.h`; 1.0 frozen artifacts
were not changed.

Full static verification:

```powershell
cmake -S . -B build-task2-full
cmake --build build-task2-full
ctest --test-dir build-task2-full --output-on-failure
```

Result: 98/98 passed, including package/install, initializers, all builder tests,
public layout, header snapshot, and old-header/new-library compatibility.

Shared-library export verification:

```powershell
cmake --build build-task2-shared --target apta_core
ctest --test-dir build-task2-shared -R '^apta.abi.public_symbols$' --output-on-failure
```

Result: shared core built and 1/1 public-symbol test passed.

Cross-target compile-only ABI verification used Clang with
`-target i686-pc-linux-gnu`, `-target i686-pc-windows-msvc`, and
`-target riscv32-esp-unknown-elf` against `tests/compile/abi_layout.c`.
All three exited 0.

`git diff --check` exited 0.

## Known build-mode limitation

A Windows MinGW all-target shared build links ordinary tests against both
`libapta.dll.a` and `libapta_core_test.a`, producing duplicate existing public
symbols. A fresh detached worktree at parent `e7789b6` reproduced the exact
failure for `apta_result_model_1_1`; root CMake adds `apta_core_test` after the
test helper already linked `apta::core`. This is a pre-existing shared-test
CMake limitation, not introduced or broadened by Task 2. Fixing it is outside
Task 2; the authoritative shared core/export gate passes.

## Files

- Public API: `include/apta/apta_result_builder.h`, `include/apta/apta.h`
- Implementation: `src/core/apta_result_builder_*.c` and internal header
- Result lifetime/storage integration: `src/core/apta_internal.h`,
  `src/core/apta_result_lifetime.c`, `src/core/apta_result_allocation.c`,
  `src/tempo/apta_s4.c`
- Tests: `tests/unit/result_builder_*.c`, initializer/layout/ABI manifests
- Exports: `abi/public-symbols-1.1.*`, `src/core/apta_windows_exports.h`

The exact final commit hash is supplied in the controller handoff because a
commit cannot contain its own content hash.
