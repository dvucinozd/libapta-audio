# `.apta` container format

**Status:** APTA 1.0 Release Candidate Draft  
**Container version:** 1  
**Profiles:** `APTA-WAVEFORM-1.0` and `APTA-CORE-ANALYSIS-1.0`

## 1. Scope

This document defines the common byte-level `.apta` container and the version-1 META, WOVR, WDTL, TEMP and LGRD sections. The normative GGRD and REVN version-1 payloads are defined by [`global-grid-container.md`](global-grid-container.md) and are part of the same APTA 1.0 container contract.

The container is a portable interchange format. Native C structure layout, pointer values, host endianness and compiler padding MUST NOT be written directly.

Future features use separately versioned optional sections or extensions. A reserved name or architecture example is not a normative allocation.

## 2. Integer encoding

All multi-octet integers are unsigned or two's-complement signed integers encoded little-endian.

Readers MUST use checked decoding and MUST NOT access a multi-octet field through an unaligned native pointer cast.

Unless otherwise stated:

- offsets are absolute byte offsets from the beginning of the file;
- sizes are byte counts;
- ranges use an exclusive end;
- reserved fields MUST be zero when written;
- readers MUST ignore zero-valued reserved fields and reject non-zero reserved fields in strict mode.

## 3. Alignment and padding

The fixed header begins at byte zero.

The section directory and every section payload MUST begin at an offset divisible by eight.

Padding bytes between structures or sections MUST be zero. Padding is not included in a section's stored size or CRC.

A canonical writer places:

1. the 96-byte fixed header;
2. any extended-header bytes;
3. zero padding to eight-byte alignment;
4. the section directory;
5. zero padding;
6. section payloads in directory order.

Readers MUST NOT require canonical ordering when all offsets and validation rules are satisfied.

## 4. CRC32C

Header and section integrity use CRC32C Castagnoli with:

```text
reflected polynomial: 0x82F63B78
initial value:        0xFFFFFFFF
input reflection:     yes
output reflection:    yes
final XOR:            0xFFFFFFFF
```

The header CRC is computed over fixed-header bytes `0..91` inclusive. The CRC field itself occupies bytes `92..95` and is not part of the computation.

A section CRC is computed over exactly `stored_size` bytes beginning at the section offset.

## 5. Fixed header

The version-1 fixed header is exactly 96 bytes.

| Offset | Size | Field | Encoding and meaning |
|---:|---:|---|---|
| 0 | 4 | `magic` | ASCII bytes `41 50 54 41` (`APTA`). |
| 4 | 2 | `header_size` | Total header bytes before alignment padding. Minimum and canonical value: `96`. |
| 6 | 2 | `container_version` | Value `1`. |
| 8 | 2 | `specification_major` | APTA specification major version. |
| 10 | 2 | `specification_minor` | APTA specification minor version. |
| 12 | 4 | `producer_api_version` | Encoded producer API version. |
| 16 | 4 | `flags` | Container flags. |
| 20 | 4 | `section_count` | Number of 40-byte directory entries. |
| 24 | 8 | `section_directory_offset` | Absolute directory offset. |
| 32 | 8 | `total_file_size` | Exact file size in bytes. |
| 40 | 8 | `total_source_frames` | Exclusive source end or `UINT64_MAX` when unknown in a permitted partial file. |
| 48 | 4 | `source_sample_rate` | Source frames per second. |
| 52 | 2 | `source_channel_count` | Source channel count. |
| 54 | 2 | `source_channel_layout` | Container channel-layout identifier. |
| 56 | 32 | `source_fingerprint` | Fingerprint bytes interpreted by `source_fingerprint_kind`. |
| 88 | 4 | `source_fingerprint_kind` | Source identity algorithm identifier. |
| 92 | 4 | `header_crc32c` | CRC32C over bytes `0..91`. |

### 5.1. Header validation

A reader MUST reject a file when:

