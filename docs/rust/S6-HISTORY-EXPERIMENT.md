# Bounded native S6 window history — 2026-10-22

Experimental development work, not a new transport grid or accuracy acceptance.
Baseline is `3d757c65bd29fb66b561cac4640a52dbf687c87d` plus the previously
verified frozen-energy patch SHA-256
`e9cc71be3e43879569cac0da1759cd3b33e41b5e88de2282b4763fee303b043c`.

## Hypothesis and fixed scope

Preserving source-aligned window verdicts before resident evidence is evicted
can retain an auditable prefix through multiple ring turnovers. Do not merge
published grids from unrelated revisions or infer timing through rejected or
missing evidence. Reuse the existing S6 estimator; change no selection rule,
threshold, numerical backend, wire format or C contract.

The explicit Cargo feature is `experimental-s6-history`. A borrowed
`WindowHistory` control attaches to a sequential `Session` before input, after
global analysis is enabled. The caller supplies a nonempty record slice. The
control supplies 128 fixed scratch floats; a feature-enabled global analyzer
adds one optional borrowed pointer. The feature is absent by default. Expected
cost is less than 1 KiB of control and at most 64 bytes per record, with no heap
allocation or record growth during input/processing. One extra estimator window
is one cooperative processing step. The host measurements do not qualify P4.

## Evidence and no-regression veto

Use only repository-generated PCM and stage fixtures. Require unchanged default
Rust/C results, immutable earlier records, source-coordinate order, exact bounded
budgeting, explicit capacity/lost-evidence diagnostics, allocation-free core work,
and successful default/feature checks and sanitizer coverage. Stop for any
default regression, implicit gap filling, overwrite of retained records, hidden
allocation, or resource ceiling failure. No recordings or holdouts are opened.

## Window and lifetime contract

- Inspect absolute 128-bin windows from frame zero exactly once. A resolved EOF
  may close the final partial window. A partial EOF bin follows the estimator's
  existing count rule; fewer than 64 available bins is `TooShort`.
- Record accepted estimates, rhythmically rejected windows, unavailable evidence
  and short tails separately. A window with missing/replaced identities cannot
  become accepted by substituting zeros. Rejected/short/lost ranges are retained.
- Preserve each accepted window's anchor, tempo and confidence as estimated at
  inspection. Later windows cannot retime or average earlier windows.
- Record capture uses source bins, not previously published grid snapshots.
  Copies made into independent caller storage survive writer destruction.
- Full capacity stops record capture; existing records remain intact. Normal
  analysis continues. `capacity_exhausted` and unfinished inspection stay visible.
- The integration is sequential only. A repeated/reordered/skipped input offset
  invalidates further capture and exposes `discontinuous_input`.
- `finished` means every input frame has an inspection record, including bad
  ranges. It does not assert full supported timing or musical correctness.

## Deliberate boundary and next work

The history is a native diagnostic view, separate from existing immutable result
graphs, GGRD/REVN, meter and transport authority. This does not activate a product
provider or export a whole-track authoritative grid. A later bounded conversion
must define segment capacity, gaps, ordinals, stable-range revisions and meter
binding before it can publish such a grid. Owning/sparse history and physical P4
placement/timing remain separate work. Existing C compatibility remains intact.

Verification commands (use external target/build directories and two workers):

```sh
cargo test -p libapta --features experimental-s6-history global_history
cargo test -p libapta --features experimental-s6-history --test global_history
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo check --workspace --lib --all-features --no-default-features --locked
python3 rust/check.py --build-root /external/build/path --jobs 2
```

## Verified host result

Rust 1.95.0 / GCC 13.3.0 on WSL Ubuntu x86-64, two build/test workers:

- Default combined matrix passes 123 C tests, 289 ordinary Rust tests and 58
  external-C groups in each debug/release profile, formatting, Clippy, no-default
  library compilation, 34 WAV interchange and two all-feature CLI cases.
- Feature checks pass all-target/all-feature Clippy with warnings as errors and
  no-default library compilation. The feature matrix passes 36 tests in each
  debug/release profile (32 library, three public history, one allocation test).
