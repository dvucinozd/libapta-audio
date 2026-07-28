# Beatgrid model

**Status:** APTA Working Draft 0.1

## 1. Scope

This document defines local and global beat timing for APTA Core 0.1.

A beatgrid describes beat positions on the decoded source-frame timeline. It is separate from tempo selection, bar/downbeat classification and phrase analysis.

APTA Core 0.1 does not assign musical bar numbers or downbeat labels. Those are reserved for future extensions.

## 2. Beat position

Beat positions use the fractional source-frame representation from `time-model.md`:

```c
typedef struct {
    uint64_t whole_frame;
    uint32_t fraction_q32;
    uint32_t reserved;
} apta_fractional_frame_t;
```

The represented position is:

```text
whole_frame + fraction_q32 / 2^32
```

Beat positions MUST be strictly increasing inside one authoritative sequence.

A beat position MUST NOT use milliseconds or resampled-frame indices as its authoritative stored coordinate.

## 3. Beat ordinal

A grid may assign a signed beat ordinal:

```c
typedef int64_t apta_beat_ordinal_t;
```

The ordinal provides continuity between adjacent segments and generations. Ordinal zero is an arbitrary grid anchor and MUST NOT be interpreted as a downbeat.

When a stable grid is extended outside its existing coverage, ordinals for previously published stable beats MUST remain unchanged unless an explicit revision is accepted.

## 4. Constant-period segment

A region with sufficiently constant beat period may use a compact segment representation.

```c
typedef struct {
    uint64_t whole_frames;
    uint32_t fraction_q32;
    uint32_t reserved;
} apta_frame_period_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t applicability_range;

    apta_fractional_frame_t anchor_position;
    apta_beat_ordinal_t anchor_ordinal;
    apta_frame_period_t frames_per_beat;

    uint32_t beat_count;
    apta_tempo_millibpm_t nominal_tempo_millibpm;

    apta_confidence_value_t confidence;
    uint8_t reserved8;
    uint16_t reserved16;

    apta_feature_state_t state;
    uint32_t flags;
    uint32_t segment_id;
    uint32_t revision;
} apta_grid_segment_t;
```

The mathematical position of beat ordinal `k` is:

```text
anchor_position + (k - anchor_ordinal) * frames_per_beat
```

Implementations MUST evaluate this expression with overflow-safe arithmetic and the rounding rules defined by the consuming operation.

`beat_count` is the number of authoritative beat positions asserted by the segment inside its declared applicability range. It MAY be zero for a provisional period hypothesis that has not yet published authoritative beats.

## 5. Segment applicability

`applicability_range` states where the segment model is asserted to apply.

The first generated beat MAY be before `applicability_range.first_frame` when the anchor exists only to preserve phase continuity. Only beats inside the declared applicability range are part of that segment's published coverage unless an accessor explicitly exposes additional context beats.

Adjacent stable segments MUST define an unambiguous boundary. They MUST NOT publish two conflicting authoritative beat positions for the same ordinal and source region.

## 6. Explicit beat representation

When a constant-period segment is inadequate, beats may be represented explicitly:

```c
typedef struct {
    apta_fractional_frame_t position;
    apta_beat_ordinal_t ordinal;

    apta_confidence_value_t confidence;
    uint8_t reserved8;
    uint16_t reserved16;

    uint32_t flags;
    uint32_t revision;
} apta_beat_t;
```

Explicit beats MUST be sorted by position and ordinal.

Two adjacent explicit beats MUST have different positions and consecutive ordinals unless a diagnostic explicitly records an omitted or uncertain beat.

## 7. Grid representation mode

A result declares one representation mode per grid scope:

```c
typedef uint32_t apta_grid_representation_t;

#define APTA_GRID_REPRESENTATION_NONE      0u
#define APTA_GRID_REPRESENTATION_SEGMENTS  1u
#define APTA_GRID_REPRESENTATION_EXPLICIT  2u
#define APTA_GRID_REPRESENTATION_HYBRID    3u
```

- `SEGMENTS`: segment expansion is authoritative.
- `EXPLICIT`: explicit beat positions are authoritative.
- `HYBRID`: segments are authoritative where declared, with explicit beats overriding or filling specifically identified ranges.

A producer MUST identify which representation is authoritative. A reader MUST NOT guess authority from whichever array is non-empty.

## 8. Grid flags

```c
#define APTA_GRID_FLAG_PROVISIONAL_PHASE    (1u << 0)
#define APTA_GRID_FLAG_DYNAMIC_TEMPO        (1u << 1)
#define APTA_GRID_FLAG_PHASE_AMBIGUITY       (1u << 2)
#define APTA_GRID_FLAG_HALF_TIME_AMBIGUITY   (1u << 3)
#define APTA_GRID_FLAG_DOUBLE_TIME_AMBIGUITY (1u << 4)
#define APTA_GRID_FLAG_USER_CONFIRMED        (1u << 5)
#define APTA_GRID_FLAG_USER_EDITED           (1u << 6)
#define APTA_GRID_FLAG_DEGRADED              (1u << 7)
```