- magic is not `APTA`;
- `header_size < 96`;
- the container version is unsupported;
- the directory offset is before `header_size`;
- the directory offset is not eight-byte aligned;
- `section_count * 40` overflows;
- the directory lies outside `total_file_size`;
- `total_file_size` differs from the actual input size for a complete seekable input;
- sample rate or channel count violates the claimed profile;
- the header CRC is invalid;
- configured file, section or allocation limits are exceeded.

A reader MAY accept `header_size > 96` only when it can safely skip the unknown extended-header bytes.

## 6. Container flags

```text
bit 0  PARTIAL_RESULT
bit 1  SOURCE_DURATION_UNKNOWN
bit 2  HAS_REQUIRED_EXTENSIONS
bits 3..31 reserved
```

`PARTIAL_RESULT` permits provisional or incomplete feature coverage.

`SOURCE_DURATION_UNKNOWN` requires `total_source_frames == UINT64_MAX`.

A final-result file MUST NOT set `SOURCE_DURATION_UNKNOWN`.

## 7. Source fingerprint kinds

```text
0  NONE
1  APPLICATION_OPAQUE_256
2  SHA256_SOURCE_OBJECT_BYTES
3  SHA256_CANONICAL_PCM_V1 (reserved; algorithm not yet normative)
```

For kind `NONE`, all fingerprint bytes MUST be zero.

For `APPLICATION_OPAQUE_256`, the application defines the identity domain and MUST compare it only within that domain.

For `SHA256_SOURCE_OBJECT_BYTES`, the fingerprint is SHA-256 of the exact source-object byte sequence. Metadata edits or container retranscoding therefore change identity even when decoded audio is perceptually identical.

A reader MUST NOT interpret an unknown fingerprint kind as a known hash algorithm.

## 8. Section directory entry

Every directory entry is exactly 40 bytes.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `type_fourcc` | Four ASCII octets stored in file byte order. |
| 4 | 2 | `section_version` | Payload version for this FourCC. |
| 6 | 2 | `flags` | Section flags. |
| 8 | 8 | `offset` | Absolute aligned payload offset. |
| 16 | 8 | `stored_size` | Bytes physically stored. |
| 24 | 8 | `logical_size` | Uncompressed logical size; equals `stored_size` in version 1. |
| 32 | 4 | `crc32c` | CRC32C of stored payload bytes. |
| 36 | 4 | `reserved` | Zero. |

Directory entries MUST NOT overlap the fixed/extended header or directory.

Two non-empty stored section ranges MUST NOT overlap.

## 9. Section flags

```text
bit 0  REQUIRED
bit 1  COMPRESSED
bit 2  ENCRYPTED
bits 3..15 reserved
```

Container version 1 does not define compression or encryption. Writers MUST clear those bits.

A reader:

- MUST reject an unknown section marked `REQUIRED`;
- MUST ignore an unknown optional section after validating its bounds;
- MUST reject unsupported compression or encryption;
- MUST reject duplicate required singleton sections;
- MAY preserve unknown optional sections during a lossless rewrite.

APTA 1.0 requires safe skipping of unknown optional sections. It does not require parse-to-result-to-serialize preservation of their bytes unless an implementation claims a separately documented opaque-preservation extension.

## 10. FourCC values

The byte sequence shown is stored literally.

| FourCC | Purpose | Version-1 status |
|---|---|---|
| `META` | Deterministic structured metadata | Optional, normative |
| `WOVR` | Overview waveform level | Normative |
| `WDTL` | Detail waveform tiles | Optional, normative |
| `TEMP` | Selected tempo and candidate set | Optional, normative |
| `LGRD` | Local constant-period beatgrid | Optional, normative |
| `GGRD` | Global or multi-segment beatgrid | Optional, normative in `global-grid-container.md` |
| `CONF` | Additional confidence payload | Reserved |
| `REVN` | Pending or applied global-grid revision | Optional, normative in `global-grid-container.md` |

Multiple `WOVR` sections are permitted only when each carries a distinct `level_id`.

