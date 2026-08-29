# APTA 1.1 WP4 GiantSteps-MTG key-development protocol

- **Status:** pre-registered; no APTA result inspected
- **Evidence class:** independent development plus sealed one-shot holdout
- **Acceptance claim:** false
- **Protocol baseline:** `ace3efae660ec64ded05c23f605d00f02135c355`
- **Candidate flag:** `APTA_ENABLE_EXPERIMENTAL_HARMONIC_HPCP=ON`

## Purpose and isolation

The retained harmonic-HPCP path is safe on the spent 60-track DJ corpus but
could not be promoted because the two ASAP label derivations were nonviable.
This protocol reopens WP4 with a genuinely new global-key dataset before any
APTA result from that dataset is generated or inspected.

The source is the official GiantSteps MTG key dataset for electronic dance
music published by the Music Technology Group at Universitat Pompeu Fabra:

- dataset page: <https://www.upf.edu/web/mtg/giantsteps-key>;
- MTG metadata checkout:
  <https://github.com/GiantSteps/giantsteps-mtg-key-dataset> at
  `fd7b8c584f7bd6d720d170c325a6d42c9bf75a6b`;
- original GiantSteps test checkout, used only as an exclusion inventory:
  <https://github.com/GiantSteps/giantsteps-key-dataset> at
  `6bcd492c825ac9b8597bc650a5f6fd18b6c43d2b`.

The MTG annotation data is published under CC BY-SA 4.0. Beatport preview audio
is downloaded only through the dataset's published URLs, retained locally and
never redistributed or committed. Source IDs, titles, metadata, local paths,
audio and private split manifests remain below ignored build storage.

At the revisions above, the MTG checkout contains 1,486 transport IDs and the
original test checkout contains 604. Their transport-ID intersection is empty,
so no original GiantSteps test item is eligible. Exactly 1,159 MTG rows have a
single canonical `A..G[#] major|minor` label and confidence `2`; every one of
the 24 tonic/mode classes contains at least 22 eligible rows.

## Frozen selection and split

The preparation tool must reject any source-revision, inventory, overlap,
grammar, confidence or checksum mismatch. Eligible rows are grouped by the 24
exact key classes. Within each class they are ordered by lowercase hexadecimal
SHA-256 of:

```text
apta-1.1-giantsteps-mtg-key-v1:track:<transport-id>
```

The first four rows in each class form the 96-track `development` split. The
next two form the 48-track `holdout`; all remaining rows are unused. The splits
must be disjoint and each must contain identical class counts. Canonical audio
is 48 kHz stereo PCM16 with metadata removed and is renamed to an opaque ID
derived from the full canonical-WAV SHA-256.

Preparation materializes `development` only. The selection code may parse all
labels solely to make the frozen class-balanced split, but it emits only a hash
of the complete selection. It must not print or export holdout labels, fetch
holdout audio, or create a holdout analysis mapping. Materializing `holdout`
requires an explicit one-shot option and a full frozen candidate revision. The
private selection seal and all canonical audio stay untracked.

## Frozen experiment

The first development comparison changes only one evidence axis:

1. production folded chroma, no experimental key flag;
2. the existing bounded harmonic-HPCP projection with
   `APTA_ENABLE_EXPERIMENTAL_HARMONIC_HPCP=ON`.

Both builds use the same exact committed source revision, Release mode,
warnings as errors, `SOURCE_DATE_EPOCH`, `--features all`, canonical audio and
opaque ordering. The report records source revision, build flags, analyzer and
manifest hashes, exact accuracy, fixes, breaks, changed verdicts,
high-confidence errors and per-class accuracy.

No harmonic weight, profile, window, confidence threshold, label filter, split
size or selector rule may change after development results are inspected. A
different tonal-evidence hypothesis requires a new pre-registration and keeps
this development split spent for that hypothesis.

## Retain, holdout and stop gates

The harmonic candidate may approach the holdout only if all of the following
are true on development:

- exact key accuracy is at least 70%;
- fixes exceed breaks and exact accuracy is higher than production;
- no new error is published at key confidence at or above 75;
- default analyzer bytes and behavior are unchanged;
- candidate Werror and sanitizer coverage pass;
- conditional key state remains the already measured 144-byte delta, with no
  result-pool growth or new resonator work.

Failure of any condition rejects this candidate without opening the holdout or
changing a threshold. If all conditions pass, freeze the exact source, flag,
parameters and tests, then materialize and run the 48-track holdout once. The
holdout retains the candidate only at at least 70% exact accuracy, positive
transfer over production and zero new high-confidence errors. A failed holdout
is spent and cannot become a tuning set.

Neither split is final DJ acceptance evidence. Even a retained candidate must
later join a complete transferable lattice/downbeat candidate, pass WP6, and
then face a newly verified WP7 corpus under the unchanged frozen evaluator.

## Tooling preflight — 2026-08-30

The first implementation preflight, still before any APTA analyzer execution,
reproduces all 1,159 eligible rows, the exact 96/48 split, 4/2 rows in every key
class and complete disjointness. The SHA-256 of the private full-selection seal
is `1fadadc5e5df343558eeb476aa2346fc688bea6ebbc98a09a996a649be3b0146`.
This hash freezes membership without publishing source IDs or holdout labels.