Ambiguity MUST be represented explicitly. A low confidence value alone is not an adequate substitute for known half-time, double-time or phase alternatives.

## 9. Local beatgrid

A local beatgrid is supported by evidence near a focus or explicit requested region.

A local grid result MUST expose:

- requested range;
- evidence range;
- applicability range;
- authoritative representation;
- lifecycle state;
- confidence;
- ambiguity flags;
- segment or explicit-beat payload.

A local grid MAY become `STABLE` while track-wide grid coverage remains absent or provisional.

An implementation SHOULD publish a useful local grid without waiting for complete-track analysis when sufficient local evidence exists.

## 10. Global beatgrid

A global beatgrid attempts to provide continuous or explicitly gapped timing over a wider track scope.

Global coverage MUST expose every discontinuity. The producer MUST NOT bridge an unavailable PCM gap or rhythmically unsupported region with apparently authoritative beats unless the bridged range is clearly marked provisional or inferred.

A global grid MAY consist of:

- one constant-period segment;
- multiple tempo segments;
- an explicit beat sequence;
- a hybrid representation.

## 11. Dynamic tempo

Dynamic tempo exists when one constant beat period does not adequately represent the declared scope.

It may be represented by:

- multiple constant-period segments;
- explicit beats;
- a hybrid of segments and explicit beats.

Segment transitions SHOULD preserve phase continuity unless the analyser has evidence of an actual discontinuity.

A dynamic-tempo result MUST set `APTA_GRID_FLAG_DYNAMIC_TEMPO` and MUST NOT claim one constant global segment merely to simplify serialization.

## 12. Tempo consistency

For a constant-period segment, nominal tempo is mathematically related to source sample rate and period:

```text
BPM = 60 * source_sample_rate / frames_per_beat
```

`nominal_tempo_millibpm` is a convenience and validation value. The fractional period is authoritative for beat expansion.

The serialized nominal tempo and period MUST agree within the numerical tolerance defined by the active conformance profile. A reader SHOULD reject a required segment whose fields are grossly inconsistent.

## 13. Coverage, gaps and inference

Beatgrid coverage is represented as one or more disjoint source-frame ranges.

A gap may mean:

- PCM is unavailable;
- evidence is insufficient;
- the feature has not yet been processed;
- the producer intentionally declined to infer timing.

The reason SHOULD be available through request state or diagnostics.

Inferred beats MAY be published only when marked with an inference/provisional flag defined by the representation version. Inferred coverage MUST NOT be silently promoted to stable evidence-backed coverage.

## 14. Stable ranges and locking

A stable beatgrid range MUST NOT change silently.

The host may later lock a stable range for playback use. Locking preserves:

- beat positions;
- ordinals;
- segment identity;
- representation authority inside the locked range.

When later global evidence conflicts with a stable or locked range, the implementation publishes a pending revision rather than mutating the current authoritative grid.

The host acceptance API and serialized revision payload are specified separately from this baseline grid representation.

## 15. Revisions

A revision proposal identifies:

- affected range;
- affected segment and/or beat identifiers;
- replacement representation;
- old and proposed confidence;
- reason flags;
- revision identifier.

Rejecting a revision preserves the existing stable grid. Accepting a revision creates a new explicit lineage or revision generation. Previously acquired immutable results remain unchanged.

## 16. Precision and deterministic expansion

Reference segment expansion MUST specify a deterministic fixed-point arithmetic procedure.

At minimum:

- multiplication MUST not overflow silently;
- fractional carry MUST be preserved;
- conversion to an integer frame for display or indexing MUST state its rounding mode;
- repeated addition and direct ordinal multiplication MUST produce equivalent positions within the reference tolerance;
- a long segment MUST not accumulate unbounded phase error through repeated integer rounding.

Implementations SHOULD retain fractional position internally and round only at an API or display boundary that requires integer frames.

## 17. Serialization

Serialized beatgrid data MUST include:

- representation mode;
- source sample-rate context;
- exact coverage ranges;
- fractional positions and periods;
- ordinals;
- lifecycle state and confidence;
- ambiguity, provenance and dynamic-tempo flags;
- segment identifiers and revisions;
- explicit authority and override rules for hybrid data.

Native C structure layout MUST NOT be serialized directly.

## 18. Conformance fixtures

Beatgrid fixtures SHOULD include:

- constant click tracks;
- different starting phases;
- half-time and double-time ambiguity;
- swing and syncopation;
- long silence before the first beat;
- breakdowns;
- abrupt tempo changes;
- gradual tempo ramps;
- live drums;
- local analysis with incomplete global coverage;
- PCM gaps;
- long tracks that expose accumulated rounding error;
- 44.1 kHz and 48 kHz sources.

Semantic conformance permits algorithmic variation. Reference-backend conformance MAY define exact beat ordinals and bounded fractional-frame tolerances for named fixtures.
