# APTA 1.1 meter/downbeat validation status

- **Snapshot date:** 2026-08-23
- **Source revision:** `a72e77308a3a05cb4f61c58f56f8d9e217538118`
- **Analyzer SHA-256:** `4e6f33ca33a82c078d08e2ca39af2ce51419d82b931b84a3b678721ea3dba82c`
- **Purpose:** targeted development evidence for real 3/4 recall and downbeat phase
- **Acceptance claim:** none; neither corpus is the final DJ acceptance corpus

## Frozen data separation

Two independently annotated datasets were prepared without reading APTA
output. Each deterministic split contains 20 3/4 and 20 4/4 tracks. Selection
is fixed by a committed seed; audio, annotations, source mappings, labels,
analyzed containers and reports stay in ignored local build directories.

The development partitions have been analyzed. Both 40-track holdouts remain
untouched by APTA and must not be opened during further candidate design.

### ASAP stress corpus

[ASAP v1.2](https://github.com/fosfrancesco/asap-dataset/tree/v1.2) supplies
performance MIDI with beat, downbeat and time-signature annotations under CC
BY-NC-SA 4.0. The tool groups by musical score before splitting, preventing
alternate performances of one work from crossing the boundary. A deterministic
local synthesizer renders canonical 48 kHz stereo excerpts.

This is an out-of-domain piano stress corpus, not DJ evidence. It exposed a
strong confound: the selected local beat period was within 10% of the annotated
performance period on only 2/40 development tracks.

- manifest SHA-256: `d29c748beb55e2f08950dcbdc82e30c6d0e49f0124a3c8beb63cdb619fda3185`
- labels SHA-256: `7f6d5e40c8771df6052fd31f46a0fb0d0e95ab990c8d89f00c4ef1fe41d882c0`
- run SHA-256: `adf7a4ed0d3743e731ea636f194e33f58a30adfc158329016e40f61c340b3f09`
- report SHA-256: `a75d3aa0e66e8af7092b4e6c9c773b1d189dae44678292c98d8301d9c281a673`

### Ballroom development corpus

The [Ballroom Rhythm Dataset loader](https://mirdata.readthedocs.io/en/stable/_modules/mirdata/datasets/ballroom.html)
documents real ballroom audio plus manually corrected beat/bar annotations,
with beat position 1 representing a downbeat. Waltz and Viennese Waltz use
positions 1–3; the other eight styles use positions 1–4. The loader records CC
BY-NC-SA 4.0 and publishes the exact archive URLs and checksums enforced by the
preparation tool.

- audio archive MD5: `2872a3e52070bc342a4510a95e2fa0b8`
- annotation archive MD5: `d0c31e1a30c0caf8fd22dec25f2174cf`
- manifest SHA-256: `0dc703b20d0603f9e6685bb0cfa83367414a6de92470eb1c4fee5f48097928a7`
- labels SHA-256: `5aa77e0b23233a38480d43a77b67f63f2407c91ac444073efce1bf8f0214d323`
- run SHA-256: `1c68803a7feeb9e48fc950b969d11ac64bcb3e9e2e1d178a3b4d15df10df5319`
- report SHA-256: `169719e5bdef260ea1695629c4d5a3259dcc86acd3ab8ab78ed751e7100c0ffc`

## Development baseline

The scorer measures meter exactly, period against the local annotated beat
interval, and the published downbeat against the nearest annotated downbeat at
a tolerance of 0.10 beat. It does not use the final DJ corpus's key gates.

| Corpus / class | Meter | Downbeat | Period <=1% | Period <=10% |
|---|---:|---:|---:|---:|
| ASAP, all | 19/40 | 4/40 | 2/40 | 2/40 |
| ASAP, 3/4 | 1/20 | 1/20 | 2/20 | 2/20 |
| ASAP, 4/4 | 18/20 | 3/20 | 0/20 | 0/20 |
| Ballroom, all | 21/40 | 6/40 | 10/40 | 17/40 |
| Ballroom, 3/4 | 1/20 | 2/20 | 0/20 | 0/20 |
| Ballroom, 4/4 | 20/20 | 4/20 | 10/20 | 17/20 |

No meter/downbeat result reached confidence 75 on either development split, so
the high-confidence error counts are zero but do not offset the low accuracy.

## Candidate disposition and next engineering boundary

An offline reference-beat experiment found that bass-weighted spectral energy
could identify the annotated bar phase on 28/40 Ballroom development tracks.
Enabling the existing experimental multiband onset path and consuming its
bounded low/mid energy in the native meter stage did not reproduce that gain:
downbeat moved only from 6/40 to 7/40, meter fell from 21/40 to 20/40, and 3/4
recall fell to 0/20. That candidate was removed.

A second bounded candidate jointly evaluated reciprocal 3:2 tempo families and
triple-meter evidence. It required a direct 3:2 relation, at least 60% local S4
correlation support, a positive grid fit, an S6 segment within 6%, a raw 3/4
margin over 4/4 of at least 0.04, and stronger 3/4 evidence than the currently
selected grid. On Ballroom development it improved meter from 21/40 to 26/40
and period within 10% from 17/40 to 20/40 while leaving every 4/4 metric
unchanged. However, downbeat fell from 6/40 to 5/40. On the independent ASAP
development split, meter fell from 19/40 to 17/40: 3/4 recall stayed at 1/20
while 4/4 recall regressed from 18/20 to 16/20. Combining the candidate with
the multiband onset experiment also regressed Ballroom 4/4 results. A bounded
downbeat-continuity follow-up did not recover the lost phase result. The joint
candidate and follow-up were therefore removed and are not production code.

The dominant defect still precedes meter phase, but these results reject a
hand-tuned 3:2 threshold gate as the next production boundary. Further work
needs richer temporal evidence that can distinguish triple-meter beat lattices
without sacrificing duple recall, followed by the same independent-development
veto before any holdout is opened. It must keep TEMP/LGRD/MTRD internally
consistent and must not merely relabel meter on top of the wrong local grid or
invent an unsupported tempo after publication.

Only the development partitions may inform that work. Once a candidate is
frozen and passes all normal tests plus the contaminated development reruns,
each untouched holdout may be run once. Holdout results then decide whether the
candidate is retained; they must not become another tuning loop.

## Rejected low-band accent phase candidate — 2026-08-25

An offline prototype on the Ballroom development partition suggested the
downbeat-phase premise was worth a native candidate: peak-normalized
bass-weighted accent energy added to broadband beat strengths improved phase
identification from 25/40 to 31/40 at an accent weight of 0.5, with a plateau
across 0.2-0.8 rather than a tuned peak.

The native candidate stored a quantized kick-band sum in every onset bin
behind a new opt-in build flag, filled a parallel bounded low-band rise series
during the existing S4 evidence refresh, and combined the two channels inside
the meter selection only. Tempo, grid and waveform paths were byte-identical
to the default build, verified by a full 114-test pass in both configurations.
The flag-off configuration also passed with warnings treated as errors.

The frozen development-partition rerun rejected the candidate:

| Metric | Baseline `a72e773` analyzer | Low-accent candidate |
|---|---:|---:|
| Meter | 21/40 | 20/40 |
| Downbeat | 6/40 | 5/40 |
| Period within 1% | 10/40 | 11/40 |
| Period within 10% | 17/40 | 17/40 |

Run metadata: source revision
`0f695dd9a5e8e564a3e2b82bd0073c932ec1be4a`, candidate analyzer SHA-256
`282364ed9d753ef0454528b2bcef60b9d6523ae173fa481f16bbd17ff9f88894`, manifest
SHA-256 unchanged (`0dc703b2...`). The holdout split was not opened.

The prototype gain did not transfer. The offline probe sampled STFT spectral
low-band flux at ground-truth beat times, while the native front end derives
beat positions from its own grid and its one-pole post-rectification envelope
is highly correlated with the broadband rise, so the second channel added
correlated noise instead of independent bar-phase evidence. Two prior accent
candidates failed the same way, which now indicates the defect is the front
end's lack of frequency selectivity before rectification rather than the
combination rule.

The candidate was removed and is not production code. Any successor must first
demonstrate, offline and on the development partitions only, that its accent
channel decorrelates from the broadband rise on APTA's own beat lattice — for
example a resonant bandpass before rectification — before another native
integration is attempted.

## Rejected resonant-bandpass accent candidate — 2026-08-25

The documented successor condition was met offline: a biquad bandpass applied
BEFORE rectification (75 Hz, Q 1.0, plateau across 55-95 Hz and Q 0.7-1.5)
decorrelated from an STFT broadband reference at Pearson 0.25 on APTA's own
lattice, and combining it with broadband beat strengths improved offline phase
identification from 16/40 to up to 21/40 with a consistent neighbourhood.

The native candidate stored the quantized bandpass energy in the same opt-in
low-storage layout, filled the parallel bounded rise series during the S4
evidence refresh, and combined channels inside meter selection only. Both
build configurations passed all 114 tests with warnings as errors.

The frozen development-partition rerun rejected the candidate again:

| Metric | Baseline `a72e773` analyzer | Bandpass accent candidate |
|---|---:|---:|
| Meter | 21/40 | 20/40 |
| Downbeat | 6/40 | 5/40 |
| Period within 1% | 10/40 | 11/40 |
| Period within 10% | 17/40 | 17/40 |

Run metadata: source revision
`0f695dd9a5e8e564a3e2b82bd0073c932ec1be4a`, manifest SHA-256 unchanged
(`0dc703b2...`). The holdout split was not opened.

Post-run diagnosis explains both failures and closes the per-beat accent
direction. Measured against the native production flux itself — the rectified
one-pole envelope rise — the pre-rectification bandpass accent correlates at
Pearson **0.634** (mean over the development partition), not 0.25: the native
envelope is already dominated by low-frequency energy, so a frequency-selective
amplitude channel duplicates it. A follow-up offline probe of a spectral-shape
channel (instantaneous bandpass/broadband energy share sampled at beat
positions) carried no usable bar-phase evidence either (9-10/40 against a
10/40 same-proxy reference; 1-2/12 on the period-correct subset).

The candidate was removed and is not production code. The remaining boundary
is no longer channel combination but evidence resolution: bar-phase decisions
need a fundamentally different signal than per-beat amplitude samples of the
existing front end — for example long-window periodicity of a dedicated
low-band series at the bar period (S6-style comb evidence) evaluated against
the same development partitions before any native integration.

## Rejected decayed-aggregation (comb) candidate — 2026-08-25

The comb premise was probed offline with both STFT series and faithful native
emulations. Decayed accumulation (one-pole, two-beat time constant, plateau
across 1-2.5 beats) of the bandpass accent improved the period-correct subset
from 2/12 to 4/12 in the native-emulated proxy and to 5/12 in the STFT proxy,
so the candidate was promoted to a minimal native trial: the same causal leaky
integration applied to the existing broadband beat strengths inside meter
collection only, behind an opt-in flag, with no storage or front-end changes.
All 114 tests passed in the flag configuration with warnings as errors.

The frozen development-partition rerun rejected it:

| Metric | Baseline `a72e773` analyzer | Comb aggregation candidate |
|---|---:|---:|
| Meter | 21/40 | 20/40 |
| 3/4 meter recall | 1/20 | 0/20 |
| Downbeat | 6/40 | 6/40 |
| 4/4 downbeat | 4/20 | 3/20 |
| 3/4 downbeat | 2/20 | 3/20 |

Run metadata: source revision `0f695dd9a5e8e564a3e2b82bd0073c932ec1be4a`,
candidate analyzer SHA-256
`18a45ad5039d23b5ac03c564c62cf952298b03ebbf9b2268fece330f5f58a232`, manifest
SHA-256 unchanged (`0dc703b2...`). The holdout split was not opened.

This third consecutive proxy-to-native transfer failure is itself the finding.
Each offline probe predicted a gain on the same partitions that the native run
did not deliver: proxies differ from the pipeline in lattice precision (Q32
refined periods versus integer bins), joint meter selection with the triple
prior, confidence gating and exact flux definitions, and those differences are
the same order as the measured effects.

The candidate was removed and is not production code. Candidate design through
offline audio proxies is closed for this stage. The required next tool is a
native meter trace — an opt-in diagnostic dump of APTA's own per-beat lattice,
exact refined periods and actual internal beat-strength series from real
analyzer sessions — so future phase candidates are designed against the true
native evidence rather than reconstructions of it.

## Native meter trace tool — 2026-08-25

The required trace capability now exists: `apta-meter-trace`, built only with
`-DAPTA_ENABLE_EXPERIMENTAL_METER_TRACE=ON`, runs a real pull-mode session and
emits one NDJSON record with APTA's published Q32 lattice, the meter selection,
the sampling lag, first beat bin and the actual internal broadband strength
series the meter stage scored. The default build configuration is unchanged
and passes all 114 tests; the flag-off build contains none of the capture
code paths.

Process correction: a batch tracing loop over the prepared Ballroom audio
directory also reached the 40 holdout tracks before the split boundary was
noticed. The 40 holdout trace files were deleted unread and no holdout trace
content was inspected or used; the development-partition traces (40 records)
are retained as local-only candidate-design evidence.

## Trace analysis closes the meter-front investigation — 2026-08-25

The native traces were analyzed against the development labels to answer,
finally, whether any scoring rule could recover bar phase from the existing
broadband per-beat series.

Findings:

- lattice period is within 1% on 10/40 tracks and within 10% on 17/40; the
  remaining 23/40 sit above a 10% period error (20 of them above 25%), so the
  lattice does not follow the music at all there;
- the annotated bar phase maps consistently onto the lattice on only 14/40
  tracks;
- the oracle ceiling — period within 1% AND consistent bar phase, i.e. every
  track where ANY scoring rule could succeed on this evidence — is **9/40**;
- the production contrast scoring already picks the true phase on 9/40, i.e.
  it operates at that ceiling; the mean top contrast is statistically the same
  for tracks it gets right (2.53) and wrong (2.66), so no threshold or rule
  change can separate them.

Conclusions, now evidence-backed rather than assumed:

1. The broadband per-beat strength series is exhausted as bar-phase evidence.
   Its information ceiling (~22% of tracks) is far below the >=90% acceptance
   gate, and current scoring sits at that ceiling.
2. Downbeat accuracy cannot be improved inside the meter stage. The dominant
   defect is upstream: beat-period/lattice quality (only 10/40 within 1%),
   which meter and downbeat scoring inherit.
3. Meter/downbeat candidate work is therefore deprioritized in favour of the
   upstream tempo/grid ensemble boundary (Task 5) until a qualifying lattice
   exists. Any future meter front-end work must introduce genuinely new
   evidence (for example dedicated low-band storage feeding an S6-style
   bar-periodicity estimator), not recombine the broadband channel.

This document's scope ends here: further updates on downbeat belong to the
tempo/grid ensemble evaluation record until its gates close.
