# APTA 1.1 container-v1 DJ sections

**Status:** APTA 1.1 implementation contract

**Container version:** 1

**Section versions:** 1

## 1. Compatibility and registry extension

This document allocates three optional FourCC sections in the unchanged APTA
container-version-1 envelope:

| FourCC | Multiplicity | Required flag | Feature |
|---|---:|---:|---|
| `MKEY` | zero or one | clear | musical key and ranked candidates |
| `MTRD` | zero or one | clear | meter, downbeat and meter segments |
| `CONF` | zero or one | clear | calibrated per-target quality records |

The APTA 1.0 normative set remains frozen. Its statement that `CONF` was
reserved accurately describes APTA 1.0. This APTA 1.1 allocation uses the
existing optional-section evolution rule: an APTA 1.0 reader validates the
common directory entry, stored range, flags and CRC32C, then skips these unknown
optional FourCCs. No fixed-header field, directory-entry field, alignment rule,
CRC rule, or existing payload meaning changes.

Canonical APTA 1.1 output orders known sections as:

```text
WOVR WDTL... META TEMP LGRD GGRD REVN MKEY MTRD CONF
```

Absent optional sections are omitted. Appending the additions preserves the
APTA 1.0 relative order and mandatory `GGRD`/`REVN` adjacency. Readers do not
require canonical directory order except for existing dependencies. Payload
offsets follow directory order and are eight-byte aligned. Every unallocated
byte below is reserved and must be zero.

## 2. Common rules

- Multibyte integers are little-endian. Signed values use two's complement.
  Native C layout, padding, pointers and `sizeof` values are never serialized.
- Source ranges are half-open `[first_frame, end_frame)` in source PCM frames,
  non-empty, and no later than a known header source duration.
- States use `PARTIAL=1`, `PROVISIONAL=2`, `STABLE=3`, `FINAL=4`. A complete
  container permits only `FINAL`; another present state requires the container
  `PARTIAL_RESULT` flag.
- Confidence is `0..100`, or `255` for unknown. No other byte is valid.
- Counts and checked sizes are limited before record traversal or allocation.
  Version-1 reference limits are 24 key candidates, 65,536 meter segments, and
  11 quality records.
- Payload version, directory version and fixed offsets/sizes equal this
  document. Stored and logical sizes match; compression/encryption are clear.
- Each singleton occurs at most once, including `CONF`. A recognized version
  other than 1 is unsupported. An incorrect required flag is corrupt.

## 3. `MKEY` version 1

### 3.1 Header (40 bytes)

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | `payload_version` | `1` |
| 2 | 1 | `feature_state` | state value |
| 3 | 1 | `confidence` | `0..100` or `255` |
| 4 | 1 | `selected_tonic` | pitch class `0..11` (`C=0`) |
| 5 | 1 | `selected_mode` | `1=major`, `2=minor` |
| 6 | 2 | `selected_tuning_cents` | signed cents, `-100..100` |
| 8 | 4 | `flags` | zero in version 1 |
| 12 | 4 | `candidate_count` | `0..24` |
| 16 | 8 | `applicability_first_frame` | inclusive source frame |
| 24 | 8 | `applicability_end_frame` | exclusive source frame |
| 32 | 4 | `candidates_offset` | exactly `40` |
| 36 | 4 | `reserved` | zero |

Tonic has no on-wire unknown sentinel: a present `MKEY` has a selected key;
absence omits the section. Tuning is signed cents from equal temperament.

### 3.2 Candidate record (16 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | `tonic`, `0..11` |
| 1 | 1 | `mode`, `1` or `2` |
| 2 | 2 | signed `tuning_offset_cents`, `-100..100` |
| 4 | 2 | `score`, `0..65535` |
| 6 | 1 | `confidence`, `0..100` or `255` |
| 7 | 1 | `reserved8`, zero |
| 8 | 4 | `flags`, zero in version 1 |
| 12 | 4 | `reserved32`, zero |

Candidates are strictly descending by score. Identity
`(tonic, mode, tuning_offset_cents)` is unique. When candidates exist, one
reproduces the selected tuple. Selected-only uses count zero. Exact payload
size is `40 + candidate_count * 16`.

## 4. `MTRD` version 1

