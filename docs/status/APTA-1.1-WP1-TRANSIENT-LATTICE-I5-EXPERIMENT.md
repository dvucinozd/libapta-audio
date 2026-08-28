# APTA 1.1 WP1 transient-lattice iteration 5

- **Status:** rejected by the pre-implementation oracle gate
- **Frozen baseline revision:** `b56965f04ce9f06280d32d9a26b61e9bf006853e`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

I3 proved that sixteen-bin local contrast increases the available period
candidate ceiling, but replacing baseline novelty caused too many DJ
period/phase breaks. I4 retained some period gain by adding causally normalized
baseline and local contrast, yet meter regressed on both independent open
development partitions. An additive fusion can promote evidence present in
only one input and therefore still replace a stable baseline accent pattern.

I5 tests a consensus novelty instead: its value is positive only when both the
existing multiband onset and I3 local contrast agree. The harmonic mean is
scale-neutral after causal normalization, has no tunable weight or threshold,
and is bounded by the two normalized inputs.

## Frozen formula and flag

Let `b[i]` be the existing multiband-onset novelty and `c[i]` the exact I3
sixteen-bin local contrast. Compute the sequential float32 causal means and
normalizations exactly as pre-registered for I4:

```text
b_mean[i] = sum(b[0..i]) / (i + 1)
c_mean[i] = sum(c[0..i]) / (i + 1)
b_norm[i] = b[i] / b_mean[i] when b_mean[i] > 0, else 0
c_norm[i] = c[i] / c_mean[i] when c_mean[i] > 0, else 0

novelty[i] = 2 * b_norm[i] * c_norm[i] / (b_norm[i] + c_norm[i])
             when b_norm[i] + c_norm[i] > 0, else 0
```

The opt-in flag will be
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I5=ON`. It must require
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON` and be mutually exclusive with
I1 through I4. The default build remains unchanged.

## Staged verification and retain/reject gate

1. Add exactly this formula to the privacy-safe oracle; do not test alternate
   means, exponents, offsets, weights or thresholds.
2. Reject before implementation if top-1 period transfer is not positive on
   both open ASAP and Ballroom development traces, or if either trace family
   shows more than one top-1 period break.
3. Before corpus execution, require default/I5 Werror and I5 ASan/UBSan
   matrices, byte-identical default artifacts, invalid-flag rejection, exact
   production/oracle trace agreement within `5e-7`, no persistent allocation
   growth, S4 flux overhead at most 50% and full-path overhead at most 5%.
4. Run exact native ASAP/Ballroom development partitions. Reject for negative
   transfer in meter, downbeat or period on either partition.
5. Only then run the already-spent 60-track DJ development corpus. Retain only
   with at least five net period/phase fixes, at most one break and no
   high-confidence safety regression.

No rejection opens a formal holdout or creates acceptance evidence.

## Oracle outcome

The exact sequential-float32 oracle retained I4's production agreement at
maximum absolute difference `1.79077144224e-8`. The newly frozen harmonic
consensus produced:

| Corpus | Metric | Baseline | I5 oracle | Fixes | Breaks | Net |
|---|---|---:|---:|---:|---:|---:|
| ASAP development | top-1 period | 2 | 3 | 3 | 2 | +1 |
| ASAP development | top-1 phase | 1 | 2 | 1 | 0 | +1 |
| ASAP development | top-3 period | 3 | 5 | 3 | 1 | +2 |
| Ballroom development | top-1 period | 10 | 12 | 3 | 1 | +2 |
| Ballroom development | top-1 phase | 11 | 12 | 2 | 1 | +1 |
| Ballroom development | top-3 period | 17 | 18 | 2 | 1 | +1 |

ASAP top-1 period has two breaks, exceeding the frozen maximum of one. I5 is
therefore rejected before implementation, native corpus execution or resource
measurement. No formula variant was tested.

Privacy-safe report SHA-256 values are:

- ASAP development: `107a2517f947bb30f32d42fe03b2a345f5c7472a88e701f482b66673d3e685c1`;
- Ballroom development: `81d0121e1107b0a1ab1c031846e53d5cb595a01dea9949a83cf59ffb36e92ca1`.

The spent DJ corpus and all formal holdouts remain unopened. This result is
development evidence only and makes no acceptance claim.
