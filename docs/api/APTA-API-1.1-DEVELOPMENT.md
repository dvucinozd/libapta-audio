# APTA 1.1 public API development contract

**Status:** implemented on the `1.1.0` development branch; not a stable-release
contract

This document describes the public API added after APTA 1.0 and the boundary
verified by the task-1-through-task-4 implementation. The installed headers are
the source authority. The stable 1.0 contract remains
[`APTA-API-ABI-1.0.md`](APTA-API-ABI-1.0.md).

## 1. Feature capabilities and result views

APTA 1.1 adds three optional feature bits:

| Capability | Result type | Accessor |
|---|---|---|
| `APTA_FEATURE_MUSICAL_KEY` | `apta_key_view_t` | `apta_result_get_key()` |
| `APTA_FEATURE_METER_DOWNBEAT` | `apta_meter_view_t` | `apta_result_get_meter()` |
| `APTA_FEATURE_CALIBRATED_QUALITY` | `apta_quality_view_t` | `apta_result_get_quality()` |

Key data contains a selected tonic in `0..11`, major/minor mode, signed tuning
offset in cents, applicability range, state, confidence and an optional ranked
candidate list. A present key always has a selected tonic and mode; an absent
key is represented by feature absence rather than an unknown selected value.

Meter data contains numerator, power-of-two denominator, downbeat source frame
and beat ordinal. Its ordered, non-overlapping segments represent meter changes
and carry their own applicability, state and confidence. A range passed to
`apta_result_get_meter()` filters availability, but a successful view exposes
the complete immutable segment list.

Quality data is one record per target feature. It carries a producer-stable
calibration model ID, evidence coverage in permille, calibrated confidence,
feature state and any of these warning flags:

- `APTA_QUALITY_FLAG_AMBIGUOUS`;
- `APTA_QUALITY_FLAG_DEGRADED`;
- `APTA_QUALITY_FLAG_OUT_OF_DOMAIN`;
- `APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT`.

`apta_result_get_quality()` accepts exactly one known, non-quality feature bit.
Quality cannot target `APTA_FEATURE_CALIBRATED_QUALITY` itself. All pointers
returned by the three accessors belong to the immutable result and remain valid
until `apta_result_release()`.

These types are storage and interchange contracts. On the current branch the
native session pipeline computes key, meter/downbeat and tempo, and — when a
host requests `APTA_FEATURE_CALIBRATED_QUALITY` alongside BPM — publishes one
BPM quality record from the accepted `isotonic-pav-clamped-v1` calibration
model (protocol ID 1867860160). The model can only lower a reported
confidence, never raise it; acceptance evidence and scope are frozen in
[`../status/APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md`](../status/APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md).
No calibrated quality is published for any other feature.

## 2. Validated external-result builder

`apta_result_builder_t` lets a host convert already-computed analysis into a
normal APTA immutable result without pretending that libapta reran analysis.
The intended uses include legacy library importers and independent analyzers.

The normal sequence is:

1. initialize `apta_result_builder_options_t` and create the builder from an
   APTA context;
2. set builder info and source information;
3. set `APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT` with a non-empty source name;
4. set any available metadata, waveform, tempo, grids, revisions, key, meter
   and quality records;
5. call `apta_result_builder_finalize()` and publish or serialize the returned
   immutable result;
6. release the result and destroy or reset the builder.

Every setter validates its structure prefix, enum values, reserved fields,
ranges, counts, ordering, pointer/count pairs and configured capacity before it
returns. Pointer-backed inputs are deep-copied. Finalization is non-consuming:
it makes a separate result-owned deep copy, so the builder can be modified,
reset, finalized again or destroyed without changing an older result.

Important cross-feature rules include:

- final waveform overview requires known source length and complete canonical
  coverage; non-final overview may be a canonical prefix or sparse spans;
- explicit grids with at least two beats are checked against selected BPM by
  every adjacent beat interval; a single beat is phase-only evidence;
- grid coverage, segments and beats must be ordered, non-overlapping and
  internally coherent;
- meter downbeats must match encoded grid positions and ordinals when a grid is
  present;
- key candidates are unique and descending by score, and one candidate must
  reproduce the selected tuple when candidates are present;
- quality targets are unique and must refer to represented or derived features.

`maximum_allocation_bytes` bounds the complete graph owned by each finalized
result. The other builder limits bound individual collections. Zero-valued
limits select library defaults. Builder-retained setter copies are also checked
against the configured ceiling, but they are not part of the finalized result
graph.

`APTA_RESULT_PROVENANCE_NATIVE_ANALYSIS` is reserved for a future native
producer path and is currently rejected as unsupported by the public builder.

## 3. Streaming container entry points

APTA 1.1 adds:

- `apta_output_stream_t` with `write`, `seek` and `flush` callbacks;
- `apta_input_stream_t` with `read_at` and `get_size` callbacks;
- `apta_result_serialize_to_stream()`;
- `apta_result_parse_from_stream()`;
- `apta_stream_parse_options_t` for selective materialization and hard limits.

The buffer APIs remain available and retain their existing signatures. See
[`../file-format/APTA-STREAMING-IO-1.1.md`](../file-format/APTA-STREAMING-IO-1.1.md)
for callback, scratch, selection and integrity rules.

## 4. ABI and version boundary

The branch carries separate 1.1 exported-symbol, public-header-delta and public
layout manifests for LP64, ILP32 and pointer-32/alignment-64 models. New structs
use the existing `struct_size`/`api_version` prefix and reserved append space.

The package `VERSION` and encoded `APTA_API_VERSION` deliberately remain at the
latest stable 1.0 values during feature development. Updating those values,
freezing the complete 1.1 ABI and publishing `v1.1.0` are release tasks, not an
implicit consequence of landing new source on this branch.

## 5. Current verification

At implementation baseline `0e524bee53fb5a528dd6e3fd138c4b4777def8f0`:

- the configured static host suite passed 99 of 99 tests;
- the shared-library ABI/export suite passed 4 of 4 tests;
- focused post-commit result, builder, DJ-section and streaming tests passed 8
  of 8 tests;
- public headers compiled under the retained ILP32 and RISC-V 32-bit probes.

This evidence covers API mechanics, validation, ownership, allocation failure,
wire round trips and compatibility. It does not qualify the future DJ analysis
algorithms or physical ESP32-P4 behavior.
