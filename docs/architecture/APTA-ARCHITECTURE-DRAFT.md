# libapta-audio

## Open Standard and Portable C Library Architecture

**Status:** Initial architecture draft  
**Project name:** `libapta-audio`  
**Primary language:** ISO C11  
**Public ABI goal:** C-compatible, portable and versioned  
**Reference implementation:** `libapta`  
**Interchange format:** `.apta`  
**Primary domain:** Progressive audio-track analysis for DJ players, embedded devices, desktop applications and servers  

> This document preserves the original target architecture. Its repository
> tree, platform list and roadmap are proposals rather than a current
> implementation inventory. Section 30 records the license now adopted by the
> repository. See
> [`../status/APTA-ROADMAP-STATUS.md`](../status/APTA-ROADMAP-STATUS.md) for
> current status and [`../../specification/APTA-SPEC.md`](../../specification/APTA-SPEC.md)
> for normative precedence.

---

## 1. Purpose

`libapta-audio` is an open standard and a portable C library for progressive analysis of prerecorded audio tracks.

Its primary purpose is to let an application begin using a track before full analysis is complete, while progressively producing:

- overview waveform;
- detailed waveform tiles;
- three-band waveform data;
- BPM estimates;
- local beatgrid around the current playback position;
- global beatgrid;
- tempo-change segments;
- confidence values;
- analysis lifecycle state;
- cacheable and portable analysis results.

The library must support environments ranging from small embedded systems to desktop and server applications.

The standard is designed around the following principle:

```text
Playback and user interaction must not wait for full-track analysis.
```

`libapta-audio` separates:

1. the normative APTA data and processing model;
2. the portable reference C implementation;
3. platform-specific scheduling, storage, codec and memory adapters;
4. device-specific integration policy.

---

## 2. Scope

### 2.1. Included in the standard

The standard defines:

- terminology;
- PCM input model;
- source-frame timeline;
- progressive analysis lifecycle;
- waveform representation;
- BPM representation;
- beatgrid representation;
- dynamic-tempo representation;
- confidence semantics;
- locked and provisional analysis regions;
- portable result snapshots;
- `.apta` interchange format;
- capability discovery;
- implementation profiles;
- API and ABI versioning principles;
- conformance requirements;
- forward-compatible extensions.

### 2.2. Included in the reference library

The target reference `libapta` implementation is intended to provide:

- incremental waveform analysis;
- three-band energy analysis;
- onset detection;
- tempo candidate generation;
- BPM estimation;
- local beat tracking;
- global beatgrid refinement;
- confidence calculation;
- progressive snapshots;
- serialization and deserialization;
- portable memory abstraction;
- optional reference DSP backends.

### 2.3. Explicit non-goals

The core standard and core library do not own:

- USB hosts;
- SD-card drivers;
- filesystems;
- network protocols;
- audio codecs;
- audio playback;
- real-time output;
- user interfaces;
- operating-system threads;
- application event loops;
- device-specific media arbitration;
- Rekordbox database parsing;
- proprietary cue or playlist formats.

The library may provide optional helper modules, but these helpers must remain outside the platform-neutral core.

---

## 3. Project identity and naming

The project is named:

```text
libapta-audio
```

Recommended naming conventions:

| Surface | Name |
|---|---|
| Repository | `libapta-audio` |
| Core library | `libapta` |
| C prefix | `apta_` |
| Include path | `<apta/apta.h>` |
| CMake package | `APTA` |
| CMake targets | `apta::core`, `apta::reference_dsp` |
| pkg-config | `libapta` |
| Interchange extension | `.apta` |
| CLI tools | `apta-analyze`, `apta-inspect`, `apta-validate` |

The public specification should use the full name **Adaptive Progressive Track Analysis** on first reference and the acronym **APTA** afterward.

---

## 4. High-level architecture

```text
                        APTA Specification
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
          ▼                    ▼                    ▼
      libapta core       .apta format       Conformance suite
          │
          ├── waveform
          ├── onset
          ├── tempo
          ├── beatgrid
          ├── confidence
          └── serialization
          │
          ▼
       Platform ports
          ├── ESP-IDF
          ├── POSIX
          ├── Windows
          ├── macOS
          ├── Zephyr
          └── bare metal
          │
          ▼
       Applications
          ├── DJ players
          ├── embedded controllers
          ├── desktop DJ software
          ├── mobile applications
          ├── media servers
          └── offline analyzers
```

