# HS-06 probability bridge implementation checkpoint

Date: 2026-10-27. Registration/source parent:
`48ca634a67bebaf9ae9ca5aa9f14bf0a6469846d`.
[Protocol](PRODUCT-HOST-PROBABILITY-PROTOCOL.md),
[machine receipt](PRODUCT-HOST-PROBABILITY-IMPLEMENTATION.json).
Status: **IMPLEMENTATION_PREFLIGHT_PASS; CLEAN_FOUR_CASE_RUN_PENDING**.

The opt-in `experimental-compact-probabilities` feature adds the registered
caller-buffer adapter, fixed synthetic CLI and independent Python checker. It
enables existing grid/HMM features and adds no dependencies or default behavior.
The six implementation paths match the registration. Old runtime kernels,
core/wire/consumer, frozen protocols/assets/results and VERSION1.0.1 are unchanged.

Input is named `[beat,downbeat,nonbeat]` f32, validated with the inclusive2^-21
ordered F64 sum guard. Output is `[ln(nonbeat/15),ln(beat),ln(downbeat)]` F64,
with promotion before separate division/log and exact -Inf for either signed
zero. No clipping,epsilon,renormalization or substitution. Five charged phases
prepare/commit each row; Finalize is one unit. Invalid acceptance is charged but
does not consume the bad row. Work-cap refusal precedes the next unit's writes.
Only Ready/NoEvidence expose immutable full arrays and final global support.
Partial rows/support remain private; cancellation/failure expose no result or
resume. No heap,I/O,callbacks or retained source references occur in Session.

Host Session is144B,alignment8; private probability/density payload36B.
Driver is960B,alignment8,including768B input chunk. Materialized outputs remain
360000B f32 probabilities plus720000B F64 densities at30000rows. These type and
array facts are not actual stack/allocator/whole-pipeline peaks or P4 placement.
Graph numeric payload is recorded from immutable slice lengths; it is not a
measurement of private graph Vec capacities or allocator residency. Workspace
and event scratch payloads are read from their actual allocated capacities.
The separate compiler-linked layout probe records144/8/960/8 in
`D:/AI/LIBAPTA/outputs/hs06-layout-probe-20261027/`;it processes no input rows.

Both Score passes borrow the identical retained support slice. The pure selector
preserves finite ties in favor of3 and returns NoPath for two impossible scores.
Winner Trace preserves Score bits and replay evidence hashes. Its decoder and
entire Workspace remain alive while HS02 consumes the borrowed path together
with full-source probabilities and global support endpoints. GridReady uses
unchanged native/wire validation. Returned event/wire copies own their storage;
tests reuse probability/density arrays after all borrowed owners are gone.

Executed checks:

- Eight integration tests plus one new internal cap test pass debug/release.
  All35 registered cases run20 schedules each (700/build),matching frozen
  numeric/status/support/work expectations and exact candidate schedule bytes.
  All11 selector cases pass. Eight small compositions run both HMM quotas,
  preserving base/shifted scores/path bits,global rows/phase/native grid and
  explicit NoPath/UnsupportedGrid/NoEvidence. Input/tail canaries and failed
  private prefixes are checked after the Session borrow ends.
- Geometry/capacity/offset/oversupply/input starvation,each-phase EOF/cancel,
  invalid values in every column,quota0 and terminal priorities,input reuse,
  absent partial results and no resume are covered. A maximum-geometry test
  processes only138 accepted/137 committed rows through unit686,then proves
  pending cancellation before another unit or buffer write. It does not run a
  full30000-row candidate or measure the registered cancel case.
- Two compile-fail doc tests prove a view cannot outlive its Session or coexist
  with a mutable Session operation while still in use. New CLI refusal tests
  reject arbitrary files/models/case names/extra arguments before output.
- Unchanged release controls pass:HS01 twelve integration plus one internal;
  HS02 seven plus one;HS03 six;HS04 seven plus one;HS05 five plus two;
  product-grid twelve;default-context five. One C-context-oracle test is
  ignored/NOT_RUN because its external oracle is unavailable.
- fmt,focused Clippy(-D warnings),release CLI and Python compilation pass.
  All395 frozen protocol bindings remain byte-identical. Existing registration
  documents/verifier retain their historical registration-only status;current
  candidate execution is recorded here and in the mutable product status.

Final development preflight B verifies35 observations,850 complete result rows
and8 composition records,through859 records/67462 transient bytes. Accepted
rows including invalid-case prefixes total864;work4353 units. The checker uses
both the scalar oracle and frozen short values. Three valid transport probes
are accepted;17 malformed bit/numeric/row/event/JSON/trailer variants are refused.
All four wall/RSS/address-space/output violation probes are refused with the
unchanged neutral supervisor. Parent537 bindings and all135 worker runtime
entries are checked before/after the fixture worker. Complete waited worker+
descendant CPU is2.891666s,below30s;all wall/RSS/AS/artifact gates pass. This is
development preflight with dirty source plus exact six-component hashes,not the
registered clean four-case experiment or full-length timing evidence.

Session.step,composition and row/event packing/writes have separate clocks.
Session construction excludes the caller's prior Vec allocations;whole worker
wall/CPU supervision includes them and all input generation/hashing.
The latter excludes final JSON/trailer flushing;checker pipe wait and whole
supervisor wall include the remaining process lifetime. Worker SELF+waited
CHILDREN CPU is recorded;parent RUSAGE_CHILDREN deltas additionally gate the
complete waited worker and its waited descendants after exit,including final
receipt/runtime work. Child HWM includes fork/exec and is not isolated Rust RAM.
Cancelled/failed prefix density hashes are diagnostic metadata only:there is
no emitted density view to certify them. Rust tests check private prefixes;
input/probability prefix bits and counters are checked independently.

External evidence:
`D:/AI/LIBAPTA/outputs/hs06-implementation-preflight-20261027-b/`.
Its exclusive artifact manifest binds23 files before the manifest itself;
24 files/358016B including manifest are below1MiB. Preflight A is retained beside
it as earlier development evidence;B matches the final six component hashes.
Before B,coverage was completed for the internal cap/registered pending prefix,
separate timing fields and complete waited CPU accounting. No registered input,
oracle,tolerance,resource cap or earlier verdict was changed.

Next:commit/push this implementation,then use a clean exact source,new external
release build/binary and exclusive output for exactly four cases in order:
fixtures,full-mixed,full-no-support,cancel.

```text
python tools/apta_compact_probabilities_synthetic.py --executable NEW_BUILD/apta-compact-probabilities-synthetic --output NEW_EXTERNAL_OUTPUT
```

The runner binds source components,protocol/control/reference/assets,actual
compiler/Cargo/feature,executable/linked libraries and Python source/cache/runtime
before cases,and rechecks all after each case. Keep CPU30s,wall45s,RSS64MiB,
AS128MiB and aggregate artifacts1MiB. Preserve failures;no subset/overwrite,
tolerance/cap/backend rescue. Audit/write a separate result and commit/push
before any later scope. Full30000-row candidate cases and measured pending
cancel remain NOT_RUN at this implementation checkpoint. No trained model,
music/labels,training,device,provider or release ran. Rights/provenance,full-size
graph parity,PF01 exact-board budgets and product PF04 remain open.
