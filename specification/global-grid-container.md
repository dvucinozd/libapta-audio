# Global-grid and revision container sections

**Status:** APTA 1.0 Release Candidate Draft  
**Container version:** 1  
**Section versions:** `GGRD` version 1 and `REVN` version 1

## 1. Scope

This document normatively defines the version-1 `.apta` payloads for global
beatgrid, dynamic-tempo and grid-revision data.

It extends the common container rules in [`file-format.md`](file-format.md).
All common header, directory, alignment, CRC32C, checked-arithmetic, allocation
limit, required/optional section and canonical writer requirements apply.

## 2. Section registry and relationship

`GGRD` and `REVN` are optional singleton sections forming one logical pair:

- a file containing `GGRD` MUST contain `REVN`;
- a file containing `REVN` MUST contain `GGRD`;
- `REVN` MUST immediately follow `GGRD` in the section directory;
- duplicate `GGRD` or duplicate `REVN` sections are invalid;
- the pair is optional unless required by a claimed profile or extension.

Canonical known-section order is:

```text
WOVR
[WDTL]
[META]
[TEMP]
[LGRD]
[GGRD]
[REVN]
```

`GGRD` and `REVN` either both appear or both do not appear.

All multi-octet integers are little-endian. Every reserved field and byte MUST
be zero in canonical output. Strict readers MUST reject non-zero reserved
values. Each payload uses the common 40-byte section descriptor and CRC32C.

## 3. `GGRD` version 1

### 3.1. Payload composition

```text
96-byte GGRD header
segment_count × 80-byte segment record
beat_count × 40-byte beat record
```

The payload has no internal padding. Its exact logical and stored size is:

```text
96 + segment_count × 80 + beat_count × 40
```

Normative Core Analysis Profile 1.0 limits are:

- `segment_count`: 1 through 8;
- `beat_count`: 0 through 3072.

A future profile may define a larger count without changing the record layout,
but a reader may enforce a lower configured resource limit and return a
resource-limit error rather than accepting the file.

### 3.2. Header layout

| Offset | Size | Field | Requirement |
|---:|---:|---|---|
| 0 | 2 | `payload_version` | Value `1`. |
| 2 | 1 | `grid_state` | `PROVISIONAL`, `STABLE` or `FINAL`. |
| 3 | 1 | `grid_confidence` | `0..100`. |
| 4 | 4 | `grid_flags` | Grid flags from `beatgrid.md`. |
| 8 | 4 | `representation` | `SEGMENTS`, `EXPLICIT` or `HYBRID`. |
| 12 | 4 | `coverage_range_count` | Value `1`. |
| 16 | 4 | `segment_count` | `1..8`. |
| 20 | 4 | `beat_count` | `0..3072`. |
| 24 | 8 | `requested_first_frame` | Inclusive start. |
| 32 | 8 | `requested_end_frame` | Exclusive end. |
| 40 | 8 | `evidence_first_frame` | Inclusive start. |
| 48 | 8 | `evidence_end_frame` | Exclusive end. |
| 56 | 8 | `applicability_first_frame` | Inclusive start. |
| 64 | 8 | `applicability_end_frame` | Exclusive end. |
| 72 | 8 | `coverage_first_frame` | Inclusive start. |
| 80 | 8 | `coverage_end_frame` | Exclusive end. |
| 88 | 8 | `reserved` | Zero. |

Every encoded range MUST be non-empty and use half-open source-frame
semantics.

Representation/count consistency:

- `APTA_GRID_REPRESENTATION_SEGMENTS` requires `beat_count == 0`;
- `APTA_GRID_REPRESENTATION_EXPLICIT` requires `beat_count > 0`;
- `APTA_GRID_REPRESENTATION_HYBRID` requires `beat_count > 0`.

The representation field is authoritative. A reader MUST NOT infer authority
only from which arrays are non-empty.

### 3.3. Segment record

Each segment record is exactly 80 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `applicability_first_frame` |
| 8 | 8 | `applicability_end_frame` |
| 16 | 8 | `anchor_whole_frame` |
| 24 | 4 | `anchor_fraction_q32` |
| 28 | 4 | `reserved_anchor` |
| 32 | 8 | `anchor_ordinal` signed two's-complement |
| 40 | 8 | `period_whole_frames` |
| 48 | 4 | `period_fraction_q32` |
| 52 | 4 | `beat_count` |
| 56 | 4 | `nominal_tempo_millibpm` |
| 60 | 4 | `segment_id` |
| 64 | 4 | `revision_id` |
| 68 | 4 | `segment_flags` |
| 72 | 1 | `segment_state` |
| 73 | 1 | `segment_confidence` |
| 74 | 6 | `reserved` |

Validation requirements:

- applicability is non-empty and contained in the grid applicability range;
- segments are sorted and non-overlapping;
- `period_whole_frames` and `period_fraction_q32` MUST NOT both be zero;
- nominal tempo is `40000..300000` millibpm;
- state, confidence and flags are valid;
- every segment revision equals the paired `REVN.revision_id`;
- nominal tempo and fractional period satisfy the active profile tolerance.

