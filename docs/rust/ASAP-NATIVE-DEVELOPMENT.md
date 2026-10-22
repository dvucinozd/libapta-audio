# Native annotated-development baseline

Registered 2026-10-22 before preparation/analysis of this native baseline.
No new selector is authorized by the rejected event-lattice oracle. Preserve
all existing DSP and its exact binary; add development-only preparation and
reference-coordinate diagnostics. Base is `1876eb34a9169299ece0d908927850e6f6e79670`
plus the retained dirty `beat-events-20261022` source inventory.

## Data boundary

Use the existing canonical ASAP v1.2 revision
`afc815c75c42e83a79c03feb6da8a35e77d4c6b8`, selection seed and score-grouped
20 triple / 20 common-time development performances. These are the previously
defined open development partition, not fresh acceptance evidence. Do not
fetch/render/analyze holdout performance MIDI or audio. A filtered no-checkout
Git cache may read the already-defined annotation/partition metadata, then
materialize only the 40 development MIDI files and source/license documentation.
Validate selected Git blob IDs, exact revision and resulting file set.

Extend the canonical preparer with explicit `--development-only`; retain its
legacy default and deterministic renderer. Pin mido 1.3.3 and numpy 2.3.5 in an
external isolated environment. The new output contains development-only labels
and explicit prepared_splits metadata; refuse nonempty output for this mode.
Preserve source-frame beat/downbeat annotations, canonical 48 kHz stereo PCM16
geometry and opaque full-content identities. This is rendered piano stress
evidence, not recorded DJ audio. ASAP derivatives remain private, with original
CC BY-NC-SA attribution/provenance; do not commit audio, labels or source maps.

## Frozen diagnostic scope

Analyze all forty 30-second excerpts with the unchanged native coarse, sparse
and energy-novelty modes via the existing hash-bound development runner. Do not
run or rescue the rejected event oracle. Use the dense reference beat and
downbeat frames, not the DJ set's constant-period extrapolation.

For every fixed source window, declare reference completeness from annotations
alone: its entire [first,end) is between the first/last annotated beats. Report
incomplete boundary windows explicitly. All windows remain in unconditional
denominators; a conditional metric may additionally use all reference-complete
windows, including grid abstentions. Never drop a row based on candidate output.

For each exact Q32 represented beat inside annotation coverage, find the nearest
annotated beat, resolve local period with the canonical nearest-five-interval
median, and apply existing 1% period / 0.10-beat phase tolerances. Confirm a whole
window's period/phase/joint correctness only with complete reference coverage
and every represented beat passing. Separately count scored/predicted beats,
period/phase-correct beats, native meter availability/correctness and downbeat
distance to the nearest annotated downbeat. A non-emission is never a success.
High-confidence wrong grids require complete reference context; report unknown
context rather than claiming safety on it. Keep 3/4 and 4/4 counts separate.

This is a baseline/coordinate probe, not FINAL export, formal acceptance, a
holdout run or a P4 measurement. No accuracy threshold promotes this baseline.
Any source/revision/hash/geometry/cohort mismatch, holdout presence, incomplete
drain, lost evidence or exhausted capture invalidates the run. Freeze exact
tooling/binary/source/label hashes before analysis and retain every outcome.
No DSP parameter, source selection or scoring threshold may change after it.

## Completed preparation and baseline

The filtered Git cache is at the exact pinned revision. Forty development MIDI
files were fetched/materialized; all forty formal-holdout MIDI blob IDs are
absent from the local Git packs. No holdout performance MIDI/audio was fetched,
rendered or analyzed. Annotation metadata was used only for the pre-existing
deterministic partition selection. Source Git blob IDs and SHA-256 values are
retained in an external private provenance manifest.

The canonical preparer now supports `--development-only`, refuses a nonempty
output in that mode, and adds explicit prepared_splits/holdout status. Its
legacy default still prepares both splits with its original manifest shape.
Mock-based tests exercise that legacy behavior without accessing real holdout
material. All 40 actual excerpts are canonical 30-second 48 kHz stereo PCM16:
20 triple and 20 common-time. All labels and source identities match. New
development-only manifest/label hashes are not asserted equal to the historical
combined 80-row manifests; tag, seed, grouping and renderer remain unchanged.

All three unchanged native modes complete all 240 source windows (57600000
source frames per mode), with no capacity loss, incomplete drain or unavailable
evidence. The dense reference covers 160 whole windows; the other 80 boundary
windows are explicitly incomplete from annotation geometry alone.