---

## 5. Repository structure

The following tree is the proposed mature repository layout, not a statement
that every listed file, backend or package target currently exists.

```text
libapta-audio/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── CHANGELOG.md
├── CONTRIBUTING.md
├── GOVERNANCE.md
├── SECURITY.md
├── VERSION
│
├── specification/
│   ├── APTA-SPEC.md
│   ├── terminology.md
│   ├── processing-model.md
│   ├── time-model.md
│   ├── waveform.md
│   ├── tempo.md
│   ├── beatgrid.md
│   ├── confidence.md
│   ├── result-model.md
│   ├── file-format.md
│   ├── profiles.md
│   ├── extensions.md
│   └── conformance.md
│
├── include/
│   └── apta/
│       ├── apta.h
│       ├── apta_version.h
│       ├── apta_types.h
│       ├── apta_config.h
│       ├── apta_allocator.h
│       ├── apta_source.h
│       ├── apta_result.h
│       ├── apta_backend.h
│       ├── apta_serialization.h
│       └── apta_errors.h
│
├── src/
│   ├── core/
│   ├── waveform/
│   ├── onset/
│   ├── tempo/
│   ├── beatgrid/
│   ├── confidence/
│   └── serialization/
│
├── backends/
│   ├── reference_float/
│   ├── reference_fixed/
│   └── external/
│
├── ports/
│   ├── espidf/
│   ├── posix/
│   ├── windows/
│   ├── zephyr/
│   └── baremetal/
│
├── tools/
│   ├── apta-analyze/
│   ├── apta-inspect/
│   ├── apta-validate/
│   └── apta-convert/
│
├── tests/
│   ├── unit/
│   ├── conformance/
│   ├── integration/
│   ├── fuzz/
│   ├── fixtures/
│   └── golden/
│
├── examples/
│   ├── pcm_push/
│   ├── pcm_pull/
│   ├── progressive_player/
│   ├── offline_analyzer/
│   └── espidf/
│
└── packaging/
    ├── pkgconfig/
    ├── cmake/
    ├── espidf/
    └── zephyr/
```

---

## 6. Language, portability and ABI policy

### 6.1. Language level

The reference implementation uses ISO C11.

The public ABI should avoid unnecessary C11-only constructs where this would reduce compatibility. Public headers should remain usable from C++ through `extern "C"` guards.

### 6.2. Opaque handles

Internal structures must not be exposed through the public ABI.

```c
typedef struct apta_context apta_context_t;
typedef struct apta_session apta_session_t;
typedef struct apta_result apta_result_t;
typedef struct apta_serializer apta_serializer_t;
```

Opaque handles allow the implementation to evolve without changing the public structure layout.

### 6.3. Versioned public structures

Public configuration structures begin with size and version fields.

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    /* versioned fields */

    uint64_t reserved[4];
} apta_config_t;
```

Rules:

- callers set `struct_size`;
- the library validates the minimum supported size;
- new fields are appended;
- reserved fields must be initialized to zero;
- incompatible layout changes require a new API or ABI major version.

### 6.4. Three independent version numbers

The project maintains:

1. APTA specification version;
2. `libapta` API/ABI version;
3. `.apta` container-format version.

Example:

```text
APTA Specification: 0.1
libapta:             0.3.0
APTA Container:      1
```

---

## 7. Core design principles

### 7.1. Incremental processing

No public API call may require full-track analysis unless explicitly requested by the caller.

The core processing function accepts a bounded work budget.

```c
typedef struct {
    uint32_t struct_size;
    uint32_t maximum_cpu_us;
    uint32_t maximum_input_frames;
    uint32_t maximum_steps;
    uint32_t flags;
} apta_work_budget_t;
```

```c
apta_status_t apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out);
```

The caller controls when processing occurs and how much work is permitted.

### 7.2. Playback independence

The library must not assume that it owns or controls playback.

It receives PCM or calls a caller-provided PCM source.

### 7.3. Deterministic timeline

All externally visible positions are expressed in source audio frames.

Milliseconds are presentation values only.

### 7.4. Progressive stability

Results move through defined lifecycle states. A result marked stable or final must not silently become less stable.

### 7.5. Explicit confidence

Confidence is not inferred from lifecycle state. Both are exposed independently.

### 7.6. Platform neutrality

The core library does not include operating-system, USB, filesystem or codec headers.

---

## 8. Public API overview

## 8.1. Status codes

```c
typedef enum {
    APTA_STATUS_OK = 0,
    APTA_STATUS_MORE_WORK,
    APTA_STATUS_WOULD_BLOCK,
    APTA_STATUS_END_OF_INPUT,

    APTA_ERROR_INVALID_ARGUMENT = -1,
    APTA_ERROR_OUT_OF_MEMORY = -2,
    APTA_ERROR_UNSUPPORTED = -3,
    APTA_ERROR_INCOMPATIBLE_VERSION = -4,
    APTA_ERROR_SOURCE = -5,
    APTA_ERROR_CORRUPT_DATA = -6,
    APTA_ERROR_CANCELLED = -7,
    APTA_ERROR_INTERNAL = -8
} apta_status_t;
```

## 8.2. Context creation

```c
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_allocator_t allocator;
    apta_logger_t logger;
    apta_clock_t clock;

    uint64_t requested_capabilities;
    uint64_t memory_limit_bytes;

    uint32_t flags;
    uint32_t worker_count;

    uint64_t reserved[4];
} apta_config_t;

