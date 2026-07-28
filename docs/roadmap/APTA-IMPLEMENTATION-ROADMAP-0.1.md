# APTA implementation roadmap 0.1

**Status:** Working roadmap  
**Primary objective:** Deliver an embedded-validated adaptive waveform MVP before stabilising the complete public ABI or `.apta` format.

## D0 — Specification foundation

Deliverables:

- repository and documentation structure;
- normative language;
- terminology;
- source time model;
- PCM input contract;
- progressive scheduling contract;
- architecture review record.

Exit gate:

- every public range uses `[first_frame, end_frame)`;
- push PCM ownership and partial acceptance are unambiguous;
- the host can express focus and priority regions;
- end of input is explicit.

## M1 — Minimal public API prototype

Deliverables:

- opaque context, session and result handles;
- fixed-width public types;
- configuration initialisers;
- allocator abstraction;
- PCM push API;
- focus and region request API;
- bounded cooperative processing;
- immutable result generations;
- cancellation and diagnostics.

Exit gate:

- C and C++ header compilation tests;
- 32-bit and 64-bit ABI layout tests;
- allocation-failure tests;
- no core dependency on OS, filesystem, USB or codec headers.

## M2 — Adaptive waveform MVP

Deliverables:

- immediate local waveform near focus;
- progressive full-track overview;
- detail tiles;
- explicit coverage and gaps;
- push-mode PCM demand;
- static-workspace mode;
- low-memory tile eviction policy.

Exit gate:

- useful local waveform is available before full-track processing;
- moving focus reprioritises analysis without discarding completed output;
- deterministic golden vectors pass on desktop and ESP-IDF.

## M3 — Early ESP-IDF validation

Deliverables:

- ESP-IDF component wrapper;
- cooperative task-loop example;
- internal RAM and optional PSRAM allocation policy;
- fixed-point or bounded float backend experiment;
- measurements for CPU time, peak memory and publication latency.

Exit gate:

- one USB host remains entirely under application ownership;
- analysis can yield within configured work limits;
- playback-critical work is not blocked by full-track analysis;
- memory usage fits a documented embedded resource class.

## M4 — Minimal `.apta` waveform container

Deliverables:

- byte-level fixed header;
- section directory;
- source identity;
- metadata section;
- overview and detail waveform sections;
- CRC definition;
- canonical little-endian writer and reader;
- allocation and section-count limits;
- round-trip, malformed-input and fuzz tests.

Exit gate:

- desktop writer output is consumed by the ESP-IDF reader;
- unknown optional sections are skipped safely;
- corrupted offsets, lengths and CRCs are rejected.

## M5 — BPM and local beatgrid

Deliverables:

- onset representation;
- tempo candidate model;
- provisional BPM;
- local beat phase and beatgrid;
- confidence semantics;
- locked range API;
- pending revision model.

Exit gate:

- focus-local BPM and beatgrid can become usable without complete-track analysis;
- half-time and double-time ambiguity is represented explicitly;
- stable locked ranges never change silently.

## M6 — Resume, merge and provenance

Deliverables:

- import of compatible partial results into a session;
- continuation of missing analysis;
- producer and backend provenance;
- generation merge rules;
- source identity validation;
- version migration policy.

Exit gate:

- waveform-only caches can be extended with tempo and grid results;
- incompatible source content is rejected;
- partial results survive restart and continue deterministically.

## M7 — Global refinement

Deliverables:

- global beatgrid;
- dynamic-tempo segments;
- explicit beat fallback representation;
- revision proposal and host acceptance flow;
- desktop inspection and validation tools.

Exit gate:

- local stable playback regions remain protected during global refinement;
- dynamic-tempo tracks are represented without forcing a false constant grid.

## M8 — Conformance and APTA 1.0 preparation

Deliverables:

- named conformance profiles;
- resource classes;
- legally redistributable fixtures;
- semantic and reference-algorithm tolerances;
- cross-platform determinism tests;
- compatibility, governance, security and release policies;
- second independent integration.

Exit gate:

- stable specification, API and container versions;
- at least two independent platform integrations;
- complete parser fuzzing campaign;
- documented backward and forward compatibility.
