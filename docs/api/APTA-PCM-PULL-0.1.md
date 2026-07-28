# APTA PCM pull input contract 0.1

**Status:** Implemented prototype contract  
**Applies to:** `apta_pcm_source_t` and pull-mode sessions

## Activation

A session uses pull input when `apta_session_config_t.input_mode` is `APTA_INPUT_MODE_PULL`.

The host attaches exactly one source while the session is still in `APTA_SESSION_CREATED` state:

```c
apta_pcm_source_t source;
apta_pcm_source_init(&source);
source.user_data = host_source;
source.read_frames = read_frames;
source.release_frames = release_frames;
source.get_total_frames = get_total_frames; /* optional */

apta_session_set_source(session, &source);
```

`read_frames` and `release_frames` are required. `get_total_frames` is optional.

If both the session configuration and `get_total_frames()` provide a known source length, the values must match. A mismatch returns `APTA_ERROR_CONFLICT`.

## Processing ownership

The host remains responsible for the decoder, filesystem, USB medium, network object or other source implementation. `libapta` only requests source-frame ranges.

`apta_session_process()` performs at most one `read_frames()` callback per call. The requested frame count is limited by:

- the scheduler's current missing range;
- the internal maximum PCM block size; and
- nonzero `apta_work_budget_t.maximum_input_frames`.

The callback must be nonblocking or cooperatively bounded. When data is temporarily unavailable it returns `APTA_STATUS_WOULD_BLOCK` rather than waiting indefinitely.

## `read_frames()` results

The callback may return:

- `APTA_STATUS_OK` with one PCM block;
- `APTA_STATUS_WOULD_BLOCK` with no block ownership transfer;
- `APTA_STATUS_END_OF_INPUT` when the requested first frame is the true source end.

Any other result is treated as a source failure.

A successful block must:

- have a valid initialized `apta_pcm_block_t` prefix;
- begin exactly at the requested source frame;
- contain at least one frame;
- contain no more than the requested frame count;
- use the sample format and channel layout configured for the session;
- remain valid until `release_frames()` is called.

Malformed callback output causes `APTA_ERROR_SOURCE` and a failed session generation.

## Block lifetime

After `APTA_STATUS_OK`, the core validates and copies accepted PCM into library-owned queue storage. It then calls `release_frames()` exactly once, including when validation or queue acceptance fails.

The callback-owned PCM pointer is never retained after `release_frames()` returns.

`release_frames()` is not called for `WOULD_BLOCK`, `END_OF_INPUT` or callback error results.

## End of input

For a known-length source, the core automatically enters draining once every required source range has been accepted.

For an unknown-length source, the callback returns `APTA_STATUS_END_OF_INPUT` at the first frame beyond the final successfully returned block. The core records that frame as the final source length and completes final partial waveform columns.

An empty known source completes without invoking `read_frames()`.

## Push/pull exclusion

A pull session rejects:

- `apta_session_push_pcm()`; and
- public `apta_session_signal_end_of_input()`.

A push session rejects `apta_session_set_source()`.

Applications may still call `apta_session_next_pcm_request()` for diagnostics, but the pull adapter uses the same internal scheduler automatically.

## Threading and reentrancy

The existing single-processing-owner rule applies. The host serializes source attachment, processing and every other mutating session call.

Source callbacks run synchronously on the thread calling `apta_session_process()`. They must not recursively call mutating APIs or destroy the invoking session or context.

Cross-thread cancellation remains cooperative. A cancellation already visible at process entry is handled before a source callback is invoked.

## Bounded-memory mode

Pull input supports caller-owned static session workspaces and `APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS`.

For a successfully created known-duration bounded session, pull processing uses the existing workspace PCM queue and fixed immutable result pool. Source reads, PCM copying, processing, WOVR/WDTL publication and result lifetime require no additional context allocator callback.
