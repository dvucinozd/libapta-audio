# APTA 1.1 WP4 FMAK semitone-band key protocol

- **Status:** rejected on sealed development evidence; evaluation closed 2026-09-04
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

## Immutable development outcome — 2026-09-04

The retained preparation and both native runs completed at exact revision
`e0369e2ce4f0bf44c0b9c14c9ef32f97012d2785`, epoch `1788114602`.
This continuation verified all 72 output hashes for each run, manifest,
mapping, source revision and analyzer hashes before scoring with the unchanged
canonical evaluator. No source audio or analysis outputs were overwritten.
The protocol was pre-registered before audio access; the 72-track development
selection is now spent.

| Frozen metric | Default | Semitone-band |
|---|---:|---:|
| Exact key | 14/72 (19.44%) | 15/72 (20.83%) |
| Major | 0/36 (0%) | 0/36 (0%) |
| Minor | 14/36 (38.89%) | 15/36 (41.67%) |
| Wrong key at confidence >=75 | 1 | 2 |

There were 3 fixes, 2 breaks, 17 changed verdicts and **1 new high-confidence
error**. The >=70% total, >=60% major, >=60% minor and no-new-high-confidence-error
gates all fail. Positive net fixes do not override any failed conjunctive gate.
The candidate is rejected and is not eligible for formal holdout access.
Its opt-in implementation remains diagnostic only; production stays unchanged.

### Software and resource evidence

Fresh GNU/WSL default and candidate Release warnings-as-errors matrices each
passed 118/118, including all three package tests. Candidate ASan/UBSan passed
115/115 (package tests are not registered in sanitizer mode). The exact native
tree is unchanged from the analyzed revision; subsequent takeover commits add
only host diagnostics/tests and status evidence. The freshly rebuilt candidate
and default analyzers match both retained executable hashes below.

The prior implementation task's retained measurements were also reviewed:
key-feed median 12.410 -> 19.717 ms (+58.9%, ceiling +210%); 120-second full-path
median 1.09 -> 1.12 s (+2.8%, ceiling +15%); +72 resonators, +960 bytes key state
and workspace, +0 result-pool bytes. These runtime figures are retained earlier
measurements, not a new benchmark from this evaluation. Fresh software passes
do not repair the algorithmic rejection or constitute physical P4 evidence.

### Reproduction anchors

All hashes below are SHA-256. Private labels, audio, mappings and source IDs
remain local and ignored. The canonical `evaluate` command produced one report
per retained run, then `compare` applied the original unchanged gates.

| Artifact | Hash |
|---|---|
| Prepared manifest | `25e853990cc25dc97929f7767de825e89b9e650c7c1b8d075b6fd98c6d0ef09e` |
| Default analyzer | `0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a` |
| Candidate analyzer | `cd08b5887382142944d8ac3329cdbcf07dfa1fd76f284a1c1a761ac3906c6f2b` |
| Inspector | `22f8f3e85a177a034157a265e4e9cf6e1ea03151f90310dd854b3fee07d8e084` |
| `build/takeover-key/default-report.json` | `5053cff8bf3dd8116b936c6b4bc3c3acc2bab4d7e243adf03d7ceec407bffd80` |
| `build/takeover-key/band-report.json` | `e5cf018e3b755bc83a28eab8cda7ed35226936a00619f5c00dd04103e01a2f19` |
| `build/takeover-key/comparison-verified.json` | `ea29780f386fbcd29b137cef7aa4143f79a23bbe5ae485e9873e72341295ac7e` |
| `build/takeover-validation/band-test.log` | `339d23d3c0a197a004e7240b755f34f326b121a88dabfe943d4ab52ab15e21b7` |
| `build/takeover-validation/default-test.log` | `e6e60160a8c2e1f0df26554fa4059bd2fdb70ea4ca60b25e59b06f7c0a8e3224` |
| `build/takeover-validation/sanitize-test.log` | `65dbc3645ae17d74b51fe6611ec6d148d13a03aa3d9a142078419bc5a8f63712` |

The provisional `comparison.json` conservatively left Werror unconfirmed while
full packaging tests were pending. `comparison-verified.json` records the
subsequent full matrix pass; both comparisons reject the candidate. Neither
report is an acceptance result. The formal 48-track MTG holdout stays unopened.
