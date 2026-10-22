# Recorded development coverage probe

Pre-registered 2026-10-22, before recorded candidate results. Baseline is
`1876eb34a9169299ece0d908927850e6f6e79670` plus the exact retained sparse-attack
candidate inventory under external `rust-s6-selection-20261022`. DSP code and
thresholds are frozen. Add host diagnostics only; do not tune the selector.

Use the 60 canonical WAVs in the existing, already-open/spent DJ review set.
Freeze exact full-file hashes and canonical geometry before analysis. These are
development inputs, never a new final corpus or untouched acceptance evidence.
Do not inspect the fresh pool or formal ASAP/Ballroom/GiantSteps holdouts.

Analyze the first 30 seconds (or the entire shorter source), at the original
48 kHz stereo PCM16 geometry, in both original-history and explicit sparse-attack
modes. The diagnostic output binds full-file SHA-256, complete source length and
the exact analyzed prefix separately; it is not a serialized native final result.
No re-encoding, threshold adaptation, track reselection or silent file skipping.

Hypothesis: synthetic sparse-attack success may not transfer to dense recorded
music. Measure supported grid windows/frames, meter availability, explicit
abstention reasons and capacity/evidence loss. Compare exact retained baseline
verdicts. No verified machine-readable frame labels have been located, so period,
beat-level/downbeat correctness and false-lattice rates are **unmeasured**, not
zero errors. Do not convert pre-review hints into verified truth.

Veto: stop promotion if the candidate emits no supported grid on all 60
nonempty sources while the original emits support. Independently, any source
hash/geometry mismatch, capacity loss, incomplete drain or changed baseline
verdict invalidates the run. Retain every abstention and failure. Do not rescue
failed transfer by changing thresholds or choosing a favorable track subset.

Resource boundary: serial host execution, two build jobs, input capped at
256 MiB per WAV, PCM decode/queue scratch of 4096 frames, caller storage sized
to the bounded prefix's window count. Desktop input/report storage may allocate;
core processing remains the previously tested allocation-free implementation.
No P4 runtime/placement or playback-provider qualification is implied.

Use the native diagnostic binary for actual Session/history processing and the
repository development runner for hash-bound freezing and paired execution.
Reuse canonical preparation/hash helpers; this provisional diagnostic path
cannot substitute for the frozen FINAL-only acceptance exporter/evaluator.
Private audio, manifests, labels and source mappings stay outside Git.

## Recorded outcome: promotion rejected

All 60 full-file hashes and canonical geometries were frozen before execution.
Both modes analyzed the same first 1440000 frames per source: 86400000 frames
in total (60 separate 30-second prefixes), partitioned into 360 windows.
All 60 paired runs completed, without capacity loss, unavailable evidence or
changed retained baseline verdicts.

| Coverage measure | Original history | Sparse-attack candidate |
|---|---:|---:|
| Sources with any supported grid | 60/60 | 0/60 |
| Supported grid windows | 360/360 | 0/360 |
| Supported frames in analyzed prefixes | 86400000/86400000 | 0/86400000 |
| Windows with meter estimates | 288/360 | 0/360 |

All 360 candidate verdicts are `InsufficientEvents`: fewer than eight separate
active-bin clusters. The frozen zero-coverage veto fires, so this candidate is
**rejected for promotion to recorded-music use**. No DSP parameter or threshold
changed after this result. The synthetic 36/36 result remains a regression
boundary for sparse impulses, not evidence of general musical suitability.

Original coverage does not prove correct tempo, musical beat level, meter or
downbeat. Correctness, false-lattice and confidence-error rates remain unmeasured
here because verified machine-readable frame labels were not supplied. The
formal acceptance evaluator was not run, and this spent set is not fresh evidence.

## Reproducible host tooling

Build the explicit host feature:

```sh
cargo build -p libapta-runtime --release --features history-diagnostics \
  --bin apta-history-diagnostic --locked
```

`apta-history-diagnostic INPUT.wav OUTPUT.json --mode=coarse|attack --frames=N`
uses the actual native Session, decoder, history, meter and exact converter. It
records full input SHA-256 and full-source length separately from the analyzed
prefix, writes no private path, refuses overwrite and retains provisional state.
This JSON is not a trusted native publication, cache, .apta file or FINAL result.

`tools/apta_1_1_native_history_development.py` reuses the canonical dataset hash
helper. `freeze-spent-development` requires the exact count and source inventory;
`run` verifies each input hash/geometry, both output scopes, complete window
partitions, drain/capacity status and identical retained baseline verdicts.
It has no formal-holdout/fresh acceptance mode. Per-source reports are content-
identified; the private input manifest stays outside Git. Five protocol tests
cover exact bytes/prefixes, source changes, privacy, invalid scope/state and
overwrite protection, including actual native binary invocation.

The frozen paired coverage report SHA-256 is
`cbe9cf108bb42d60cd6a12abbab363bc9893c2f5556cba3cf9945529a84678fb`.
Evidence and exact source/binary hashes are external under
`D:/AI/LIBAPTA/outputs/rust-s6-recorded-20261022/` and
`/home/daniel/.local/share/libapta-audio/rust-rewrite/recorded-20261022/`.
Private audio and manifests are not copied into tracked files.

Next algorithm work must pre-register a new temporal evidence front end that
distinguishes musical onsets from continuous positive sample-amplitude changes.
Do not merely lower the sparse event-count/spacing thresholds to rescue these
recordings. Evaluate emitted timing and abstentions on labeled open-development
data before native-result/product adoption; preserve formal holdouts and P4 gates.

## Software verification

The exact host tooling passes 289 default / 320 all-feature workspace tests in
each debug/release profile, all-target Clippy with warnings as errors, rustfmt
and all-feature no-default-library compilation. Five development-protocol tests
pass with the release diagnostic binary and again with the new binary compiled
with AddressSanitizer/leak detection. Python and prebuilt std are not instrumented;
this does not rerun the prior 58 focused core sanitizer tests or qualify P4.

Every DSP source hash still matches the frozen sparse-attack candidate. Native
binary, Python runner, private manifest and per-output hashes bind the recorded
run; both processing source and later verification snapshots are retained. A
canonical-runner `sys` import repair is included in the verification snapshot and
checked separately; it does not change the recorded analyzer/runner. Full C/
oracle interchange and actual product-consumer probes are historical, not rerun
here. The aggregate coverage report intentionally leaves accuracy and
false-lattice rates null. Passing tooling tests reproduce the coverage rejection;
they do not turn it into algorithm acceptance.
