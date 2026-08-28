# APTA 1.1 WP4 ASAP key-development protocol

- **Status:** pre-registered; no key-signature values or analyzer results read
- **Frozen baseline revision:** `0d5be302e5d1ed0799a3f4649d333e50d0738699`
- **Evidence class:** independent development only
- **ASAP rhythm split:** reuse frozen development IDs only
- **ASAP holdout:** unopened for key labels and evaluation
- **Acceptance claim:** false

## Purpose

The already-spent 60-track DJ corpus cannot establish transfer for another key
candidate. WP4 therefore reuses the legally prepared ASAP piano development
audio as an independent domain. Key truth comes from MIDI key-signature meta
events, not from APTA output or the DJ labels.

This protocol is frozen before inspecting any selected key-signature value.

## Fixed input boundary

Use the existing ASAP preparation artifacts and nothing from its holdout split:

- `prepared/labels.json` identifies the 40 development track IDs;
- `prepared/sources.private.json` maps only those IDs to their source MIDI,
  window start and frozen audio hash;
- `prepared/audio/track-<opaque>.wav` is the existing canonical 48 kHz mono
  development audio;
- the source MIDI is opened only after its opaque ID is confirmed to be in the
  development set.

Do not enumerate, derive, store or score a key for an ASAP holdout ID. The
private source mapping and derived labels remain in ignored build directories;
public reports contain only opaque IDs and aggregate counts.

## Frozen label derivation

For each development MIDI, merge tracks in standard MIDI order and integrate
tempo messages to absolute seconds exactly as `mido` defines. Collect
`key_signature` meta messages and normalize their `key` field:

- a name without trailing `m` is major;
- a name with trailing `m` is minor;
- enharmonic spellings map to the same pitch class;
- tonic pitch classes use the public APTA order C=0 through B=11.

At the frozen audio `window_start_seconds`, select the last key signature at or
before the boundary. Exclude the track, with an opaque reason code, when:

- there is no active key signature at the window start;
- a later key-signature event inside the prepared audio window changes tonic or
  mode;
- a key token cannot be normalized;
- the source MIDI or prepared audio hash does not match its frozen mapping.

Repeated identical key-signature events are not modulation and remain valid.
Do not infer a missing key from notes, filenames, score metadata or an analyzer.
Do not replace excluded rows.

The output CSV schema is exactly `track,key_tonic,key_mode`. Its companion
manifest binds the frozen ASAP manifest hash, source-mapping hash, label hash,
included opaque IDs, excluded opaque IDs/reason codes, derivation-tool hash and
runtime versions.

## Viability and use gates

- require at least 24 included development tracks;
- require at least one major and one minor track;
- freeze the derived labels and manifest before running any APTA key build;
- run the production baseline and each already-frozen candidate once over the
  exact same included audio set;
- require positive net exact-key fixes, at most one break and no new
  high-confidence key error before a candidate may proceed to the spent DJ
  comparison;
- absolute exact-key accuracy must reach at least 70% on this independent
  development set before any formal key holdout is considered.

Failure closes the candidate or dataset stage without changing label rules,
opening the ASAP key holdout, consuming a fresh DJ acceptance corpus or making
an acceptance claim.
