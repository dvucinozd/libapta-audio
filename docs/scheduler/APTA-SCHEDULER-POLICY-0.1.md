# APTA scheduler policy 0.1

**Status:** implementation contract  
**Scope:** focus and region scheduling for waveform PCM demand and cooperative processing

## Ordering key

An active region request is ordered by the following deterministic key:

1. higher effective priority;
2. earliest non-zero `soft_deadline_monotonic_ns`;
3. lower enqueue serial (FIFO);
4. lower request ID as a final deterministic tie-break.

A zero deadline means that the request has no declared deadline. At equal effective priority, every non-zero deadline precedes a zero deadline.

## Effective priority and bounded aging

The stored public request priority is never mutated permanently.

For one scheduler decision, effective priority is:

```text
effective = min(255, base_priority + skipped_decisions * 8)
```

`skipped_decisions` is capped at 32. The selected active request resets its skip counter to zero. Every other active request increments its counter by one, up to the cap.

This provides deterministic starvation prevention without requiring a monotonic clock callback. The soft deadline remains an absolute monotonic timestamp supplied by the application, but comparing two declared deadlines does not require reading the current clock.

## Decision boundaries

A decision is recorded when:

- `apta_session_next_pcm_request()` successfully returns a PCM demand; or
- `apta_session_process()` actually consumes at least one PCM frame.

A validation-only internal replay check does not count as a new decision.

A cooperative process call records the request associated with the first selected PCM node. Aging therefore advances once per host-visible process call, not once per internal 256-frame work chunk.

## Focus interaction

Focus has its configured priority but no enqueue serial or deadline. A region request with a strictly higher effective priority wins. At equal effective priority, a region request wins through its deadline/FIFO key.

Repeated focus selections age waiting region requests, so a continuously active focus cannot permanently starve them.

## PCM queue ordering

Before the overview processing base is called, queued PCM nodes are stably ordered by the best active request or focus range that overlaps each node. The base analyser then continues to use its established priority selection logic.

The wrapper temporarily projects effective request priorities into the internal request copy used by the base call and restores every original priority before returning. Request states and diagnostics produced by the base call are preserved.

## Detail interaction

Detail-only and overview-plus-detail region requests use the same ordering key. The detail selector temporarily sorts a read-only request snapshot and restores the original request table before returning.

Feature-specific replay acceptance continues to validate against the base detail selector and does not advance aging a second time.

## Boundedness

The implementation adds fixed fields to each of the sixteen embedded request slots and performs no heap allocation for scheduling. Temporary sorting uses bounded stack arrays and the already bounded PCM linked list.
