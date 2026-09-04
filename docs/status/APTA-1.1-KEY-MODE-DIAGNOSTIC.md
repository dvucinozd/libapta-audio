# APTA 1.1 synthetic key-mode diagnostic

- Baseline: `de4a2e69188a8500729638a74bc6d4960613445a`
- Mode: diagnostic, not a new detector or acceptance experiment
- Inputs: generated synthetic vectors and PCM only; no corpus/holdout access
- Native algorithm, thresholds, layout and output format: unchanged

## Question and fixed design

The semitone-band run scored 0/36 major keys. Existing centered-correlation
tests already demonstrate a common-floor mode bias; that rejected candidate
must not be presented as a new solution. Separate mode/rotation plumbing from
the effect of the tonal front end and cumulative evidence.

Before execution fix these cases for every tonic and both modes:

1. Exact shipped KP profile and ideal equal-amplitude triad vectors, each with
   additive chroma floors 0, 1 and 4. These are controlled mathematical inputs,
   not physical noise amplitudes. Exact zero-floor profiles must select their
   own tonic/mode; triad/floor decisions are measured, not required to be right.
2. Four seconds of 48 kHz floating-point PCM: tonic triad, subdominant triad,
   dominant major triad, tonic triad. Major uses I-IV-V-I, minor uses i-iv-V-i.
   Each sine has amplitude 0.15, root MIDI 48+tonic+degree, third +4/+3,
   fifth +7. Frequencies follow A440 equal temperament, phases use absolute
   frame time. Fixed conditions: clean, +1/3 semitone, clean plus deterministic
   uniform noise in [-0.02, 0.02]. No clipping is possible; all fundamentals
   stay inside the native C3-B5 measurement range.
3. At each complete native one-second window, record delta chroma and cumulative
   chroma, all 24 independent double-precision cosine reference scores,
   native top-three candidates, mode, tonic and confidence. Compare native
   selection with the reference within 2e-6, allowing float ties. Reference
   scores are not a new native scoring path. They use the frozen profile
   constants and must agree on clean profiles and every measured vector.
4. Silence must remain unavailable. Non-finite/negative chroma must be rejected.
   Repeated executions in the same build must be byte-identical.

Use the default and rejected semitone-band builds separately. Synthetic chord
tonic is an intended stimulus identity, not a sufficient musical ground truth
for global key (especially IV/V windows). No synthetic accuracy authorizes a
corpus claim. Four-window accumulation is not a model of whole-track mixing.

## Boundaries and stop gates

Implement an explicitly built host diagnostic executable with JSON output and
internal assertions. It reuses native feed/select functions, with an isolated
zero-initialized session as the existing key-feed benchmark does. It does not
test decoder/publication/serialization; existing musical-key tests cover those.
The diagnostic changes no library source and adds no runtime RAM/CPU cost to
the library. Stop if the independent reference disagrees, windows are missing,
values are invalid or exact-profile mode/rotation plumbing fails. Do not change
profile values, floors, noise, detuning or acceptance limits to improve results.

Required checks: default/band Werror diagnostic builds and native key tests,
ASan/UBSan diagnostic execution, repeatability, unchanged analyzer hashes and a
privacy-safe aggregate report. Record source/build/report hashes and clearly
separate previously known floor bias from any newly observed front-end effect.

## Outcome — 2026-09-04

The protocol and native diagnostic were committed before execution at
`b9c50df868b327baf8af362b618e3f9e176d361f`. Each build produced all 720 rows;
every native top-three decision agreed with the independent double-precision
cosine reference within the declared tolerance. Default and semitone-band
repeats were byte-identical. The sanitizer candidate report also exactly
matched its Release report.

### Mode/rotation plumbing works on controlled vectors

Both builds correctly select all 24 exact profiles and all 24 zero-floor
triads. For each of the additive floors 1 and 4, all 12 major profiles and all
12 major triads select minor; all corresponding minor stimuli retain their
tonic and mode. This extends the already-known common-floor effect across all
tonics; it is not a newly discovered centered-correlation remedy.

