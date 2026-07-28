# Waveform model

**Status:** APTA Working Draft 0.1

## 1. Scope

This document defines portable progressive waveform results for APTA Core 0.1.

The waveform model covers:

- a low-resolution track overview;
- higher-resolution detail tiles;
- explicit source-frame coverage and gaps;
- progressive publication near playback focus;
- normalized peak and energy values;
- optional three-band energy values.

The model does not define user-interface colours, pixel geometry or zoom behaviour.

## 2. Source relationship

Every waveform column represents one half-open source-frame range:

```text
[first_frame, end_frame)
```

A column MUST NOT claim coverage for PCM that was not available to the analyser.

A waveform result with missing PCM MUST represent the missing area as a gap. It MUST NOT interpolate across the gap and report the interpolated area as analysed coverage.

## 3. Analysis signal

Waveform values are computed from a normalized signed analysis signal `x` in the mathematical range `[-1, +1]`.

For APTA Core 0.1 reference-waveform conformance:

- mono input uses the mono sample directly;
- stereo input uses `(left + right) / 2` for the analysis signal;
- arithmetic MUST use sufficient intermediate precision to avoid overflow before division;
- input values outside the nominal range are clipped before persistent waveform quantization;
- NaN and infinity follow the PCM diagnostic rules in `pcm-input.md`.

Implementations MAY support additional channel layouts. A producer claiming reference-waveform conformance for more than two channels MUST document and identify the normative channel-reduction rule used. Otherwise the result is semantically conforming but not reference-waveform bit-exact.

## 4. Column geometry

A waveform level has a constant nominal number of source frames per column:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t level_id;
    uint32_t frames_per_column;

    apta_source_frame_t origin_frame;
    uint32_t flags;
    uint32_t reserved32[3];
} apta_waveform_level_info_t;
```

Column `i` nominally represents:

```text
[origin_frame + i * frames_per_column,
 origin_frame + (i + 1) * frames_per_column)
```

The final column of a known source MAY contain fewer than `frames_per_column` frames. Its exact exclusive end MUST be exposed by the containing range or tile descriptor.

`frames_per_column` MUST be greater than zero and MUST remain constant for one `level_id` within one result lineage.

## 5. Normalized column payload

The baseline portable column is:

```c
typedef struct {
    int16_t minimum;
    int16_t maximum;
    uint16_t rms;

    uint8_t low;
    uint8_t mid;
    uint8_t high;
    uint8_t flags;
} apta_waveform_column_t;
```

### 5.1. Minimum and maximum

`minimum` and `maximum` are the minimum and maximum normalized analysis-signal samples observed in the represented source-frame range.

Quantization is:

```text
q(x) = -32768                              when x <= -1
q(x) =  32767                              when x >= +1
q(x) = round_to_nearest_ties_to_even(x * 32767) otherwise
```

The special lower endpoint preserves representation of full-scale negative input.

A valid non-empty column MUST satisfy:

```text
minimum <= maximum
```

### 5.2. RMS

`rms` represents linear root-mean-square energy of the normalized analysis signal:

```text
r = sqrt(sum(x[n]^2) / N)
rms = round_to_nearest_ties_to_even(clamp(r, 0, 1) * 65535)
```

`N` is the number of source frames actually contributing to the column.

RMS is linear, not decibel encoded.

### 5.3. Three-band values

`low`, `mid` and `high` are optional normalized energy descriptors in the range `0..255`.

When `APTA_WAVEFORM_COLUMN_HAS_3BAND` is not set, all three values MUST be zero and MUST be ignored by readers.

APTA Core 0.1 defines the meaning as relative non-negative energy estimates for low-, mid- and high-frequency content over the same column range. It does not yet mandate one bit-exact filterbank. Producers MUST identify their filterbank or backend provenance when publishing three-band data.

A future reference-filterbank profile may define exact crossover frequencies, filtering, windowing and quantization without changing the baseline column layout.

## 6. Column flags

```c
#define APTA_WAVEFORM_COLUMN_VALID       (1u << 0)
#define APTA_WAVEFORM_COLUMN_PROVISIONAL (1u << 1)
#define APTA_WAVEFORM_COLUMN_CLIPPED     (1u << 2)
#define APTA_WAVEFORM_COLUMN_HAS_3BAND   (1u << 3)
#define APTA_WAVEFORM_COLUMN_DEGRADED    (1u << 4)
```

Rules:

- `VALID` means the column contains analysed PCM data.
- `PROVISIONAL` means the column may be recomputed under the declared lifecycle rules.
- `CLIPPED` means at least one contributing sample exceeded the nominal input range before clipping or was at integer full scale.
- `HAS_3BAND` makes the three band fields meaningful.
- `DEGRADED` means the producer used a documented reduced-quality path.
- bits not defined by the active specification version MUST be zero when written and ignored when read unless marked required by a future container extension.

A gap is represented by absent coverage, not by a fabricated zero-valued valid column.

## 7. Overview waveform

The overview waveform is intended to provide bounded memory use for full-track navigation.

One overview descriptor contains:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_waveform_level_info_t level;
    uint32_t column_count;
    uint32_t coverage_range_count;

    const apta_waveform_column_t *columns;
    const apta_frame_range_t *coverage_ranges;
} apta_waveform_overview_view_t;
```

