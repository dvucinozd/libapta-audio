# APTA container version 1 section registry

**Status:** Final APTA 1.0  
**Container version:** 1  
**Applies to:** all `.apta` readers, writers and conformance fixtures

## 1. Authority and scope

This document is the sole APTA 1.0 authority for:

- the version-1 standard FourCC registry;
- section multiplicity and required/optional status;
- cross-section dependencies;
- directory-order requirements;
- known and unknown section-version handling;
- canonical versus valid non-canonical ordering;
- strict and permissive reserved-field behaviour;
- partial-result and unknown-duration legality;
- container-major and section-version evolution.

[`file-format.md`](file-format.md) remains authoritative for the common header,
directory, CRC, alignment and the `META`, `WOVR`, `WDTL`, `TEMP` and `LGRD`
payload layouts. [`global-grid-container.md`](global-grid-container.md) remains
authoritative for the `GGRD` and `REVN` payload layouts. When either document
contains an older multiplicity, dependency, ordering, strictness, finality or
future-compatibility statement, this registry controls.

The standard container-version-1 section set is exactly:

```text
META WOVR WDTL TEMP LGRD GGRD REVN
```

`CONF` remains reserved and is not an allocated APTA 1.0 section.

## 2. Registry

| FourCC | Version | Multiplicity | Required flag | Dependencies | APTA 1.0 role |
|---|---:|---:|---|---|---|
| `WOVR` | 1 | exactly one | MUST be set | none | mandatory overview waveform base |
| `WDTL` | 1 | zero or more | MUST be clear | requires `WOVR` | optional detail tiles |
| `META` | 1 | zero or one | MUST be clear | requires `WOVR` | optional deterministic metadata |
| `TEMP` | 1 | zero or one | MUST be clear | requires `WOVR` | optional selected tempo and candidates |
| `LGRD` | 1 | zero or one | MUST be clear | requires `TEMP` | optional local constant-period grid |
| `GGRD` | 1 | zero or one | MUST be clear | requires `TEMP` and paired `REVN` | optional global grid |
| `REVN` | 1 | zero or one | MUST be clear | requires paired `GGRD` | global-grid revision state |

A version-1 standard `.apta` container therefore always has one and only one
`WOVR` section. Multiple overview levels are not representable in container
version 1. A future section version or container version may define a
multi-level overview representation without changing the meaning of version-1
`WOVR`.

One `WDTL` section may contain multiple tiles. Multiple `WDTL` sections are
valid only when every `(level_id, tile_index)` identity is unique across the
entire container. A duplicate tile identity is corrupt even when the duplicate
payload bytes are identical. Recovery-mode duplicate suppression is not part of
APTA 1.0 conformance.

`GGRD` and `REVN` form one logical pair. Either both are absent or both are
present exactly once. `REVN` MUST immediately follow `GGRD` in the section
directory.

## 3. Canonical directory order

A canonical writer emits known sections in this order:

```text
WOVR
WDTL ...
META
TEMP
LGRD
GGRD
REVN
```

Absent optional sections are omitted. Multiple `WDTL` sections, when emitted,
are contiguous and ordered by the first tile identity in each section. Tile
descriptors remain ordered by `(level_id, tile_index)` inside each section.

A reader MUST NOT require canonical ordering except for the mandatory adjacency
and order of the `GGRD`/`REVN` pair. A non-canonical but otherwise valid file may
place `META`, `TEMP`, `LGRD` and `WDTL` in another directory order. Dependency
validation is semantic and MUST NOT depend on whether the dependency appears
earlier or later in the directory.

Canonical payload offsets follow directory order and are eight-byte aligned.
A reader validates offsets and overlap independently of directory order.

## 4. Known section versions

For every standard FourCC in this registry:

- version `1` has the meaning defined by the APTA 1.0 normative set;
- any other section version is a recognized but unsupported version;
- a reader MUST return an unsupported-version result for a recognized
  unsupported version, regardless of the `REQUIRED` flag;
- a reader MUST NOT treat a recognized unsupported version as an unknown
  optional section;
- a writer claiming APTA 1.0 MUST emit only version `1` standard sections.

Adding fields that change a version-1 payload's fixed offsets, record size,
semantic interpretation, multiplicity or dependency requires a new section
version or a new FourCC. A writer MUST NOT silently place such a change under
section version `1`.

## 5. Unknown FourCC handling

After validating the common directory entry, stored range, alignment, overlap,
compression/encryption flags and CRC:

