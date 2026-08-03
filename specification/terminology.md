# APTA terminology

**Status:** APTA 1.0 Release Candidate Draft

## Analysis session

A stateful analysis instance associated with one immutable source PCM format and one logical audio track.

## Analysis feature

A requested output category such as waveform overview, waveform detail, BPM, local beatgrid or global beatgrid.

## Source frame

One simultaneous sample position across every source channel. A stereo frame contains one left-channel sample and one right-channel sample.

## Source-frame index

A zero-based integer identifying a source frame. Source-frame index zero is the first PCM frame of the logical track.

## Source-frame range

A half-open interval `[first_frame, end_frame)`. It includes `first_frame` and excludes `end_frame`.

## Source format

The sample rate, channel count, sample representation and channel interpretation assigned to an analysis session. The source format is immutable after the session accepts its first PCM block.

## PCM block

A contiguous group of decoded PCM source frames supplied to or borrowed by the library.

## Push input

An input mode in which the host application supplies PCM blocks to the session.

## Pull input

An input mode in which the library requests source-frame ranges through host callbacks.

## Focus

A transient host-provided indication of the playback position and near-future region most relevant to user interaction. Focus does not transfer playback ownership to the library.

## Region request

An explicit request to analyse one source-frame range for a specified feature set, priority and optional soft deadline.

## Priority

An unsigned scheduling value in which a larger value represents stronger preference. Priority orders work but does not itself create a real-time guarantee.

## Soft deadline

A monotonic time by which a result is desired. The implementation attempts to satisfy it but may miss it because of unavailable PCM, insufficient work budget or computational limits.

## Coverage

The exact source-frame range for which a particular result payload is valid.

## Gap

A source-frame range for which required PCM or requested analysis output is not available.

## Provisional result

A usable result that may be revised as additional evidence becomes available.

## Stable result

A result whose published stable region will not change silently. A conflicting improvement requires an explicit revision record or a new unlocked region.

## Final result

A result for which the requested analysis scope is complete and no additional input is expected to improve that scope under the current configuration.

## Result generation

An immutable published snapshot of analysis data identified by a monotonically increasing generation number within a session.

## Locked range

A source-frame range whose stable beatgrid or other lockable result cannot be silently changed.

## Pending revision

A proposed replacement for data that conflicts with a locked or previously stable range.

## Work budget

Host-provided limits controlling how much cooperative processing one call may perform.

## Backpressure

A condition in which the session cannot accept more PCM or work without the host first allowing processing or releasing resources.

## Capability

A feature or behavioural property that an implementation can expose using the normative APTA data model.

## Profile

A named set of mandatory capabilities and behavioural requirements used for conformance claims.

## Reference implementation

The portable `libapta` implementation maintained with the APTA specification. Its algorithms are informative unless a document explicitly defines reference-algorithm conformance.
