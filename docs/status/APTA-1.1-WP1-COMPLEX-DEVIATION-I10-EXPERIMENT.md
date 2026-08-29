# APTA 1.1 WP1 complex-deviation iteration 10

- **Status:** pre-registered; implementation not started
- **Frozen baseline revision:** `0baeb5a87be51747984e05b41612debbbd87ac47`
- **Evidence class:** open development only
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Failure-driven hypothesis

I9 introduced normalized spectral-magnitude change but its signal dominated
production novelty in the median 98.9% of ASAP and 95.2% of Ballroom trace
bins. It therefore behaved as a dense replacement rather than sparse attack
evidence and strongly regressed both period and phase. A different magnitude
normalization, threshold, band weighting or max-fusion coefficient would tune
the rejected representation and is not authorized.

I10 tests one genuinely new information axis: failure of a bin's complex STFT
phase to follow its two-frame linear prediction. A stationary sinusoid has a
constant phase advance and predicts its next complex value; an attack,
frequency discontinuity or incoherent percussive component violates that
trajectory even when normalized spectral power changes densely. No I9 power
or spectral-flux value participates in I10.

## Frozen representation and formula

Only under `APTA_ENABLE_EXPERIMENTAL_COMPLEX_DEVIATION_I10=ON`, S4 owns a
bounded streaming state with:

- 512 consecutive reconstructed mono source samples;
- the same periodic Hann window and deterministic single-precision radix-2
  512-point FFT used by the qualified I9 trace infrastructure;
- two previous complex spectra for the included one-sided bins;
- one aligned `uint16_t` I10 value for each existing 4,096 S4 onset bin.

The hop remains exactly 128 source frames and included frequencies remain 40 Hz
through `min(16000 Hz, 0.45 * source_sample_rate)`, excluding DC. These values
are reused to isolate complex phase prediction as the only new evidence axis;
they are not variables to sweep.

For included bin `k`, let `X_i[k]` be the current complex FFT coefficient,
`A_i[k] = abs(X_i[k])`, and `u_i[k] = X_i[k] / A_i[k]` when
`A_i[k] > 1e-20`, otherwise complex zero. After two reference spectra, freeze:

```text
rotation_i[k] = u_(i-1)[k] * conjugate(u_(i-2)[k])
prediction_i[k] = X_(i-1)[k] * rotation_i[k]
residual_i[k] = abs(X_i[k] - prediction_i[k])

complex_deviation_i =
    sum_k residual_i[k] /
    (1e-20 + sum_k (A_i[k] + A_(i-1)[k]))
```

The triangle inequality bounds the ratio to `[0, 1]`; any non-finite result is
an internal error. The first two complete frames of a contiguous run establish
history and contribute zero. A source discontinuity clears both spectra and
the sample window. Assign each result to the S4 bin containing the FFT frame
centre, quantize with `floor(value * 65535 + 0.5)` and retain the maximum of
the two aligned hops in each S4 bin.

The implementation must compute the unit-phase rotation with complex multiply
and conjugation, not `atan2`, `sin` or `cos` per spectrum bin. No magnitude
weight, logarithm, adaptive threshold, temporal median, whitening history,
refractory rule, fitted coefficient or tempo-dependent routing is permitted.

The trace-only external oracle formula is exactly:

```text
novelty_i10[bin] = max(production_novelty[bin], complex_deviation[bin])
```

Production novelty, lag scan, prior, candidate count/order, refinement and
phase search remain unchanged until both frozen oracle gates authorize the
exact fusion. Default behavior, public ABI, serialization and the 16-byte
`apta_internal_onset_bin_t` remain unchanged.

## Resource boundary

Conditional persistent I10 state, including its aligned trace ring, must remain
at or below 24 KiB, add no result-pool storage and keep the frozen 30-minute P4
workspace below 1.5 MiB. No allocation is allowed after S4 preparation. The
trace-only host full-path median runtime overhead ceiling is 35% against an
otherwise identical Release trace build. A candidate that passes the oracle
must later pass the exact physical P4 allocation/deadline contract.

## Staged verification and frozen gates

1. Implement only the conditional complex predictor, trace accessor and exact
   offline oracle. Test silence, partial windows, a bin-centred stationary
   sinusoid, amplitude rise, frequency discontinuity, impulse, gaps,
   hop/bin alignment and ring replacement. Do not change analyzer novelty.
2. Require default and I10/trace Werror suites, I10 ASan/UBSan,
   invalid-option rejection, exact 16-byte onset-bin layout, byte-identical
   default analyzer, bounded workspace, deterministic trace bytes and the 35%
   host runtime ceiling.
3. Capture only the already-open ASAP and Ballroom development partitions.
   Reject before analyzer implementation unless top-1 period and joint top-1
   period/phase both transfer positively on both corpora, neither metric has
   more than one break on either corpus, and top-three period does not regress.
4. Only after that gate, evaluate the already-spent 60-track DJ trace. Reject
   before analyzer implementation unless joint top-1 period/phase has at least
   five net fixes with at most one break and top-three period does not regress.
5. Only after both oracle gates, implement the exact `max` fusion and require
   oracle/production agreement within `5e-7`, complete Werror/sanitizer
   matrices, unchanged default hashes/resource boundaries and no confidence
   safety regression.
6. Run exact native ASAP and Ballroom development partitions. Reject for
   negative meter, downbeat, period or phase transfer on either corpus, any
   operational regression or any high-confidence safety regression.
7. Only then run the spent DJ corpus. Retain I10 only with at least five net
   beatgrid fixes, at most one beatgrid break, non-negative meter/downbeat/key
   transfer and no high-confidence safety or operational regression.

Any failed stage rejects I10 without changing FFT size, hop, frequency range,
normalization, quantization, fusion rule or gate. It does not authorize another
complex-deviation variant, a formal holdout, fresh acceptance corpus or
acceptance claim.