One `WDTL` section may contain multiple tiles. Multiple `WDTL` sections are permitted when their tile identities do not conflict.

`TEMP` and `LGRD` are optional singleton sections. `LGRD` version 1 requires one valid `TEMP` version-1 section in the same container.

`GGRD` and `REVN` are optional singleton sections that MUST appear as an adjacent pair in that order. Their payload, count, revision and cross-section rules are normative in [`global-grid-container.md`](global-grid-container.md).

## 11. `META` section version 1

`META` version 1 contains one deterministic CBOR data item using the deterministic encoding rules of RFC 8949.

The top-level item MUST be a map.

Recognized integer keys are:

```text
1  producer_name             text string
2  producer_version_string   text string
3  backend_name              text string
4  backend_version           text string
5  creation_unix_time        unsigned integer seconds; optional
6  application_source_id     byte string or text string
7  comments                  text string; informative
```

Unknown keys MUST be ignored. Duplicate keys MUST be rejected.

The parser MUST enforce configured limits for nesting depth, item count and text/byte-string size.

`META` MUST NOT contain executable code, absolute filesystem paths required for interpretation, or credentials.

## 12. `WOVR` section version 1

One `WOVR` section stores one overview level.

### 12.1. Overview header

The section begins with a 48-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `level_id` |
| 4 | 4 | `frames_per_column` |
| 8 | 8 | `origin_frame` |
| 16 | 4 | `logical_column_count` |
| 20 | 4 | `span_count` |
| 24 | 8 | `span_directory_offset` relative to section start |
| 32 | 8 | `column_data_offset` relative to section start |
| 40 | 4 | `flags` |
| 44 | 4 | `reserved` |

`frames_per_column` MUST be non-zero.

`logical_column_count` is the number of column positions in the level geometry, including positions not yet covered by analysed data.

### 12.2. Overview span entry

Each span entry is 32 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `first_frame` |
| 8 | 8 | `end_frame` |
| 16 | 4 | `first_column_index` |
| 20 | 4 | `column_count` |
| 24 | 4 | `data_column_offset` |
| 28 | 4 | `reserved` |

`data_column_offset` is measured in columns from `column_data_offset`, not in bytes.

Span source ranges MUST be non-empty, sorted by `first_frame` and non-overlapping.

The logical column interval MUST lie inside `logical_column_count`.

The referenced packed column interval MUST lie inside the section column payload.

### 12.3. Packed waveform column

Each column is exactly 10 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | signed `minimum` |
| 2 | 2 | signed `maximum` |
| 4 | 2 | unsigned `rms` |
| 6 | 1 | `low` |
| 7 | 1 | `mid` |
| 8 | 1 | `high` |
| 9 | 1 | `flags` |

Values use the semantics in `waveform.md`.

A gap has no span and no valid fabricated column.

### 12.4. WOVR validation

A reader MUST validate:

- all relative offset additions for overflow;
- header, span directory and packed columns inside the section;
- sorted non-overlapping spans;
- logical and packed column bounds;
- `minimum <= maximum` for valid columns;
- reserved column flag bits according to strictness policy;
- source-frame ranges consistent with level geometry, allowing a shorter final column.

## 13. `WDTL` section version 1

A `WDTL` section begins with a 16-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `tile_count` |
| 4 | 4 | `flags` |
| 8 | 8 | `tile_directory_offset` relative to section start |

Each tile descriptor is 48 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `level_id` |
| 4 | 4 | `tile_index` |
| 8 | 8 | `first_frame` |
| 16 | 8 | `end_frame` |
| 24 | 4 | `first_column_index` |
| 28 | 4 | `column_count` |
| 32 | 8 | `columns_offset` relative to section start |
| 40 | 4 | `feature_state` |
| 44 | 2 | `flags` |
| 46 | 1 | `confidence` |
| 47 | 1 | `reserved` |

Tile column arrays use the same packed 10-byte column encoding as `WOVR`.

