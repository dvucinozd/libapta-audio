# Energy-novelty onset experiment

Pre-registered 2026-10-22 before implementing or evaluating this candidate.
Baseline: `1876eb34a9169299ece0d908927850e6f6e79670` plus the exact dirty
`novelty-20261022/baseline-source` snapshot and SHA-256 inventory outside Git.
The rejected sparse front end and its recorded 0/360 result remain available.

Hypothesis: block energy relative to its preceding background can separate
transients over continuous audio, where positive sample-amplitude differences
never return to zero. Change only onset extraction; retain sparse-grid period,
spacing (1%), phase (0.10 beat), endpoint and eight-event requirements. Meter
scoring remains unchanged. No tempo prior, missing-beat inference or fallback.

Frozen candidate: quantized absolute PCM squared, summed in source-aligned
256-frame blocks; divide by actual block count. Background is the arithmetic
mean of the preceding ceil(80 ms / block duration) complete blocks. Novelty is
max(energy-background, 0)/(energy+background), or zero for zero denominator.
Select peaks strictly above 0.20 and maximal within ceil(60 ms / block duration)
blocks on each side; equal peaks choose the earliest. Ignore incomplete
background/lookahead contexts instead of padding them with silence. Peak time
is the block start (256-frame resolution); no sub-block interpolation. A
source-start transient may consequently be missed. Preserve this limitation.

Implementation: a separate `experimental-s6-onset-grid` feature and explicit
runtime `novelty` mode, with caller opt-in before Session attachment. Existing
coarse and sparse modes retain their verdicts. Extra energy storage is one u64
per AttackBin: target host size <=24 bytes, <=16 KiB extra per 2048-bin ring.
At most 1024 novelty values (4096 bytes) plus 128 events (1024 bytes) of scratch;
rolling background O(1024), peak scan bounded by sample rate <=768 kHz
(<=361 neighbors per block), plus unchanged event checks. This bounded step is
not a demonstrated P4 deadline; timing/stack placement remain physical gates.
No core heap allocations, unsafe code or new dependency.

Tests before recorded evaluation: constant carrier must not create ongoing
onsets; amplitude bursts over a nonzero carrier must create source-local events;
silence, missing ring identities, partial EOF/context and capacity remain
explicit. Exercise the real Session, no-allocation path and existing sparse
regressions. Synthetic correctness is development evidence only.

Recorded evaluation: the same frozen 60 spent-development canonical WAVs,
first 30 seconds each, original geometry/full-file hashes. Run coarse, sparse
and novelty modes serially with the native diagnostic and canonical development
runner. Keep all windows and reasons, compare exact old mode outputs with the
previous recorded run. No fresh pool or formal holdout access. Verified frame
labels are still unavailable: coverage, changed verdicts and abstentions are
measurable; musical correctness, fixes/breaks and false-lattice rates are not.

Veto: no recorded support rejects grid promotion; any baseline change, source
hash/geometry drift, incomplete drain, lost evidence, capacity loss or allocation
invalidates the run. Confidence stays below 50, all outputs provisional. Any
nonzero coverage remains development-only pending independent labeled transfer.
Do not alter thresholds, spacing criteria or track selection after the run.
No product-provider activation, FINAL result export, release or acceptance claim.

## Frozen outcome: onset extraction advances, grid promotion rejected

All 60 sources and all 360 windows completed in all three modes, covering
86400000 source frames per mode. The private manifest's source rows are exactly
identical to the prior recorded probe. Its 120 coarse/sparse output files are
also byte-identical to the previous run, not merely equal aggregate counts.

| Measure | Coarse | Sparse attacks | Energy novelty |
|---|---:|---:|---:|
| Sources with supported grid | 60/60 | 0/60 | 0/60 |
| Supported windows | 360/360 | 0/360 | 0/360 |
| Meter estimates | 288/360 | 0/360 | 0/360 |
| Insufficient events | 0 | 360 | 0 |
| Irregular event lattice | 0 | 0 | 360 |

Each novelty window reaches the common timing vetoes with at least eight and
at most 128 selected events. This resolves the sparse front end's continuous-
activity collapse, but does not show that those events are musical beats.
Every window fails a retained irregular-lattice check (adjacent spacing,
endpoint support or accumulated phase). No output was promoted, no fallback
supplied missing timing, and the predeclared zero-support veto fires. This
front-end-plus-selector candidate is rejected for grid promotion. All thresholds
and timing vetoes stayed frozen; no rescue tuning followed the result.

