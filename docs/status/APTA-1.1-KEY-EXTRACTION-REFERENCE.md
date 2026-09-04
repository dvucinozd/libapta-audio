# APTA 1.1 key extraction numerical reference

- Baseline: `d9a3abadd9569f7e6b5bdaa7c0df25f2cf040844`
- Evidence: same frozen synthetic PCM as the key-mode diagnostic; no corpus
- Purpose: distinguish extraction arithmetic from representation behavior
- Production change / acceptance claim: none

## Frozen diagnostic design

Before execution, reuse the exact C-generated float PCM, 48 kHz, four windows,
all 24 tonic/mode stimuli and the same three conditions. Do not regenerate
audio in a second language. Keep the original diagnostic output unchanged
when its new extraction-reference compile option is absent.

At every native four-sample averaging step compute two double references:

1. `effective`: the same float sum/divide and native float coefficient,
   interpreted as frequency `acos(coefficient/2)`. This isolates the native
   resonator/energy/log/folding/accumulation arithmetic from coefficient and
   averaging precision.
2. `nominal`: average the identical float PCM in double precision and compute
   double trigonometry at the frozen float frequency/probe-ratio constants.
   Preserve the actual frequency table, not idealized note frequencies.

Use a double complex oscillator to accumulate the direct Fourier projection
at every bin (not the native Goertzel recurrence). Independently compute a
double Goertzel at the same reference frequency and require energies to agree
within `1e-6 * max(1, Fourier energy)` at every window/bin. Analytic silence and
a single unit input impulse (decimated amplitude 1/4, energy 1/16) must pass;
impulse energy tolerance is 1e-9. Reject invalid/non-finite states or lost
windows. Never relax these reference self-consistency limits after execution.

Apply the same log compression, averaging over probes and octave folding in
double, keep both per-window and cumulative chroma, then cast once to float
and use the unchanged native selector. Report chroma differences and every
changed tonic/mode verdict. Native/reference differences are observations,
not a correctness gate or permission to change production arithmetic.

Use separate explicit-build diagnostic target and a new report format. Reuse
the old 720-row stimulus topology (576 PCM rows carry reference fields).
Require unchanged old diagnostic reports, exact-build repeatability, Werror,
ASan/UBSan and unchanged analyzer hashes. Default and rejected semitone-band
are evaluated separately. No additional native RAM/CPU, ABI or wire changes.

Even agreement cannot prove correctness on arbitrary inputs, sample rates,
durations, clipping, aliasing or real music. If the higher-precision model
still collapses major mode, arithmetic alone is not a sufficient repair for
these stimuli. New representation experiments require separate preregistration.

## Outcome — 2026-09-04

The protocol and instrument were committed before execution at
`f31d0e15d725af3c797233bc67a3963e30071810`. Each default/band report contains
720 rows, including 576 PCM rows with both references. Every reference passed
the energy identity and analytic silence/impulse checks. All original native
observations and old-format report bytes are unchanged.

| Measure | Default | Semitone-band |
|---|---:|---:|
| Effective reference changed tonic/mode | 0/576 | 0/576 |
| Nominal reference changed tonic/mode | 0/576 | 0/576 |
| Max effective chroma error / max reference chroma | 0.0001087332 | 0.0000918460 |
| Max nominal chroma error / max reference chroma | 0.0021483058 | 0.0014080245 |
| Max Fourier/Goertzel energy discrepancy, normalized by max(1,E) | 1.073e-10 | 1.507e-10 |

The chroma metric is a per-row maximum absolute component difference divided
by `max(1, maximum reference component)`, then maximized over rows. It is not
relative error for every individual pitch class; near-zero bins can have much
larger relative errors. Both references preserve the original four-window
synthetic results, including clean major default 4/12 and band 0/12. Even the
nominal double reference still predicts minor for all 36 band major stimuli.

**Conclusion:** finite-precision extraction is not a sufficient explanation or
repair for the observed synthetic major-mode collapse. The independently
computed Fourier representation, with the same log compression/probe average/
octave folding and unchanged selector, exhibits the same decisions. Small
numerical differences exist, but no tonic/mode verdict changes in this matrix.
This supports investigating representation contrast and its interaction with
raw-cosine scoring, not a blanket conversion of production arithmetic to double.

This does not identify which single representation step is responsible,
establish causality for every real song, prove the absence of all numeric bugs,
or validate arbitrary sample rates/long-duration accumulation. The input PCM
is the same native float sequence in both references; the nominal path does
not reconstruct unquantized source audio. The final selector still uses float
after one reference-chroma cast. No new detector is proposed or promoted by
this test, and rejected centered correlation remains rejected.

## Reproduction and verification

Explicitly build `apta_key_extraction_reference` in default and semitone-band
Release/Werror directories, then run with `--json` into fresh output paths.
The target adds `APTA_KEY_EXTRACTION_REFERENCE=1` only to the host instrument;
the library has no new options, state or behavior. Build/run the band target
under ASan/UBSan as well. Keep `apta_key_mode_diagnostic` reports unchanged.

Use `tools/apta_key_extraction_reference_summary.py` with `--default-report`,
`--band-report`, `--previous-default`, `--previous-band`,
`--native-source-revision` and `--output`. It pins the old report hashes,
checks complete topology and unchanged native fields, validates reference
metrics, counts every changed verdict and refuses an existing output path.
The small parser tolerance accounts for nine-significant-digit native JSON
chroma serialization, not relaxation of the C reference-energy gate.

Verification: default and band Release/Werror each 121/121 CTest; band
ASan/UBSan 116/116; six original plus four new Python summary tests pass.
Both new reports repeat byte-identically, and the band sanitizer report is
byte-identical to band Release. Both analyzer SHA-256 values remain unchanged.
No source under `src/` or `include/` was edited. Runtime cost of the diagnostic
is not a proposed P4 algorithm cost; production resource delta is zero.

Reports and logs are local under `build/key-extraction-reference/`. The retained
aggregate with exact source, executable/report hashes and tests is
[`../../evidence/1.1/key-extraction-reference-20260904.json`](../../evidence/1.1/key-extraction-reference-20260904.json).
