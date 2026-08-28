# APTA 1.1 WP1 transient-lattice iteration 8

- **Status:** pre-registered; no I8 trace, oracle or analyzer result collected
- **Frozen baseline revision:** `c9a10700681e0bc2667567444822c536a33e0f9f`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

I1 through I7 only transform the same quantized low/mid/high/broadband mean
energies. Their mixed transfer shows that local contrast is useful but that a
bin mean cannot reliably distinguish an impulsive attack from sustained energy
with the same average magnitude. Another history length, fusion weight or
candidate-routing rule would therefore tune the existing representation rather
than test new evidence.

I8 captures one genuinely new bounded statistic: the maximum quantized
broadband magnitude observed inside each 256-frame S4 onset bin. The ratio of
that crest to the already-stored broadband mean is high for a sparse attack and
lower for sustained energy. Multiplying production novelty by this normalized
sharpness should retain rising attacks while suppressing filter tails and
sustained crossings before autocorrelation.

## Frozen representation and formula

Only under `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I8=ON`, S4 assigns the
otherwise-unused `apta_internal_onset_bin_t.reserved8` byte as
`peak_magnitude_q8`. For each finite, clamped broadband magnitude `m` already
computed by S4:

```text
sample_q8 = floor(m * 255)
peak_magnitude_q8 = max(peak_magnitude_q8, sample_q8)
```

The bin is already zeroed on ring replacement, so no new state or reset rule is
introduced. S6 continues to ignore and leave the byte zero. The internal onset
bin must remain exactly 16 bytes in multiband and non-multiband builds.

For completed S4 bins, let `b[i]` be the exact captured production multiband
novelty, `p[i] = peak_magnitude_q8 / 255`, and `e[i]` the existing normalized
broadband mean. Freeze the candidate novelty as:

```text
excess[i] = max(p[i] - e[i], 0)
sharpness[i] = excess[i] / (p[i] + e[i] + 1/255)
novelty_i8[i] = b[i] * sharpness[i]
```

There is no fitted coefficient, threshold, history length, exponent, temporal
shift or fallback selector. I8 novelty is used for the existing lag scan,
refinement and phase search exactly as production novelty is used today.

The I8 option must require
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON`, be mutually exclusive with I1
through I7, and leave the default build byte-identical. Trace output may expose
only normalized peak values aligned with the existing opaque onset arrays.

## Staged verification and frozen gates

1. Implement only the peak-byte accumulator, privacy-safe trace accessor and
   exact offline formula. Add synthetic tests for silence, sustained level,
   one-sample impulse, quantization edges, partial final bins, ring replacement
   and unchanged S6/default layout. Do not implement analyzer novelty yet.
2. Require default and trace/I8 Werror suites, I8 ASan/UBSan, invalid-flag
   rejection, exact 16-byte onset-bin layout, byte-identical default artifacts,
   no persistent workspace/result-pool growth and deterministic trace output.
3. Capture only the already-open ASAP and Ballroom development partitions.
   Reject before analyzer implementation if top-1 period and joint top-1
   period/phase do not both transfer positively on both corpora, or if either
   metric has more than one break on either corpus.
4. Only after the external oracle gate, evaluate the already-spent 60-track DJ
   trace. Reject before analyzer implementation unless joint top-1
   period/phase has at least five net fixes with at most one break and the
   top-three period ceiling does not regress.
5. Implement the exact analyzer formula and require oracle/production agreement
   within `5e-7`, default/I8 Werror and I8 sanitizer matrices, invalid
   combination rejection, no persistent allocation growth, S4 flux overhead
   at most 25%, full-path overhead at most 5%, and unchanged default hashes.
6. Run exact native ASAP and Ballroom development partitions. Reject for
   negative meter, downbeat, period or phase transfer on either corpus, any
   high-confidence safety regression, or any operational regression.
7. Only then run the already-spent DJ corpus. Retain I8 only with at least five
   net beatgrid fixes, at most one beatgrid break, non-negative meter/downbeat/
   key transfer and no high-confidence safety or operational regression.

Any failed stage rejects I8 without changing thresholds, trying a neighboring
formula, opening an ASAP/Ballroom formal holdout, consuming a fresh acceptance
corpus or making an acceptance claim. Passing all stages retains an opt-in WP1
candidate; it does not by itself complete WP1 or authorize release promotion.