apta_status_t apta_create(
    const apta_config_t *config,
    apta_context_t **context_out);

void apta_destroy(
    apta_context_t *context);
```

## 8.3. Session creation

```c
typedef struct {
    uint32_t struct_size;

    uint32_t source_sample_rate;
    uint16_t channel_count;
    uint16_t sample_format;

    uint64_t total_frames;
    uint64_t requested_features;
    uint64_t memory_budget_bytes;

    apta_profile_t profile;
    apta_latency_class_t latency_class;

    uint32_t flags;
    uint32_t reserved32[4];
    uint64_t reserved64[4];
} apta_session_config_t;

apta_status_t apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

void apta_session_destroy(
    apta_session_t *session);
```

## 8.4. PCM push input

```c
typedef enum {
    APTA_SAMPLE_S16_INTERLEAVED = 1,
    APTA_SAMPLE_S24_INTERLEAVED,
    APTA_SAMPLE_S32_INTERLEAVED,
    APTA_SAMPLE_F32_INTERLEAVED,
    APTA_SAMPLE_F32_PLANAR
} apta_sample_format_t;

typedef struct {
    uint32_t struct_size;

    const void *data;
    const void *planes[8];

    uint64_t first_source_frame;
    uint32_t frame_count;
    uint32_t sample_rate;

    uint16_t channel_count;
    uint16_t sample_format;

    uint32_t flags;
} apta_pcm_block_t;

apta_status_t apta_session_push_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block);
```

The push API is suitable when the host application already decodes audio.

## 8.5. PCM pull input

```c
typedef struct {
    uint32_t struct_size;
    void *user_data;

    apta_status_t (*read_frames)(
        void *user_data,
        uint64_t first_source_frame,
        uint32_t requested_frames,
        apta_pcm_block_t *block_out);

    void (*release_frames)(
        void *user_data,
        apta_pcm_block_t *block);

    uint64_t (*get_total_frames)(
        void *user_data);

    uint32_t (*get_sample_rate)(
        void *user_data);

    uint16_t (*get_channel_count)(
        void *user_data);
} apta_pcm_source_t;

apta_status_t apta_session_set_source(
    apta_session_t *session,
    const apta_pcm_source_t *source);
```

The library does not know whether the source is a file, USB medium, network object or in-memory buffer.

## 8.6. Processing

```c
apta_status_t apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out);
```

## 8.7. Cancellation

```c
void apta_session_request_cancel(
    apta_session_t *session);

bool apta_session_is_cancelled(
    const apta_session_t *session);
```

## 8.8. Result snapshots

```c
const apta_result_t *apta_session_acquire_result(
    const apta_session_t *session);

void apta_result_release(
    const apta_result_t *result);
```

Snapshots are immutable after publication.

---

## 9. Capability model

```c
typedef uint64_t apta_capabilities_t;

