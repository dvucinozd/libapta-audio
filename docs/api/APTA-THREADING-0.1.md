# APTA threading and destruction contract 0.1

**Status:** Draft implementation contract  
**Applies to:** `libapta` public API 0.1 prototype

## 1. Host serialization model

One analysis session has one processing owner.

The host MUST serialize these mutating operations for the same session:

- `apta_session_set_source()`;
- `apta_session_push_pcm()`;
- `apta_session_signal_end_of_input()`;
- `apta_session_set_focus()`;
- `apta_session_request_region()`;
- `apta_session_cancel_region_request()`;
- `apta_session_next_pcm_request()`;
- `apta_session_lock_grid_range()`;
- `apta_session_process()`;
- `apta_session_destroy()`.

The current prototype does not internally serialize all combinations of those calls. Calling them concurrently on one session is outside the contract unless a later document explicitly permits the combination.

`apta_session_process()` detects another concurrent processing call and returns `APTA_ERROR_BUSY`, but this detection is not a substitute for host serialization of all mutating operations.

## 2. Thread-safe cancellation

`apta_session_request_cancel()` MAY be called from another thread while `apta_session_process()` is running.

`apta_session_is_cancel_requested()` MAY be called concurrently with cancellation or processing.

Cancellation is cooperative. The processing owner observes the atomic request at a bounded API boundary and publishes the cancelled lifecycle state.

## 3. Immutable result readers

The following operations MAY run concurrently with session processing and result publication:

- `apta_session_acquire_result()`;
- `apta_result_get_info()`;
- `apta_result_get_generation()`;
- `apta_result_get_available_features()`;
- all feature accessors on an acquired result, including tempo and beatgrid accessors;
- `apta_result_release()`.

Each acquired result generation is immutable. Pointers returned through a result view remain valid until the corresponding acquired result is released.

A reader MUST NOT retain a view pointer after releasing its result.

Multiple threads MAY independently acquire, inspect and release the same generation.

## 4. Publication ordering

A successful result publication atomically replaces the session's current-result pointer under an internal lock.

A reader observes either:

- the complete previous generation; or
- the complete new generation.

A reader MUST NOT observe a partially initialized generation.

Generation identifiers increase monotonically within one session lineage. Different reader threads are not guaranteed to observe every intermediate generation.

Focus movement, Stage S4 tempo/grid refresh and grid-range locking may each publish a new immutable generation.

## 5. Session destruction

`apta_session_destroy()` MUST NOT run concurrently with any other operation that receives the same session pointer, including `apta_session_acquire_result()`.

The host must first:

1. stop new session API calls;
2. join or otherwise quiesce session reader/processing threads;
3. call `apta_session_destroy()` exactly once.

Destroying a session releases the session's own reference to its current result. Independently acquired results remain valid and may outlive the session.

## 6. Context destruction

`apta_context_destroy()` MUST NOT run concurrently with context or session operations.

The call returns `APTA_ERROR_BUSY` while any session exists, any acquired result remains alive, or tracked context allocation remains outstanding.

The host must destroy all sessions and release all results before destroying the context.

## 7. Callback requirements

A custom allocator used by a context MUST be safe for every thread from which the host calls APTA APIs that may allocate or release results.

A logger callback and monotonic-clock callback MUST follow the reentrancy and thread-safety requirements of the host's calling pattern.

PCM pull callbacks are invoked synchronously by the host-serialized `apta_session_process()` owner. They MUST follow the PCM pull contract, MUST NOT recursively call a mutating API on the same session and MUST NOT destroy the invoking session or context.

Callbacks MUST NOT recursively destroy the context or active session that invoked them.

## 8. Memory visibility

The reference implementation uses C11 atomic reference counts, atomic cancellation state and acquire/release synchronization for current-result publication.

This implementation detail supports the contract but is not itself the portable API guarantee. Consumers rely on the observable rules in this document, not on internal atomic types.

## 9. Current non-guarantees

The 0.1 prototype does not guarantee:

- lock-free result acquisition;
- hard real-time behavior;
- concurrent PCM producers;
- concurrent focus/request/grid-lock mutation with processing;
- destruction racing with any other call;
- callback execution on a particular thread;
- fairness between arbitrary host threads.

## 10. Conformance tests

Threading tests include:

- cancellation requested from another thread;
- multiple concurrent immutable-result readers;
- result publication while readers hold older generations;
- generation monotonicity per reader;
- result lifetime beyond session destruction;
- focus-driven Stage S4 publication while older results remain immutable.

Race-detector and sanitizer jobs remain required before stable API status.
