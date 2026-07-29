# APTA Stage S6 conformance manifest 0.1

**Status:** Verified implementation-candidate manifest  
**Core merge:** `501e42a06bbb912980f8b35c06488befa2fd2a86`  
**Container merge:** `9d80f680469a7bec4e914c061e306f880bfc3f36`  
**Primary verification:** GitHub Actions PR CI runs `#266` and `#275`

## Scope

This manifest records self-tested behavior of the reference Stage S6 implementation:

- global beatgrid refinement;
- multiple tempo/grid segments;
- dynamic-tempo representation;
- optional explicit beats;
- immutable revision publication;
- locked-range pending/apply workflow;
- canonical `GGRD` and `REVN` interchange.

It is not an APTA 1.0 certification or formal profile claim.

## Public API checks

- `APTA_FEATURE_GLOBAL_BEATGRID` capability and session feature;
- `APTA_FEATURE_DYNAMIC_TEMPO` capability and session feature;
- dynamic tempo requires global beatgrid;
- global beatgrid requires BPM and waveform overview;
- `apta_grid_revision_view_t` has versioned public structure fields;
- `apta_grid_revision_view_init` produces a valid empty view;
- `apta_result_get_grid_revision` returns immutable result-owned revision data;
- `apta_session_apply_grid_revision` rejects zero, stale and unavailable revisions;
- public headers compile as C11 and C++11.

## Processing checks

- Stage S6 consumes the existing normalized PCM stream;
- no second decoder, channel conversion or copied PCM queue;
- fixed 2048-frame global bins;
- fixed 16384-bin resident capacity;
- bounded 128-bin refinement windows;
- minimum and stable evidence thresholds;
- maximum eight segments;
- maximum 4096 explicit beats;
- constant-period global segment representation;
- multi-segment dynamic-tempo representation;
- hybrid representation with explicit beats;
- ordered non-overlapping segments;
- ordered beat positions and strictly increasing ordinals;
- independent confidence and lifecycle state;
- explicit degraded flag when bounded representation is incomplete.

## Revision checks

- first geometry publishes nonzero revision identifier;
- geometry changes advance revision identity;
- lifecycle-only changes do not fabricate geometry revision;
- segments and beats carry the active revision identity;
- old immutable generations retain original geometry and revision;
- a conflict with a locked local range publishes `PENDING`;
- locked local geometry is not silently replaced;
- explicit apply publishes `APPLIED` state;
- stale revision application is rejected.

## Memory and lifetime checks

- heap session/result ownership is balanced;
- static workspace holds S6 session-side state;
- bounded result slots reserve S6 extension, coverage, segments and beats;
- no context allocator calls after successful bounded create;
- two-slot publication behavior remains bounded;
- acquired S6 results remain valid after session destruction;
- acquired pooled results remain valid after caller workspace overwrite;
- parser allocations are limited before allocation;
- every injected parser allocation failure releases all prior allocations.

## Writer checks

- canonical little-endian `GGRD` version 1;
- canonical little-endian `REVN` version 1;
- exact fixed header/record sizes;
- exact segment and beat count accounting;
- pair emitted only for valid global grid state;
- `REVN` immediately follows `GGRD`;
- all reserved fields zero;
- CRC32C for both payloads;
- canonical eight-byte section alignment;
- unchanged byte output when Stage S6 data is absent;
- byte-identical writer → reader → writer round trip.

## Reader checks

- complete-buffer bounds and checked arithmetic;
- exact versions and payload sizes;
- CRC validation;
- duplicate S6 section rejection;
- missing-pair rejection;
- adjacency/order rejection;
- reserved-field validation;
- lifecycle, representation and confidence validation;
- half-open non-empty range validation;
- segment and beat count limits;
- segment ordering and containment;
- beat ordering and ordinal validation;
- tempo and period validation;
- representation/count consistency;
- revision consistency across sections and records;
- configured allocation limit;
- immutable copied ownership;
- cleanup after all injected allocation failures;
- rejection of every truncated prefix;
- rejection of a canonical file with one trailing byte.

## Verified runtime tests

The verified build registers 68 runtime tests. Dedicated Stage S6 tests are:

- `apta.s6.global_grid`;
- `apta.s6.revision`;
- `apta.s6.bounded`;
- `apta.serialization.s6`;
- `apta.serialization.s6_allocation_failure`.

These execute alongside the full core, waveform, metadata, Stage S4, desktop-tool, memory, bounded-publication, concurrency, cancellation and prior container suites.

## Hardening configuration

CI run `#275` completed successfully with:

- GCC release build and runtime tests;
- Clang sanitized build;
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- 68 runtime tests in both build modes;
- canonical `valid-s6.apta` generation;
- `GGRD`/`REVN`-specific libFuzzer dictionary tokens;
- bounded 2000-run libFuzzer smoke with maximum input length 4096 bytes and five-second per-input timeout.

## Algorithm-vector interpretation

The current deterministic Stage S6 vectors include:

- a constant global click track using an exact 12-global-bin beat period;
- an abrupt transition from a 12-bin period to a 9-bin period;
- multiple global segments;
- hybrid representation and explicit beats;
- retained mid-analysis result immutability;
- locked-range conflict and application.

These are reference-algorithm vectors, not a claim that third-party analyzers must produce bit-identical tempo or phase estimates.

## Remaining gates for a formal claim

- stable APTA 1.0 specification and public API/ABI;
- independently implemented `GGRD`/`REVN` writer or reader;
- cross-endian fixture evidence;
- broader tempo-ramp, swing, syncopation and live-drum fixtures with redistribution rights;
- measured workspace, stack and latency reports on declared targets;
- maintained long-running fuzz campaign and minimized corpus report;
- ESP-IDF and second-platform validation;
- governance approval for an official Core Profile claim.