- The explicit large-PCM release test passes known and unknown duration using
  33,816,576 frames at 8 kHz. All 129 source-aligned windows remain recorded from
  frame zero, while the ordinary resident global view starts after zero. This
  checks history retention, not whole-track transport or estimator accuracy.
- Stage tests preserve 384 records through three full S6 ring turnovers, with
  rejected islands retained. Short tails, eviction, finite capacity, invalid
  attachment/input, cancellation, zero/one-step work and clock yielding pass.
- AddressSanitizer with leak detection passes the same 36 feature tests. The
  large-PCM test is separately executed in release, not under the sanitizer.
  Prebuilt std and the C oracle are not sanitizer-instrumented by this command.
- On this host the control is 560 bytes and a record 40 bytes. The attached
  all-stage session test observes zero allocations across construction and
  processing after caller storage is prepared. Default builds exclude the
  history field/module; no P4 layout, stack or timing budget is claimed.

Raw logs and the hashed local source inventory are retained outside the repository
under `D:/AI/LIBAPTA/outputs/rust-s6-history-20261022/`. The tested source is the
baseline above plus the recorded local source changes, not a new commit or
release. Formal accuracy and physical hardware gates were not run.

## Provisional segment conversion continuation

The next bounded slice uses `history_grid::requirements` and `convert_into` under
the same explicit feature. Baseline is the previous history report/source
inventory (inventory SHA-256
`ccb5019856b749c615a4de9e471c620de33a1bd262c283ce4417d2d9e8004840`).

Hypothesis: a deterministic two-pass conversion can preserve every accepted
window's original anchor/tempo/confidence and append-stable segment identity,
while exposing exact supported ranges without inventing timing for gaps. It
adds no detector or processing state, allocates nothing and requires one segment
and one range per accepted record in caller storage. CPU cost is linear in
record count; beats are expanded on demand with the existing checked accessor.
Stop for hidden allocation, prefix retiming, unrepresented gaps, changed default
output, out-of-range beats, or any output mutation on failed conversion.

Conversion validates the whole public history view before writing. Invalid input
or insufficient output capacity leaves both buffers untouched. Extra buffer
capacity is untouched. Accepted windows remain separate; rejected/unavailable/
short windows have no segment. Records must partition the inspected prefix in
source order; the final partial record requires resolved EOF. Uninspected source
tails and capture exhaustion stay explicit in the returned view. Empty or fully
unsupported histories produce empty segments, not fallback timing.

Each segment is provisional with the provisional-phase flag, even after EOF.
Its ID is the source window index plus one, so IDs preserve rejected gaps.
Q32 period is derived from sample rate and the original millibpm with integer
division; authoritative beat count includes exactly positions before the window
end. Ordinals consecutively enumerate **represented** beats; they do not infer
the number of beats in a gap and must not be used as bar/downbeat phase.

The caller supplies a nonzero revision in the source/history namespace. The same
revision preserves identical field values in earlier segments as records
are appended. Reinterpreting earlier records requires a new explicit revision;
conversion never applies Session GGRD/REVN proposals or mutates prior copies.
Returned slices borrow only independent output storage, not input history.

This native graph may contain more than eight segments. The existing eight-
segment GGRD wire limit remains intact: the converter does not truncate, merge
or serialize this graph. No product transport, meter binding or stable-grid
acceptance is implied. These require subsequent explicit integration evidence.

Conversion verification on Rust 1.95.0 / GCC 13.3.0 / WSL x86-64 passes the
complete default matrix (123 C tests, 289 ordinary Rust tests and 58 external-C
groups per debug/release profile), 41 feature tests per profile, 41 feature
AddressSanitizer/leak tests and the explicit large-PCM release test. The real
129-window histories are converted and every represented beat is expanded and
checked inside its exact segment range. Five converter tests additionally cover
independent Q32 arithmetic, gaps, prefix/revision retention, input destruction,
malformed input, untouched output on failure, capacity, short EOF and a graph
larger than the wire limit. The existing allocation-counter test includes actual
conversion and beat lookup with zero allocations. Default retained smoke outputs
remain byte-identical to the earlier baseline.

