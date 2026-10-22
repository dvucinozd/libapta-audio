# Continuous-guided quantized construction — preflight

Date: 2026-10-22. Registration was committed/pushed at
`93b6fe11ae0eeee542c1a10226969aa2915cec03` before code or corpus measurements.
The [fixed protocol](QUANTIZED-GUIDED-PROTOCOL.json) has SHA-256
`2f5287b6e36742348ba9a012694c26fe6c495ce17278d146cc525d651b29be5f`.

`tools/apta_1_1_quantized_guided_reference.py` computes exactly one integer
anchor per original ordinal from the saved continuous path and exact domain.
Strict endpoints are respected; rounding uses nearest integer with upward ties,
clamped to the integer domain. No available integer refuses the family. Exact
period/phase/ownership/count/junction intersection and integer-tempo inversion
then produce contiguous candidate pieces. Backward dynamic programming finds
the minimum count within this fixed anchor family, with longest-first ties.
All points/ordinals and original S6 membership are independently checked after
expansion. Complete paths are retained before the eight-segment wire test.

All 75 feasible sources will undergo the guided-family test. Existing native
witnesses remain explicit, unchanged controls in the separately reported union;
they are not counted as new gains. The runner replays all old anchored and
continuous plans and old scores, and requires identical legacy native responses
for all 47 previous witnesses before accepting the study.

The existing host `segmented_reference_probe` adds `SEGMENT_GUIDED_V1` with an
explicit integer anchor per piece. Legacy `SEGMENT_REF_V1` uses its original
reference anchors through the same guarded builder. New inputs retain the
original reference envelope, ordinal, phase, period, ownership, count and actual
successor checks; strict/canonical GGRD, complete native/builder with MTRD and
meter roundtrip/grid lookup remain mandatory. Python separately checks original
window ownership. No library/native validator, limit, DSP or production change.

Mechanical contradiction classification reads only the existing bounds and
certificates. First source-order representatives stay private. Nominal-range,
geometry-domain and chain incompatibility are not listening judgments or a
reason to change frozen tolerances/annotations.

Verification passes **112 Python tests**: 12 new constructor tests plus all 100
previous tests. New tests cover strict integer-domain rounding, unavailable
integer anchors, corrupted saved points, explicit relaxation/family failure,
small exhaustive partition minima/ties, fractional meter binding, versioned
request layout, source/window membership and the three mechanical categories.
A generated continuous-feasible path with no solution in this guided family
is retained as a negative case without searching other anchors.

The host Rust probe passes **11 tests** plus 26 unchanged builder/grid/native
tests in each debug/release profile: **37 per profile**. Added controls exercise
a shifted explicit anchor with fractional positions and complete meter/native/
builder/wire acceptance, escaping anchors, actual-junction failure, and equality
with legacy points. Existing synthetic nine-segment native-only rejection remains.
Three generated Python-to-Rust fixtures pass exact points/ordinals and complete
native/builder/wire/meter checks through legacy and new input formats.

Rustfmt, focused warnings-as-errors Clippy, no-default-features library check
and release probe build pass. Full Rust/C/sanitizer/CI/P4 matrices are not rerun
or claimed for this host-only addition. All graphs remain provisional with
zero uncalibrated confidence and pending revision. No FINAL/product adoption.

Script, synthetic bridge and verification log are external under
`D:/AI/LIBAPTA/outputs/rust-quantized-guided-20261022/`. Build outputs remain
under the private WSL `rust-rewrite/quantized-guided-20261022/target/` directory.
Commit/push before the single clean-source, fixed 100-source run. Close this
reference-construction sequence after recording its result, then return to
label-blind saved learned events; no alternate reference-anchor optimizer follows.
