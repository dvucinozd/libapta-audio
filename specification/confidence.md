# Confidence model

**Status:** APTA Working Draft 0.1

## 1. Purpose

Confidence communicates the implementation's estimated reliability of an analysis value. It is independent of lifecycle state, feature availability and user acceptance.

## 2. Representation

Confidence uses an unsigned octet:

```c
typedef uint8_t apta_confidence_value_t;

#define APTA_CONFIDENCE_MIN      0u
#define APTA_CONFIDENCE_MAX    100u
#define APTA_CONFIDENCE_UNKNOWN 255u
```

Values `101` through `254` are reserved and MUST NOT be emitted by APTA Core 0.1.

`0` means the implementation evaluated the value and has effectively no supporting confidence. `255` means confidence was not computed or is unavailable. These meanings MUST NOT be conflated.

## 3. Semantic bands

| Value | Meaning |
|---:|---|
| `0–24` | insufficient evidence |
| `25–49` | weak |
| `50–74` | usable with restrictions |
| `75–89` | strong |
| `90–100` | very strong |
| `255` | unknown or not computed |

The bands support interoperability and diagnostics. They do not mandate colours, warnings or control behaviour in a user interface.

## 4. Per-feature and per-region confidence

Confidence MUST be attached to the narrowest useful object, for example:

- one BPM candidate;
- one selected BPM value;
- one grid segment;
- one explicit beat;
- one pending revision;
- one feature coverage range.

A track-wide confidence value MUST NOT conceal substantially weaker local regions when local confidence is available.

## 5. Independence from lifecycle

Examples:

- a provisional BPM may have confidence `86` but remain provisional because additional track evidence could reveal double-time ambiguity;
- a stable local grid may have confidence `58` because the input is rhythmically weak;
- a final waveform may use `APTA_CONFIDENCE_UNKNOWN` when confidence is not meaningful for deterministic amplitude aggregation.

Applications MUST inspect lifecycle state and confidence separately.

## 6. Calibration

The standard does not mandate one confidence algorithm. However, an implementation claiming conformance MUST document:

- what evidence contributes to each confidence value;
- whether confidence is calibrated globally or per feature;
- known conditions that reduce reliability;
- whether values are comparable between different algorithm backends.

A backend SHOULD produce monotonic interpretation: values in a higher semantic band should, over a representative validation corpus, correspond to more reliable results than lower-band values.

## 7. Ambiguity flags

Confidence alone does not encode alternative interpretations.

Tempo and beatgrid results MUST expose explicit ambiguity flags or candidates for cases such as:

- half-time versus nominal tempo;
- double-time versus nominal tempo;
- multiple plausible beat phases;
- unstable dynamic-tempo segmentation.

A high confidence in the existence of two plausible candidates is not equivalent to high confidence in one selected candidate.

## 8. User-confirmed data

User confirmation is provenance, not automatically confidence `100`.

An implementation MAY preserve an algorithmic confidence value and separately flag a value as user-confirmed or user-edited.

## 9. Serialization

Serialized confidence MUST preserve the exact integer value and its associated feature, range or candidate.

Readers MUST preserve unknown value `255` and MUST reject or safely map reserved values according to the container-version rules.
