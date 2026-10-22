# Native dense evidence capture — stage A

Date: 2026-10-22. This is stage A of the
[registered temporal experiment](S6-TEMPORAL-MODEL-EXPERIMENT.md), with unchanged
[frozen parameters](S6-TEMPORAL-MODEL-PROTOCOL.json). It captures native evidence;
it implements no temporal selector, changes no scoring rule and makes no new
musical accuracy, FINAL publication or physical P4 claim.

Base revision is `1876eb34a9169299ece0d908927850e6f6e79670` plus the retained
dirty registration snapshot. The protocol SHA-256 remains
`bf8dc41fcce167b3ed9fb23d08d8106121e1c0e74c2d012d253025d893c9bbf2`.

## Capture contract

Core `experimental-s6-temporal-trace` explicitly implies the existing energy
novelty and event-trace features. Runtime `temporal-trace-diagnostics` adds
`--mode=novelty-dense` to `apta-history-diagnostic`. The ordinary novelty mode
and all other existing modes retain their output shape. The new mode emits
ordinary novelty JSON with one additional `dense_trace` field per source window.
Builds without its feature reject it before opening input.

`DenseTrace` copies the already calculated f32 array in the same native pass,
before peak thresholding/neighborhood filtering. It retains up to 1024 values,
count, known initial-background length and completeness. Coordinates use the
containing source range; JSON explicitly records first bin, 256-frame spacing
and final partial-bin length. Initial bins without the native 80 ms background
are declared startup zeros, while missing source/ring context clears capture
and marks it incomplete. A complete dense array remains complete if later
sparse peak extraction hits its independent 128-event limit.

The exporter converts each native f32 to f64 before decimal formatting, allowing
a host JSON decoder to recover that exact native value. The canonical validator
checks exact f32 representability, finite [0,1] values, count, coordinates,
startup zeros, completeness and final-bin geometry. An implementation review
corrected shortest-f32 decimal export before any real capture; those initial
prechecks are preserved under `pre-json-fidelity/`, followed by verification
of the corrected exact-source snapshot. No detector arithmetic changed.

`DenseTrace` is 4104 host bytes, below the registered 4128-byte cap. A selection
record with both event and dense traces is 6224 bytes, versus 2120 with only
events and 64 without trace features. The extra size is 4104 bytes per opted-in
record. Default processing layouts do not grow. Existing named novelty/event
scratch arrays remain 4096 + 1024 bytes; these source-level sizes do not prove
compiler stack usage, PSRAM placement or physical P4 resource suitability.
The actual Session allocation counter covers enabled dense capture.

## Reproduction and evidence scope

```sh
cargo build -p libapta-runtime --release --locked \
  --features temporal-trace-diagnostics --bin apta-history-diagnostic
python3 tools/apta_1_1_native_dense_development.py \
  --prepared /data/existing-development-only-preparation \
  --analyzer /build/apta-history-diagnostic \
  --baseline /data/retained-native-analysis/report.json \
  --expected-baseline-sha256 <registered-baseline-report-hash> \
  --output /new/dense-capture \
  --source-revision 1876eb34a9169299ece0d908927850e6f6e79670 \
  --candidate-inventory-sha256 <exact-dirty-source-inventory-hash>
```

The dedicated tool accepts only explicit development-only preparations before
native execution, binds labels/manifest/cohort to the retained annotated report
and preflights every retained native output hash. It reuses canonical geometry,
source hashing and paired-mode execution. It performs no label scoring or
candidate selection. Output identity must match every retained ordinary file;
removing only `dense_trace` must reproduce ordinary novelty exactly. Incomplete
dense evidence, source drift or overwrite attempts fail closed.

Use only the existing forty ASAP and forty Ballroom development sources and
their registered prefix/dense-reference hashes. Preserve original full-source
identities and prefix geometry, including the short Ballroom excerpt. No new
preparation, conversion, selection, holdout audio or fresh DJ input is involved.

Private source snapshot, inputs, raw native traces and manifests are retained at
`/home/daniel/.local/share/libapta-audio/rust-rewrite/temporal-trace-20261022/`.
Aggregate reports, scripts and logs are at
`D:/AI/LIBAPTA/outputs/rust-s6-temporal-trace-20261022/`.