### Native PCM reproduces the asymmetry without real music

The following counts match the intended tonic AND mode after four chords,
out of 12 transpositions per mode. They are synthetic diagnostic counts, not
accuracy estimates for songs. A short chord progression is not an independently
annotated global-key corpus.

| PCM condition | Default major | Default minor | Band major | Band minor |
|---|---:|---:|---:|---:|
| Clean | 4/12 | 11/12 | 0/12 | 12/12 |
| +1/3 semitone | 0/12 | 6/12 | 0/12 | 9/12 |
| Uniform noise +/-0.02 | 3/12 | 11/12 | 0/12 | 12/12 |

All 36 band major progressions select minor. On clean major progressions, 10/12
are parallel-mode swaps after four windows. The first tonic-only window already
matches just 7/12 in default and 2/12 in band; after all four windows those become
4/12 and 0/12. IV/V windows themselves are not expected to identify the global
tonic, so their individual key changes are not classified as errors.

The mean ratio `min(chroma) / mean(chroma)` for clean major cumulative evidence
increases from about 0.43 to 0.57 in default and 0.60 to 0.76 in band between
windows 1 and 4. The band representation has a larger common floor and loses
major/minor contrast in these stimuli. This is consistent with the raw-cosine
floor bias and shows that external labels/complex mixes are not necessary to
reproduce the symptom. It does NOT prove that every real-corpus error has this
cause, or isolate resonator numerical error from leakage/compression/folding.

### What this does and does not establish

- No general major/minor encoding or rotation failure was observed in the
  selector. This instrument does not independently audit serialized labels.
- The independent reference agrees with the selector; a ranking arithmetic
  mismatch is not the observed cause in these 1,440 diagnostic rows.
- Failure appears by the native tonal front end and persists/worsens with
  accumulation. The front end and raw-cosine scoring must be considered together.
- A separate numerical reference for the resonator/decimator is still needed
  to distinguish floating-point implementation effects from representation
  design. The current reference checks scoring, not extraction.
- Do not re-enable the previously rejected centered-correlation candidate,
  subtract a fitted floor, or tune confidence using these results. A future
  replacement needs a separately frozen hypothesis and independent development
  evidence. No corpus, holdout or new acceptance set was opened here.

## Reproduction and verification

Build the explicit `apta_key_mode_diagnostic` target in a GNU Release/Werror
build, once with default options and once with
`APTA_ENABLE_EXPERIMENTAL_SEMITONE_BAND_KEY=ON`. Run the executable with `--json`
into a new output file; no arguments runs the same assertions without a report.
The instrument is excluded from default build/test/install graphs.

Summarize using `tools/apta_key_mode_diagnostic_summary.py` with
`--default-report`, `--band-report`, `--native-source-revision` and `--output`.
The standard-library-only parser checks complete stimulus topology, finite
evidence, build identity and native/reference agreement and refuses overwrite.
Its six synthetic tests run as `python tests/unit/key_mode_diagnostic_summary.py`.

Latest reconfigured native matrices: default Release/Werror 121/121, band
Release/Werror 121/121, band ASan/UBSan 116/116. The Release inventory includes
classification, installed conformance and versioned interchange checks in
addition to the prior 118-test inventory. The instrument separately passed
all 720 rows in each of the three builds. No library source/layout changed;
both analyzer hashes still match the frozen semitone evaluation. These are
host checks, not physical P4 evidence.

Machine-readable retained aggregate:
[`../../evidence/1.1/key-mode-diagnostic-20260904.json`](../../evidence/1.1/key-mode-diagnostic-20260904.json).
Full generated reports, repeats, summary and test logs remain under the ignored
`build/key-mode-diagnostic/` directory. The aggregate records their exact hashes.
