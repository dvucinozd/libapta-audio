# APTA 1.1 WP4 FMAK temporal chord-state key protocol

- **Status:** pre-registered; audio unopened and first run not started
- **Evidence class:** independent local development; no acceptance claim
- **Protocol baseline:** `a5dc66a81db2e5715fe5f736dac1fbedd9131b1e`
- **Candidate flag:** `APTA_ENABLE_EXPERIMENTAL_TEMPORAL_CHORD_KEY=ON`
- **Formal holdout:** the unopened 48-track GiantSteps-MTG split frozen by
  `APTA-1.1-WP4-GIANTSTEPS-KEY-DEVELOPMENT-PROTOCOL.md`

## Failure mode and one-axis hypothesis

Production folds every one-second tonal window into one lifetime chroma before
selecting a key. That destroys chord succession: a pitch class carried by a
short but harmonically decisive tonic or dominant is indistinguishable from
the same accumulated energy produced by melody, percussion leakage or a
different chord. The two spent candidates changed octave/harmonic projection
and the final profile comparison, but neither introduced temporal harmonic
state.

This candidate changes only that evidence axis. Every completed one-second
window produces a local 12-bin chroma in addition to the unchanged production
accumulation. The candidate classifies one local major/minor triad, weights the
vote by the winning margin and accumulates bounded support for 24 global keys.
No label, genre, title, confidence result or corpus output may choose a weight,
threshold or formula after this protocol is committed.

For a local chroma `x`, each tonic-rotated triad template has weights `1.0` on
root, `1.0` on its major or minor third, `0.7` on the perfect fifth and `0.0`
elsewhere. All 24 chord scores use cosine similarity. Let `c1` and `c2` be the
best and second-best chord scores and `m=max(0,c1-c2)`. A tie therefore adds no
evidence; there is no post-hoc margin threshold.

The winning chord adds `m*w` to each compatible global-key state using these
frozen scale-degree weights:

| Global mode | Winning chord evidence | Weight |
|---|---|---:|
| major | I major / V major / IV major | 1.00 / 0.85 / 0.70 |
| major | vi minor / ii minor / iii minor | 0.60 / 0.45 / 0.30 |
| minor | i minor / V major / iv minor | 1.00 / 0.85 / 0.70 |
| minor | III major / VI major / VII major | 0.60 / 0.45 / 0.30 |

At refresh, each global-key score is accumulated support divided by the sum of
all nonzero winning margins. The normal three-candidate ordering, score
encoding, publication interval, stable-window rule and existing confidence
formula remain unchanged. The candidate adds exactly 24 float support values
and one float margin sum under its opt-in compile flag; it adds no resonator,
result field, wire byte or default-build state.

## Independent source and legal boundary

Development uses FMAK version 1.0, published as Zenodo record
`10.5281/zenodo.10719860`, and its Free Music Archive audio:

- 5,489 expert/manual song-level key and mode annotations across 17 genres;
- annotation/source checkout
  `23dbccd73584af14c65528298b17f64f14ec11d4`;
- frozen `fma_keys_metadata.csv` MD5
  `d80a03bc8659edc60e335bd7f6bdf12a` and SHA-256
  `7ec4bd22eb5ff7958fbf9d8c44869f955fc6b50b67896b229665b3b92e80190d`;
- frozen `000-019.zip` size 4,150,442,299 bytes and MD5
  `b86f6414820c1422b2c6cdf87be1ef3a`.

FMA metadata is published under CC BY 4.0, while each audio item retains the
license chosen by its artist. FMAK and FMA describe the material as research
data. Consequently audio, source IDs, source mappings, per-track licenses and
labels stay in ignored local build storage. Nothing from the corpus is shipped
in a package or release, and this experiment makes no acceptance or
redistribution claim.

Only metadata and checksums were inspected before this protocol. No FMAK audio
was downloaded, decoded, analyzed or auditioned, and the formal MTG holdout was
not opened.

## Frozen 96-track selection

The inventory parser must accept exactly 5,489 unique positive integer track
IDs and exactly one supported `tonic major|minor` label per row. Enharmonic
flats normalize to the existing C=0 through B=11 sharp-tonic representation.
Only IDs below 20,000 are eligible because they are contained in the single
frozen `000-019.zip` transport object. The frozen metadata contains 695 such
rows and at least 11 rows in every one of the 24 classes.

For each mode in `major, minor` order and tonic in numeric `0..11` order:

1. sort eligible rows by lowercase SHA-256 of
   `apta-1.1-fmak-temporal-chord-v1:track:<decimal-track-id>`, then numeric ID;
2. take the first four rows;
3. sort the final selection by tonic, mode and numeric source ID solely for
   deterministic private materialization.

This yields exactly four rows per class, 48 major, 48 minor and 96 total. The
private selection seal is SHA-256
`44a78001a6fbdc92975eed0594e5693b8cfc0dbc54c4b31385ec63ec833772cc`
over the compact, key-sorted JSON array of decimal `source_id`, numeric
`key_tonic` and lowercase `key_mode`, followed by one newline.

Preparation must verify the complete archive MD5 before extraction, reject any
missing or extra selected member, and canonicalize selected audio to 48 kHz
stereo PCM16 WAV with metadata removed. Canonical audio receives an opaque ID
derived from its full SHA-256. Tracked output may contain aggregate counts and
seals, never source membership, titles, URIs or private mappings.

The split is spent after the first baseline/candidate report is inspected. No
formula, chord template, scale-degree weight, threshold, profile, confidence
rule or selection rule may change in response to it. A different candidate
requires another pre-registration and genuinely new evidence.

## Frozen experiment and promotion gates

Baseline and candidate must be Release warnings-as-errors builds from the same
exact committed source revision. They use identical canonical audio,
`SOURCE_DATE_EPOCH`, `--features all`, opaque order and export/evaluation code.
The first comparison records exact and per-mode accuracy, per-class accuracy,
fixes, breaks, changed verdicts, error families and high-confidence errors.

The temporal candidate may approach the one-shot MTG holdout only if every
development gate passes:

- exact key accuracy is at least 70%;
- major and minor exact accuracy are each at least 60%;
- fixes exceed breaks and total exact accuracy exceeds production;
- no new incorrect key is published at confidence at or above 75;
- default analyzer output and ordinary tests are unchanged;
- candidate Werror and sanitizer suites pass;
- conditional key-analysis state and session workspace each increase by at
  most 128 bytes, result-pool capacity is unchanged, and no resonator is added.

Failure of any gate rejects the candidate and leaves the MTG holdout unopened.
If all gates pass, freeze the exact revision, flag and formulas, then run the
48-track MTG holdout once. Retention requires at least 70% exact accuracy, at
least 60% accuracy in each mode, positive transfer over production and zero new
high-confidence errors. A failed holdout is spent and cannot be tuned against.

Even a retained key candidate is not final DJ acceptance evidence. It must
join transferable beat-lattice and downbeat work, pass WP6 integration, and
then face the newly verified WP7 corpus under its unchanged evaluator.
