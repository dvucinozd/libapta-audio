# WOVR version-1 lifecycle-state flags

**Status:** APTA 1.0 Release Candidate Draft  
**Applies to:** `.apta` container version 1, `WOVR` section version 1

## 1. Purpose

The `flags` field at offset 40 of the 48-byte `WOVR` header preserves the lifecycle state of the serialized overview level.

Without this field definition, exact partial, provisional, stable and final semantics would be lost when an immutable result is written to `.apta`.

## 2. Encoding

Bits `0..2` contain one `apta_feature_state_t` numeric value:

```text
1  PARTIAL
2  PROVISIONAL
3  STABLE
4  FINAL
```

The mask is:

```text
0x00000007  FEATURE_STATE_MASK
```

Bits `3..31` are reserved and MUST be zero in version 1.

A writer MUST emit exactly one of the four defined values. `ABSENT`, `FAILED` and unknown numeric values MUST NOT be serialized as a `WOVR` payload.

A strict reader MUST reject:

- a zero lifecycle value;
- a lifecycle value greater than `FINAL`;
- any non-zero reserved flag bit.

## 3. Relationship to container flags

A `WOVR` state other than `FINAL` requires the container `PARTIAL_RESULT` flag.

A `FINAL` `WOVR` section does not by itself prove that every other feature in the container is final. Container-level finality depends on every required serialized feature and on known source duration.

When `SOURCE_DURATION_UNKNOWN` is set, `PARTIAL_RESULT` MUST also be set and a `WOVR` state MUST NOT be `FINAL`.

## 4. Canonical writer rule

A canonical writer copies the in-memory overview lifecycle state into bits `0..2` without remapping:

```text
wovr_flags = feature_state & 0x7
```

All remaining bits are written as zero.