- an unknown section with `REQUIRED` set MUST be rejected as unsupported;
- an unknown optional section MUST be skipped safely;
- parsing an unknown optional section does not make its bytes part of the APTA
  result model;
- parse-to-result-to-serialize lossless preservation of unknown optional bytes
  is not an APTA 1.0 requirement;
- an implementation claiming opaque preservation MUST define a separate
  versioned extension and ownership/lifetime contract.

The `HAS_REQUIRED_EXTENSIONS` container flag is an advisory summary. It does
not replace per-section `REQUIRED` validation. A reader MUST still inspect every
directory entry.

## 6. Extended headers

Container version 1 has a 96-byte fixed header. A reader MAY accept
`header_size > 96` only when it can:

- validate that the directory begins at or after `header_size`;
- skip all unknown extended-header bytes without interpreting them;
- preserve checked arithmetic and alignment guarantees.

Unknown extended-header bytes are opaque, not version-1 reserved fields. Strict
mode MUST NOT require those unknown bytes to be zero. The version-1 canonical
writer emits `header_size == 96` and no extended-header bytes.

A future standard that requires interpretation of extended-header bytes MUST
allocate that meaning through a new container version or an explicitly
versioned required extension.

## 7. Section flags

For standard version-1 sections:

- `WOVR` MUST set `REQUIRED`;
- every other standard section MUST clear `REQUIRED`;
- every standard section MUST clear `COMPRESSED` and `ENCRYPTED`;
- all unallocated section-flag bits MUST be zero.

A reader MUST reject compression, encryption and unallocated section-flag bits.
For a recognized version-1 section, an incorrect `REQUIRED` value is corrupt.
This rule is structural and applies in strict and permissive mode.

## 8. Reserved fields and padding

A canonical writer MUST write zero to every defined reserved field and every
alignment-padding byte.

A strict reader MUST reject a non-zero value in any defined version-1 reserved
field or padding byte that it validates.

A permissive reader MAY ignore non-zero values in defined reserved fields only
when all of the following are true:

- the field has no allocated version-1 semantics;
- ignoring it cannot change an offset, size, count, state, flag, identity,
  ordering, range, allocation or security decision;
- every other structural and semantic rule remains valid.

Permissive mode does not permit:

- unknown semantic flag bits;
- invalid enum values;
- unsupported versions;
- non-zero compression or encryption bits;
- malformed deterministic CBOR;
- invalid CRCs, offsets, sizes, counts, ranges, dependencies or ordering;
- non-zero canonical padding that would make section boundaries ambiguous.

Readers MUST expose the same semantic result for strict-valid and
permissive-only inputs after ignored reserved values are removed.

## 9. Container and feature finality

`PARTIAL_RESULT` is the aggregate lifecycle declaration for the serialized
result.

A container without `PARTIAL_RESULT` set is complete. Every present standard
feature state MUST be final:

- `WOVR.flags` state is `FINAL`;
- every `WDTL.feature_state` is `FINAL`;
- `TEMP.feature_state` is `FINAL`;
- `LGRD.grid_state` and its segment state are `FINAL`;
- `GGRD.grid_state` and every segment state are `FINAL`;
- `REVN.revision_state` is `APPLIED`.

A container with `PARTIAL_RESULT` set MAY mix final, stable and provisional
features. The state encoded by each section remains authoritative. Presence of
a section never implies finality.

A non-final feature state in any present section requires `PARTIAL_RESULT`.
A pending `REVN` requires `PARTIAL_RESULT`. A final `GGRD` paired with a pending
`REVN` is invalid because the pair would make contradictory authority claims
for the same serialized generation.

A canonical writer sets `PARTIAL_RESULT` when the result session is incomplete,
when any emitted feature is not final, when a pending revision is emitted or
when source duration is unknown.

## 10. Unknown source duration

`SOURCE_DURATION_UNKNOWN` is valid only when:

- `PARTIAL_RESULT` is set;
- `total_source_frames == UINT64_MAX`;
- every emitted section uses exact currently known ranges and coverage;
- no section claims complete final coverage of an unknown source end.

When `total_source_frames == UINT64_MAX`, `SOURCE_DURATION_UNKNOWN` MUST be set.
When source duration is known, `SOURCE_DURATION_UNKNOWN` MUST be clear.
A complete container MUST NOT carry unknown source duration.

## 11. Cross-section consistency