#define APTA_CAP_WAVEFORM_OVERVIEW  (1ULL << 0)
#define APTA_CAP_WAVEFORM_DETAIL    (1ULL << 1)
#define APTA_CAP_WAVEFORM_3BAND     (1ULL << 2)
#define APTA_CAP_BPM                (1ULL << 3)
#define APTA_CAP_LOCAL_BEATGRID     (1ULL << 4)
#define APTA_CAP_GLOBAL_BEATGRID    (1ULL << 5)
#define APTA_CAP_DYNAMIC_TEMPO      (1ULL << 6)
#define APTA_CAP_CONFIDENCE         (1ULL << 7)
#define APTA_CAP_GRID_LOCKING       (1ULL << 8)
#define APTA_CAP_KEY                (1ULL << 9)
#define APTA_CAP_DOWNBEAT           (1ULL << 10)
#define APTA_CAP_PHRASE             (1ULL << 11)
```

```c
apta_capabilities_t apta_get_capabilities(
    const apta_context_t *context);

apta_capabilities_t apta_result_get_features(
    const apta_result_t *result);
```

An implementation must not claim a capability that it cannot serialize and expose using the normative APTA data model.

---

## 10. Standard profiles

## 10.1. APTA Waveform Profile

Required capabilities:

- PCM input;
- source-frame timeline;
- progressive overview waveform;
- partial result snapshots;
- waveform serialization.

## 10.2. APTA Core Profile

Required capabilities:

- all Waveform Profile capabilities;
- BPM;
- local beatgrid;
- confidence;
- grid locking;
- `.apta` serialization.

## 10.3. APTA Performance Profile

Required capabilities:

- all Core Profile capabilities;
- bounded processing;
- nonblocking PCM push;
- immutable snapshots;
- provisional and stable lifecycle;
- latency-class behavior;
- backpressure-friendly processing.

## 10.4. APTA Full Profile

Required capabilities:

- all Performance Profile capabilities;
- global beatgrid;
- dynamic tempo;
- three-band waveform;
- key;
- downbeat;
- phrase analysis.

Profile conformance does not require the reference algorithm, only compliant behavior and output.

---

## 11. Time model

## 11.1. Source-frame time

```c
typedef struct {
    uint64_t frame;
    uint32_t sample_rate;
} apta_time_t;
```

The source-frame timeline is the authoritative timeline for:

- waveform boundaries;
- beat positions;
- locked ranges;
- cue interoperability;
- progressive analysis ranges.

## 11.2. Fractional positions

Fractional frame values use fixed-point representation.

```c
typedef uint64_t apta_frame_q32_t;
```

Recommended interpretation:

```text
upper 32 bits: integer frame
lower 32 bits: fractional frame
```

This avoids mandatory `double` support on embedded targets.

## 11.3. Conversion rules

Implementations must use overflow-safe conversion.

```text
seconds = source_frame / source_sample_rate
```

Conversion to milliseconds or nanoseconds must not alter the stored authoritative position.

---

## 12. Progressive lifecycle model

```c
typedef enum {
    APTA_RESULT_EMPTY = 0,
    APTA_RESULT_PARTIAL,
    APTA_RESULT_PROVISIONAL,
    APTA_RESULT_STABLE,
    APTA_RESULT_FINAL,
    APTA_RESULT_FAILED
} apta_result_state_t;
```

### EMPTY

No useful analysis is available.

### PARTIAL

A subset of requested data exists. Examples:

- some waveform columns;
- a local analyzed range;
- incomplete track coverage.

### PROVISIONAL

The value is usable but may still be revised.

### STABLE

The value may be extended, but existing stable regions must not be changed without an explicit revision event.

### FINAL

The requested analysis scope is complete.

### FAILED

The session cannot continue without a new source or new configuration.

Lifecycle state and confidence must remain separate.

---

## 13. Confidence model

```c
typedef struct {
    uint8_t waveform;
    uint8_t bpm;
    uint8_t beat_phase;
    uint8_t beatgrid;
    uint8_t dynamic_tempo;
    uint8_t key;
    uint8_t downbeat;
    uint8_t phrase;
} apta_confidence_t;
```

Values range from `0` to `100`.

The specification defines semantic bands:

| Range | Meaning |
|---:|---|
| 0–24 | insufficient evidence |
| 25–49 | weak |
| 50–74 | usable with restrictions |
| 75–89 | strong |
| 90–100 | very strong |

Applications decide which confidence level is required for Sync, Quantize or other functions.

The standard must not mandate one UI treatment.

---

## 14. Waveform data model

## 14.1. Overview waveform

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

Required semantics:

- `minimum` and `maximum` represent signed peak range;
- `rms` represents normalized energy;
- `low`, `mid`, `high` use a normalized 0–255 range;
- flags identify validity and provisional state.

## 14.2. Coverage

```c
typedef struct {
    uint64_t first_source_frame;
    uint64_t last_source_frame;
    uint32_t first_column;
    uint32_t column_count;
} apta_waveform_range_t;
```

Partial waveform publication must include explicit coverage.

## 14.3. Detail tiles

```c
typedef struct {
    uint32_t tile_index;
    uint64_t first_source_frame;
    uint64_t last_source_frame;

    uint32_t column_count;
    const apta_waveform_column_t *columns;
} apta_waveform_tile_t;
```

Tile size is implementation-configurable. Serialized tile descriptors must include explicit column count and frame coverage.

---

## 15. Tempo data model

```c
typedef struct {
    uint32_t bpm_x100;
    uint8_t confidence;
    uint8_t flags;
    uint16_t reserved;
} apta_tempo_value_t;
```

Flags identify:

- provisional;
- stable;
- half-time ambiguity;
- double-time ambiguity;
- dynamic tempo;
- user-confirmed.

Tempo candidates may also be exposed:

```c
typedef struct {
    uint32_t bpm_x100;
    uint16_t score;
    uint8_t confidence;
    uint8_t relation;
} apta_tempo_candidate_t;
```

The standard does not prescribe the underlying tempo-estimation algorithm.

---

## 16. Beatgrid data model

## 16.1. Constant-tempo segment

```c
typedef struct {
    uint64_t first_beat_frame;
    uint32_t beat_count;

    uint32_t bpm_x100;
    uint64_t frames_per_beat_q32;

    uint8_t confidence;
    uint8_t flags;
    uint16_t reserved;
} apta_grid_segment_t;
```

## 16.2. Explicit beat list

An implementation may serialize explicit beat positions when a segment model is insufficient.

```c
typedef struct {
    uint64_t source_frame;
    uint8_t confidence;
    uint8_t flags;
    uint16_t reserved;
} apta_beat_t;
```

## 16.3. Dynamic tempo

Dynamic tempo may be represented using:

- multiple grid segments;
- explicit beats;
- a combination of both.

The result must state which representation is authoritative.

---

## 17. Grid locking and revisions

## 17.1. Locked ranges

```c
typedef struct {
    uint64_t first_source_frame;
    uint64_t last_source_frame;
    uint32_t reason_flags;
} apta_locked_range_t;
```

A stable locked range must not be silently changed.

## 17.2. Revision model

When better analysis conflicts with a locked range, the implementation may publish:

- the current stable grid;
- a pending revision;
- the scope of the proposed change;
- confidence of the proposed revision.

```c
typedef struct {
    uint64_t first_source_frame;
    uint64_t last_source_frame;
    uint32_t revision_id;
    uint8_t confidence;
    uint8_t flags;
    uint16_t reserved;
} apta_pending_revision_t;
```

The host application decides when to apply the revision.

---

## 18. Result snapshot

```c
typedef struct {
    uint32_t struct_size;

    uint32_t specification_version;
    uint32_t producer_version;
    uint32_t generation;

    apta_capabilities_t available_features;
    apta_result_state_t state;

    uint32_t progress_permille;

    apta_confidence_t confidence;

    apta_tempo_value_t tempo;

    uint32_t waveform_column_count;
    const apta_waveform_column_t *waveform_columns;

    uint32_t grid_segment_count;
    const apta_grid_segment_t *grid_segments;

    uint32_t locked_range_count;
    const apta_locked_range_t *locked_ranges;

    uint32_t pending_revision_count;
    const apta_pending_revision_t *pending_revisions;

    uint64_t reserved[8];
} apta_result_info_t;
```

The library exposes result data through read-only accessors. Applications must not modify internal arrays.

---

## 19. Memory abstraction

## 19.1. Allocator API

```c
typedef enum {
    APTA_MEMORY_DEFAULT = 0,
    APTA_MEMORY_FAST = 1 << 0,
    APTA_MEMORY_LARGE = 1 << 1,
    APTA_MEMORY_PERSISTENT = 1 << 2,
    APTA_MEMORY_TEMPORARY = 1 << 3,
    APTA_MEMORY_DMA = 1 << 4
} apta_memory_flags_t;