All 360 selected verdicts differ from both retained baselines: accepted coarse
windows or too-short sparse windows become explicit rejected windows. Supported
coverage remains zero relative to sparse. Fixes, breaks, musical correctness,
beat level, downbeat and false-lattice rates are unmeasured without verified
frame labels. Coarse coverage is not evidence of accurate musical timing.
Formal acceptance was not run and formal holdouts/fresh inputs were unopened.

The next algorithm axis is beat-lattice selection over multiple acoustic events:
first obtain independently verified open-development beat/downbeat coordinates,
then pre-register a selector that distinguishes beat support from extra/missing
acoustic events and compares tempo families without requiring every onset to
be a beat. Account explicitly for the 256-frame onset quantization. Retain
these frozen failure cases; do not relax their spacing threshold after seeing
them. Stable publication, actual consumer adoption and P4 acceptance remain open.

## Reproduction and software evidence

Build the explicit feature and use the existing canonical development runner:

```sh
cargo build -p libapta-runtime --release --features onset-diagnostics \
  --bin apta-history-diagnostic --locked
python3 tools/apta_1_1_native_history_development.py run \
  --manifest /private/spent-development.private.json \
  --audio-root /private/canonical --analyzer /build/apta-history-diagnostic \
  --output /new/development-results --candidate novelty \
  --source-revision 1876eb34a9169299ece0d908927850e6f6e79670
```

The source revision identifies the commit base; the dirty source inventory is
required to reproduce this local candidate. The default runner still compares
coarse/sparse only. Novelty requires both the build feature and explicit caller
or CLI opt-in. Reports remain provisional JSON and cannot enter FINAL export.

Verification passes 289 default / 322 all-feature workspace tests per debug and
release profile, 45 focused sparse-only tests, all-target Clippy with warnings
as errors, rustfmt and all-feature no-default-library compilation. Six protocol
tests pass with the novelty release binary; the sparse-only build passes its
five applicable cases and explicitly skips the novelty case. Tests exercise
actual continuous PCM through Session, exact source-local phase, unchanged
sparse refusal, constant-carrier abstention, missing history/context and EOF,
identity/privacy, overwrite protection and zero core processing allocations.
The initial synthetic carrier used a constant absolute amplitude and therefore
did not exercise continuous raw attack activity; that fixture was corrected to
an eight-sample oscillator before recorded evaluation. DSP parameters did not
change in response to the fixture failure.

Host AttackBin storage is 24 bytes (previously 16): +16384 bytes per 2048-bin ring.
The named novelty/event scratch arrays total 5120 bytes before compiler frame
overhead. The complete serial 180-process run, including source hashing and file
I/O, took 134.03 seconds wall time, 23.07 seconds user / 19.16 seconds system,
maximum RSS 264660 KiB under WSL. These are desktop batch costs, not per-window
DSP timings, a P4 memory measurement or a stack placement/deadline guarantee.
The full C/oracle matrix and actual consumer probe were not rerun for this
feature-only Rust change; their evidence remains historical.

AddressSanitizer with leak detection passes 60 focused core/history tests and
all six protocol tests with the instrumented native diagnostic binary. Python
and prebuilt std are not instrumented. The measured host control sizes are
MeterHistory 72 bytes and MeterRecord 56 bytes; P4 layout/timing remains unrun.

External evidence: `D:/AI/LIBAPTA/outputs/rust-s6-novelty-20261022/` and
`/home/daniel/.local/share/libapta-audio/rust-rewrite/novelty-20261022/`.
Private source mappings and audio remain outside Git.

- Exact dirty baseline inventory SHA-256:
  `f7593a48eb4f5a66ddf1bf2c026648044f900f81d29284caa889b387958c3154`.
- Frozen candidate source inventory SHA-256:
  `65d49637483352a50dbd934735b2008d089df9bd8e2c73103c410cae616774a1`.
- Aggregate recorded report SHA-256:
  `00c1d0e2748ed70611a2ff7a8daf6fd8c6d54cd5144bd29b646e485c7883aea3`.
