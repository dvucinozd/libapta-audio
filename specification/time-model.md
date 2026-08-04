# Source time model

**Status:** Final APTA 1.0

## 1. Authoritative timeline

Every externally visible analysis position MUST be represented on the decoded source-frame timeline.

Milliseconds, seconds, samples after resampling and display pixels are derived representations and MUST NOT replace the stored authoritative source-frame position.

## 2. Source-frame type

The public API uses an unsigned 64-bit source-frame index:

```c
typedef uint64_t apta_source_frame_t;
```

The first frame of a track has index `0`.

The constant `UINT64_MAX` is reserved for `APTA_TOTAL_FRAMES_UNKNOWN` when used in a total-frame-count field. It MUST NOT be used as a valid frame index.

## 3. Half-open ranges

All APTA source-frame ranges use the form:

```text
[first_frame, end_frame)
```

A public range structure is represented as:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    apta_source_frame_t first_frame;
    apta_source_frame_t end_frame;
} apta_frame_range_t;
```

For a non-empty valid range:

```text
first_frame < end_frame
```

An empty range has:

```text
first_frame == end_frame
```

A range with `first_frame > end_frame` is invalid.

Using an exclusive end removes ambiguity from adjacent ranges and makes the frame count equal to:

```text
frame_count = end_frame - first_frame
```

## 4. Sample rate

The source sample rate is an unsigned integer number of frames per second.

A conforming core implementation MUST support 44,100 Hz and 48,000 Hz for profiles that accept general music input. Additional rates MAY be supported.

The source sample rate is immutable for the lifetime of a session.

## 5. Fractional frame positions

Beat phase and derived timing may require sub-frame precision. APTA uses a split fixed-point representation rather than a 64-bit Q32 value whose integer component would be limited to 32 bits.

```c
typedef struct {
    uint64_t whole_frame;
    uint32_t fraction_q32;
    uint32_t reserved;
} apta_fractional_frame_t;
```

`fraction_q32` represents a fraction in the range `[0, 1)` using `fraction_q32 / 2^32`.

The value represented is:

```text
whole_frame + fraction_q32 / 2^32
```

Serialized fractional values MUST use the same mathematical interpretation regardless of implementation arithmetic.

## 6. Conversion to time units

Conversion to seconds is mathematically:

```text
seconds = source_frame / sample_rate
```

Integer conversions MUST specify rounding. Unless another document overrides the rule:

- converting a frame boundary to an earlier-or-equal timestamp uses floor;
- converting an exclusive end boundary to a containing display interval uses ceiling;
- nearest conversion uses round-to-nearest, ties-to-even.

Implementations MUST use overflow-safe arithmetic. Multiplication SHOULD be performed using a sufficiently wide intermediate type or an explicitly checked quotient/remainder algorithm.

## 7. Ordering and discontinuities

PCM blocks and analysis requests MAY refer to non-contiguous and out-of-order ranges. Their source-frame indices still belong to one logical timeline.

A gap in supplied PCM does not renumber later frames. For example, supplying `[0, 4096)` followed by `[8192, 12288)` leaves `[4096, 8192)` unavailable.

## 8. Unknown duration

A session MAY begin with an unknown total frame count. The host MUST later signal the final exclusive end frame when end of input is known.

After final end-of-input signalling, no accepted PCM block may extend beyond the declared final end frame.

## 9. Comparison and equality

Source-frame positions are equal only when their frame indices and source sample-rate context refer to the same logical source timeline.

Applications MUST NOT compare positions from unrelated tracks solely by numeric frame index.
