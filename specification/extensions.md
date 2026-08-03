# Extension model

**Status:** APTA 1.0 Release Candidate Draft

## 1. Purpose

APTA extensions add capabilities, result payloads, container sections or platform integration contracts without silently changing APTA 1.0 core semantics.

An extension MUST be independently versioned and MUST declare its compatibility requirements.

## 2. Extension identity

A public extension identifier uses:

```text
APTA-EXT-<NAME>-<major>.<minor>
```

Names use uppercase ASCII letters, digits and hyphens.

Examples:

```text
APTA-EXT-KEY-1.0
APTA-EXT-DOWNBEAT-1.0
APTA-EXT-PHRASE-1.0
```

A vendor-private extension uses a reverse-domain namespace in documentation:

```text
vendor.example.apta.<name>
```

Vendor-private identifiers MUST NOT use the `APTA-EXT-` namespace.

## 3. Required extension document

Every extension specification defines:

- identifier and version;
- problem and scope;
- dependencies on Core or other extensions;
- capability bits or discovery mechanism;
- public API additions;
- result model;
- lifecycle and confidence behaviour;
- serialization payload and FourCC when applicable;
- required/optional compatibility behaviour;
- security limits;
- conformance tests;
- versioning and migration rules.

An extension is not normative merely because a capability bit, FourCC or placeholder appears in an architecture document.

## 4. Capability allocation

APTA 1.0 core capability bits `0..31` are allocated by the Core specification.

Bits `32..47` are reserved for future standards-track APTA extensions.

Bits `48..63` are implementation-private and MUST NOT be serialized as portable standard capability claims.

A standards-track bit requires an accepted extension specification and conformance tests.

Implementations MUST NOT advertise key, downbeat, phrase or another deferred feature through an APTA 1.0 core capability bit before its extension is normative.

## 5. Public API additions

Extension declarations SHOULD be placed under:

```text
include/apta/extensions/<extension-name>.h
```

They MUST use:

- `APTA_API` and `APTA_CALL`;
- fixed-width public storage;
- extensible structure prefixes;
- opaque objects where independent lifetime is required;
- Core ownership and threading conventions.

An extension header MUST be includable after `apta.h` and SHOULD also compile independently with documented prerequisites.

## 6. Container FourCC allocation

Standards-track extensions request one or more FourCC values in the public registry maintained by the project.

A FourCC allocation record includes:

- literal four-byte value;
- owner extension identifier;
- payload version range;
- singleton or repeatable status;
- required/optional semantics;
- canonical ordering key;
- security and resource limits.

A vendor-private section SHOULD use a FourCC beginning with lowercase ASCII when practical. This convention reduces collision risk but does not establish ownership by itself.

Unknown optional sections are skipped according to `file-format.md`. Unknown required sections make the file unsupported.

## 7. Required versus optional extensions

A container or result may declare an extension as required only when the base result cannot be interpreted correctly without it.

A required extension MUST NOT be used merely to force recognition of optional metadata or a producer-specific optimization.

When an optional extension is unknown:

- Core data remains usable;
- unknown extension payload is ignored safely;
- capability discovery does not claim the unknown feature;
- lossless rewriting MAY preserve opaque payload when supported.

When a required extension is unknown, the reader MUST return an unsupported-version or unsupported-extension error without partially presenting misleading dependent data as complete.

## 8. Version compatibility

Extension major versions may introduce incompatible semantics.

A minor version:

- MAY append optional fields;
- MAY allocate previously reserved flags;
- MAY add optional section records;
- MUST preserve existing field interpretation;
- MUST define how older readers skip new optional content.

A patch version corrects errors without changing the portable data model.

The extension specification MUST define the minimum reader and writer versions needed for each payload version.

## 9. Dependency rules

An extension may depend on:

- a minimum APTA Core version;
- another extension and version range;
- a named conformance profile;
- a container version.

Dependencies MUST be acyclic.

A reader MUST validate required dependencies before exposing extension data as available.

Optional dependency absence MUST result in documented degradation, not silent reinterpretation.

## 10. Lifecycle, confidence and provenance

Extensions producing analysis data MUST use or explicitly extend Core lifecycle and confidence semantics.

An extension MUST state:

- whether data is range-scoped or track-scoped;
- what `PARTIAL`, `PROVISIONAL`, `STABLE` and `FINAL` mean;
- whether confidence is meaningful and how unknown confidence is represented;
- what ambiguity flags or alternate candidates exist;
- how user edits and producer/backend provenance are preserved;
- whether stable data can receive pending revisions.

An extension MUST NOT redefine Core confidence values `0..100` or `255` with incompatible meanings.

## 11. Security requirements

Every parser extension treats payloads as untrusted.

The specification MUST set limits for:

- payload size;
- item and record counts;
- nested data depth;
- string and binary object size;
- referenced ranges and offsets;
- decompression ratio when compression is introduced;
- aggregate allocation.

Extension parsers MUST participate in fuzzing and malformed-input regression testing before stable status.

## 12. Registration process

A standards-track extension follows:

```text
1. Open APTA-RFC describing the problem.
2. Reserve provisional identifiers without claiming final ownership.
3. Draft API, result and serialization models.
4. Implement at least one reference or experimental producer and consumer.
5. Add conformance and malformed-input tests.
6. Review compatibility, security and resource impact.
7. Approve the extension and finalize allocations.
```

Provisional identifiers may change before approval and MUST NOT be treated as stable ecosystem allocations.

## 13. Experimental extensions

An experimental extension MUST be labelled `Experimental` and MUST NOT be enabled by default in a stable Core profile.

Experimental serialized payloads SHOULD include an implementation namespace or UUID in metadata to reduce accidental cross-producer interpretation.

Promotion to standards track requires a migration or compatibility plan for experimental data already emitted.

## 14. Core promotion

An extension may be incorporated into a future Core major version when:

- semantics are stable;
- at least two implementations exist where practical;
- conformance coverage is sufficient;
- resource impact is understood;
- identifier and migration rules are finalized;
- governance approves Core inclusion.

Core promotion does not permit silent reinterpretation of older extension payloads.

## 15. Initial reserved extension topics

The following topics are reserved for future RFC work but are not yet normative extensions:

- musical key and key-change segments;
- downbeat, meter and bar structure;
- phrase and section classification;
- audio-content fingerprinting;
- compressed `.apta` sections;
- encrypted private sections;
- arbitrary-stride PCM input;
- distributed or network analysis coordination.
