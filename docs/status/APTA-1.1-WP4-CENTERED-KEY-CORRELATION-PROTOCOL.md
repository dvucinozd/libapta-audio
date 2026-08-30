# APTA 1.1 WP4 centered key-correlation development protocol

- **Status:** pre-registered 2026-08-30; no analyzer result inspected
- **Evidence class:** independent local development; no acceptance claim
- **Protocol baseline:** `36b0aa88556f34a833f2c7208a3d9a510484e05a`
- **Candidate flag:** `APTA_ENABLE_EXPERIMENTAL_CENTERED_KEY_CORRELATION=ON`
- **Formal holdout:** the unopened 48-track GiantSteps-MTG split frozen by
  `APTA-1.1-WP4-GIANTSTEPS-KEY-DEVELOPMENT-PROTOCOL.md`

## Failure mode and hypothesis

The rejected balanced GiantSteps-MTG development run exposes a strong mode
bias: production is exact on only 1/48 major tracks and 20/48 minor tracks, and
29/96 errors are parallel-mode errors. The implementation describes its key
profile comparison as correlation, but currently uses cosine similarity on
strictly non-negative chroma and profile values. Cosine similarity is not
invariant to a constant broadband chroma pedestal. A direct, label-free profile
check demonstrates the defect: adding a constant `1.0` to every bin of the
exact C-major KP profile changes the raw cosine preference from C major to C
minor, although the pitch-class contrast is unchanged.

The candidate changes only the profile comparison to mean-centered normalized
correlation. For chroma `x` and a tonic-rotated profile `p`, the score is:

```text
sum_i((x_i - mean(x)) * (p_i - mean(p))) /
sqrt(sum_i((x_i - mean(x))^2) * sum_i((p_i - mean(p))^2))
```

This representation should remove common-mode spectral energy while retaining
the relative pitch-class shape that distinguishes tonic and mode. It does not
change Goertzel bins, windows, profiles, confidence thresholds, result layout,
wire bytes or default behavior.

## Independent development source and legal boundary

Development uses the original official GiantSteps key dataset metadata:

- repository: <https://github.com/GiantSteps/giantsteps-key-dataset>;
- exact checkout: `6bcd492c825ac9b8597bc650a5f6fd18b6c43d2b`;
- inventory: 604 unique Beatport preview IDs with key annotations and published
  transport MD5 values;
- primary transport: the repository's current JKU backup URL,
  `https://www.cp.jku.at/datasets/giantsteps/backup/`.

A pre-registration transport probe confirmed HTTP 200 for the first three
sorted objects and an exact published MD5 match for the downloaded first
object. The repository does not contain an explicit license file and its README
asks users to contact the creators for more information. Consequently this
source is local research-only development evidence: audio, selected membership,
mappings and labels are never redistributed or committed, and it cannot serve
as a formal holdout, final acceptance corpus or release artifact. The
separately licensed and unopened GiantSteps-MTG holdout remains the only formal
WP4 transfer gate.

## Frozen 96-track selection

All 604 `.key` annotations must parse to exactly one canonical tonic and
`major|minor`, must match the corresponding `sources.xlsx` global-key value
after flat-to-sharp normalization, and must have a matching `.md5` entry. The
tool rejects a dirty or wrong source revision, missing/duplicate IDs, unknown
labels, workbook disagreement, missing hashes or changed inventory.

Selection is mode-balanced and tonic-stratified without consulting APTA:

1. For each mode and tonic class, sort rows by lowercase hexadecimal SHA-256
   of `apta-1.1-giantsteps-original-centered-v1:track:<transport-id>`.
2. Take up to four rows from every tonic class. This yields 45 major and 48
   minor rows because C-sharp major has only one source item.
3. Fill the remaining three major positions from all unused major rows ordered
   by the same hash. No minor fill is required.
4. Sort the resulting 48 major and 48 minor rows by canonical class and private
   source ID solely for deterministic local materialization.

The private selection seal records the complete ordered membership hash but no
source ID or title is emitted to tracked evidence. Canonical audio is 48 kHz
stereo PCM16 WAV with metadata removed and an opaque name derived from its full
SHA-256. The preparation tool validates the published MP3 MD5 before
conversion and the canonical geometry afterward.

This development split is spent after the first baseline/candidate result is
inspected. No threshold, formula, profile, selection rule or confidence rule
may be changed in response to it. A different candidate needs another
pre-registration and genuinely new development evidence.

## Frozen experiment and gates

Baseline and candidate are Release warnings-as-errors builds from the same
exact committed source revision. Both use identical canonical audio,
`SOURCE_DATE_EPOCH`, `--features all`, opaque ordering and export/evaluation
tooling. The report records revision, flags, analyzer/manifest hashes, exact
accuracy, major/minor accuracy, per-class accuracy, fixes, breaks, changed
verdicts and high-confidence errors.

The candidate may approach the one-shot MTG holdout only if every development
gate passes:

- exact key accuracy is at least 70%;
- major and minor accuracy are each at least 60%;
- fixes exceed breaks and total exact accuracy exceeds production;
- no new incorrect key is published at confidence at or above 75;
- default analyzer output and ordinary tests are unchanged;
- candidate Werror and sanitizer suites pass;
- `sizeof(apta_internal_key_analysis_t)`, session workspace and result-pool
  capacity are unchanged, and the candidate adds no resonator or persistent
  state.

Failure of any gate rejects the candidate and leaves the MTG holdout unopened.
If all gates pass, freeze the exact revision, flag and formula, then run the
48-track MTG holdout once. Retention requires at least 70% exact accuracy, at
least 60% accuracy in each mode, positive transfer over production and zero new
high-confidence errors. A failed holdout is spent and cannot be tuned against.

Even a retained key candidate is not final DJ acceptance evidence. It must join
a transferable lattice/downbeat candidate, pass WP6 integration and then face
a newly verified at-least-48-track WP7 corpus under the unchanged frozen
evaluator.
