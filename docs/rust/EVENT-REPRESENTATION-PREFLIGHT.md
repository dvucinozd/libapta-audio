# Saved-event audit implementation and preflight

Date: 2026-10-22. Registration was committed and pushed at
`f71c418c944f7ebf7b04b49f4d54ce33614eb55a` before implementation or new audit
scores. The [protocol](EVENT-REPRESENTATION-PROTOCOL.json) is unchanged.

`tools/apta_1_1_event_representation.py` compares saved event coordinates with
the unchanged OLS control and frozen scorer, adds separately named common
point/interval and one-to-one coverage diagnostics, compresses equal integer
intervals exactly, and binds meter to original event ordinals. It verifies all
prior input/report hashes and refuses a dirty source tree or existing output.
Only the already-open development artifacts are read; no model is executed.

The host-only Rust example `event_representation_audit` exercises existing
payload writers, strict readers, meter/grid binding and complete-result
validators without modifying them. Explicit events and compressed segment
expansion must preserve exact coordinates and ordinals. More than eight wire
segments is an explicit rejection, never a truncation. Unsupported segment
tempo ranges are reported; no clamping or fabricated beats are allowed.

`tools/probes/event_representation_consumer.rs` is compiled in an isolated
external Cargo project against the actual unchanged M1 adapter. Only that
project overrides libapta's dependency; the consumer checkout and production
pin remain unchanged. The positive constant-grid fixture first passes native
validation, then converts to playback beats. Provisional graphs yield tempo
only with an untouched output bank. Known gaps and multiple meters pass native
structure validation but reject in the consumer. Capacity, pending revision
and duplicate-coordinate rejection leave destination storage unchanged.
Variable explicit intervals fail native validation before adoption.

Initial synthetic fixtures used the invalid stereo layout value 3. A positive
constant-grid native/builder control exposed this fixture error; it is corrected
to layout 2 before any saved-event scoring. The validators were not changed.
Negative cases alone are insufficient evidence that a fixture is meaningful.

Verification passes ten new Python representation tests, ten unchanged neural
projection/provenance tests, and six unchanged annotation/scorer tests. The new
Rust example has four tests and the focused existing suites contain 26 tests
(builder 10, grid 7, native validation 9); all thirty pass in debug and release
with warnings as errors. Rustfmt, focused Clippy and no-default-features library
compilation pass. The actual consumer probe passes eight synthetic policy
cases. This is focused host evidence; complete DSP/platform/P4 matrices and
sanitizer runs are not claimed for this host-only addition.

The lightweight representation suite is added to `rust/check.py`. No new core
dependency, feature, processing path, storage or allocation is introduced.
External build products, consumer Cargo manifest/lockfile and verification log
are under `D:/AI/LIBAPTA/outputs/rust-event-representation-20261022/` and
`/home/daniel/.local/share/libapta-audio/rust-rewrite/event-representation-20261022/`.
Commit and push this checkpoint before running the saved-event audit from the
clean exact implementation. No musical or production conclusion is inferred
from these preflight tests.
