# Stage S9 P1 — Normative scope freeze status

**Status:** implementation complete on the P1 branch; awaiting review and CI  
**Parent audit:** [`S9-FREEZE-AUDIT.md`](S9-FREEZE-AUDIT.md)  
**Work order:** [`../roadmap/APTA-1.0-WORK-ORDER.md`](../roadmap/APTA-1.0-WORK-ORDER.md)

## Scope completed

- promote one APTA 1.0 release-candidate normative set;
- define the complete 1.0 core feature and deferred-feature boundary;
- approve WAVEFORM-1.0, ADAPTIVE-WAVEFORM-1.0 and CORE-ANALYSIS-1.0;
- approve REFERENCE-WAVEFORM-1.0 and prohibit unavailable reference tempo/grid claims;
- adopt implemented GGRD and REVN version-1 names and contracts;
- correct obsolete seeding geometry and future-import statements;
- distinguish multi-platform integrations from independent DSP implementations;
- generate a blob-hashed normative manifest.

## Deliberately unchanged

- public API version remains 0.3.0 until P2;
- container version remains 1;
- canonical writer bytes are unchanged;
- DSP behaviour and test expectations are unchanged;
- no final APTA 1.0 claim is issued.

## P1 acceptance

P1 is complete when documentation and link checks pass, the PR contains no
production-code change, and the maintainer approves the scope/profile set.

## Next phase

P2 freezes the version model and public API/ABI: compatibility predicate,
structure layouts, exported symbols, source geometry/identity access and
old-header/new-library tests.