typedef struct {
    void *user_data;

    void *(*allocate)(
        void *user_data,
        size_t size,
        size_t alignment,
        uint32_t flags);

    void (*deallocate)(
        void *user_data,
        void *memory);

    void *(*reallocate)(
        void *user_data,
        void *memory,
        size_t new_size,
        size_t alignment,
        uint32_t flags);
} apta_allocator_t;
```

## 19.2. Static-workspace mode

```c
typedef struct {
    size_t minimum_bytes;
    size_t recommended_bytes;
    size_t required_alignment;
} apta_memory_requirements_t;

apta_status_t apta_query_memory_requirements(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out);
```

The host may provide a static workspace to avoid dynamic allocation.

---

## 20. Algorithm backend interface

The core may use replaceable algorithm backends.

```c
typedef struct {
    uint32_t struct_size;
    uint32_t backend_version;

    apta_status_t (*create)(
        const void *config,
        const apta_allocator_t *allocator,
        void **backend_state_out);

    apta_status_t (*process)(
        void *backend_state,
        const apta_pcm_block_t *block,
        const apta_work_budget_t *budget);

    apta_status_t (*flush)(
        void *backend_state);

    void (*destroy)(
        void *backend_state);
} apta_backend_v1_t;
```

Backend categories:

- waveform;
- filterbank;
- onset;
- tempo;
- beatgrid;
- fingerprint;
- serialization.

A vendor may use a proprietary algorithm while retaining the public APTA data model and file compatibility.

---

## 21. Threading model

The core supports three integration modes.

### 21.1. Cooperative single-thread mode

The host periodically calls:

```c
apta_session_process(session, &budget, &progress);
```

### 21.2. Application-managed worker mode

The application schedules one or more sessions on its own task or thread pool.

### 21.3. Optional library-managed worker mode

A platform adapter may implement workers through host callbacks.

The platform-neutral core must not require threads.

Thread-safety rules:

- separate sessions may be processed concurrently when the context supports it;
- one session must not be processed concurrently by multiple workers unless explicitly supported;
- result acquisition is thread-safe;
- immutable result snapshots may be read concurrently;
- cancellation is thread-safe.

---

## 22. `.apta` interchange format

## 22.1. Purpose

The `.apta` file is a portable interchange format, not merely an implementation cache.

It allows:

- desktop analysis with embedded playback;
- analysis sharing between devices;
- cache persistence;
- third-party analyzers;
- validation and inspection;
- format conversion.

## 22.2. Container layout

```text
Fixed header
Section directory
META section
WOVR section
WTIX section
WDTL section
TEMP section
BGRD section
CONF section
LOCK section
REVN section
Extension sections
```

## 22.3. Header

```c
typedef struct {
    uint8_t magic[4];              /* "APTA" */

    uint16_t container_version;
    uint16_t header_size;

    uint32_t specification_version;
    uint32_t producer_version;

    uint32_t flags;
    uint32_t section_count;

    uint64_t total_size;

    uint8_t source_fingerprint[32];

    uint32_t header_crc32;
    uint32_t reserved32[3];
} apta_file_header_t;
```

## 22.4. Section descriptor

```c
typedef struct {
    uint32_t type_fourcc;
    uint16_t version;
    uint16_t flags;

    uint64_t offset;
    uint64_t size;

    uint32_t crc32;
    uint32_t reserved;
} apta_file_section_t;
```

## 22.5. Required compatibility rules

Readers must:

- validate all offsets and sizes;
- reject overlapping required sections;
- reject unsupported required-section versions;
- ignore unknown optional sections;
- enforce configured allocation limits;
- reject integer overflows;
- verify required CRCs;
- preserve source-frame semantics.

Writers must:

- use canonical little-endian encoding for packed binary sections;
- zero reserved fields;
- write accurate section sizes;
- avoid pointer-dependent layout;
- document extension ownership.

## 22.6. Metadata encoding

Recommended:

- canonical CBOR for structured metadata;
- packed binary arrays for waveform and grid data.

The format specification must define canonical keys and numeric units.

---

## 23. Serialization API

```c
typedef struct {
    void *user_data;

    apta_status_t (*write)(
        void *user_data,
        const void *data,
        size_t size);

    apta_status_t (*seek)(
        void *user_data,
        uint64_t offset);

    apta_status_t (*flush)(
        void *user_data);
} apta_output_stream_t;

