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
