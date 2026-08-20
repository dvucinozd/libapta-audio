# Task 3 implementation report

## Outcome

Task 3 extends the unchanged APTA container-v1 envelope with optional `MKEY`,
`MTRD`, and `CONF` sections. Existing sections and 1.0 fixtures retain their
bytes and semantics when the new features are absent. New sections are emitted
in deterministic `MKEY`, `MTRD`, `CONF` order after the existing canonical
sequence and are parsed into ordinary immutable result-owned storage used by
the existing getters and release path.

The wire contract is specified in `specification/APTA-1.1-DJ-SECTIONS.md`.
Every integer is explicitly little-endian, payloads are independently
versioned, reserved bytes are zero, and no public structure layout or pointer
is serialized. The reference bounds are 24 key candidates, 65,536 meter
segments, and 11 per-target quality records. Units and sentinels are specified
for source frames, beat ordinals, cents, confidence, and evidence permille.

## TDD evidence

The initial combined golden/round-trip test was added before production
changes. It failed as intended:

```powershell
ctest --test-dir build-task3-red -R '^apta\.serialization\.dj_sections_roundtrip$' --output-on-failure
```

Result: 0/1; `dj_sections_roundtrip.c:207` expected four directory entries but
the old writer emitted only `WOVR`.

After the writer/reader slice, the malformed test was added before the new
FourCCs were registered with the base parser. Its first run failed 0/1 at line
221: required `MKEY` returned `APTA_ERROR_UNSUPPORTED` instead of the required
`APTA_ERROR_CORRUPT_DATA`. Registering the recognized singleton fixed the
classification. The tool test was also captured RED 0/1 because
`apta_tool_section_is_known("MKEY")` was false before tool registration.

Final focused GREEN:

```powershell
cmake --build build-task3-red --target apta_dj_sections_roundtrip apta_dj_sections_malformed apta_compat_1_0_container_skip apta_tool_dj_sections -j 6
ctest --test-dir build-task3-red -R 'apta\.(serialization\.dj_sections_(roundtrip|malformed)|compat\.1_0_container_skip|tools\.dj_sections)' --output-on-failure
```

Result: 4/4 passed.

## Golden, malformed, and compatibility coverage

`tests/fixtures/dj-sections-v1-combined.apta.hex` is a committed 664-byte
fixture constructed by the independent Python producer in the same fixture
directory. The test consumes the committed file rather than regenerating its
expected bytes. It checks exact directory order, individual payload fields,
combined bytes, getter reconstruction, buffer round-trip, and
writer-reader-writer byte identity. A separate verification compared generator
output with the committed hex and passed exactly. Existing 1.0 golden tests
cover the absent-new-section case and remained unchanged.

The malformed matrix checks every truncation prefix from 0 through 663, thus
covering fixed-header, directory, payload, padding, and final-byte boundaries.
It checks CRC corruption for each new section; oversized counts; `UINT64_MAX`
stored-size overflow; wrong version/required flags; duplicate `MKEY`, `MTRD`,
and `CONF`; duplicate quality targets; invalid key, tuning, confidence, meter,
range, ordering, identity, and segment IDs; non-zero reserved bytes in strict
and permissive modes; missing target-feature dependencies; and unknown
optional sections before, between, and after the new sections.

Reader and writer cross-validate quality targets against parsed source
features and each meter-segment downbeat against any available local/global
grid. Count and checked-size/allocation ceilings are applied before traversal
or allocation. The writer additionally rejects non-zero reserved public fields
and sets the container partial flag when a new payload is not final.

The frozen `tests/compat/1.0.0/frozen_container_consumer.c` links no libapta
code and recognizes only APTA 1.0 FourCCs. It validates the v1 header,
directory, ranges, flags, and CRC, consumes `WOVR`, and proves that all three
new optional sections are safely skipped. Conversely, the installed
conformance/interchange and unchanged 1.0 fixture suite prove that the current
reader still consumes 1.0 data.

## Verification

Static monolithic Ninja build completed successfully. A full run before the
final visibility-only cleanup passed 106/106. After that cleanup, 104/106 tests
passed; the two package consumers initially selected absent NMake only because
`CMAKE_GENERATOR` was missing in that shell. Re-running those exact two tests
with `CMAKE_GENERATOR=Ninja` passed 2/2. Therefore every test in the 106-test
monolithic suite passed on the final tree, including installed conformance,
versioned interchange, old-header/new-library, public layout, header snapshot,
allocation failure, and all existing serialization goldens.

Shared verification:

```powershell
cmake -S . -B build-task3-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-task3-shared --target apta_core -j 6
ctest --test-dir build-task3-shared -R '^apta\.abi\.public_symbols$' --output-on-failure
```

With Espressif LLVM 20's `llvm-readobj` directory appended to `PATH`, the
shared core built and the public-symbol gate passed 1/1. This gate caught and
removed two accidental internal `_s6` exports before completion.

Compile-only ABI probes using Espressif Clang 20 against
`tests/compile/abi_layout.c` passed for `i686-pc-linux-gnu`,
`i686-pc-windows-msvc`, and `riscv32-esp-unknown-elf`.

`apta-inspect` parsed the binary form of the committed fixture and reported all
three new views in JSON. `git diff --check` exited 0. No release version was
changed.

## Files

- Wire format: `specification/APTA-1.1-DJ-SECTIONS.md`, specification and
  file-format indexes
- Writer/reader: `src/serialization/apta_dj_writer.c`,
  `src/serialization/apta_dj_reader.c`, S6 wrapper symbol layering, recognized
  section registry
- Golden/conformance: committed combined fixture, independent fixture
  generator, round-trip and malformed tests, frozen 1.0 consumer
- Tools: `tools/apta_inspect.c`, `tools/apta_tool_common.c`, tool regression
  test
- Build/test registration: root and test `CMakeLists.txt`

## Risks and scope

The fixed reference limits are deliberately conservative and are part of this
version-1 section contract. The frozen legacy consumer proves framing-level
skip safety, not the behavior of every third-party 1.0 implementation. New
analysis algorithms and streaming serialization remain out of scope.

The exact final commit hash is supplied in the controller handoff because a
commit cannot contain its own content hash; the handoff value is read with
`git rev-parse HEAD` after commit creation.