### 3.4. Beat record

Each explicit beat record is exactly 40 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `whole_frame` |
| 8 | 4 | `fraction_q32` |
| 12 | 4 | `reserved_position` |
| 16 | 8 | `ordinal` signed two's-complement |
| 24 | 4 | `revision_id` |
| 28 | 4 | `beat_flags` |
| 32 | 1 | `confidence` |
| 33 | 7 | `reserved` |

Validation requirements:

- every position is inside the grid applicability range;
- positions are strictly increasing in full Q32 frame order;
- ordinals are strictly increasing;
- confidence and flags are valid;
- every beat revision equals the paired `REVN.revision_id`.

A profile or diagnostic may permit a non-consecutive ordinal when an omitted or
uncertain beat is represented explicitly. Version 1 does not permit duplicate
positions.

## 4. `REVN` version 1

`REVN` is exactly 80 bytes:

| Offset | Size | Field | Requirement |
|---:|---:|---|---|
| 0 | 2 | `payload_version` | Value `1`. |
| 2 | 1 | `revision_state` | `PENDING` or `APPLIED`. |
| 3 | 1 | `revision_confidence` | `0..100`. |
| 4 | 4 | `revision_flags` | Defined revision flags only. |
| 8 | 4 | `revision_id` | Non-zero. |
| 12 | 4 | `previous_revision_id` | Previous lineage identifier or zero. |
| 16 | 4 | `proposed_representation` | Matches paired GGRD. |
| 20 | 4 | `proposed_segment_count` | Matches paired GGRD. |
| 24 | 4 | `proposed_beat_count` | Matches paired GGRD. |
| 28 | 4 | `reserved32` | Zero. |
| 32 | 8 | `affected_first_frame` | Inclusive start. |
| 40 | 8 | `affected_end_frame` | Exclusive end. |
| 48 | 32 | `reserved` | Zero. |

Accepted revision states:

```text
APTA_GRID_REVISION_PENDING
APTA_GRID_REVISION_APPLIED
```

Accepted revision flags:

```text
APTA_GRID_REVISION_FLAG_CONFLICTS_LOCKED_RANGE
APTA_GRID_REVISION_FLAG_DYNAMIC_TEMPO
APTA_GRID_REVISION_FLAG_DEGRADED
```

The affected range MUST be non-empty. Proposed representation and counts MUST
exactly match the paired `GGRD` header.

A pending revision does not mutate an older immutable result. Applying a
revision creates a new generation and lineage relationship as defined by
[`lifecycle.md`](lifecycle.md) and [`result-model.md`](result-model.md).

## 5. Canonical writer requirements

A canonical writer MUST:

- emit the pair only when valid global-grid data is available;
- emit exactly one `GGRD` followed immediately by one `REVN`;
- write exact fixed-width records with no internal payload padding;
- write every reserved value as zero;
- order segments and beats as required above;
- compute each payload CRC after complete encoding;
- update section count, total size and header CRC correctly;
- preserve canonical output for results without global-grid data;
- produce byte-identical output after canonical writer-reader-writer round trip.

## 6. Reader requirements

A reader MUST treat both payloads as untrusted and validate before exposing the
feature as available:

- common container and section bounds;
- section version and exact payload size;
- payload CRC32C;
- singleton pairing and adjacency;
- reserved values;
- lifecycle, representation, flags and confidence;
- range geometry;
- segment and beat counts against profile and configured limits;
- segment ordering and containment;
- beat ordering and ordinals;
- tempo range and non-zero period;
- representation/count consistency;
- revision identity across GGRD, every segment, every beat and REVN;
- aggregate allocation limits before allocation.

Recognized malformed data is corrupt. An unsupported recognized section
version is unsupported rather than an unknown optional section.

## 7. Partial results

A file marked `PARTIAL_RESULT` MAY contain a provisional GGRD/REVN pair when:

- all published ranges and counts are exact;
- state and confidence expose provisional status;
- missing coverage remains an explicit gap;
- a pending revision is not presented as applied;
- unknown source duration follows the common finality rules.

A final global-grid feature MUST NOT carry a pending revision affecting the same
scope.

## 8. Conformance requirements

Container conformance for this module includes:

- canonical segment-only, explicit and hybrid fixtures;
- dynamic multi-segment fixture;
- pending and applied revision fixtures;
- independent producer and consumer vectors;
- writer-reader-writer byte identity;
- every prefix truncation and trailing-byte rejection;
- duplicate, missing and reordered pair cases;
- unsupported section versions;
- non-zero reserved values;
- count overflow and exact-size mismatch;
- range, ordering, tempo and representation conflicts;
- revision mismatch across every record type;
- allocation-limit and allocation-failure sweeps;
- sanitizer and fuzz coverage.