The actual public ABI MAY expose equivalent information through accessors rather than embedding pointers in a permanently stable structure.

Coverage ranges MUST identify every analysed source region. Multiple disjoint ranges are permitted.

An overview MAY be published before track duration is known. Later generations MAY append columns and coverage but MUST follow lifecycle and stable-revision rules for previously published stable columns.

## 8. Detail tiles

A detail waveform is divided into independently publishable tiles:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t level_id;
    uint32_t tile_index;

    apta_frame_range_t source_range;
    uint32_t first_column_index;
    uint32_t column_count;

    const apta_waveform_column_t *columns;
    uint32_t flags;
    uint32_t reserved32[3];
} apta_waveform_tile_view_t;
```

Tile identity is the tuple:

```text
(level_id, tile_index)
```

Within one result lineage, a tile identity MUST retain the same nominal geometry. A producer that changes tile geometry MUST allocate a new level identifier or result lineage.

Tiles MAY be produced out of track order. This enables local waveform publication around playback focus before background regions are analysed.

## 9. Progressive behaviour

When waveform data is requested around playback focus, an APTA Performance Profile implementation:

- MUST prioritise missing local detail work over unrelated background detail work at lower priority;
- SHOULD publish usable local columns without waiting for the complete overview;
- MUST preserve explicit coverage when local and background regions are disjoint;
- MUST NOT discard a stable published tile merely because focus moved;
- MAY evict internal unpublished work or non-persistent detail cache under memory pressure.

A low-memory implementation MAY retain a bounded number of detail tiles while retaining a complete or progressively growing overview.

## 10. Aggregation and deterministic boundaries

Column boundaries are determined only by `origin_frame`, `frames_per_column` and the source end. They MUST NOT depend on processing block boundaries.

Supplying identical PCM as different block sizes or in a different non-conflicting order MUST produce identical reference-waveform columns for the same level geometry.

Implementations MUST carry sufficient aggregation state across PCM blocks so a column split across blocks is equivalent to one contiguous submission.

## 11. Lifecycle and revisions

Waveform state is attached to explicit coverage.

A later generation MAY:

- add new coverage;
- fill a former gap;
- replace provisional columns with refined columns;
- add optional three-band data;
- mark a completed range stable or final.

A later generation MUST NOT silently change stable column values for the same level and source coverage. Such a change requires an explicit revision event or a new source/configuration lineage.

## 12. Conformance

Waveform conformance has two levels:

1. **semantic waveform conformance** — correct ranges, units, flags, lifecycle and serialization semantics;
2. **reference waveform conformance** — additionally produces the specified mono/stereo reduction, peak quantization, RMS quantization and deterministic column boundaries within exact golden-vector expectations.

Three-band values are excluded from bit-exact reference conformance until a reference-filterbank profile is standardised.

Conformance fixtures MUST include:

- silence;
- positive and negative full scale;
- impulses at column boundaries;
- stereo cancellation cases;
- clipped floating-point input;
- columns split across PCM blocks;
- out-of-order non-overlapping blocks;
- gaps;
- partial final columns;
- 44.1 kHz and 48 kHz sources.
