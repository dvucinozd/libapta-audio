# PCM input contract

**Status:** APTA 1.0 Release Candidate Draft

## 1. Scope

This document defines decoded PCM input behaviour for APTA analysis sessions. Codec, file, USB and network access remain host responsibilities.

A session uses exactly one input mode: push or pull. Mixing both modes in one session is invalid unless a future extension explicitly permits it.

## 2. Immutable session format

The host configures the following source properties before PCM is accepted:

- sample rate;
- channel count;
- sample format;
- channel interpretation;
- known total frame count or `APTA_TOTAL_FRAMES_UNKNOWN`.

After the first PCM frame is accepted, these properties MUST remain unchanged for that session.

A host that encounters a true source-format change MUST terminate the session or create a new logical session.

## 3. Baseline sample formats

APTA 1.0 defines the following in-memory formats:

```c
typedef uint32_t apta_sample_format_t;

#define APTA_SAMPLE_S16_NATIVE_INTERLEAVED  1u
#define APTA_SAMPLE_S24_3LE_INTERLEAVED     2u
#define APTA_SAMPLE_S32_NATIVE_INTERLEAVED  3u
#define APTA_SAMPLE_F32_NATIVE_INTERLEAVED  4u
#define APTA_SAMPLE_F32_NATIVE_PLANAR       5u
```

Semantics:

- `S16_NATIVE_INTERLEAVED`: signed two's-complement 16-bit samples in host byte order.
- `S24_3LE_INTERLEAVED`: signed two's-complement 24-bit samples packed into exactly three little-endian octets.
- `S32_NATIVE_INTERLEAVED`: signed two's-complement 32-bit samples in host byte order.
- `F32_NATIVE_INTERLEAVED`: IEEE-754 binary32 samples in host byte order, nominally in `[-1.0, +1.0]`.
- `F32_NATIVE_PLANAR`: one contiguous IEEE-754 binary32 plane per channel.

Values outside `[-1.0, +1.0]` in a floating-point stream MAY be clamped for analysis. NaN and infinity MUST NOT propagate into persistent result data; an implementation MUST either replace them with zero and record a diagnostic or reject the affected block.

Core 0.1 PCM blocks are tightly packed. Arbitrary frame or plane stride is deferred to an extension.

## 4. PCM block structure

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    const void *data;
    const void *planes[8];

    apta_source_frame_t first_frame;
    uint32_t frame_count;

    uint32_t flags;
    uint32_t reserved32[3];
} apta_pcm_block_t;
```

Rules:

- Interleaved formats use `data`; every `planes[]` entry MUST be null.
- Planar formats set `data` to null and provide exactly `channel_count` non-null plane pointers.
- `frame_count` MUST be greater than zero for a data block.
- The represented range is `[first_frame, first_frame + frame_count)`.
- Addition of `first_frame` and `frame_count` MUST be checked for overflow.
- Reserved fields MUST be zero.

## 5. Ownership and lifetime

The host retains ownership of all PCM memory.

For push input, the library MUST finish reading or copy every accepted frame before `apta_session_push_pcm()` returns. The host MAY immediately reuse or release the corresponding memory after return.

For pull input, PCM memory returned by `read_frames` remains valid until the library calls `release_frames`. The library MUST call `release_frames` exactly once for each successfully borrowed non-empty block.

Callbacks MUST document whether they can block. A Performance Profile source callback SHOULD be nonblocking or predictably bounded.

## 6. Push API and partial acceptance

The push API is:

```c
apta_status_t apta_session_push_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);
```

`accepted_frames_out` is REQUIRED and MUST be set on every return after argument validation.

Possible outcomes:

| Status | Accepted frames | Meaning |
|---|---:|---|
| `APTA_STATUS_OK` | `frame_count` | Entire block accepted. |
| `APTA_STATUS_MORE_WORK` | `1..frame_count` | Frames accepted; cooperative processing is available or required. |
| `APTA_STATUS_WOULD_BLOCK` | `0` | No frame accepted because of backpressure. |
| negative error | `0` unless explicitly documented | Block rejected. |

When fewer than `frame_count` frames are accepted, accepted frames are always the prefix of the block. The caller MAY retry the unaccepted suffix with an adjusted `first_frame`, pointer and `frame_count`.

The library MUST NOT report `WOULD_BLOCK` after consuming frames.

## 7. Range order, gaps and overlap

Push PCM MAY be supplied out of source-frame order so the host can satisfy priority analysis requests.

Gaps are permitted. A gap remains unavailable until matching PCM is supplied or the session ends.

A block MAY overlap previously supplied PCM. If the overlapping PCM is byte-equivalent after format interpretation, the implementation MAY treat it as a duplicate. If it differs, the implementation MUST do one of the following:

- reject it as conflicting source data; or
- accept it only through an explicit source-revision mode that invalidates affected provisional results.

Core 0.1 defaults to rejecting conflicting overlap.

## 8. End of input

Push mode uses an explicit end-of-input call:

```c
apta_status_t apta_session_signal_end_of_input(
    apta_session_t *session,
    apta_source_frame_t final_end_frame);
```

`final_end_frame` is the exclusive source-frame end of the logical track.

After successful signalling:

- the declared total frame count becomes final;
- new PCM extending past `final_end_frame` MUST be rejected;
- repeated signalling with the same value MAY succeed idempotently;
- signalling a different value MUST fail;
- missing ranges before `final_end_frame` remain explicit gaps.

A result MUST NOT become `FINAL` solely because no PCM has recently arrived. Finality requires explicit end-of-input knowledge or a pull source that reports a stable total frame count and end condition.

## 9. Pull source contract

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    apta_status_t (*read_frames)(
        void *user_data,
        apta_source_frame_t first_frame,
        uint32_t requested_frames,
        apta_pcm_block_t *block_out);

    void (*release_frames)(
        void *user_data,
        apta_pcm_block_t *block);

    uint64_t (*get_total_frames)(void *user_data);
} apta_pcm_source_t;
```

`read_frames` rules:

- it MUST return a block beginning at `first_frame` unless returning `WOULD_BLOCK`, `END_OF_INPUT` or an error;
- it MAY return fewer than `requested_frames`;
- it MUST NOT return more than `requested_frames`;
- it MUST use the immutable session source format;
- `END_OF_INPUT` means no frame exists at `first_frame` or later;
- `WOULD_BLOCK` means data is temporarily unavailable and no borrowed block was returned.

The core MUST NOT assume that pull callbacks represent a filesystem. They may be backed by USB media, a network object, application cache or memory.

## 10. Push-mode PCM requests

A push-mode host SHOULD be able to ask which PCM the scheduler needs next:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    apta_frame_range_t range;
    uint64_t feature_mask;
    uint8_t priority;
    uint8_t flags;
    uint16_t reserved16;
    uint32_t request_token;
} apta_pcm_request_t;

apta_status_t apta_session_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out);
```

This call reports demand; it does not perform I/O and does not transfer source ownership.

## 11. Backpressure

Backpressure MAY result from:

- exhausted session memory;
- a full internal PCM queue;
- an unreleased result generation;
- a processing dependency that requires host CPU time;
- configured limits.

The host responds by invoking bounded processing, releasing snapshots, satisfying higher-priority PCM requests or reducing submitted work.

An implementation MUST NOT solve backpressure by unbounded allocation.
