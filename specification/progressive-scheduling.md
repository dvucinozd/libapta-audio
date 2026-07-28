# Progressive and adaptive scheduling model

**Status:** APTA Working Draft 0.1

## 1. Purpose

Progressive processing means useful results can be published before full-track completion. Adaptive processing additionally means the host can direct analysis toward the region and features currently needed for playback or user interaction.

An implementation claiming APTA Performance Profile conformance MUST implement the scheduling behaviour in this document.

## 2. Scheduling ownership

The host owns operating-system scheduling, playback and I/O.

The APTA session owns ordering of its internal analysis work subject to:

- host feature requests;
- region priorities;
- available PCM;
- work budgets;
- memory limits;
- result stability rules.

The portable core MUST NOT create a mandatory thread.

## 3. Feature mask

Feature requests use a fixed-width mask:

```c
typedef uint64_t apta_feature_mask_t;

#define APTA_FEATURE_WAVEFORM_OVERVIEW  (1ULL << 0)
#define APTA_FEATURE_WAVEFORM_DETAIL    (1ULL << 1)
#define APTA_FEATURE_WAVEFORM_3BAND     (1ULL << 2)
#define APTA_FEATURE_BPM                (1ULL << 3)
#define APTA_FEATURE_LOCAL_BEATGRID     (1ULL << 4)
#define APTA_FEATURE_GLOBAL_BEATGRID    (1ULL << 5)
#define APTA_FEATURE_DYNAMIC_TEMPO      (1ULL << 6)
#define APTA_FEATURE_CONFIDENCE         (1ULL << 7)
#define APTA_FEATURE_GRID_LOCKING       (1ULL << 8)
```

Key, downbeat and phrase bits are reserved for future normative extensions and are not core capabilities in version 0.1.

## 4. Priority values

Priority is an unsigned value from `0` through `255`; larger values are processed first when dependencies permit.

Recommended classes are:

```c
#define APTA_PRIORITY_BACKGROUND          32u
#define APTA_PRIORITY_NORMAL              96u
#define APTA_PRIORITY_INTERACTIVE        192u
#define APTA_PRIORITY_PLAYBACK_CRITICAL  240u
```

Values are ordering hints, not hard real-time guarantees.

An implementation MUST preserve relative ordering between simultaneously runnable requests of different priority. It SHOULD provide starvation prevention for lower-priority work.

## 5. Playback focus

The host communicates the current playback context using:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_source_frame_t playhead_frame;
    uint64_t lookbehind_frames;
    uint64_t lookahead_frames;

    apta_feature_mask_t feature_mask;
    uint8_t priority;
    uint8_t flags;
    uint16_t reserved16;
    uint32_t reserved32[2];
} apta_focus_t;

apta_status_t apta_session_set_focus(
    apta_session_t *session,
    const apta_focus_t *focus);
```

Focus represents the range:

```text
[max(0, playhead - lookbehind), playhead + lookahead)
```

with checked saturation at the known source end.

Setting focus replaces the previous focus state. It does not cancel explicit region requests.

The host MAY update focus frequently. The call MUST be bounded and MUST NOT run full analysis synchronously.

## 6. Explicit region requests

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t range;
    apta_feature_mask_t feature_mask;

    uint64_t soft_deadline_monotonic_ns;
    uint32_t request_id;

    uint8_t priority;
    uint8_t flags;
    uint16_t reserved16;
    uint32_t reserved32[2];
} apta_region_request_t;
```

Creation and cancellation use:

```c
apta_status_t apta_session_request_region(
    apta_session_t *session,
    const apta_region_request_t *request,
    uint32_t *request_id_out);

apta_status_t apta_session_cancel_region_request(
    apta_session_t *session,
    uint32_t request_id);
```

A caller-supplied `request_id` of zero asks the library to allocate a non-zero identifier. Non-zero caller identifiers MAY be supported when uniqueness is guaranteed.

Cancellation stops future work that exists only for that request. It MUST NOT remove already published results or work still required by another request.

## 7. Request lifecycle

An explicit request moves through these logical states:

```text
QUEUED -> WAITING_FOR_PCM -> RUNNABLE -> PARTIALLY_SATISFIED -> SATISFIED
   |              |            |                 |
   +--------------+------------+-----------------+-> CANCELLED
                                      |
                                      +-> FAILED
```

