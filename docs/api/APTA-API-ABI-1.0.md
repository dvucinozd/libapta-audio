# APTA public API and ABI 1.0

**Status:** release-candidate contract
**Package candidate:** `1.0.0-rc.1`
**API:** `1.0.0`

## Version domains

The root `VERSION` file is the package release source. CMake and
`apta_version.h` must agree with it. Specification and container versions are
separate compatibility domains; the version-1 container does not change merely
because the package or API receives a compatible 1.x update.

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

## Freeze progression

The checked-in 1.0 header snapshot, public layout manifests and public symbol
manifest are added by the remaining P2 commits. After the P2 exit gate, any new
public API requires an explicit S9 freeze exception.