Tile identities `(level_id, tile_index)` MUST be unique in one result generation.

Tile descriptors MUST be sorted by `(level_id, tile_index)` in canonical output.

## 14. `TEMP` section version 1

`TEMP` stores one selected tempo value and its ordered candidate set. The section flags are zero and the section is optional.

### 14.1. Tempo header

The section begins with a 56-byte header:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | `payload_version` | Value `1`. |
| 2 | 1 | `feature_state` | `PROVISIONAL`, `STABLE` or `FINAL`. |
| 3 | 1 | `confidence` | `0..100`. |
| 4 | 4 | `tempo_flags` | Tempo ambiguity and provenance flags. |
| 8 | 4 | `tempo_millibpm` | Selected tempo in millibeats per minute. |
| 12 | 4 | `candidate_set_id` | Revision identifier for this candidate set. |
| 16 | 8 | `evidence_first_frame` | Inclusive source-frame start. |
| 24 | 8 | `evidence_end_frame` | Exclusive source-frame end. |
| 32 | 8 | `applicability_first_frame` | Inclusive range start. |
| 40 | 8 | `applicability_end_frame` | Exclusive range end. |
| 48 | 4 | `candidate_count` | `1..3` in reference format version 1. |
| 52 | 4 | `reserved` | Zero. |

Tempo values MUST be in `40000..300000` millibpm. Evidence and applicability ranges MUST be non-empty.

### 14.2. Tempo candidate entry

Each candidate is exactly 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `tempo_millibpm` |
| 4 | 2 | `score` |
| 6 | 1 | `confidence` |
| 7 | 1 | `relation_to_selected` |
| 8 | 4 | `flags` |
| 12 | 4 | `reserved` |

Candidates MUST be ordered by non-increasing score. Candidate confidence MUST be `0..100`. `relation_to_selected` uses the normative tempo-relation identifiers from `tempo.md`.

## 15. `LGRD` section version 1

`LGRD` stores one local constant-period grid segment and one explicit coverage range. It does not represent a global or dynamic-tempo grid.

The payload is exactly 144 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | `payload_version` | Value `1`. |
| 2 | 1 | `grid_state` | `PROVISIONAL`, `STABLE` or `FINAL`. |
| 3 | 1 | `grid_confidence` | `0..100`. |
| 4 | 4 | `grid_flags` | Grid flags, including `LOCKED`. |
| 8 | 4 | `representation` | Must be `SEGMENTS`. |
| 12 | 4 | `segment_count` | Must be `1`. |
| 16 | 8 | `requested_first_frame` | Requested-range start. |
| 24 | 8 | `requested_end_frame` | Requested-range end. |
| 32 | 8 | `evidence_first_frame` | Evidence-range start. |
| 40 | 8 | `evidence_end_frame` | Evidence-range end. |
| 48 | 8 | `applicability_first_frame` | Applicability-range start. |
| 56 | 8 | `applicability_end_frame` | Applicability-range end. |
| 64 | 8 | `coverage_first_frame` | Coverage-range start. |
| 72 | 8 | `coverage_end_frame` | Coverage-range end. |
| 80 | 8 | `anchor_whole_frame` | Integer anchor position. |
| 88 | 4 | `anchor_fraction_q32` | Fractional anchor component. |
| 92 | 4 | `reserved_anchor` | Zero. |
| 96 | 8 | `anchor_ordinal` | Signed two's-complement beat ordinal. |
| 104 | 8 | `period_whole_frames` | Integer frames per beat; non-zero. |
| 112 | 4 | `period_fraction_q32` | Fractional period component. |
| 116 | 4 | `beat_count` | Beats represented inside applicability. |
| 120 | 4 | `nominal_tempo_millibpm` | Must match selected `TEMP` tempo. |
| 124 | 4 | `segment_id` | Stable segment identity. |
| 128 | 4 | `revision` | Segment revision. |
| 132 | 4 | `segment_flags` | Segment-level flags. |
| 136 | 1 | `segment_state` | `PROVISIONAL`, `STABLE` or `FINAL`. |
| 137 | 1 | `segment_confidence` | `0..100`. |
| 138 | 2 | `reserved16` | Zero. |
| 140 | 4 | `reserved32` | Zero. |