A request may move repeatedly between `WAITING_FOR_PCM` and `RUNNABLE` as new regions become available.

The public API SHOULD expose request progress and terminal status without requiring mutation of an immutable result snapshot.

## 8. Soft deadlines

`soft_deadline_monotonic_ns` is zero when no deadline is specified.

A non-zero deadline is expressed in the same monotonic clock domain supplied through the context configuration.

The implementation:

- MUST NOT interpret a soft deadline as wall-clock UTC time;
- SHOULD prioritise work whose deadline is nearer when priorities are equal;
- MUST continue safely after a missed deadline;
- SHOULD expose a missed-deadline diagnostic;
- MUST NOT claim a hard real-time guarantee solely because the field exists.

## 9. Cooperative work budget

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t maximum_input_frames;
    uint32_t maximum_steps;
    uint32_t soft_time_budget_us;
    uint32_t flags;
} apta_work_budget_t;
```

Rules:

- `maximum_input_frames` is a hard limit on newly consumed PCM frames during the call when non-zero.
- `maximum_steps` is a hard implementation-defined unit limit when non-zero. The implementation MUST document the maximum granularity of one step.
- `soft_time_budget_us` is a best-effort elapsed-time target, not a hard upper bound.
- A zero field means that field does not independently limit the call.
- At least one limiting field SHOULD be non-zero in latency-sensitive applications.

Processing uses:

```c
apta_status_t apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out);
```

The function MUST return after a bounded amount of internal work. A backend MUST provide interruption points sufficiently fine for the profile it claims.

## 10. Work-selection order

Subject to dependency availability, a conforming scheduler SHOULD select work in this order:

1. playback-critical focus work with an imminent deadline;
2. higher-priority explicit region requests;
3. interactive focus work;
4. normal requested track coverage;
5. background global refinement and cache completion.

Within one priority class, the scheduler SHOULD prefer:

- missing data before refinement;
- regions nearest the playhead;
- requests closest to their soft deadline;
- older requests to prevent starvation.

The exact algorithm is implementation-defined, but observable behaviour MUST preserve priority and bounded-processing requirements.

## 11. Dependency scheduling

A requested feature may require prerequisite work. For example:

- local beatgrid requires local onset information;
- BPM may require an onset envelope over a wider range;
- waveform detail requires PCM for the requested tile;
- global beatgrid may depend on multiple local segments.

The scheduler MAY enqueue prerequisites automatically. Automatically created work inherits the strongest priority of the request that currently depends on it.

## 12. PCM demand

In pull mode, the scheduler requests missing source ranges through the pull source.

In push mode, the scheduler exposes its next useful PCM demand through `apta_session_next_pcm_request()` as defined by `pcm-input.md`.

The scheduler SHOULD coalesce adjacent PCM demands when doing so does not delay higher-priority work or exceed configured memory limits.

## 13. Progressive publication

The session SHOULD publish a new immutable result generation when one of these events occurs:

- requested coverage becomes newly usable;
- a provisional value becomes stable;
- confidence crosses a documented semantic threshold;
- a requested region becomes satisfied;
- a pending revision is created;
- the overall lifecycle changes;
- a meaningful failure or diagnostic state changes.

The implementation MAY coalesce publication events to respect memory and scheduling budgets, but MUST NOT indefinitely hide completed playback-critical results.

## 14. Focus movement and retained results

Moving focus does not invalidate already analysed data.

The implementation MAY evict internal temporary state for old regions under memory pressure. Published persistent result data MUST follow the snapshot and serialization retention rules.

A low-memory profile MAY retain only bounded detail tiles while preserving an overview waveform and explicit coverage metadata.

## 15. Failure and degradation

If a request cannot be completed because the capability is unsupported, the implementation MUST report `APTA_ERROR_UNSUPPORTED` for that request without failing unrelated supported work.

If memory limits prevent all requested features, the implementation SHOULD degrade according to documented profile policy, for example:

1. preserve playback-critical local output;
2. preserve overview waveform;
3. reduce background detail retention;
4. defer global refinement.

Silent production of a different feature set is not permitted; available features and request status MUST remain observable.
