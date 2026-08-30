# APTA 1.1 WP4 FMAK semitone-band key protocol

- **Status:** pre-registered; selected audio remains unopened
- **Frozen baseline revision:** `202ba177476ab021d1c2599987a5d4cfab503f8b`
- **Evidence class:** independent local development; no acceptance claim
- **Candidate flag:** `APTA_ENABLE_EXPERIMENTAL_SEMITONE_BAND_KEY=ON`
- **Formal holdout:** unopened

## Failure-driven hypothesis

Four rejected WP4 candidates changed harmonic projection, profile centering or
temporal aggregation, but every one consumed the same 36 exact-frequency
Goertzel measurements: one equal-tempered centre frequency for each semitone in
three octaves. A one-second rectangular resonator is narrow enough that ordinary
recording detuning and energy between semitone centres can be strongly
under-represented. Profile or temporal rescoring cannot recover pitch-class
energy that the front end never measured.

This candidate changes exactly that tonal representation. It integrates three
fixed, equally weighted probes across each semitone cell while leaving the
window, decimator, pitch range, logarithmic compression, profile selector,
candidate ordering, confidence encoding and publication contract unchanged.

## Frozen representation

For every existing centre frequency `f[k]`, instantiate exactly three Goertzel
resonators at offsets `r = {-1/3, 0, +1/3}` semitone:

```text
f[k,r] = f[k] * 2^(r / 12)
E[k,r] = q1^2 + q2^2 - coefficient[k,r] * q1 * q2

band[k] = (log1p(max(E[k,-1/3], 0))
         + log1p(max(E[k, 0], 0))
         + log1p(max(E[k,+1/3], 0))) / 3
```

Each one-second completed window adds `band[k]` to the existing folded chroma
class `k mod 12`. The offsets divide one semitone into three equal cells and
are fixed before corpus access. No tuning estimate, best-probe selection,
adaptive bandwidth, learned weight, alternate window, extra octave or profile
change is permitted. Non-finite energy remains zero under the existing rule.

The default build retains its exact single-probe computation and bytes. The
candidate adds exactly 72 resonators (108 total), may add at most 1,024 bytes
to key state/workspace, adds no result-pool bytes and must remain below the
frozen 1.5 MiB P4 workspace ceiling. Median full-path host runtime overhead must
not exceed 15%; measured key-feed overhead must not exceed 210% relative to the
single-probe baseline.

## Disjoint development split

The two prior FMAK selections contain four tracks per tonic/mode class each and
are spent. From the 503 eligible members remaining in archive `000-019`, every
one of the 24 tonic/mode classes still contains at least three tracks. Freeze a
third selection with seed `apta-1.1-fmak-semitone-band-v1`:

- 72 total tracks;
- 36 major and 36 minor;
- exactly three tracks for every tonic/mode class;
- zero overlap with either spent FMAK selection;
- selection SHA-256
  `87e62603ea8213056d1f5fd8ef8e8b50ff6fa914f795859ac840d55f48194c5f`.

Selection uses only public metadata and stable opaque hashing. Audio, source
IDs, source paths, private labels and mappings remain ignored local artifacts.
No selected audio may be extracted before this protocol, the exact preflight
tool and its tests are committed.

## Staged gates

1. Preflight must reproduce the two spent seals, the new seal, 3/3 class
   balance, 36/36 mode balance, 503-member remaining inventory and zero overlap
   with both spent selections.
2. Implement only the frozen three-probe average behind the candidate flag.
   Add deterministic fixtures for centre-tuned and +/-1/3-semitone tones,
   silence, invalid input, window boundaries, reset/snapshot behavior and exact
   default invariance.
3. Require default and candidate Release warnings-as-errors matrices, candidate
   ASan/UBSan, invalid-option and mutual-exclusion checks, byte-identical default
   analyzer, deterministic candidate bytes, exact +72 resonators, state and
   workspace deltas within 1,024 bytes, unchanged result pool and both frozen
   runtime ceilings.
4. Only then prepare the sealed 72-track split once and run exact baseline and
   candidate analyzers from one committed revision with the same frozen epoch,
   manifest and inspector.
5. The candidate is formal-holdout eligible only if every gate is true:
   exact key accuracy at least 70%, major at least 60%, minor at least 60%,
   accuracy strictly improves, fixes exceed breaks, no new confidence>=75
   error, all software/resource/runtime gates pass and there are no execution
   failures.
6. Only a complete pass may open the untouched 48-track formal MTG holdout once.

Failure of any stage rejects the candidate and spends the 72-track split. It
does not authorize probe-offset, probe-count, aggregation, profile, confidence
or threshold tuning against the result.