### 4.1 Header (48 bytes)

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | `payload_version` | `1` |
| 2 | 1 | `feature_state` | state value |
| 3 | 1 | `confidence` | `0..100` or `255` |
| 4 | 2 | `numerator` | `1..32` |
| 6 | 2 | `denominator` | power of two in `1..32` |
| 8 | 4 | `flags` | zero in version 1 |
| 12 | 4 | `segment_count` | `1..65536` |
| 16 | 8 | `downbeat_frame` | source PCM frame |
| 24 | 8 | `downbeat_ordinal` | signed beat ordinal |
| 32 | 4 | `segments_offset` | exactly `48` |
| 36 | 4 | `reserved32` | zero |
| 40 | 8 | `reserved64` | zero |

Header meter/downbeat fields exactly equal the first segment.

### 4.2 Meter-segment record (56 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `applicability_first_frame` |
| 8 | 8 | `applicability_end_frame` |
| 16 | 8 | `downbeat_frame` |
| 24 | 8 | signed `downbeat_ordinal` |
| 32 | 2 | `numerator`, `1..32` |
| 34 | 2 | `denominator`, power of two in `1..32` |
| 36 | 1 | `feature_state` |
| 37 | 1 | `confidence`, `0..100` or `255` |
| 38 | 2 | `reserved16`, zero |
| 40 | 4 | `flags`, zero in version 1 |
| 44 | 4 | `segment_id`, non-zero |
| 48 | 8 | `reserved64`, zero |

Segments are sorted by applicability start, non-overlapping, and have unique
IDs. In canonical applicability order, non-zero segment IDs are strictly
increasing, making validation deterministic O(n) even at the 65,536-record
limit. The header state is a conservative lower bound: every segment state is
at least as mature as the header state. A complete container therefore has a
`FINAL` header and only `FINAL` segments. Downbeat frame lies within its
segment; ordinals strictly increase. When a grid is present, every segment
downbeat identifies an exact encoded grid
beat/anchor with the same whole frame and ordinal. Exact payload size is
`48 + segment_count * 56`.

## 5. `CONF` version 1

`CONF` is the APTA 1.1 allocation of the FourCC reserved by APTA 1.0. It holds
multiple per-target quality records.

### 5.1 Header (16 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `payload_version`, `1` |
| 2 | 2 | `record_size`, exactly `32` |
| 4 | 4 | `record_count`, `1..11` |
| 8 | 4 | `records_offset`, exactly `16` |
| 12 | 4 | `reserved`, zero |

### 5.2 Quality record (32 bytes)

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 8 | `target_feature` | one defined non-quality feature bit |
| 8 | 4 | `calibration_model_id` | producer-stable model identifier |
| 12 | 2 | `evidence_coverage_permille` | `0..1000`, or `65535` unknown |
| 14 | 1 | `confidence` | `0..100`, or `255` unknown |
| 15 | 1 | `feature_state` | state value |
| 16 | 4 | `flags` | quality warning flags |
| 20 | 4 | `reserved32` | zero |
| 24 | 8 | `reserved64` | zero |

Allowed warning bits are `AMBIGUOUS=1<<0`, `DEGRADED=1<<1`,
`OUT_OF_DOMAIN=1<<2`, and `DETECTOR_DISAGREEMENT=1<<3`. A target is represented
by another parsed section or a feature derived from it. `CALIBRATED_QUALITY`
cannot target itself. Targets are unique; canonical records strictly increase
by numeric feature bit. Exact size is `16 + record_count * 32`.

## 6. Validation and deterministic output

A reader validates common framing and all payloads before publication. It
rejects CRC failure, truncation, trailing bytes, duplicates, unsupported
versions, wrong required flags, non-zero reserved bytes, invalid enums,
sentinels, ranges or counts, arithmetic overflow, overlapping stored ranges,
ordering defects, and cross-feature conflicts. Allocation ceilings apply to
the aggregate immutable result graph before each allocation. Normal result
release owns every successfully allocated array.

A canonical writer emits a section only when its feature bit and complete
internally consistent storage are present. It sorts quality records by target,
writes reserved/padding bytes as zero, and is byte-deterministic for an equal
immutable result and options. With all three features absent, APTA 1.0 output
bytes and semantics remain unchanged.

## 7. Compatibility evidence

The committed `tests/fixtures/dj-sections-v1-combined.apta.hex` is independently
constructed. The current reader parses it and writer-reader-writer reproduces
it byte-for-byte. The standalone frozen consumer in
`tests/compat/1.0.0/frozen_container_consumer.c` links no libapta code; it
validates v1 header/directory/ranges/CRC and skips all three additions as
unknown optional sections. The committed APTA 1.0 fixture suite remains the
reverse-compatibility authority.
