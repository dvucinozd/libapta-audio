# Phase 4 — Independent Rekordbox tempo-corpus status

**Measurement date:** 2026-08-02

**Source branch:** `agent/phase4-rekordbox-corpus` from merge `7644565`

**Corpus role:** frozen external validation; not used to select current parameters

**Audio policy:** no source recording or decoded window enters the repository

## 1. Why this corpus exists

The existing tempo and endorsement rules were developed and measured on 68
Rekordbox-annotated tracks. In particular, the S6 endorsement tolerances and
candidate-score threshold were selected on that same set. Section 28.4 of the
S4 status record therefore called a second annotated library the missing test
of whether the result generalized.

Phase 4 supplies that test from a previously untouched Rekordbox USB export.
The baseline was frozen and all three modes were run before interpreting any
outlier or changing any algorithm parameter:

- S4 alone;
- S4 with S6 requested, allowing the existing endorsement rule;
- S6 global-grid nominal tempo.

This is independent data, not an independent implementation of APTA and not a
human-certified tempo catalogue. Ground truth remains the tempo a Rekordbox
user sees and mixes against.

## 2. Read-only inventory and integrity boundary

The FAT32 device contained:

- 191 content rows in `exportLibrary.db`;
- 190 visible MP3 files;
- 189 `ANLZ0000.DAT` files visible to the initial PowerShell traversal;
- Rekordbox `DAT`, `EXT` and `2EX` analysis sets under `PIONEER/USBANLZ`.

Three database-referenced analysis locations could not be used:

- two `USBANLZ` directories could not be traversed;
- one additional `ANLZ0000.DAT` could not be opened;
- one of those unavailable records also referenced an unreadable audio path.

Windows and WSL reported filesystem I/O errors for those locations (Windows
error 1392 / WSL invalid-argument traversal failure). No repair, `chkdsk`,
database update or write to the USB device was attempted. The importer records
the three exclusions and continues; it does not silently reduce the
denominator after analysis begins.

The resulting evaluation population is **188 tracks**. All 188 have:

- a readable source recording;
- a parseable `PPTH` path tag;
- a non-empty `PQTZ` beat grid;
- a successfully decoded local WAV window;
- one result in each of the three APTA modes.

The two source fingerprints are:

| Rekordbox file | SHA-256 |
|---|---|
| `PIONEER/rekordbox/export.pdb` | `eb1a4bb57da65ecf6be6f89afde16ce4b9f74e6f3c445ee5591d9e963098659e` |
| `PIONEER/rekordbox/exportLibrary.db` | `90e9bd5d3d0c495d5c2160d2e2e338e5e2a94d271e8de7da2bd1ea756fd113e3` |

## 3. Reproducible import contract

`tools/rekordbox_tempo_corpus.py` uses only the Python standard library for
Rekordbox parsing. It walks `PIONEER/USBANLZ`, reads the UTF-16BE track path
from `PPTH`, reads the beat entries from `PQTZ`, and selects the modal
hundredth-BPM value as ground truth. This is the same contract used for the
earlier 68-track measurement, now encoded and tested instead of performed as an
ad-hoc extraction.

For every eligible source, FFmpeg decodes:

- start at 0:30;
- duration 90 seconds;
- 44.1 kHz;
- stereo PCM16 WAV.

The 188 temporary windows total 2,984,714,476 bytes and remain under the
ignored local `build/` directory. Their filenames are opaque stable IDs derived
from normalized Rekordbox paths. Public manifests and reports contain those
IDs, BPM annotations and measurements, but no title, artist, source path or
audio.

The corpus covers 95.00 to 155.00 BPM, with a median of 124.015 BPM. Every one
of the 188 available `PQTZ` grids uses one constant tempo value; there is no
dynamic-ground-truth ambiguity inside this population.

Typical commands, with the USB treated as a read-only source by the workflow,
are:

```sh
python3 tools/rekordbox_tempo_corpus.py prepare \
  --device-root /mnt/f \
  --output-dir build/phase4-rekordbox \
  --jobs 4

python3 tools/rekordbox_tempo_corpus.py run \
  --prepared build/phase4-rekordbox \
  --corpus-exe build/tools/apta-tempo-corpus
```

`apta-tempo-corpus --results-csv FILE` is the new machine-readable boundary.
The Python runner verifies that every mode emits exactly the manifest
cardinality before it generates aggregate or per-track JSON.

## 4. Frozen baseline result

Accuracy means the reported tempo is within one percent of the modal Rekordbox
tempo, matching the existing harness classification.

| Mode | Within 1% | Within 0.1% | Metrical-ratio errors | Other errors | Median error among correct |
|---|---:|---:|---:|---:|---:|
| S4 | 143/188 (76.1%) | 112 | 4 | 41 | 0.030% |
| S4 + S6 endorsement | **166/188 (88.3%)** | **123** | **2** | **20** | 0.031% |
| S6 | 164/188 (87.2%) | 22 | 8 | 16 | 0.396% |

