# APTA Stage S6 container sections 0.1

**Status:** Reference implementation contract  
**Container version:** 1  
**Section versions:** `GGRD` version 1, `REVN` version 1  
**Implementation merge:** `9d80f680469a7bec4e914c061e306f880bfc3f36`

## 1. Scope

This document records the binary contract implemented by the reference writer and reader for Stage S6 global beatgrid and revision data.

It is an implementation-candidate reference, not a frozen APTA 1.0 normative format.

## 2. Section relationship and order

`GGRD` and `REVN` are optional singleton sections, but they form one logical pair:

- a file containing `GGRD` must contain `REVN`;
- a file containing `REVN` must contain `GGRD`;
- `REVN` must immediately follow `GGRD` in the section directory;
- duplicate `GGRD` or `REVN` sections are invalid.

The canonical writer appends the pair after all earlier recognized sections. Existing waveform, metadata and Stage S4 sections remain unchanged.

```text
WOVR
[WDTL]
[META]
[TEMP]
[LGRD]
[GGRD]
[REVN]
```

Square brackets indicate optional sections. `GGRD` and `REVN` either both appear or both do not appear.

All multibyte integers are little-endian. All reserved bytes are zero. Each section uses the existing 40-byte directory descriptor and CRC32C Castagnoli payload checksum.

## 3. `GGRD` version 1

### 3.1. Payload composition

```text
96-byte GGRD header
segment_count × 80-byte segment record
beat_count × 40-byte beat record
```

The payload has no internal padding. Its exact size is:

```text
96 + segment_count × 80 + beat_count × 40
```

Limits:

- `segment_count`: 1 through 8;
- `beat_count`: 0 through 4096.

### 3.2. Header layout

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | section version, value `1` |
| 2 | 1 | grid lifecycle state |
| 3 | 1 | grid confidence |
| 4 | 4 | grid flags |
| 8 | 4 | grid representation |
| 12 | 4 | coverage range count, value `1` |
| 16 | 4 | segment count |
| 20 | 4 | beat count |
| 24 | 8 | requested first frame |
| 32 | 8 | requested end frame |
| 40 | 8 | evidence first frame |
| 48 | 8 | evidence end frame |
| 56 | 8 | applicability first frame |
| 64 | 8 | applicability end frame |
| 72 | 8 | coverage first frame |
| 80 | 8 | coverage end frame |
| 88 | 8 | reserved, zero |

Accepted lifecycle states are `PROVISIONAL`, `STABLE` and `FINAL`.

Accepted representations are:

- `APTA_GRID_REPRESENTATION_SEGMENTS`;
- `APTA_GRID_REPRESENTATION_EXPLICIT_BEATS`;
- `APTA_GRID_REPRESENTATION_HYBRID`.

Representation/count consistency:

- `SEGMENTS` requires `beat_count == 0`;
- `EXPLICIT_BEATS` and `HYBRID` require `beat_count > 0`.

Every encoded range is half-open and non-empty: `[first_frame, end_frame)`.

### 3.3. Segment record layout

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | applicability first frame |
| 8 | 8 | applicability end frame |
| 16 | 8 | anchor whole frame |
| 24 | 4 | anchor fractional Q32 |
| 28 | 4 | reserved, zero |
| 32 | 8 | anchor beat ordinal, signed two's-complement |
| 40 | 8 | whole frames per beat |
| 48 | 4 | fractional frames per beat Q32 |
| 52 | 4 | segment beat count |
| 56 | 4 | nominal tempo in millibpm |
| 60 | 4 | segment identifier |
| 64 | 4 | revision identifier |
| 68 | 4 | segment flags |
| 72 | 1 | segment lifecycle state |
| 73 | 1 | segment confidence |
| 74 | 6 | reserved, zero |

Validation requirements:

- segment applicability is non-empty and contained in the grid applicability range;
- segments are ordered and non-overlapping;
- whole frames per beat is nonzero;
- nominal tempo is within the reference range `40000..300000` millibpm;
- lifecycle and confidence values are valid;
- every segment revision matches the paired `REVN.revision_id`.

