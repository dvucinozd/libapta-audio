# APTA 1.1 WP1 transient-lattice iteration 8

- **Status:** rejected by the external-development oracle gate
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

## Trace-instrumentation checkpoint

The trace-only I8 implementation preserves the 16-byte onset bin and adds no
persistent field or allocation. The current multiband baseline and I8 workspace
probe reports are byte-identical, SHA-256
`4fc4f275a57ec9bffa4f89a041dd0376c2b911921abf1784a1bccf6622ae2699`.
The current full-feature P4 30-minute query remains 942,288 workspace bytes and
537,104 result-pool bytes.

The default analyzer remains byte-identical to the WP0 artifact, SHA-256
`0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`.
Dependency and mutual-exclusion configurations reject invalid I8 combinations.
The I8 Werror matrix excluding the separately exercised source-archive gate
passes 124/124; the I8 ASan/UBSan matrix passes 120/120. Synthetic peak,
trace-energy and transient-ring tests pass, as do all S6, ABI and default-layout
tests.

The I8 trace executable SHA-256 is
`b4b7b2a84a2b7ac993ef0f934fc5a48aa11649425587d8ffe86a0cf85aeaffb0`.
All 40 ASAP and 40 Ballroom development traces have exact label coverage,
finite four-channel energy/flux geometry, finite aligned peak arrays in
`[0, 1]`, opaque track IDs and exact captured top-three candidate reproduction.
A repeated real trace is byte-identical, SHA-256
`624046cab7e0a75e311db3dee0907cebe161851efcb5f485ddcc2eb6774a00c0`.

## Oracle outcome

The exact frozen peak-sharpness multiplication produced:

| Corpus | Metric | Baseline | I8 | Fixes | Breaks | Net |
|---|---|---:|---:|---:|---:|---:|
| ASAP development | top-1 period | 2 | 0 | 0 | 2 | -2 |
| ASAP development | top-1 phase | 1 | 0 | 0 | 1 | -1 |
| ASAP development | joint top-1 period+phase | 0 | 0 | 0 | 0 | 0 |
| ASAP development | top-3 period | 3 | 3 | 1 | 1 | 0 |
| Ballroom development | top-1 period | 10 | 9 | 1 | 2 | -1 |
| Ballroom development | top-1 phase | 11 | 10 | 1 | 2 | -1 |
| Ballroom development | joint top-1 period+phase | 7 | 7 | 2 | 2 | 0 |
| Ballroom development | top-3 period | 17 | 17 | 0 | 0 | 0 |

Two executions per corpus produced byte-identical reports:

- ASAP development SHA-256:
  `33c6fc834d8cbbd71bc821feb90d695439a5ab9b4ab29c4eabb652a24a9ff34a`;
- Ballroom development SHA-256:
  `24a3280d8adec894cf55d1a23585dcbeae9ea308ac316f362fd3ac59ddfa752b`.

Both top-1 period transfers are negative and both corpora exceed the frozen
maximum of one period break. I8 is therefore rejected before the spent-DJ
trace stage, analyzer novelty implementation or native corpus execution. No
neighboring peak formula was tested. ASAP/Ballroom formal holdouts and the
fresh acceptance corpus remain unopened, and this result makes no acceptance
claim.
