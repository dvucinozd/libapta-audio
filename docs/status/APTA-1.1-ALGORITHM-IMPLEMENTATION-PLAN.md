# APTA 1.1 algorithm and release implementation plan

- **Plan baseline:** `4f21c2c2e1d03bb16686671a368f273a034b403e`
- **Development branch:** `1.1.0`
- **Plan status:** active
- **Stable package authority:** `1.0.1`; do not bump version metadata while
  any frozen 1.1 blocker remains open

## Objective

Close the remaining APTA 1.1 release blockers without tuning against formal
holdouts or reusing the rejected 60-track corpus as fresh acceptance evidence.
The engineering order is beat-lattice evidence, downbeat/bar phase, musical
key, integrated candidate qualification, one-shot holdouts, a new final corpus,
physical ESP32-P4 evidence, and release freeze.

Tempo/grid ensemble and calibrated BPM confidence are already accepted. Meter
already passes the rejected final corpus gate. The unresolved algorithmic
deficits are key accuracy and beat-lattice/downbeat phase.

## Non-negotiable boundaries

- The frozen final gates and correctness definitions do not change after
  observing candidate results.
- The existing 60-track musician-verified corpus is development evidence only.
  Its official rejection remains immutable.
- Ballroom and ASAP development partitions may be used during design. Their
  formal holdouts stay unopened until the complete candidate is frozen.
- A future final acceptance corpus contains at least 48 newly and independently
  verified tracks not used for candidate selection.
- Every experiment starts with a recorded hypothesis, exact baseline revision,
  build flags, evidence source, resource expectation, no-regression veto and
  stop condition.
- Experimental code remains opt-in. Default builds and stable 1.0 behavior must
  remain unchanged until promotion is justified.
- Do not grow `apta_internal_onset_bin_t` beyond its 16-byte ceiling. New
  temporal evidence must use bounded scratch/state or derived evidence so the
  dominant S6 allocation does not spill from the intended memory class.
- Private audio, filenames, paths, mappings and staging labels never enter the
  repository.

## Baselines to preserve

The official rejected 60-track result is key 15/60, meter 58/60, downbeat 5/60
and beatgrid 4/60. Subsequent development baselines are:

- KP profiles plus log-compressed key evidence: 20/60;
- opt-in harmonic projection: 22/60, two fixes, zero breaks and zero
  high-confidence key errors;
- opt-in three-band downbeat: 7/60, two fixes and zero breaks;
- cached S4 top-three period oracle: 49/60 versus 40/60 for the selected period.

These values are comparison aids, not acceptance claims.

## Work package 0 — freeze the development harness

**Status:** complete at `a7ed0de6666dbff8a3eb0ac047a2cb26098a11b0`; see
`APTA-1.1-WP0-DEVELOPMENT-HARNESS.md`.

**Purpose:** make every later candidate directly comparable.

Implementation:

1. Record a clean default and all-current-experiments build at the plan
   baseline.
2. Retain machine-readable baseline outputs for the spent DJ corpus and open
   Ballroom/ASAP development partitions.
3. Extend the existing key/meter traces only where required to expose
   per-window or per-lattice evidence; keep traces behind development flags.
4. Add one comparison report that reports per family: fixes, breaks, changed
   verdicts, exact accuracy, high-confidence errors, execution failures and
   resource delta.

Verification and exit:

- default Werror suite and current opt-in Werror suite pass;
- sanitizer suite passes for the enabled experimental paths;
- repeated baseline runs produce identical verdicts and hashes;
- reports contain only opaque track IDs.

## Work package 1 — new transient/onset evidence

**Status:** closed without promotion. Transient-lattice iterations 1 through 8
are rejected. The
privacy-safe spent-DJ trace diagnostic at revision
`fb6c2e13bfe5a2ff9b71fe3c96dd4d30f05a955e` shows useful causal-harmonic phase
evidence but does not authorize promotion. I8's new within-bin peak-to-mean
statistic also failed external transfer. The frozen production multiband onset
path is retained; its spent-DJ top-three period ceiling is 49/60, so WP2 may
begin without an onset change. Formal holdouts remain unopened.

**Hypothesis:** the existing single-step broadband/per-band rise loses transient
shape and local contrast. A bounded multi-timescale onset representation can
separate true periodic attacks from filter tails and sustained energy, exposing
the correct lag/phase more consistently.

Proposed implementation boundary:

- add an opt-in `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE` path;
- derive fast-rise, slow local floor, band balance and local peak/refractory
  evidence from the existing bounded band-energy timeline;