### 3.4. Beat record layout

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | beat whole frame |
| 8 | 4 | beat fractional Q32 |
| 12 | 4 | reserved, zero |
| 16 | 8 | beat ordinal, signed two's-complement |
| 24 | 4 | revision identifier |
| 28 | 4 | beat flags |
| 32 | 1 | beat confidence |
| 33 | 7 | reserved, zero |

Validation requirements:

- each beat position is inside the grid applicability range;
- beat positions are non-decreasing in full Q32 frame order;
- ordinals are strictly increasing;
- confidence is valid;
- every beat revision matches the paired `REVN.revision_id`.

## 4. `REVN` version 1

`REVN` is a fixed 80-byte payload.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | section version, value `1` |
| 2 | 1 | revision state |
| 3 | 1 | revision confidence |
| 4 | 4 | revision flags |
| 8 | 4 | revision identifier |
| 12 | 4 | previous revision identifier |
| 16 | 4 | proposed representation |
| 20 | 4 | proposed segment count |
| 24 | 4 | proposed beat count |
| 28 | 4 | reserved, zero |
| 32 | 8 | affected first frame |
| 40 | 8 | affected end frame |
| 48 | 32 | reserved, zero |

Accepted revision states:

- `APTA_GRID_REVISION_PENDING`;
- `APTA_GRID_REVISION_APPLIED`.

Accepted flags:

- `APTA_GRID_REVISION_FLAG_CONFLICTS_LOCKED_RANGE`;
- `APTA_GRID_REVISION_FLAG_DYNAMIC_TEMPO`;
- `APTA_GRID_REVISION_FLAG_DEGRADED`.

The revision identifier is nonzero. The affected range is non-empty. Proposed representation and counts must exactly match the paired `GGRD` header.

## 5. Canonical writer rules

The reference writer:

- emits the pair only when valid global grid data is present;
- emits exactly one `GGRD` and one `REVN`;
- places `REVN` immediately after `GGRD`;
- uses exact fixed-width records and shortest possible file size under the eight-byte section-alignment rule;
- writes every reserved field as zero;
- computes payload CRC32C after writing each complete section;
- updates header section count, total size and header CRC;
- preserves byte-identical output for results without Stage S6 data;
- produces byte-identical output after writer → reader → writer round trip.

## 6. Reader hardening rules

The reference reader treats the complete input buffer as untrusted and validates:

- container and section bounds;
- exact section versions;
- exact payload sizes;
- CRC32C;
- singleton pairing and adjacency;
- zero reserved fields;
- lifecycle, representation and confidence enums;
- range geometry;
- segment and beat limits;
- segment ordering and containment;
- beat ordering and ordinals;
- tempo range and nonzero period;
- representation/count consistency;
- revision identity across `GGRD`, segments, beats and `REVN`;
- configured allocation limit before S6 allocations.

Recognized malformed S6 data is rejected as corrupt. An unsupported recognized section version is rejected as unsupported. All partially allocated S6 state is released through normal result cleanup.

## 7. Ownership after parsing

The parser allocates and owns:

- one S6 result extension;
- one coverage range;
- the exact segment array;
- the exact beat array when present.

Views point only into result-owned memory. The parsed result is immutable and may outlive the source buffer.

## 8. Verification vectors

The reference suite verifies:

- dynamic global grid round trip;
- byte-identical reserialization;
- strict parsing;
- every truncated byte prefix;
- one trailing byte;
- duplicate, missing and reordered S6 sections;
- unsupported section version;
- nonzero reserved bytes;
- segment-count overflow;
- cross-section revision mismatch;
- configured allocation limit;
- every parser allocation failure point;
- canonical `valid-s6.apta` fuzz seed;
- bounded libFuzzer smoke with `GGRD` and `REVN` dictionary tokens.