apta_status_t apta_result_serialize(
    const apta_result_t *result,
    const apta_output_stream_t *output,
    const apta_serialize_config_t *config);
```

Deserialization:

```c
typedef struct {
    void *user_data;

    apta_status_t (*read)(
        void *user_data,
        uint64_t offset,
        void *buffer,
        size_t size);

    uint64_t (*get_size)(
        void *user_data);
} apta_input_stream_t;

apta_status_t apta_result_deserialize(
    apta_context_t *context,
    const apta_input_stream_t *input,
    const apta_deserialize_config_t *config,
    apta_result_t **result_out);
```

---

## 24. Track identity

The standard distinguishes:

- file identity;
- audio-content identity;
- application identity.

```c
typedef struct {
    uint8_t fast_fingerprint[32];
    uint8_t full_content_hash[32];

    uint64_t source_size;
    uint64_t total_frames;

    uint32_t sample_rate;
    uint16_t channel_count;
    uint16_t flags;
} apta_track_identity_t;
```

The fast fingerprint may be generated from selected file regions and decoded metadata.

The full content hash is optional and should not be required in latency-sensitive load paths.

---

## 25. Reference DSP pipeline

The reference implementation may use:

```text
PCM
 ├── channel downmix
 ├── amplitude aggregation
 ├── three-band filterbank
 ├── anti-alias filtering
 ├── decimation
 ├── windowing
 ├── FFT
 ├── spectral flux
 ├── adaptive threshold
 ├── onset peaks
 ├── tempo candidates
 ├── phase search
 ├── local beat tracker
 └── global beatgrid refinement
