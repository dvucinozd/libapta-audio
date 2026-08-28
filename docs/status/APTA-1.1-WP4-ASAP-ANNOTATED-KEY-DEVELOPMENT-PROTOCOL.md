# APTA 1.1 WP4 ASAP annotated-key development protocol

- **Status:** pre-registered; label values not inspected
- **Frozen baseline revision:** `af7398e19d448a9f9617df0f128196fa1e86ee54`
- **Evidence class:** independent development only
- **Annotation source:** ASAP `perf_key_signatures`
- **ASAP rhythm split:** reuse frozen development IDs only
- **ASAP holdout:** unopened for key labels and evaluation
- **Acceptance claim:** false

## Purpose and separation from D1

The frozen MIDI-meta-event derivation in
`APTA-1.1-WP4-ASAP-KEY-DEVELOPMENT-PROTOCOL.md` is nonviable and remains
unchanged. This D2 protocol tests a distinct, independently authored source:
ASAP's performance key-signature annotations. It is not a fallback for missing
MIDI events and does not combine, replace or infer any D1 label.

This protocol is committed before inspecting any selected
`perf_key_signatures` value.

## Fixed input boundary

Use the existing frozen ASAP preparation and the exact ASAP source checkout
already bound by that preparation:

- `prepared/labels.json` identifies exactly 40 development track IDs;
- `prepared/sources.private.json` maps those IDs to source paths, frozen audio
  hashes and window starts;
- `prepared/manifest.json` supplies the frozen 30-second window duration;
- `prepared/audio/track-<opaque>.wav` supplies the existing development audio;
- `asap_annotations.json` supplies only the annotation row addressed by each
  selected development source path.

The labeler must require the ASAP Git revision already frozen by the meter
corpus (`afc815c75c42e83a79c03feb6da8a35e77d4c6b8`) and bind the complete
annotation file by SHA-256. It must not enumerate, report, derive or score a key
for a holdout ID. Public evidence contains only opaque IDs and aggregate reason
counts; private derived labels remain under ignored build paths.

## Frozen D2 label derivation

For each selected development source path, read `perf_key_signatures` as a
mapping from finite, non-negative performance seconds to a key token. Sort
events numerically; duplicate numeric times, non-increasing event order after
normalization, malformed containers and non-string tokens are invalid.

Normalize tokens using only the following grammar:

- tonic is one of `C`, `C#`, `Db`, `D`, `D#`, `Eb`, `E`, `Fb`, `E#`, `F`,
  `F#`, `Gb`, `G`, `G#`, `Ab`, `A`, `A#`, `Bb`, `B` or `Cb`;
- an exact trailing lowercase `m` denotes minor; no suffix denotes major;
- enharmonic spellings map to the same pitch class;
- tonic pitch classes use the public APTA order C=0 through B=11.

At the frozen audio `window_start_seconds`, select the last annotation at or
before the boundary. Exclude the track, with an opaque reason code, when:

- there is no matching annotation row or no `perf_key_signatures` mapping;
- there is no active signature at the window start;
- a later event inside the prepared audio window changes tonic or mode;
- an event time or token violates the frozen grammar;
- the prepared audio hash or frozen source-path mapping does not verify.

Repeated identical signatures are not modulation. The interval is
`(window_start_seconds, window_end_seconds)`, so an event exactly at the end is
outside the prepared evidence. Do not consult `midi_score_key_signatures`, MIDI
meta events, notes, filenames, score metadata or any analyzer. Do not replace
excluded rows.

The output CSV schema is exactly `track,key_tonic,key_mode`. Its canonical JSON
manifest binds the ASAP revision, full annotation-file hash, preparation input
hashes, label hash, included opaque IDs, excluded opaque IDs/reason codes,
derivation-tool hash and Python runtime.

## Viability and candidate gates

- require at least 24 included development tracks;
- require at least one major and one minor track;
- run the derivation twice and require byte-identical CSV and manifest output;
- freeze both artifacts before running any APTA key build;
- if viable, run production baseline and the already-frozen harmonic-projection
  candidate once on the exact same included audio set;
- require positive net exact-key fixes, at most one break and no new
  high-confidence key error before the candidate may return to spent-DJ
  comparison;
- require at least 70% exact-key accuracy on this independent development set
  before any formal key holdout is considered.

Failure closes D2 or the candidate without changing these rules, opening any
ASAP holdout key evidence, consuming a fresh DJ acceptance corpus or making an
acceptance claim.