S6's one-percent count is strong, but it is still much less precise as a grid
clock. Among correct non-zero-error answers, median time to drift half a beat
is:

| Mode | Median minutes to half a beat |
|---|---:|
| S4 | 13.34 |
| S4 + S6 endorsement | 12.50 |
| S6 | **1.02** |

The within-0.1-percent column and drift time are why S6's 87.2 percent must not
be read as a replacement for the locally refined tempo.

The baseline run used the ordinary unoptimized local CMake build and took
29 minutes 4 seconds for all three modes. That elapsed time is harness context,
not a library performance result.

## 5. Endorsement generalizes as a net gain, not as a no-regression rule

On the original 68 tracks, endorsement fired eight times, fixed seven misses
and broke none. On the new 188-track corpus it:

- changed 43 selected tempi;
- fixed 27 S4 misses;
- broke 4 S4-correct tracks;
- produced a net gain of 23 exact tracks.

The accuracy benefit is large and real: 76.1 to 88.3 percent. The stronger
claim that promotion is harmless is false outside the development corpus.

One broken answer is particularly important: S4 was within 0.03 BPM of a
109.98 BPM annotation at confidence 79, while endorsement promoted 121.599 BPM
at the same confidence. This is not a one-percent boundary artefact. The other
three broken tracks cross the one-percent boundary by smaller margins, but
they still disprove “none broken”.

No endorsement threshold is changed here. Selecting a new tolerance or score
cutoff on all 188 validation tracks would merely repeat the fitting problem
this phase was created to detect.

## 6. The actionable-confidence requirement fails

The documented actionable gate remains frozen at confidence 75. At that gate:

| Mode | Correct admitted | Wrong admitted | Metrical-ratio errors |
|---|---:|---:|---:|
| S4 | 92 | 3 | **3** |
| S4 + S6 endorsement | 91 | 4 | **2** |
| S6 | 88 | 4 | **2** |

B1 requires zero high-confidence metrical-ratio errors. It therefore does not
hold on this independent corpus for any of the three modes.

The S4 failures are three clear half-time selections:

| Opaque ID | Rekordbox BPM | APTA BPM | Confidence |
|---|---:|---:|---:|
| `rbx-2c25ce6ff567f0c5` | 139.650 | 69.837 | 92 |
| `rbx-9dbb39f79866fb37` | 136.990 | 68.485 | 94 |
| `rbx-e00ee00692b89216` | 154.000 | 76.981 | 90 |

Endorsement corrects the first but retains the other two. It also introduces
two non-metrical errors above the gate, so its total wrong-admitted count rises
from three to four even though its metrical-ratio count falls.

The complete threshold sweep is:

| Gate | S4 correct/wrong | Endorsed correct/wrong | S6 correct/wrong |
|---:|---:|---:|---:|
| 50 | 138/19 | 145/12 | 164/24 |
| 55 | 135/10 | 137/8 | 163/22 |
| 60 | 123/6 | 122/7 | 162/18 |
| 65 | 115/5 | 114/6 | 153/15 |
| 70 | 111/3 | 109/5 | 122/9 |
| 75 | 92/3 | 91/4 | 88/4 |
| 80 | 77/3 | 77/3 | 36/1 |
| 85 | 68/3 | 69/2 | 7/0 |
| 90 | 53/3 | 54/2 | 0/0 |
| 95 | 16/0 | 16/0 | 0/0 |

Raising the S4 gate to 95 would remove observed errors but admit only 16 of 188
tracks, and the choice would be fitted to this validation set. It is recorded
as a sweep point, not adopted as a fix.

## 7. Confidence separation

| Mode | Correct mean | Incorrect mean | Incorrect max | Correct min |
|---|---:|---:|---:|---:|
| S4 | 79.3 | 51.3 | 94 | 40 |
| S4 + S6 endorsement | 74.7 | 56.8 | 94 | 31 |
| S6 | 74.2 | 66.2 | 80 | 52 |

The old corpus showed a clean high-confidence region for S4. This one does
not: the strongest wrong S4 result has confidence 94. Endorsement improves
selection accuracy but narrows rather than improves confidence separation.
Confidence and selection therefore need separate follow-up work.

## 8. Phase-4 conclusion and next boundary

Phase 4 achieved its primary purpose: a repeatable, previously untouched
corpus now tests assumptions learned from the first library.

The evidence supports these claims:

- the existing endorsement rule produces a substantial net accuracy gain;
- the rule is not no-regression outside its development corpus;
- S4 remains more precise than S6 when it is correct;
- confidence 75 is not a safe actionable gate on this population;
- B1's zero-high-confidence-metrical-error requirement is open again.

The next algorithm phase should first partition development and held-out data,
then diagnose the retained half-time selections and the four broken promotions.
Any new prior, endorsement or confidence rule must be selected on a training
partition and reported unchanged on a held-out partition. The 188-track frozen
baseline in this document remains the comparison point.
