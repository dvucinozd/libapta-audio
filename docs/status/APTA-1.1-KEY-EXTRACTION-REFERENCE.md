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
