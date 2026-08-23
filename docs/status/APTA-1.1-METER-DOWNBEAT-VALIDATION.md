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

The dominant defect precedes meter phase. The current local period is within
10% on 17/20 Ballroom 4/4 tracks and 0/20 3/4 tracks. Ten of the 20 triple-meter
tracks place the annotated beat rate within 10% of a 2:3 or 3:2 relation of the
selected local tempo, and eight expose an approximately correct tempo in at
least one global segment. The next candidate must therefore evaluate bounded
3:2 tempo families jointly with triple-meter evidence and keep TEMP/LGRD/MTRD
internally consistent. It must not merely relabel meter on top of the wrong
local grid or invent an unsupported tempo after publication.

Only the development partitions may inform that work. Once a candidate is
frozen and passes all normal tests plus the contaminated development reruns,
each untouched holdout may be run once. Holdout results then decide whether the
candidate is retained; they must not become another tuning loop.
