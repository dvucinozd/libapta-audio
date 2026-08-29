# APTA 1.1 WP1 spectral-flux iteration 9

- **Status:** trace-only implementation qualified; external-development oracle pending
- **Frozen baseline revision:** `1f0e956c4fc5a9188f6fa6598a44eac051ec9cd1`
- **Trace implementation revision:** `9413aaab3a87726753e7bf217685cb17580fc309`
- **Evidence class:** open development only
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Failure-driven hypothesis

WP1 iterations I1 through I7 transformed the same quantized low/mid/high and
broadband mean-magnitude history. I8 added within-bin peak magnitude, but still
had no frequency-selective change information and transferred negatively. The
evidence is therefore exhausted for another amplitude-history formula.

I9 tests one new axis: positive frame-to-frame change in the normalized short
time power spectrum. A frequency-selective attack can change spectral shape
even when the 256-sample band means are flat or dominated by a sustained tail.
Combining that independent change evidence with production novelty may expose a
better period and phase ceiling without changing the public grid contract.

## Frozen representation

Only under `APTA_ENABLE_EXPERIMENTAL_SPECTRAL_FLUX_I9=ON`, S4 owns a bounded
streaming spectral-onset state:

- 512 consecutive mono source samples;
- a periodic Hann window `w[n] = 0.5 - 0.5*cos(2*pi*n/512)`;
- a deterministic radix-2, single-precision 512-point FFT;
- a previous normalized one-sided power spectrum;
- one `uint16_t` spectral-flux value for each of the existing 4,096 S4 bins.

The hop is exactly 128 source frames. A frame is assigned to the 256-source-frame
S4 bin containing its centre. Discontinuous input resets the sample window and
previous spectrum; the first complete frame of a new run establishes the
reference spectrum and contributes zero flux.

For FFT bin `k`, include frequencies from 40 Hz through
`min(16000 Hz, 0.45 * source_sample_rate)`, excluding DC. Let `P_i[k]` be the
squared magnitude in frame `i` and freeze the normalized power as:

```text
N_i[k] = P_i[k] / (1e-20 + sum_j P_i[j])
spectral_flux_i = sum_k max(N_i[k] - N_(i-1)[k], 0)
```

Clamp the finite frame flux to `[0, 1]`, quantize as
`floor(flux * 65535 + 0.5)`, and retain the maximum of the two aligned FFT hops
in each S4 bin. No logarithm, fitted coefficient, adaptive threshold, whitening
history, frequency weighting or tempo-dependent routing is permitted.

The trace-only stage exposes the dequantized spectral-flux series beside the
existing opaque production trace. If the external oracle gate passes, the only
analyzer formula authorized for implementation is:

```text
novelty_i9[bin] = max(production_novelty[bin], spectral_flux[bin])
```

The existing lag scan, prior, top-three ordering, refinement and phase search
remain unchanged. The default build, public ABI, serialization and
`apta_internal_onset_bin_t` must remain unchanged; its size ceiling remains 16
bytes.

## Resource boundary

At 48 kHz the front end executes 375 FFT frames per second. Conditional
persistent state must remain at or below 24 KiB, add no result-pool storage and
keep the frozen 30-minute P4 workspace below 1.5 MiB. No allocation is allowed
after S4 preparation. The trace-only host full-path runtime overhead ceiling is
35%; a candidate that passes the oracle must later pass the exact physical P4
deadline and allocation contract before promotion.

## Staged verification and frozen gates

1. Implement only the conditional streaming spectral state, deterministic
   trace accessor and offline oracle formula. Add synthetic tests for silence,
   a stationary sinusoid, an impulse, a frequency change, amplitude-only rise,
   gaps, partial windows, hop/bin alignment and ring replacement. Do not change
   analyzer novelty.
2. Require default and I9/trace Werror suites, I9 ASan/UBSan, invalid-option
   rejection, exact 16-byte onset-bin layout, byte-identical default analyzer,
   bounded workspace growth and deterministic trace bytes.
3. Capture only the already-open ASAP and Ballroom development partitions.
   Reject before analyzer implementation unless top-1 period and joint top-1
   period/phase both transfer positively on both corpora, with no more than one
   break per metric on either corpus and no top-three period regression.
4. Only after that external gate, evaluate the already-spent 60-track DJ trace.
   Reject before analyzer implementation unless joint top-1 period/phase has at
   least five net fixes with at most one break and top-three period does not
   regress.
5. Only after both oracle gates, implement the exact `max` fusion and require
   oracle/production agreement within `5e-7`, default/I9 Werror and sanitizer
   matrices, unchanged default hashes, the resource boundary above and no
   confidence-safety regression.
6. Run exact native ASAP and Ballroom development partitions. Reject for
   negative meter, downbeat, period or phase transfer on either corpus, any
   operational regression or any high-confidence safety regression.
7. Only then run the spent DJ corpus. Retain I9 only with at least five net
   beatgrid fixes, at most one beatgrid break, non-negative meter/downbeat/key
   transfer and no high-confidence safety or operational regression.

Any failed stage rejects I9 without changing FFT size, hop, frequency range,
normalization, quantization, fusion rule or gate. It does not authorize a
neighboring parameter, an ASAP/Ballroom formal holdout, a fresh acceptance
corpus or an acceptance claim.

## Trace-instrumentation checkpoint

Revision `9413aaab3a87726753e7bf217685cb17580fc309` implements only the
pre-registered conditional streaming FFT state, aligned `uint16_t` trace ring,
privacy-safe trace output and exact offline `max` oracle. Production
`onset_flux`, analyzer novelty, lag ranking, phase search, public ABI and wire
output remain unchanged. The conditional state is 9,264 bytes and its 4,096
value trace ring is 8,192 bytes, for 17,456 bytes of persistent I9 state. A
compile-time assertion enforces the frozen 24 KiB ceiling.

The current qualification results are:

- I9 Release Werror matrix excluding the separately exercised archive gate:
  124/124 pass;
- I9 ASan/UBSan matrix: 108/108 pass;
- clean sibling-clone binary/source archive reproducibility gate: 1/1 pass;
- dependency and I8/I9 mutual-exclusion configurations: rejected as required;
- exact multiband onset-bin layout: 16 bytes;
- default analyzer SHA-256:
  `0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`,
  byte-identical to the WP0 artifact;
- baseline and I9 trace executable SHA-256 values respectively:
  `313416ab6cd1fb71c4a04e6a0abc1673bf7c7feaba77a2c5ada206c610cfc5b7`
  and `c02f6fe044eff853268021ec6fd8beee4d92e12f586a0f9b0efbae40456c56d7`;
- frozen full P4 30-minute queried workspace: 942,288 -> 951,552 bytes
  (+9,264), recommended workspace: 1,001,197 -> 1,011,040 bytes
  (+9,843), result pool unchanged at 537,104 bytes;
- two repeated real development traces are byte-identical, SHA-256
  `7b808040e3afc5ec73aae64fc292ee2c5985648ce7d7b0e337ec57d7fb53cb3d`.

The trace-only full-path runtime gate used the first ten sorted Ballroom
development WAVs, identical Release trace flags except for I9, and three
alternating baseline/I9 passes. Baseline wall times were 11.041, 9.586 and
6.118 seconds; I9 times were 9.897, 11.077 and 11.437 seconds. The medians are
9.586 and 11.077 seconds, a 15.6% overhead below the frozen 35% ceiling.

This is implementation/resource evidence only. No ASAP or Ballroom formal
holdout, spent DJ trace or fresh acceptance corpus was opened by this
checkpoint, and it makes no algorithmic or acceptance claim.