- write the candidate novelty into bounded refresh scratch rather than growing
  the 16-byte onset-bin type;
- preserve the current production flux and selector as the baseline path;
- expose privacy-safe onset trace fields needed for offline comparison;
- add synthetic fixtures covering impulses, sustained tones, syncopation,
  kick/snare alternation, off-beat hats, silence and bounded ring rollover.

Initial retain/reject criterion:

- retain only if it produces at least five net period/phase fixes with at most
  one break on the spent DJ set and shows a positive transfer on both open
  Ballroom and ASAP development partitions;
- reject immediately for a default-behavior change, operational failure,
  onset-bin growth, high-confidence safety regression or negative transfer on
  either independent development partition.

## Work package 2 — temporal beat-lattice tracker

**Status:** closed without promotion. Both the unweighted continuity path and
strict-majority selector failed external-development transfer. Production
retains the frozen full-window S4 rank zero; formal holdouts remain unopened.

**Hypothesis:** one global autocorrelation/phase choice cannot represent tracks
whose transient reliability changes over time. Tracking a small bounded family
of lag/phase hypotheses over consecutive windows can recover a stable lattice
without selecting isolated high-scoring aliases.

Proposed implementation boundary:

- reuse S4's bounded top candidates and the new novelty signal;
- score a fixed small number of lag/phase states across consecutive windows;
- include bounded transition penalties for implausible tempo/phase jumps while
  preserving legitimate S6 dynamic-tempo behavior;
- publish only through the existing S4/S6 grid contracts; do not introduce a
  new public result or wire format;
- keep production candidate ordering and serialization invariants intact;
- add deterministic tests for half/double time, phase drift, tempo changes,
  short evidence, final refresh, rollover and cancellation.

Promotion checkpoint:

- selected period approaches the existing top-three oracle without trading
  correct selections away: target at least 47/60 period-correct on the spent
  set, no more than two period breaks, and positive external-development
  transfer;
- grid confidence must not become more optimistic on wrong results;
- CPU, workspace and result-pool deltas are measured against the P4 profile.

If the top-three oracle itself remains below the development target after the
new onset evidence, return to work package 1 rather than adding selector rules.

## Work package 3 — meter and downbeat on the stabilized lattice

**Status:** active. WP3 starts from the unchanged production period and beat
phase at revision `dfc1d1916fcf63ccdc19f893864f1d57696a0081`.

**Hypothesis:** once beat period and phase are reliable, bar phase can be chosen
from the temporal pattern of low/mid/high accents rather than from a single
aggregate onset sequence.

Implementation:

- retain the existing 3/4 versus 4/4 contract;
- score bar-position evidence over multiple consecutive bars;
- distinguish low-frequency pulse evidence from mid/high backbeat and off-beat
  evidence;
- allow an override only when its margin and temporal consistency satisfy a
  predeclared gate;
- preserve the exact MTRD-to-refined-grid ordinal/Q32 binding;
- extend focused meter/downbeat regressions and the trace taxonomy.

Exit criterion:

- meter remains at or above its existing development baseline;
- downbeat demonstrates a large, transferable gain rather than a small local
  patch: target at least 80% on each open development family before a holdout is
  considered;
- no meter/downbeat high-confidence safety regression and no grid-period break
  caused by the bar-phase path.

## Work package 4 — adaptive harmonic key evidence

**Hypothesis:** the current global folded chroma is underdetermined on full
mixes. Combining tuning-aware harmonic salience with robust temporal evidence
can reduce neighbouring-key and relative/parallel-mode errors.

Implementation order:

1. Extend the opt-in key trace with per-window chroma, entropy, tuning choice
   and verdict stability.
2. Replace fixed global accumulation in an experimental path with bounded
   per-window normalization and robust/adaptive aggregation.
3. Improve harmonic salience from the retained octave-resolved spectrum while
   preventing low-frequency fifths and loud frames from dominating.
4. Evaluate continuous or finer tuning estimation only if it can reuse bounded
   evidence at acceptable CPU cost; the existing three-bank experiment remains
   diagnostic until its cost is justified.
5. Gate a candidate verdict by temporal agreement and calibrated separation;
   do not compare raw scores from differently shaped evidence spaces.
6. Prepare a legally usable independent key-development set before promotion.

Retain/reject criterion:

- retain an iteration only when it has positive net fixes on the spent set,
  zero new high-confidence errors and a clear gain on the independent
  key-development set;