Exact local source inventories, commands, logs and hashes are external under
`D:/AI/LIBAPTA/outputs/rust-s6-grid-20261022/`. This is host software development
evidence; no musical recordings, formal acceptance corpus, physical P4 run,
runtime budget or product transport acceptance is claimed.

## Mechanical meter binding continuation

Baseline is the prior provisional-grid source inventory, SHA-256
`a1ababa6f54ef82e655d5131659aa991db5604573888e06df58ae98159a2a826`.
The same explicit feature now offers `history_grid::bind_meter_into`.

Hypothesis: source-scoped meter labels can be mechanically associated with a
chosen grid revision without rebasing ordinals, rounding coordinates or bridging
unsupported evidence. This is validation/association, not a new meter or phase
estimator. The caller supplies one bounded output record per label; no mutable
processing state or allocation is added. Work is linear in grid/label count,
with checked constant-time beat lookup and two validation/copy passes.

Veto: stop for inferred/rebased phase, acceptance through a gap/window boundary,
changed default behavior, output mutation on failure, implicit state promotion
or any allocation. Tests use only synthetic fixtures and existing generated PCM.

- The caller provides the expected nonzero source/history revision. A mismatch
  is a conflict. Meter labels must already use that grid's ordinal namespace.
  Legacy/local ordinals are never silently rewritten to the new enumeration.
- Validate the entire public grid before binding, including exact period/count,
  segment IDs, revisions, states, range arrays, totals and ordinal progression.
  Externally constructed inconsistent views cannot bypass validation.
- A meter range must fit wholly inside one supported segment. A range across
  two independently estimated windows is a conflict even when those windows
  are adjacent. A label cannot authorize timing in a gap or uninspected tail.
- Its frame and ordinal must match one actual beat. The whole frame is compared
  under the MTRD rule; the full Q32 position is retained separately. No rounding
  tolerance, nearest-beat lookup, downbeat search or bar-phase continuation.
- Validate ordered nonoverlapping label ranges, increasing IDs/downbeat ordinals,
  meter shape and confidence (including the existing unknown value 255).
- All validation precedes capacity checking/copying. Any failure leaves the
  entire output unchanged; spare capacity is untouched. Output owns copied data
  and survives destruction of input grid/labels. It records grid segment/revision.
- State is capped at provisional; partial input stays partial. A matched label
  does not prove musical correctness or make the grid stable.

The actual 320000-frame local meter fixture spans beyond the first supported
global window. Its binding must still reject, preserving previous output.
Successful label fixtures are explicitly synthetic, never presented as classifier
acceptance. This preserves the outstanding local/global algorithm mismatch.

Source identity and the expected revision travel in the caller's source-scoped
owner; this borrowed graph does not authenticate provenance or detect a caller
mixing two sources with the same revision. No product owner, MTRD/GGRD export,
stable publication, provider activation or hardware gate changes here.

Binding verification passes 289 default and 308 all-feature workspace tests in
each debug/release profile, all-target Clippy with warnings as errors, no-default
library checks, the large-PCM release history/conversion case, and 46 focused
feature tests under AddressSanitizer with leak detection. The allocation-counter
test includes synthetic successful binding and transactional rejection of the
actual local-meter result. Five new binding tests cover Q32 preservation,
partial/provisional state, revision conflicts, gaps and adjacent-window crossings,
inconsistent public grids, malformed labels, capacity and retained-copy lifetime.

The previous full C/oracle/CLI matrix remains historical baseline evidence; it
was not rerun for this feature-only binding slice. No C or default estimator
source changed. Raw commands, exact tested/local source inventories and logs
are under `D:/AI/LIBAPTA/outputs/rust-s6-meter-20261022/`. Classifier accuracy,
runtime/P4 measurements, stable transport and product integration remain open.

## Agreed continuation checkpoint — 2026-10-22

The next development unit is the complete PCM-to-consumer path, starting with
actual meter/downbeat evidence on the selected history-grid lattice. The
[canonical next-step handoff](MIGRATION.md#agreed-next-step--resume-after-2026-10-22)
defines its order, completion criteria and remaining qualification gates.
Mechanical binding remains a validator; successful synthetic labels must not
substitute for that evidence path.
