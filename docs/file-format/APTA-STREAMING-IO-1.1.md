# APTA 1.1 bounded streaming container I/O

**Status:** implemented development contract

This document describes the callback-based container I/O added on the `1.1.0`
branch. It uses the same container-version-1 wire format and validation rules as
the existing buffer API; it does not define a second file format.

## Output stream

Initialize `apta_output_stream_t` with `apta_output_stream_init()` and provide:

- `write(user_data, data, requested_bytes, written_bytes_out)`;
- `seek(user_data, absolute_position)`;
- `flush(user_data)`.

`write` may complete partially and will be retried. Success with zero progress
for a non-zero request is treated as a source error. Positions are absolute
64-bit byte offsets. The destination must already be empty or truncated because
the callback set intentionally does not contain a truncate operation.

`apta_result_serialize_to_stream()` writes the canonical directory and payloads,
seeks to finalize container framing, flushes the target and returns the exact
byte count. `maximum_output_bytes` in `apta_serialize_options_t` remains the
hard output ceiling. A callback failure is returned to the caller; libapta does
not own transaction, temporary-file, rename or filesystem durability policy.

Hosts writing removable media should therefore use their own transaction, for
example an empty `.part` target, successful serialization and flush, followed
by an atomic rename when the platform supports it.

## Input stream

Initialize `apta_input_stream_t` with `apta_input_stream_init()` and provide:

- `read_at(user_data, absolute_offset, data, requested_bytes, read_bytes_out)`;
- `get_size(user_data, size_out)`.

`read_at` may make partial progress and is retried. The input size must remain
stable for the complete parse call. The parser uses absolute reads, so the host
does not need to expose shared cursor state.

`apta_result_parse_from_stream()` validates the fixed header, directory,
alignment, stored ranges, sizes, flags and CRC32C before publishing an immutable
result. Recognized selected sections receive their full payload validation.
Known but unselected sections are still framed, bounded and CRC-validated;
selection reduces result materialization, not file-integrity checking. Unknown
optional sections follow the container-v1 skip rule.

## Selective materialization

`apta_stream_parse_options_t.requested_features` chooses which known result
features are materialized. The default is `APTA_FEATURE_ALL_KNOWN`.

Dependencies represented by the wire format are still enforced. For example,
meter/downbeat data is cross-checked against a present encoded grid even when a
host ultimately requests only a subset of features. A successful result reports
only the features actually materialized.

## Hard limits and scratch

The stream parser exposes independent ceilings for:

- total input bytes;
- section count;
- bytes in one section;
- overview span and waveform-column counts;
- aggregate immutable-result allocations;
- temporary scratch bytes.

Zero in an individual numeric limit selects the corresponding library default.
`apta_stream_parse_options_init()` currently selects strict parsing, all known
features, 64 sections, 65,536 overview spans, 16,777,216 waveform columns,
256 MiB input/section/allocation ceilings and 64 KiB scratch.

Those generic defaults are compatibility limits, not the future ESP32-P4 DJ
profile. Embedded hosts should set smaller explicit limits for their product.

If `scratch_buffer` is null, libapta allocates at most
`maximum_scratch_bytes` through the context allocator and releases it before the
parse returns. If a caller supplies scratch, `scratch_buffer_size` must be at
least `maximum_scratch_bytes`; no temporary scratch allocation is then needed.
Result-owned payloads still use the context allocator and remain bounded by
`maximum_allocation_bytes`.

## Buffer API compatibility

The existing functions remain supported:

- `apta_result_query_serialized_size()`;
- `apta_result_serialize()`;
- `apta_result_parse()`.

For an equivalent immutable result and canonical options, buffer and streaming
serialization produce identical bytes. Streaming parsing publishes the same
result semantics for the selected features. Existing APTA 1.0 files remain
readable, and existing 1.0 readers continue to skip the optional 1.1
`MKEY`/`MTRD`/`CONF` sections.

## Ownership and concurrency

Callbacks execute synchronously inside the public call and must not retain
library-owned data pointers. The host owns callback state and must serialize
access according to its own threading policy. A returned result follows normal
immutable-result ownership and must be released with `apta_result_release()`.

The library does not open files, choose paths, synchronize device caches,
recover partial writes or rename targets. Those responsibilities remain with
the host platform.

## Verification boundary

The implementation tests cover partial callback progress, stalled callbacks,
seek/flush and read failures, deterministic buffer/stream equivalence,
selective feature loading, caller and allocated scratch paths, CRC corruption,
limit failures, allocation failures, APTA 1.0 parsing and all three 1.1 DJ
sections. This is host evidence; physical USB-media and power-loss behavior must
be verified by the integrating product.