- do not approach a formal holdout until exact key accuracy is at least 70% on
  independent development evidence and the remaining error taxonomy shows a
  plausible path to the frozen 75% gate;
- measure resonator work, refresh cost and key-analysis state growth for every
  retained candidate.

## Work package 5 — integrated release candidate

Combine only retained lattice, downbeat and key candidates. Remove superseded
experimental branches and keep one auditable candidate path.

Required verification:

- default and candidate Werror matrices;
- ASan/UBSan candidate matrix;
- unit, integration, serialization, ABI/layout and package tests;
- clean corpus analyzer/export/evaluator round trip;
- explicit fixes/breaks and confidence-safety reports;
- workspace, result-pool, stack/state and CPU comparison;
- no changes to stable 1.0 semantics, frozen acceptance thresholds or private
  evidence boundaries.

The integrated candidate may proceed only when all open development sets show
transfer and no unresolved operational or safety regression remains.

## Work package 6 — one-shot formal holdouts

Freeze source, flags, parameters and tests before opening any holdout. Run each
formal Ballroom/ASAP holdout once and record a retain/reject decision. A failed
holdout returns the candidate to development with that holdout considered
spent; it must not become an iterative tuning set.

Proceed only if meter/downbeat/lattice transfer remains strong and the complete
regression matrix is green. A key holdout follows the same rule once a suitable
independent key protocol has been frozen.

## Work package 7 — new final DJ acceptance corpus

Follow `APTA-1.1-QUALIFICATION-RUNBOOK.md` exactly:

1. select and canonicalize at least 48 genuinely new tracks;
2. obtain independent musician verification without showing APTA output;
3. freeze opaque IDs, canonical WAV hashes and labels;
4. build the exact clean candidate and record analyzer/inspector hashes;
5. run anonymous `--features all` analysis once;
6. export completed FINAL results;
7. execute the unchanged frozen evaluator and retain the complete report.

All final gates are conjunctive: key >=75%, meter >=95%, downbeat >=90%,
beatgrid >=90%, beat-period error <=1%, cyclic downbeat error <=0.10 beat, and
no more than 5% high-confidence errors per family at confidence >=75.

Any failed gate rejects the candidate. Do not tune against that corpus.

## Work package 8 — physical ESP32-P4 evidence (parallel track)

This work can start as soon as hardware is available and should run in parallel
with algorithm development, then be repeated for the exact integrated
candidate if its binary or memory profile changes.

- flash the exact ESP-IDF 6.0.2 `esp32p4` build;
- run all release-target DJ features continuously for at least 1,800 seconds;
- collect real workspace/result-pool, heap/PSRAM, p99/max process latency,
  allocation, deadline, input-drop, thermal and USB/audio coexistence evidence;
- validate with `tools/apta_1_1_p4_hardware_evidence.py`;
- commit only the privacy-safe validated JSON evidence.

Synthetic or host-generated JSON cannot close this blocker.

## Work package 9 — release freeze and publication

Only after work packages 5 through 8 pass:

1. close the readiness evidence blockers and require `freeze-eligible`;
2. generate the pre-freeze snapshot from a clean exact checkout;
3. freeze final API/ABI/wire documents and deliberately update version metadata;
4. regenerate package and release evidence;
5. rerun the complete native/shared/ILP32/ESP-IDF/P4/fuzz/container/package
   matrix against the exact candidate;
6. review the exact tag commit, create `v1.1.0`, publish release artifacts and
   perform the guarded registry publication with owner credentials.

## Execution order and checkpoints

| Order | Work package | Decision checkpoint |
|---:|---|---|
| 1 | WP0 — harness | deterministic baseline and privacy-safe comparison |
| 2 | WP1 — onset evidence | >=5 net fixes, <=1 break, positive external transfer |
| 3 | WP2 — lattice tracker | >=47/60 spent-set period oracle approach and transfer |
| 4 | WP3 — downbeat | >=80% on each open development family, meter preserved |
| 5 | WP4 — key | >=70% independent development accuracy, safety clean |
| 6 | WP5 — integration | complete software/resource matrix green |
| 7 | WP6 — holdouts | one-shot transfer retain decision |
| 8 | WP7 — final corpus | every frozen DJ gate passes |
| parallel/final | WP8 — P4 hardware | validated exact-candidate physical evidence |
| last | WP9 — freeze/release | exact release matrix, tag and publication |

The immediate next implementation task is the first
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE` onset-evidence slice from WP1.