The header source geometry is authoritative for the container. Every standard
section MUST use source-frame coordinates compatible with the header sample
rate, channel geometry and known total frame count.

Additional requirements:

- `LGRD` requires one valid `TEMP` section;
- `LGRD.nominal_tempo_millibpm` MUST equal the selected `TEMP` tempo;
- `GGRD` requires one valid `TEMP` section;
- every GGRD segment nominal tempo MUST be in the normative tempo range and
  consistent with its encoded period under the claimed profile tolerance;
- every GGRD segment and explicit beat revision MUST equal the paired
  `REVN.revision_id`;
- `REVN` representation and counts MUST equal the paired `GGRD` header;
- locked local or global ranges retain their normative identity, range, anchor,
  period, ordinal and revision semantics;
- duplicate singleton sections are corrupt;
- stored section ranges MUST NOT overlap.

`META` is descriptive and MUST NOT override source geometry, feature state,
section identity or normative result values.

## 12. Canonical versus valid non-canonical input

Canonical output is a deterministic encoding policy, not an additional semantic
profile. A valid non-canonical input may differ in:

- directory order, except the `GGRD`/`REVN` adjacency rule;
- legal section and payload padding placement;
- ignored reserved values accepted only in permissive mode;
- optional metadata values;
- segmentation or tile grouping that preserves all normative identities and
  values.

A canonical reader-writer round trip is required to produce canonical output.
It is not required to reproduce a valid non-canonical input byte-for-byte.

Canonical writer-reader-writer byte identity is required for committed
canonical fixtures produced by the same writer version and options.

## 13. Version evolution

### 13.1. Section-version change

Use a new section version when one FourCC retains the same broad purpose but its
payload layout or semantics change incompatibly. A reader may continue to parse
other known sections while rejecting the recognized unsupported version as
unsupported.

### 13.2. New optional FourCC

Use a new optional FourCC for an independently skippable feature. Older readers
skip it after common validation. The new section MUST NOT alter the meaning of
existing version-1 sections.

### 13.3. New required FourCC

Use a required FourCC only when a consumer cannot correctly interpret the
container without it. Older readers reject it. The writer SHOULD set
`HAS_REQUIRED_EXTENSIONS` as an advisory summary.

### 13.4. Container-version change

A new container version is required when compatibility cannot be expressed by
independent section versions or extensions, including changes to:

- fixed-header field offsets or required interpretation;
- directory-entry layout or size;
- common alignment, CRC, stored/logical-size or overlap rules;
- unknown-section skipping rules;
- global interpretation that changes existing section meaning;
- mandatory container-wide ordering not expressible as a section dependency.

Container version 1 MUST remain unchanged for APTA 1.0 unless an approved
incompatibility report demonstrates that one of those common mechanisms must
change.

## 14. Reader decision order

A conforming reader SHOULD apply validation in this order:

1. API arguments and configured resource limits;
2. fixed header, version, flags, size and CRC;
3. directory arithmetic, bounds and overlap;
4. each entry's common flags, alignment, size and CRC;
5. registry multiplicity, known version and dependencies;
6. section payload structure and reserved policy;
7. cross-section identities and semantic consistency;
8. partial/finality and unknown-duration rules;
9. allocation and result publication.

A malformed recognized payload is corrupt. A recognized unsupported version or
unknown required section is unsupported. A configured resource ceiling returns
the implementation's resource-limit status rather than being misreported as
corruption.

## 15. Conformance matrix

The version-1 container suite MUST cover:

- canonical legal combinations for every registry row;
- a full standard-section container;
- unknown optional and unknown required sections;
- recognized unsupported section versions;
- extended-header skipping;
- duplicate and missing singleton dependencies;
- multiple WOVR rejection;
- WDTL cross-section duplicate tile identities;
- GGRD/REVN absence, order, adjacency and identity conflicts;
- strict rejection and permissive acceptance of defined reserved fields;
- semantic flag rejection in both modes;
- complete-container non-final feature rejection;
- partial-container final/non-final combinations;
- known and unknown source-duration combinations;
- canonical writer-reader-writer byte identity;
- every prefix truncation, trailing bytes, CRC, overflow, alignment and overlap
  boundary;
- configured count and allocation ceilings;
- 32-bit, 64-bit, Windows, sanitizer and fuzz execution.

Fixture manifests MUST identify the container version, section sequence,
section versions, canonical status, byte size, SHA-256 digest, producer and the
expected strict/permissive result.
