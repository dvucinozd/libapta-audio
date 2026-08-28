# APTA 1.1 WP1 transient-lattice iteration 6

- **Status:** pre-registered oracle candidate, not implemented
- **Frozen baseline revision:** `2d176c3627b2de396f510a5d9037207cba5247c6`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

I3's sixteen-bin local contrast raises the period candidate ceiling but its
decaying positive tail changes too many phase and meter decisions. I4's
additive fusion and I5's harmonic consensus did not reliably preserve the
baseline accent pattern. I6 does not mix evidence families. It temporally
sharpens the exact I3 contrast by retaining positive centered curvature, so a
broad tail contributes less than its attack peak.

One neighbour on each side spans 512 source frames, about 10.7 ms at 48 kHz.
That is an onset-localization scale rather than a corpus-derived parameter.
No alternate width, threshold, exponent or mixture will be tested.

## Frozen formula and flag

Let `c[i]` be the exact sequential-float32 I3 sixteen-bin local contrast.
Missing neighbours outside the evidence interval are zero:

```text
neighbour_mean[i] = 0.5 * (c[i - 1] + c[i + 1])
novelty[i] = max(c[i] - neighbour_mean[i], 0)
```

The opt-in flag will be
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I6=ON`. It must require
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON` and be mutually exclusive with
I1 through I5. The default build remains unchanged.

## Staged verification and retain/reject gate

1. Add exactly this centered-curvature formula to the privacy-safe oracle.
2. Reject before implementation unless top-1 period transfer is positive on
   both open ASAP and Ballroom development traces, neither family has more
   than one top-1 period break, and phase transfer is non-negative.
3. Before corpus execution, require default/I6 Werror and I6 ASan/UBSan
   matrices, byte-identical default artifacts, invalid-flag rejection, exact
   production/oracle trace agreement within `5e-7`, no persistent allocation
   growth, S4 flux overhead at most 50% and full-path overhead at most 5%.
4. Run exact native ASAP/Ballroom development partitions. Reject for negative
   transfer in meter, downbeat or period on either partition.
5. Only then run the already-spent 60-track DJ development corpus. Retain only
   with at least five net period/phase fixes, at most one break and no
   high-confidence safety regression.

No rejection opens a formal holdout or creates acceptance evidence.