| Measure | Coarse S6 | Sparse | Energy novelty |
|---|---:|---:|---:|
| Emitted timing / all windows | 238/240 | 0/240 | 0/240 |
| Confirmed period / reference-complete windows | 1/160 | 0/160 | 0/160 |
| Confirmed all-beat phase / reference-complete windows | 5/160 | 0/160 | 0/160 |
| Confirmed joint period and phase / reference-complete windows | 1/160 | 0/160 | 0/160 |
| Predicted beats inside annotations | 2290/2373 | 0/0 | 0/0 |
| Correct phase among scored beats | 632/2290 | 0/0 | 0/0 |
| Correct local period among scored beats | 149/2290 | 0/0 | 0/0 |

Zero-denominator beat ratios are unavailable, not perfect accuracy. The coarse
joint result is also 1/240 confirmed windows unconditionally; boundary windows
are not inferred correct or silently removed. Meter is emitted in 183 windows
and correct in 106. Downbeats are scorable in 169 emitted meter records, correct
in 21. There are no high-confidence grid emissions/errors; this does not offset
the poor timing accuracy or establish a confidence-safety qualification.

| Coarse S6 by reference meter | 3/4 | 4/4 |
|---|---:|---:|
| Emitted grid | 118/120 | 120/120 |
| Joint confirmed / reference-complete windows | 0/80 | 1/80 |
| Correct meter / meter emissions | 34/92 | 72/91 |
| Correct downbeat / scorable meter records | 12/87 | 9/82 |

These are rendered-piano, per-window/all-beat metrics, not the earlier C
one-result-per-track metrics, a DJ transfer claim or formal acceptance. The
rejected event oracle was not run on this corpus. No new selector or DSP change
was implemented. The result supplies a frozen external development regression
baseline for subsequent independently registered work.

## Reproduction and checks

```sh
python3 tools/apta_1_1_asap_meter_validation.py prepare \
  --asap-root /data/pinned-asap --output /new/development --development-only
python3 tools/apta_1_1_native_annotated_development.py \
  --prepared /new/development --analyzer /build/apta-history-diagnostic \
  --output /new/native-baseline \
  --source-revision 1876eb34a9169299ece0d908927850e6f6e79670 \
  --candidate-inventory-sha256 <exact-dirty-tooling-inventory-sha256>
```

Only development-only declarations are accepted before labels/audio are opened
by the native runner. It reuses canonical preparation, identity, geometry,
hash-bound native execution and nearest-five-reference-interval helpers.
Tests cover source-offset preservation, meter, half-tempo aliases, expressive
timing, boundary context, abstentions, holdout refusal and legacy preparation.
Nine preparer and four dense-scoring tests pass. Native processing/runtime and
test-driver source hashes are identical to the previous verified checkpoint;
the binary is exactly unchanged. Its 289 default / 323 all-feature debug/release
tests and 61 focused sanitizer tests remain historical evidence, not rerun for
this Python-only data tooling. No C/oracle/product/P4 matrix was rerun.

The serial 120-process analysis, hashing and dense scoring took 9.80 seconds
wall, 8.78 user / 1.01 system seconds, maximum RSS 24424 KiB under WSL. This is
desktop cost, not physical P4 timing, stack placement or memory qualification.

Private artifacts are external under
`/home/daniel/.local/share/libapta-audio/rust-rewrite/asap-development-20261022/`;
aggregate reports/scripts are under
`D:/AI/LIBAPTA/outputs/rust-asap-development-20261022/`.

- Tooling source inventory SHA-256:
  `370de33dfbe688b31692f3706806885c9460fbbc0012cb10e7eab2fc571570fc`.
- Development-only prepared manifest SHA-256:
  `fcfc551620cd60d85115d19a515ef4507097c854eb7c8fb7eb69cfbc9b1648cf`.
- Dense reference labels SHA-256:
  `d181cf59fbe96442b3f893b66aa9ca56eb5e2c4b609a2c0782a879c0fb67372b`.
- Dense native baseline report SHA-256:
  `bc1fbbbefbbfae387e5bb418452163c9628d40ece8989a9ba074cd9ad47ffe74`.

The subsequent [Ballroom native baseline](BALLROOM-NATIVE-DEVELOPMENT.md) now
brings the frozen real-audio development partition through this same scoring
path. Both external baselines are available. The successor
[dense temporal model](S6-TEMPORAL-MODEL-EXPERIMENT.md) is registered; its first
implementation stage captures dense native evidence and verifies unchanged
outputs. Keep formal holdouts closed, do not rescue the rejected DJ event-support
formula or promote a detector from software tests.
