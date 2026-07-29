# APTA independent reference fixture 0.1

**Status:** Verification candidate  
**Fixture:** `reference-wovr-meta.apta`  
**Repository encoding:** lowercase hexadecimal text  
**Decoded size:** 303 bytes  
**SHA-256:** `394403f6e0617cde449f88c35b87d7d3a136ca304ae4874cba65310724a1d7d2`

## Independence

The fixture is produced by `tests/fixtures/generate_reference_fixture.py` without importing, linking or invoking `libapta`.

The producer explicitly encodes:

- every little-endian integer field;
- fixed header and directory offsets;
- CRC32C Castagnoli;
- the `WOVR` header, span and packed column;
- deterministic CBOR for `META`;
- zero alignment padding.

The committed hex fixture and its machine-readable JSON manifest are checked against fresh generator output in a path-filtered GitHub Actions workflow.

## Fixture content

The decoded file contains:

- version-1 container header;
- one required final `WOVR` section;
- one optional version-1 `META` section;
- 1024 mono source frames at 48000 Hz;
- one zero-amplitude valid overview column;
- producer name `external`;
- creation time `1700000000`;
- byte source ID `01 02 03`;
- comments `fixture`.

## Consumer verification

`tests/unit/external_fixture.c`:

1. decodes the committed hexadecimal fixture;
2. parses it through the public API;
3. verifies container, waveform and metadata semantics;
4. serializes the immutable parsed result again;
5. requires exact equality with all 303 independently produced bytes.

The fixture workflow also verifies that the produced binary size and SHA-256 match `reference-wovr-meta.manifest.json`.

## Claim scope

Successful verification provides an independently produced little-endian container fixture and a recorded machine-readable fixture hash. It does not substitute for execution on a native big-endian target or for third-party certification.
