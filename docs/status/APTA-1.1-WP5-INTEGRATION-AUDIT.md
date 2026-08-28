# APTA 1.1 WP5 integration audit

- **Status:** cleanup boundary frozen; final verification pending
- **Audited revision:** `cc2aba61e618f8b1c6b244457b98377e3da4cc51`
- **Integrated algorithm candidate:** retained production defaults
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Candidate boundary

WP1 through WP4 promoted no new onset, lattice, downbeat or key path. The only
auditable WP5 algorithm candidate is therefore the retained production build
with every `APTA_ENABLE_EXPERIMENTAL_*` option disabled. The accepted tempo
ensemble and BPM confidence calibration already present in that build remain
unchanged.

The exact audited build uses GCC 13.3.0, CMake 3.28.3, Release, tests, tools,
desktop adapters and warnings-as-errors. It produces `apta-analyze` SHA-256
`0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`,
byte-identical to the WP0 deterministic-replay analyzer. Its full P4 30-minute
resource declaration also remains identical to WP0: 941,216-byte minimum
workspace, 1,000,058-byte recommendation and 537,104-byte result pool.

## Exact-head software evidence

A clean `git archive` of the audited revision was built on WSL ext4 to avoid
the known ignored-build traversal bottleneck on WSL/NTFS:

| Matrix | Result |
|---|---:|
| Release, Werror, default flags | 121/121 pass |
| Debug, Werror, ASan/UBSan, default flags | 116/116 pass |

The release matrix includes source/binary packaging, installed conformance,
versioned interchange, unit/integration, CLI, serialization, compatibility and
ABI checks. The sanitizer configuration registers only sanitizer-applicable
tests. The local NTFS build itself completed cleanly before its source archive
subprocess stalled in the filesystem RPC; that environmental run is not counted
because the clean ext4 run covers the exact committed source successfully.

The analyzer's byte identity means the existing WP0 60/60 deterministic replay
remains exact without re-exposing or reprocessing the spent corpus.

## Frozen source cleanup

Remove only experimental branches that their completed work packages rejected
or superseded:

- `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE` (WP1 I1);
- `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I2`;
- `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I3`;
- `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I4`;
- `APTA_ENABLE_EXPERIMENTAL_KEY_TUNING`.

Retain these disabled-by-default diagnostics because the closure records still
assign them a specific evidence role:

- multiband onset plus I8 within-bin peak trace;
- three-band downbeat phase;
- harmonic HPCP projection;
- key and meter trace tools.

Historical reports keep their original flag names and results. Operational
examples must stop presenting retired flags as current candidates.

After cleanup, require:

- default configure, build and full Werror matrix;
- retained-diagnostics configure, build and focused tests;
- default ASan/UBSan matrix;
- byte-identical default `apta-analyze` hash and unchanged resource probe;
- no public API, ABI, wire or `VERSION` change.

These software gates cannot override the algorithm readiness gate. The
production candidate remains ineligible for WP6 unless the open development
evidence meets the WP1-WP4 transfer criteria.