```

These algorithms are informative for interoperability and testing. They are not mandatory for third-party compliant implementations.

---

## 26. Reference processing phases

```text
PHASE 0  source validation
PHASE 1  immediate local waveform
PHASE 2  progressive overview waveform
PHASE 3  onset envelope
PHASE 4  provisional BPM
PHASE 5  local beatgrid
PHASE 6  stable local ranges
PHASE 7  global beatgrid
PHASE 8  dynamic-tempo refinement
PHASE 9  final result
```

A session may skip phases when data already exists or a requested capability is disabled.

---

## 27. Conformance suite

## 27.1. Required test categories

- public API tests;
- ABI layout tests;
- memory-allocation failure tests;
- PCM push tests;
- PCM pull tests;
- waveform golden vectors;
- BPM vectors;
- beatgrid vectors;
- progressive-state tests;
- locked-range tests;
- serialization round trips;
- malformed-file tests;
- fuzzing;
- cross-platform determinism tests.

## 27.2. Audio fixtures

Fixtures should include:

- click tracks from 40 to 300 BPM;
- 44.1 kHz and 48 kHz;
- mono and stereo;
- half-time patterns;
- double-time patterns;
- swing;
- syncopation;
- long intros;
- breakdowns;
- tempo ramps;
- abrupt tempo changes;
- silence;
- clipped material;
- low-level material;
- live-drum examples that can be legally redistributed.

## 27.3. Numerical tolerances

Conformance must distinguish:

- exact format compatibility;
- reference-algorithm numerical tolerance;
- semantic interoperability.

A third-party implementation is not required to produce bit-identical BPM or beat positions unless it claims reference-algorithm conformance.

---

## 28. Command-line tools

## 28.1. `apta-analyze`

```text
apta-analyze input.flac --output input.apta
apta-analyze input.wav --profile performance
apta-analyze input.mp3 --features waveform,bpm,beatgrid
```

## 28.2. `apta-inspect`

```text
apta-inspect input.apta
apta-inspect input.apta --json
apta-inspect input.apta --section BGRD
```

## 28.3. `apta-validate`

```text
apta-validate input.apta
apta-validate input.apta --strict
```

## 28.4. `apta-convert`

Future uses:

- legacy cache to `.apta`;
- `.apta` version migration;
- waveform-only extraction;
- JSON diagnostic export.

---

## 29. Packaging

Supported distribution methods:

- source vendoring;
- static library;
- shared library;
- CMake package;
- `pkg-config`;
- ESP-IDF Component Manager;
- Zephyr module;
- release archives.

Recommended CMake targets:

```text
apta::core
apta::serialization
apta::reference_dsp
apta::port_posix
apta::port_espidf
```

Example:

```cmake
find_package(APTA 1.0 REQUIRED)