## Completed comparison and verification

All eighty already-open development sources complete all four native modes
without capture loss, capacity exhaustion or incomplete drain. Every one of
the 240 ordinary coarse/attack/novelty files is byte-identical to its retained
baseline. Stripping only the dense field reproduces ordinary novelty for all
eighty extra files and all 480 windows. No holdout audio is opened and no source
is converted or selected again.

| Evidence captured | ASAP development | Ballroom development |
|---|---:|---:|
| Sources / source windows | 40 / 240 | 40 / 240 |
| Complete native dense bins | 225000 | 224991 |
| Positive values <=0.20 retained | 58805 | 51007 |
| Explicit startup bins | 600 | 600 |
| Unchanged ordinary files | 120 | 120 |

The 449991 total bins include weak information absent from a thresholded peak
list; this proves preservation, not that it identifies musical beats. The
unchanged outputs retain the previous coarse joint results (1/160 ASAP and
9/165 Ballroom complete-reference windows), empty sparse/novelty timing, and
the previous confidence limitations. No selector accuracy, fixes/breaks or
confidence improvement is measured in stage A.

Verification of the final exact source passes:

- 289 default / 328 all-feature workspace tests in each debug and release;
- 47 non-trace onset and 48 event-trace-only focused tests, preserving layouts;
- 66 focused native AddressSanitizer tests with leak detection;
- 23 Python release checks: seven history protocol, five dense protocol, five
  frozen event-oracle regressions and six unchanged dense-scoring regressions;
- seventeen protocol/oracle checks with instrumented native execution;
- rustfmt, all-target/all-feature Clippy with warnings denied, and all-feature
  no-default-library compilation; the actual Session allocation counter is zero.

Tests cover subthreshold values, silence, partial source bins, lost ring
context, source offsets/wrap, independent peak overflow, capture capacities,
chunk independence, atomic nonfinite-input rejection, cancellation, a fresh
source, immutable retained records, exact f32 export, unsupported builds,
holdout/identity refusal and overwrite protection. Python and prebuilt standard
libraries are not sanitizer-instrumented. Full C/oracle and actual product
consumer matrices are not rerun by this checkpoint.

Serial hash verification, 320 native processes and capture checks take 26.54
seconds wall (23.38 user / 2.39 system), maximum RSS 25732 KiB under WSL.
This includes four modes rather than the earlier three; it is aggregate host
cost, not a paired native processing-overhead or physical P4 benchmark.

Exact identities:

- Candidate source inventory SHA-256:
  `5932bdc89af91e8a69899f395a302ad31c64f3e7055fbaeb0016d8b40f5bc134`.
- Dense-enabled analyzer SHA-256:
  `b28672d90609b58b09fb10445483ad305628c9eb377910046ea9fae74f1d4d09`.
- Original retained analyzer SHA-256:
  `a92dd17ec68206306c487a39f53102cf1ceb3be660c7135d322b1d162173eeef`.
- ASAP capture report SHA-256:
  `459fc15bb7ce1df5eb1725a7e8b9328580474011650cab83b21f45372d60f736`.
- Ballroom capture report SHA-256:
  `7cb0d72b81706a095b09b1bf2349918a6c150ab4736fd9c128e29528f69907cb`.

Machine-readable aggregate results are in external `report.json`, paired
`*-capture-report.json`, `capture-summary.json` and the retained logs. Source
revision, dirty inventory, protocol/data/binary hashes and per-output identities
are independently checked before completion. Changes remain local on
`rust-rewrite`; nothing is committed, pushed, released or adopted by a consumer.

## Next boundary

The subsequent [stage B checkpoint](S6-TEMPORAL-HOST-CHECKPOINT.md) implements
the registered label-blind recurrence and synthetic tests. Its known synthetic
boundary alias remains documented. Stage C is still unrun: the ordered
ASAP-then-Ballroom attempt must stop on the first failed gate. The rejected
event oracle, scorer tolerances and registration stay frozen. Production
provider, consumer pin, stable version and eight-segment wire limit remain
unchanged; native grid/meter adoption and physical P4 evidence remain open.
