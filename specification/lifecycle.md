# Analysis lifecycle

**Status:** APTA Working Draft 0.1

## 1. Separate lifecycle domains

APTA defines separate state machines for:

- the analysis session;
- each analysis feature;
- explicit region requests.

A single aggregate state MUST NOT hide a failure in one optional feature or incorrectly imply that all feature data has equal stability.

## 2. Session state

```c
typedef uint32_t apta_session_state_t;

#define APTA_SESSION_CREATED    0u
#define APTA_SESSION_ACTIVE     1u
#define APTA_SESSION_DRAINING   2u
#define APTA_SESSION_COMPLETED  3u
#define APTA_SESSION_CANCELLED  4u
#define APTA_SESSION_FAILED     5u
```

### CREATED

The session exists but has not accepted PCM and has not begun processing.

### ACTIVE

The session may accept PCM, satisfy requests and publish result generations.

### DRAINING

End of input is known. The session is completing remaining runnable work under host-provided budgets.

### COMPLETED

Every mandatory requested scope that can be completed from available input is final. Optional unsupported or explicitly cancelled requests do not prevent completion.

### CANCELLED

The host requested cancellation. Already acquired immutable results remain valid until released.

### FAILED

A fatal session-level error prevents further processing. Feature-local errors SHOULD NOT force this state when independent work can continue.

## 3. Session transitions

Allowed transitions are:

```text
CREATED -> ACTIVE
CREATED -> CANCELLED
CREATED -> FAILED

ACTIVE -> DRAINING
ACTIVE -> CANCELLED
ACTIVE -> FAILED

DRAINING -> COMPLETED
DRAINING -> CANCELLED
DRAINING -> FAILED
```

`COMPLETED`, `CANCELLED` and `FAILED` are terminal session states.

Destroying a session is an ownership operation and is not a lifecycle state transition.

## 4. Feature state

Each feature and relevant coverage region uses:

```c
typedef uint32_t apta_feature_state_t;

#define APTA_FEATURE_ABSENT       0u
#define APTA_FEATURE_PARTIAL      1u
#define APTA_FEATURE_PROVISIONAL  2u
#define APTA_FEATURE_STABLE       3u
#define APTA_FEATURE_FINAL        4u
#define APTA_FEATURE_FAILED       5u
```

### ABSENT

No usable payload exists for the feature and range.

### PARTIAL

Usable payload exists for only a subset of requested coverage or required elements.

### PROVISIONAL

The payload is usable but may change when additional evidence is processed.

### STABLE

The payload is stable for its declared coverage. It may be extended outside that coverage. A conflicting change inside stable coverage requires an explicit revision event.

### FINAL

The requested feature scope is complete under the current source, configuration and end-of-input state.

### FAILED

The feature cannot be produced for the declared scope. The failure record MUST expose an error or diagnostic reason.

## 5. Feature-state monotonicity

For unchanged coverage and unchanged source identity, the normal progression is:

```text
ABSENT -> PARTIAL -> PROVISIONAL -> STABLE -> FINAL
```

Implementations MAY skip states.

A feature MUST NOT silently move from `STABLE` or `FINAL` to a less stable state for the same coverage. A source revision, configuration replacement or accepted pending revision creates an explicit new lineage and generation event.

`FAILED` may be terminal for one request or range without making the whole feature globally failed.

## 6. Coverage and state

State is meaningful only with explicit coverage.

For example, a local beatgrid may be `STABLE` for `[100000, 500000)` while the global beatgrid remains `PARTIAL` for the track.

An implementation MUST NOT label an entire feature `STABLE` merely because one local region is stable.

## 7. Finality

A feature may become `FINAL` only when:

- the requested scope is precisely known;
- required source input for that scope is known to be complete or explicitly unavailable as a recorded gap;
- all required processing for the scope has completed;
- no pending revision applies to the same scope.

Silence from a push source is not evidence of end of input.

## 8. Cancellation

Cancellation prevents new discretionary processing. It does not retroactively invalidate already published immutable results.

A cancelled explicit request has its own request status. Shared work still required by another active request MAY continue.

## 9. Failure scope

Errors are classified as:

- operation-local;
- request-local;
- feature-range-local;
- feature-global;
- session-fatal.

The implementation SHOULD choose the narrowest correct scope. For example, unsupported phrase analysis must not prevent waveform analysis.

## 10. Publication

A new result generation MUST identify every feature or range whose state changed materially.

The aggregate snapshot MAY expose a convenience summary state, but applications MUST be able to inspect per-feature state and coverage.
