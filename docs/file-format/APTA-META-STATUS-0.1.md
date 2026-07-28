# APTA META section status 0.1

**Status:** Feature-branch implementation candidate  
**Container section:** optional `META`, version 1  
**Encoding:** deterministic CBOR map

## Public ownership model

Applications configure metadata through `apta_session_set_metadata()` while a session remains in `APTA_SESSION_CREATED`.

The setter copies every supplied text and byte field into library-owned storage. Caller buffers may be changed or released immediately after a successful call. Each immutable result generation owns a separate copied metadata block, so metadata views remain valid until the result is released and may outlive the session.

`apta_session_set_metadata(session, NULL)` removes metadata. Passing an initialized empty `apta_metadata_t` creates a present empty META map. These states are intentionally distinct.

## Public fields

The implementation supports the version-1 recognized keys:

| Key | API field | Type |
|---:|---|---|
| 1 | `producer_name` | UTF-8 text |
| 2 | `producer_version_string` | UTF-8 text |
| 3 | `backend_name` | UTF-8 text |
| 4 | `backend_version` | UTF-8 text |
| 5 | `creation_unix_time` | unsigned integer |
| 6 | `application_source_id` | UTF-8 text or byte string |
| 7 | `comments` | UTF-8 text |

Presence flags preserve explicitly present empty text values. Source-ID presence and text/byte representation are carried by `application_source_id_kind`.

## Bounded limits

| Field | Maximum bytes |
|---|---:|
| Producer name | 255 |
| Producer/backend version | 127 each |
| Backend name | 255 |
| Application source ID | 1024 |
| Comments | 4096 |
| Total copied field bytes | 8192 |

Parser traversal is additionally bounded to eight CBOR nesting levels, 256 visited unknown-value items and 64 top-level map entries. Existing file, section and allocation limits remain authoritative.

## UTF-8 and validation

Recognized text fields are validated as well-formed UTF-8. Overlong encodings, surrogate code points, invalid continuation bytes and values above U+10FFFF are rejected.

Reserved API fields and unknown metadata flags must be zero. Absent text fields must have zero size. Present fields may legally be empty.

## Canonical writer

The writer preserves byte-identical output when metadata is absent.

When metadata is present, canonical directory order is:

1. `WOVR`;
2. optional `WDTL`;
3. optional `META`.

The writer uses definite-length CBOR, ascending integer keys and shortest integer/length encoding. It adds no temporary heap allocation. Existing waveform payloads are moved as an unchanged block, section offsets are updated and section/header CRC32C values remain valid.

## Reader

The reader layers on top of the hardened waveform parser. Header, directory, section overlap, CRC and unknown-required-section checks therefore execute before META decoding.

META decoding rejects:

- duplicate META sections;
- unsupported META versions;
- non-map top-level values;
- indefinite-length maps or strings;
- duplicate or non-ascending keys;
- non-canonical integer/length encoding;
- wrong types for recognized keys;
- recognized values exceeding fixed limits;
- trailing bytes after the top-level item;
- invalid recognized UTF-8;
- configured allocation-limit violations.

Unknown ascending integer keys are skipped through a bounded recursive CBOR walker.

## Test package

The feature package adds:

- metadata initializer and ABI-prefix checks;
- session/result ownership and lifetime testing;
- empty-versus-absent metadata testing;
- canonical META byte fixture and writer/parser/writer identity;
- combined `WOVR + WDTL + META` round-trip;
- malformed META corpus;
- session-setter and parser allocation-failure cleanup;
- canonical `valid-meta.apta` fuzz seed and META dictionary tokens.

The complete suite registers 33 runtime tests before CI verification.

## Deferred work

The first implementation does not provide:

- arbitrary application-defined metadata construction through the public API;
- lossless preservation of unknown META keys during rewrite;
- a standalone metadata-only `.apta` profile;
- configurable per-field metadata limits;
- streaming CBOR input;
- formal long-running metadata fuzz campaign evidence.
