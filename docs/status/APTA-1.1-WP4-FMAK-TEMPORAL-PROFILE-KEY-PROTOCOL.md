# APTA 1.1 WP4 FMAK temporal-profile key protocol

- **Status:** pre-registered; selected audio remains unopened
- **Evidence class:** independent local development; no acceptance claim
- **Protocol baseline:** `6370eb2abeb027b9dfdd30aa327e4e13648e5d3c`
- **Candidate flag:** `APTA_ENABLE_EXPERIMENTAL_TEMPORAL_PROFILE_KEY=ON`
- **Formal holdout:** the unopened 48-track GiantSteps-MTG split frozen by
  `APTA-1.1-WP4-GIANTSTEPS-KEY-DEVELOPMENT-PROTOCOL.md`

## Failure mode and one-axis hypothesis

The rejected temporal chord-state candidate demonstrated that temporal tonal
evidence can produce positive net fixes, but hard classification of each
one-second window discarded most of the 24-state profile evidence and created
36 new high-confidence errors. Production has the complementary problem: it
adds every window to one lifetime chroma before profile scoring, so loud or
long passages dominate and window-level agreement is lost.

This candidate changes only the temporal aggregation axis. It retains the
production spectral bins, logarithmic compression, 12-bin chroma, fixed
Temperley/Kostka-Payne major/minor profiles, 24-state ordering, publication
interval, stable-window rule and candidate count. It does not classify chords,
change profiles, add label-dependent weights or inspect confidence while
choosing a key.

For each completed one-second window, compute all 24 ordinary non-centered
cosine profile scores `s[j]` from its local chroma. Let `f=min(s)` and
`e[j]=max(0,s[j]-f)`. If `sum(e)` is positive, add `e[j]/sum(e)` to state `j`
and increment the valid-window count. At refresh, rank states by accumulated
support divided by the valid-window count. This is equal-window soft evidence:
each valid window contributes total weight one, every relative profile score is
retained, and no single winner or loudness-dependent margin controls the vote.

The existing candidate score encoding and confidence formula remain unchanged.
The candidate adds exactly 24 float support values and one 32-bit valid-window
counter under its opt-in compile flag. It adds no resonator, result field, wire
byte or default-build state. All existing experimental key candidates remain
mutually exclusive.

No profile, floor, normalization, weight, confidence rule, threshold or
selection rule may change after this protocol is committed in response to the
new split.

## Independent and disjoint source boundary

The source, checksum and legal boundaries remain those documented for FMAK
version 1.0 in
`APTA-1.1-WP4-FMAK-TEMPORAL-CHORD-KEY-PROTOCOL.md`. The frozen transport archive
is already present locally because it contains the spent first split. Before
this protocol, only metadata and the already-spent 96 audio members were
decoded or analyzed. None of the 96 members selected below was extracted,
decoded, analyzed or auditioned.

Source audio, IDs, mappings, labels, licenses and filenames remain in ignored
private storage and never enter tracked evidence or packages. This is research
development evidence, not release acceptance and not a redistribution claim.

## Frozen disjoint 96-track selection

Inventory validation is inherited unchanged from the spent FMAK protocol:
exactly 5,489 metadata rows, 695 rows in the frozen `000-019.zip` ID range and
all 24 tonic/mode classes. First reproduce the spent selection and require its
seal to equal
`44a78001a6fbdc92975eed0594e5693b8cfc0dbc54c4b31385ec63ec833772cc`.
Exclude all 96 of those IDs, leaving exactly 599 eligible rows.

For each mode in `major, minor` order and tonic in numeric `0..11` order:

1. sort remaining rows by lowercase SHA-256 of
   `apta-1.1-fmak-temporal-profile-v1:track:<decimal-track-id>`, then numeric ID;
2. take the first four rows;
3. sort the final selection by tonic, mode and numeric source ID solely for
   deterministic private materialization.

This produces four rows per class, 48 major, 48 minor, 96 total and exactly
zero overlap with the spent split. The new private selection seal is SHA-256
`1c94629c8513fbab9d97e4c05e1394a9b9e1cd5070e126ddd0a0ce0ab1e89121`
over the same compact key-sorted private selection representation used by the
prior protocol. `tools/apta_1_1_fmak_temporal_profile_key_development.py`
recomputes both seals and fails closed on inventory, prior-selection, class,
count or overlap drift.

Preparation must verify the complete archive MD5 before extraction, select
exactly these members and canonicalize them to 48 kHz stereo PCM16 WAV with
metadata removed. Canonical audio receives opaque IDs derived from full audio
SHA-256. Tracked output may retain only aggregate counts and seals.

The split is spent when the first complete baseline/candidate comparison is
inspected. No rerun may be used to choose a formula, threshold or confidence
rule. A failed candidate requires a new pre-registration and genuinely new
evidence.

## Frozen experiment and promotion gates

Baseline and candidate must be Release warnings-as-errors builds from the same
exact committed source revision. They use identical canonical audio,
`SOURCE_DATE_EPOCH`, `--features all`, opaque order and export/evaluation code.
The first comparison records exact and per-mode accuracy, fixes, breaks,
changed verdicts and new high-confidence errors.

The temporal-profile candidate may approach the one-shot MTG holdout only if
every gate passes:

- exact key accuracy is at least 70%;
- major and minor exact accuracy are each at least 60%;
- fixes exceed breaks and total exact accuracy exceeds production;
- no new incorrect key is published at confidence at or above 75;
- default analyzer output and ordinary tests are unchanged;
- candidate warnings-as-errors and sanitizer suites pass;
- conditional key state and session workspace each increase by at most 128
  bytes;
- result-pool bytes are unchanged and no resonator is added.

Failure of any gate rejects the candidate and keeps the formal holdout closed.
Passing all development gates makes it only holdout-eligible; it is not an
acceptance or release result.