target_link_libraries(
    my_player
    PRIVATE
        apta::core
        apta::reference_dsp
)
```

---

## 30. Licensing

Current repository licensing model:

| Asset | License |
|---|---|
| Specification and documentation | Apache-2.0 |
| Reference implementation | Apache-2.0 |
| Test code | Apache-2.0 |
| Example code | Apache-2.0 |
| Generated fixtures | Apache-2.0 unless the fixture records another compatible redistribution license |

The root [`LICENSE`](../../LICENSE) applies to the repository. Source, build and
test files use SPDX identifiers where the file format supports comments.

```c
// SPDX-License-Identifier: Apache-2.0
```

A legal review should still be completed before the first stable release.

---

## 31. Security requirements

The parser and serializer must treat all external `.apta` data as untrusted.

Required protections:

- checked arithmetic;
- bounded allocations;
- maximum-section count;
- maximum-file size policy;
- offset validation;
- overlap validation;
- recursion limits for structured metadata;
- CRC validation;
- rejection of unsupported required sections;
- fuzz testing;
- no execution of embedded code;
- no absolute filesystem paths in the portable format.

The project must provide a `SECURITY.md` with a private vulnerability-reporting path.

---

## 32. Governance and change process

Proposed process:

```text
1. Open issue or APTA-RFC.
2. Define problem and compatibility impact.
3. Draft specification changes.
4. Implement reference support.
5. Add conformance tests.
6. Demonstrate at least two implementations where practical.
7. Approve and merge.
```

Document types:

- `APTA-SPEC`: normative standard;
- `APTA-RFC`: proposed standard change;
- `APTA-ADR`: implementation architecture decision;
- `APTA-CONFORMANCE`: test definition.

Breaking changes require a new major specification or ABI version.

---

## 33. Development roadmap

## Stage S0 — Foundation

- create repository;
- establish license;
- write project charter;
- define terminology;
- define non-goals;
- add contribution and security policies.

## Stage S1 — Portable core API

- opaque handles;
- allocator abstraction;
- PCM push and pull;
- bounded process API;
- cancellation;
- immutable result snapshots.

## Stage S2 — Waveform Profile

- overview waveform;
- detail tiles;
- progressive coverage;
- serialization;
- conformance vectors.

## Stage S3 — `.apta` container

- header;
- section directory;
- metadata encoding;
- CRC rules;
- parser;
- serializer;
- fuzz suite.

## Stage S4 — Tempo and local grid

- BPM model;
- candidate model;
- confidence;
- local beatgrid;
- locked ranges;
- provisional-to-stable lifecycle.

## Stage S5 — Reference desktop tools

- POSIX source adapter;
- decoder integration;
- `apta-analyze`;
- `apta-inspect`;
- `apta-validate`.

## Stage S6 — Global grid and dynamic tempo

- global refinement;
- segment representation;
- explicit beat representation;
- revision model.

## Stage S7 — ESP-IDF port

- ESP allocator;
- ESP clock;
- optional ESP-DSP backend;
- cooperative scheduler example;
- embedded memory profiles.

## Stage S8 — Second independent platform

Implement and validate at least one of:

- Zephyr;
- Windows;
- STM32 bare metal;
- Linux DJ player;
- mobile application.

## Stage S9 — APTA 1.0

Requirements:

- stable specification;
- stable public API;
- stable `.apta` format;
- conformance suite;
- at least two independent platform integrations;
- parser fuzzing;
- documented compatibility policy.

---

## 34. Definition of success

`libapta-audio` is successful when:

1. one application can analyze a track and another can consume the `.apta` result;
2. embedded and desktop implementations use the same public data model;
3. progressive results are usable before full-track completion;
4. applications retain scheduling and playback control;
5. vendors can replace algorithms without breaking interoperability;
6. malformed result files are safely rejected;
7. at least two independent devices or applications pass the conformance profile;
8. the public ABI and file format can evolve through documented versioning.

---

## 35. Final architecture rule

```text
libapta-audio owns:
    analysis state
    waveform data
    tempo data
    beatgrid data
    confidence
    result serialization

libapta-audio does not own:
    playback
    USB
    filesystem
    codecs
    threads
    user interface
    device-specific scheduling
```

This separation is mandatory. It is what allows the same library and standard to serve embedded DJ players, desktop software, mobile applications and analysis servers.
