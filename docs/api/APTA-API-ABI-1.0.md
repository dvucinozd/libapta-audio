# APTA public API and ABI 1.0

**Status:** Stable APTA 1.0 contract  
**Current package:** `1.0.1`  
**Public API:** `1.0.0`  
**Shared-library ABI:** `SOVERSION 1`

## Version domains

The root `VERSION` file is the package release source. CMake and
`apta_version.h` must agree with it. Specification, package, public API, shared-library
ABI and container versions are separate compatibility domains. A compatible package patch does not change the specification version, container version
or `SOVERSION`.

## API compatibility

Every extensible public structure begins with `struct_size` and `api_version`.
The library accepts a caller when:

- API major equals the library API major;
- caller minor is not newer than the library minor;
- patch is ignored for ABI compatibility;
- `struct_size` covers every field read by that entry point.

A newer caller minor is rejected unless a later API explicitly documents a
safe prefix rule. A different major is rejected. Public 1.x symbols and field
interpretations are append-only; removal or incompatible reinterpretation
requires API 2.0.

## Source information and checkpoint identity

`apta_result_get_source_info()` exposes the geometry and optional 256-bit
identity that the library uses for serialization and seeding checks. Supported
identity kinds are application-opaque bytes and SHA-256 of exact source-object
bytes. Equal geometry is not proof of equal audio.

When both a session and checkpoint carry identities, they must match. Missing
identity is accepted by default so hosts retain policy control. A host that
requires identity for checkpoint continuation sets
`APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING`.

## Stable change control

The checked-in 1.0 public-header snapshot, LP64/ILP32/LLP64 layout manifests and
public-symbol manifest freeze the published 1.x ABI surface. A patch release may
correct implementation defects, documentation and package metadata without
changing that surface. A compatible minor release may append versioned fields
or symbols only with explicit compatibility analysis, tests and conformance
evidence. Any removal, layout reinterpretation or incompatible calling-contract
change requires API/ABI 2.0.

The immutable `v1.0.0` tag records the first stable contract. Later 1.x tags
record compatible maintenance without rewriting that published history.
