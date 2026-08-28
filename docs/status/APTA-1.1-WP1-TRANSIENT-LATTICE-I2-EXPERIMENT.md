# APTA 1.1 WP1 transient-lattice iteration 2

- **Status:** implementation verified, pre-corpus
- **Frozen baseline revision:** `d79feebec66b0d91cc1255cd9117284e3555d840`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

Iteration 1 changed only five spent-set period signatures, produced no net
downbeat/full-grid fixes, broke one correct period and made S4 flux roughly
3.7 times more expensive. Its additive evidence remained dominated by absolute
broadband magnitude and repeatedly reread seventeen ring bins for every output
bin. The external development gains indicate that local transient shape is
useful, but I1 neither normalizes loudness changes nor computes it efficiently.

Iteration 2 therefore tests a materially different signal: amplitude-normalized
positive contrast against the previous bin, a four-bin floor and a sixteen-bin
floor, computed in one sequential pass with bounded rolling state. The
hypothesis is that equalizing strong and moderate attacks across sections will
make periodic evidence more consistent, while the rolling implementation will
remove repeated onset-ring division and history reads.

## Frozen formula and flags

The isolated baseline enables only
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON`. The candidate adds
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I2=ON`; iteration 1 remains off and
the two transient flags are mutually exclusive.

For positive contrast `d(x, r)`, I2 uses:

```text
d(x, r) = max(x - r, 0) / (x + r + 1/255)
band = mean(0.50*d(current, previous)
          + 0.25*d(current, fast_floor)
          + 0.25*d(current, slow_floor))
raw = 0.50*d(broadband_current, broadband_previous) + 0.50*band
novelty = 0.75*raw + 0.25*max(raw - 0.50*previous_raw, 0)
```

The `1/255` denominator floor is derived from the retained multiband magnitude
quantization, not from corpus observation. Four and sixteen bins retain the
pre-registered WP1 time scales. These constants are frozen before any I2 corpus
run; changing them creates another named iteration.

## Verification and resource gates

The existing production-ring fixture must pass for silence, impulses, sustain,
syncopation, kick/snare alternation, off-beat hats and rollover. Default Werror,
candidate Werror and candidate ASan/UBSan matrices must remain green, the
default analyzer must remain byte-identical to WP0, and the onset bin must stay
within sixteen bytes.

I2 may add no persistent workspace or result-pool allocation. Its fixed rolling
state lives on the S4 refresh-call stack. Against the multiband-only profiling
baseline, median S4 flux time may grow by at most 50% and median full-path total
time by at most 5% on the frozen cost-probe protocol.

## Retain/reject gate

The functional WP1 gate is unchanged: at least five net period/phase fixes with
at most one break on the spent DJ set, positive transfer on both open ASAP and
Ballroom development partitions, and no new high-confidence safety regression.
Reject for a default change, operational or sanitizer failure, allocation or
layout growth, resource-gate failure, more than one spent-set break, or negative
transfer on either open development partition. A rejected result does not open
a holdout and cannot become acceptance evidence.

## Pre-corpus implementation checkpoint

The final one-pass implementation passes the default Werror matrix 120/120,
the I2 Werror matrix 122/122 and the I2 ASan/UBSan matrix 118/118. The I1
production-ring regression fixture also remains green. The default analyzer
SHA-256 is still
`0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`,
byte-identical to WP0.

Against the multiband-only build, the P4 30-minute workspace remains 941,248
bytes, recommended workspace 1,000,092 bytes and result pool 537,104 bytes.
`apta-analyze` grows from 221,320 to 221,376 bytes (+56), while `libapta.a`
grows from 483,118 to 483,326 bytes (+208). The rolling state is statically
bounded at 320 bytes and exists only in the no-inline flux-fill call.

Three alternating 120-second cost-probe runs per build produced these medians
with identical evidence-bin and refresh-scan counts:

| Path | Total baseline | Total I2 | Total delta | S4 flux baseline | S4 flux I2 | Flux delta |
|---|---:|---:|---:|---:|---:|---:|
| BPM | 1,179.954 ms | 1,195.808 ms | +1.34% | 50,303 us | 53,301 us | +5.96% |
| full | 1,255.288 ms | 1,293.378 ms | +3.03% | 41,660 us | 44,276 us | +6.28% |

Both frozen CPU gates pass. An earlier equivalent inlined placement exceeded
the full-path gate because its rolling state enlarged every refresh stack
frame; moving the unchanged formula into a dedicated fill helper removed that
gated-call overhead before any corpus observation.