All requested, evidence, applicability and coverage ranges MUST be non-empty. The nominal tempo MUST equal the selected tempo in the accompanying `TEMP` section.

A locked local grid sets the normative `LOCKED` flag in both grid and segment flags. A reader MUST preserve the locked anchor, period, ordinal, segment identity and applicability range.

## 16. Duplicate and conflict rules

A reader MUST reject:

- two `WOVR` sections with the same `level_id`;
- two detail tiles with the same identity but different payload;
- duplicate `TEMP` singleton sections;
- duplicate `LGRD` singleton sections;
- duplicate `GGRD` or `REVN` singleton sections;
- an unpaired or non-adjacent `GGRD`/`REVN` pair;
- `LGRD` without `TEMP`;
- an `LGRD` nominal tempo that differs from the selected `TEMP` tempo;
- section ranges that overlap;
- a required singleton section appearing more than once;
- conflicting source format information between header and a required section.

An implementation performing a non-canonical recovery MAY ignore a byte-identical duplicate optional tile only when the profile explicitly permits recovery mode. Strict validation rejects duplicates.

## 17. Parser resource limits

Before allocation, a parser MUST apply configured limits including:

- maximum file size;
- maximum section count;
- maximum size per section;
- maximum overview levels;
- maximum waveform columns;
- maximum detail tiles;
- maximum tempo candidates;
- maximum local-grid coverage ranges and segments;
- maximum global-grid segments and explicit beats;
- maximum metadata nesting and item count;
- maximum aggregate allocation.

Arithmetic used to calculate allocation size MUST be checked before allocation.

A parser MUST NOT allocate `logical_size` bytes merely because an untrusted file requests it.

## 18. Partial results

A file with `PARTIAL_RESULT` may contain:

- disjoint waveform spans;
- provisional waveform columns;
- provisional tempo candidates;
- a provisional local grid with explicit evidence, applicability and coverage;
- a provisional global grid with a valid paired revision section;
- unknown source duration;
- missing overview, detail, tempo or grid regions.

Every partial feature MUST retain exact coverage and state. A reader MUST NOT infer finality from the presence of a section.

## 19. Canonical writer rules

A canonical version-1 writer:

- writes a 96-byte header;
- writes the directory immediately after aligned header bytes;
- orders known sections as `WOVR`, `WDTL`, `META`, `TEMP`, `LGRD`, `GGRD`, `REVN`, omitting absent optional sections and keeping `REVN` immediately after `GGRD`;
- orders overview spans and tile descriptors as required above;
- orders tempo candidates by non-increasing score;
- writes zero padding and reserved fields;
- emits no duplicate sections or tile identities;
- uses deterministic CBOR for `META`;
- writes exact total size and CRC values;
- writes uncompressed and unencrypted payloads.

Canonical byte identity is not required between producers when optional metadata or semantically equivalent analysis values differ.

## 20. Security conformance cases

Malformed-container tests MUST include:

- truncated header and directory;
- integer overflow in directory size;
- section offset before the directory end;
- section ending beyond file size;
- overlapping sections;
- invalid alignment;
- duplicate required sections;
- unsupported required FourCC or version;
- invalid CRC;
- excessive section, column, candidate or segment counts;
- malformed deterministic CBOR;
- WOVR span and column range overflow;
- WDTL duplicate tile identity;
- duplicate `TEMP`, `LGRD`, `GGRD` or `REVN`;
- `LGRD` without `TEMP`;
- missing, reordered or mismatched `GGRD`/`REVN`;
- invalid tempo, state, confidence, relation or range fields;
- `TEMP`/`LGRD` nominal-tempo conflict;
- non-zero reserved values in strict mode.
