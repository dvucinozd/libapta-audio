# Tempo model

**Status:** APTA Working Draft 0.1

## 1. Scope

This document defines portable tempo estimates and tempo candidates for APTA Core 0.1.

Tempo is a range-scoped analysis result. A local estimate around playback focus may become usable before a track-wide estimate exists.

The standard defines units, lifecycle, confidence and ambiguity semantics. It does not require one tempo-estimation algorithm.

## 2. Tempo unit

Tempo is stored as thousandths of one beat per minute:

```c
typedef uint32_t apta_tempo_millibpm_t;
```

The mathematical BPM value is:

```text
BPM = tempo_millibpm / 1000
```

Examples:

```text
120000 = 120.000 BPM
128500 = 128.500 BPM
```

Value `0` means no tempo value is present. It MUST NOT be used as a valid musical tempo.

A conforming implementation MUST use checked arithmetic when converting tempo to frame periods.

## 3. Selected tempo value

A selected tempo estimate is represented logically as:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t evidence_range;
    apta_tempo_millibpm_t tempo_millibpm;

    apta_confidence_value_t confidence;
    uint8_t reserved8;
    uint16_t reserved16;

    apta_feature_state_t state;
    uint32_t flags;
    uint32_t candidate_set_id;
    uint32_t reserved32[2];
} apta_tempo_value_t;
```

The public ABI MAY expose equivalent information through accessors.

`evidence_range` identifies the source range whose evidence supports the value. It is not necessarily the complete requested range.

A tempo value MUST NOT be presented as track-wide when it was estimated only from a local region.

## 4. Tempo flags

```c
#define APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY   (1u << 0)
#define APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY (1u << 1)
#define APTA_TEMPO_FLAG_MULTIPLE_PHASES       (1u << 2)
#define APTA_TEMPO_FLAG_DYNAMIC               (1u << 3)
#define APTA_TEMPO_FLAG_USER_CONFIRMED        (1u << 4)
#define APTA_TEMPO_FLAG_USER_EDITED           (1u << 5)
#define APTA_TEMPO_FLAG_DEGRADED              (1u << 6)
```

Ambiguity flags indicate that plausible alternatives remain. They MUST NOT be replaced by lowering confidence alone.

`DYNAMIC` means one constant tempo value does not adequately describe the declared scope. The value MAY still provide a representative or local tempo, but authoritative timing requires the beatgrid or dynamic-tempo representation.

User-confirmed or user-edited provenance does not automatically imply confidence `100`.

## 5. Tempo candidates

An implementation MAY expose multiple candidates:

```c
typedef uint32_t apta_tempo_relation_t;

#define APTA_TEMPO_RELATION_INDEPENDENT 0u
#define APTA_TEMPO_RELATION_HALF        1u
#define APTA_TEMPO_RELATION_DOUBLE      2u
#define APTA_TEMPO_RELATION_THREE_HALF  3u
#define APTA_TEMPO_RELATION_TWO_THIRDS  4u

typedef struct {
    apta_tempo_millibpm_t tempo_millibpm;
    uint16_t score;
    apta_confidence_value_t confidence;
    uint8_t reserved8;
    apta_tempo_relation_t relation_to_selected;
    uint32_t flags;
} apta_tempo_candidate_t;
```

`score` is an unsigned backend-relative ranking value in `0..65535`. Scores are comparable only within one candidate set produced by the same backend generation unless the producer documents stronger calibration.

Candidates in one set MUST be ordered from most preferred to least preferred. Equal score ordering MUST be deterministic for a reference backend.

`relation_to_selected` describes a known metric relationship to the selected candidate. It does not prove that either interpretation is musically correct.

## 6. Candidate-set identity

A candidate set has a non-zero identifier scoped to one session lineage.

When additional evidence changes the candidate ordering, a new result generation MAY:

- retain the candidate-set identifier and increment a candidate revision when the set represents continued refinement; or
- allocate a new identifier when the interpretation lineage changes materially.

The selected tempo value SHOULD reference the candidate set from which it was chosen.

## 7. Local and global tempo

APTA distinguishes:

- **local tempo** — supported by evidence near a requested or focused range;
- **track tempo** — intended to summarise a wider or complete source scope;
- **dynamic tempo** — timing varies enough that one constant value is insufficient.

A local tempo result can be `STABLE` for its evidence range while the track tempo remains `PARTIAL` or `PROVISIONAL`.

An implementation MUST expose the evidence and declared coverage so applications do not confuse local and global scope.

## 8. Stability

A provisional tempo MAY change as additional evidence arrives.

A stable tempo for unchanged declared coverage MUST NOT change silently. A conflicting refinement requires:

- an explicit pending revision;
- accepted source/configuration revision; or
- a new result lineage.

A tempo may become `FINAL` only when its requested scope and end-of-input conditions are known and no applicable pending revision exists.

## 9. Relationship to beatgrid

Tempo and beat phase are separate results.

A selected BPM value does not establish:

- the position of the first beat;
- beat phase;
- bar boundaries;
- downbeats;
- correctness of every beat on a dynamic-tempo recording.

Applications requiring Sync or Quantize SHOULD inspect beatgrid state and confidence in addition to tempo state and confidence.

Tempo derived from a grid segment SHOULD agree with the segment period within the numerical tolerance defined by the conformance profile.

## 10. Range requests and progressive analysis

When a host requests local BPM for a priority region, the implementation MAY analyse a wider evidence window when required by the algorithm.

The result MUST distinguish:

- the host-requested range;
- the actual evidence range;
- the range over which the selected tempo is asserted to apply.

The implementation SHOULD publish a provisional useful estimate as soon as sufficient evidence exists rather than waiting for full-track analysis.

## 11. Failure and insufficient evidence

Silence, noise, weak rhythm or too-short input may produce no usable tempo.

The implementation MUST distinguish:

- feature absent because processing has not run;
- temporarily insufficient evidence;
- feature-range failure;
- unsupported capability.

A zero tempo value MUST NOT be used as the sole failure indication. Lifecycle state and diagnostics provide the machine-readable reason.

## 12. Serialization

Serialized tempo data MUST include:

- unit version;
- tempo value;
- evidence and applicability ranges;
- lifecycle state;
- confidence;
- ambiguity and provenance flags;
- candidate-set identity when candidates are present;
- producer/backend provenance sufficient to interpret backend-relative scores.

Unknown optional candidate relationships MUST be ignored safely. Unknown required tempo representation versions MUST be rejected.

## 13. Conformance fixtures

Tempo fixtures SHOULD include:

- click tracks from 40 through 300 BPM;
- half-time and double-time patterns;
- sparse intros;
- breakdowns;
- swing and syncopation;
- silence and low-level material;
- abrupt tempo changes;
- gradual tempo ramps;
- local regions whose tempo differs from the track median;
- 44.1 kHz and 48 kHz sources.

Semantic conformance does not require two independent algorithms to select bit-identical tempo values. Reference-backend conformance MAY define exact or bounded numerical expectations for named fixtures.
