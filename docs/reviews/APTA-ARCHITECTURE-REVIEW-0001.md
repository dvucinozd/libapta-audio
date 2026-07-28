# APTA Architecture Review 0001

**Reviewed document:** `docs/architecture/APTA-ARCHITECTURE-DRAFT.md`  
**Review status:** Completed  
**Disposition:** Architecture accepted as a project foundation; not yet accepted as a normative interoperable specification.

## 1. Executive finding

The architecture draft establishes a strong project vision and correct separation between a portable analysis core and host-owned playback, decoding, I/O and scheduling facilities.

Its principal weakness is specification precision. Several public structures and format concepts are illustrative rather than sufficient for two independent implementations to interoperate.

The draft should therefore remain an informative architecture document while normative requirements are extracted into the `specification/` directory.

## 2. Strong architectural decisions

The review accepts the following decisions:

1. The portable core consumes decoded PCM and does not own codecs.
2. USB, filesystem, networking, playback and user-interface control remain outside the core.
3. Source frames form the authoritative timeline.
4. Processing is incremental and host-budgeted.
5. Published results are immutable generations.
6. Lifecycle state and confidence are independent.
7. Algorithm backends may be replaced without changing normative result semantics.
8. `.apta` is intended as a portable interchange format rather than an opaque private cache.
9. The core must remain usable in cooperative single-thread environments.
10. External `.apta` data is untrusted and requires bounded parsing and fuzz testing.

## 3. Priority-zero gaps

### P0-1: Missing adaptive region API

The architecture promises local analysis around playback but originally has no public mechanism for communicating the playhead, lookahead, requested range, feature priority or deadline.

Resolution direction:

- add host focus state;
- add explicit priority-region requests;
- expose PCM demand in push mode;
- define bounded adaptive scheduling behaviour.

Initial normative work is recorded in `specification/progressive-scheduling.md`.

### P0-2: Incomplete PCM ownership and backpressure contract

The original push function does not report partial acceptance and does not define pointer lifetime, retries, out-of-order input, overlap, gaps or explicit end of input.

Resolution direction:

- require `accepted_frames_out`;
- define synchronous consumption/copy ownership;
- permit out-of-order ranges for adaptive analysis;
- reject conflicting overlap by default;
- add explicit end-of-input signalling.

Initial normative work is recorded in `specification/pcm-input.md`.

### P0-3: ABI policy is internally inconsistent

The architecture requires size/version prefixes but not every example structure contains both fields. Public enum width, `bool`, `size_t`, calling convention and symbol visibility are not yet stabilised.

Resolution direction:

- use fixed-width integer storage in public structures;
- place `struct_size` and `api_version` at the start of every extensible public structure;
- distinguish source portability from binary ABI compatibility;
- define calling convention and visibility macros before publishing headers.

### P0-4: `.apta` is not yet a byte-level format

The current container sketch lacks exact offsets, alignment, FourCC values, CRC variant, padding, section duplication rules, canonical metadata keys and source-fingerprint algorithm.

Resolution direction:

- create a byte-offset table rather than serializing native C structures;
- define little-endian read/write primitives;
- normatively specify every required section;
- build malformed-file and fuzz tests with parser limits.

### P0-5: Advertised capabilities exceed defined data models

Key, downbeat and phrase capabilities appear in the capability and Full Profile lists without normative result or serialized representations.

Resolution direction:

- remove them from APTA Core 0.1;
- reserve extension identifiers;
- add them only after dedicated normative extensions exist.

## 4. Priority-one gaps

### P1-1: Time budget semantics

A microsecond budget cannot generally be a hard guarantee across arbitrary DSP backends. It should be specified as a soft elapsed-time target, while input-frame and step limits remain hard bounds.

### P1-2: Fractional source-frame range

A single unsigned Q32.32 value limits the whole-frame component to 32 bits. The replacement model uses a 64-bit whole-frame component plus a 32-bit fractional field.

### P1-3: Waveform semantics

Peak, RMS and three-band values need normative units, channel combination rules, frequency bands, quantisation and boundary handling before format interoperability is possible.

### P1-4: Range conventions

Fields named `last_source_frame` are ambiguous. Every range should use the half-open `[first_frame, end_frame)` convention.

### P1-5: Lock and revision control API

The data model contains locked ranges and pending revisions but lacks host operations for lock, unlock, accept and reject.

### P1-6: Resume from partial result

Deserialization alone does not define how a partial cached result seeds a new analysis session. A result-import and compatibility-validation contract is required.

### P1-7: Track identity

The standard distinguishes file and audio identity but must define canonical hash algorithms and canonical decoded-PCM representation.

### P1-8: Threading and callback reentrancy

The specification must state which operations may run concurrently, whether callbacks may re-enter the API, and how destruction interacts with outstanding snapshots.

## 5. Repository decision

The project now uses these documentation boundaries:

- `docs/architecture/` for informative system architecture;
- `specification/` for normative interoperability requirements;
- `docs/api/` for public API and ABI design work;
- `docs/file-format/` for container design before normative adoption;
- `docs/reference/` for implementation algorithms and measurements;
- `docs/ports/` for platform integration;
- `docs/reviews/` for audits such as this document;
- `docs/decisions/` for APTA Architecture Decision Records;
- `docs/roadmap/` for staged implementation planning.

## 6. Implementation-readiness gate

Implementation of a minimal waveform prototype may begin after these items are defined:

- normative source time and interval rules;
- immutable PCM source format;
- PCM push ownership and backpressure;
- end-of-input behaviour;
- focus and region request model;
- bounded process semantics;
- result-generation ownership.

Stable public headers and `.apta` serialization should not be declared until the ABI and byte-level format reviews are complete.

## 7. Review conclusion

The architecture is technically credible and suitable as the foundation of an open standard. The project should now proceed by converting one subject at a time into testable normative requirements, beginning with PCM input and adaptive scheduling.
