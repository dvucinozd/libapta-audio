// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_INTERNAL_H
#define APTA_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdatomic.h>

#include <apta/apta.h>

#ifdef APTA_INTERNAL_PROFILE_S4
#include "apta_internal_profile.h"
#endif

/*
 * C3: geometry and capacity constants.
 *
 * Every value a host might reasonably need to change is #ifndef-guarded so it
 * can be overridden from the build system, for example
 * -DAPTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN=256. Derived constants stay
 * derived and are deliberately not overridable. Section "Invariants" below
 * asserts everything the code relies on; the bounded buffers
 * break silently otherwise.
 */
#ifndef APTA_INTERNAL_MAX_REGION_REQUESTS
#define APTA_INTERNAL_MAX_REGION_REQUESTS 16u
#endif
#ifndef APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN
#define APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN 1024u
#endif

/* Not overridable: this is the published level identifier for detail tiles. */
#define APTA_INTERNAL_DETAIL_LEVEL_ID 1u

#ifndef APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN
#define APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN 256u
#endif
#ifndef APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE
#define APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE 64u
#endif

/* Derived: must continue to follow from its inputs. */
#define APTA_INTERNAL_DETAIL_TILE_FRAMES \
    (APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN * \
     APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE)

#ifndef APTA_INTERNAL_MAX_DETAIL_TILES
#define APTA_INTERNAL_MAX_DETAIL_TILES 4u
#endif
#ifndef APTA_INTERNAL_PROCESS_CHUNK_FRAMES
#define APTA_INTERNAL_PROCESS_CHUNK_FRAMES 256u
#endif
#ifndef APTA_INTERNAL_MAX_PUSH_FRAMES
#define APTA_INTERNAL_MAX_PUSH_FRAMES 4096u
#endif
#ifndef APTA_INTERNAL_SCHEDULER_AGE_STEP
#define APTA_INTERNAL_SCHEDULER_AGE_STEP 8u
#endif
#ifndef APTA_INTERNAL_SCHEDULER_MAX_SKIPS
#define APTA_INTERNAL_SCHEDULER_MAX_SKIPS 32u
#endif

#define APTA_INTERNAL_RESULT_FLAG_POOLED (1u << 0)

#if defined(_MSC_VER)
typedef union {
    long double long_double_value;
    long long long_long_value;
    void *pointer_value;
} apta_internal_max_align_t;
#else
typedef max_align_t apta_internal_max_align_t;
#endif

#define APTA_INTERNAL_MAX_ALIGNMENT \
    alignof(apta_internal_max_align_t)

#ifndef APTA_INTERNAL_ONSET_FRAMES_PER_BIN
#define APTA_INTERNAL_ONSET_FRAMES_PER_BIN 256u
#endif
/* The onset bin store is a ring addressed with `bin_index % capacity`, not a
 * mask, so a non-power-of-two capacity is correct. It costs an integer
 * division per lookup; a power of two lets the compiler strength-reduce it. */
#ifndef APTA_INTERNAL_ONSET_BIN_CAPACITY
#define APTA_INTERNAL_ONSET_BIN_CAPACITY 4096u
#endif
#ifndef APTA_INTERNAL_MIN_TEMPO_BINS
#define APTA_INTERNAL_MIN_TEMPO_BINS 512u
#endif
#ifndef APTA_INTERNAL_STABLE_TEMPO_BINS
#define APTA_INTERNAL_STABLE_TEMPO_BINS 1024u
#endif
#ifndef APTA_INTERNAL_MAX_TEMPO_CANDIDATES
#define APTA_INTERNAL_MAX_TEMPO_CANDIDATES 3u
#endif

/* A2: how many new onset bins must accumulate before the tempo estimate is
 * recomputed. One process call of 1024 frames advances the evidence range by
 * at most four 256-frame bins, so without a gate the full autocorrelation runs
 * on every call. 32 bins is 8192 frames, about 186 ms at 44.1 kHz; the
 * estimate is not meaningfully improved by re-running it every 23 ms. */
#ifndef APTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS
#define APTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS 32u
#endif

/* Phase 7: one scheduler step advances this many correlation lags.  The
 * default keeps a single step comfortably below an audio block on the P4,
 * while maximum_steps == 0 still permits an intentionally unbounded call. */
#ifndef APTA_INTERNAL_S4_LAGS_PER_STEP
#define APTA_INTERNAL_S4_LAGS_PER_STEP 4u
#endif

/* A4: APTA_FEATURE_CONFIDENCE is deliberately absent. It is a modifier that
 * qualifies whatever features a host actually requested, not a request for
 * tempo analysis; including it here made WAVEFORM_OVERVIEW | CONFIDENCE
 * activate the whole autocorrelation estimator. */
#define APTA_INTERNAL_S4_FEATURES \
    (APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID | \
     APTA_FEATURE_GRID_LOCKING)

#define APTA_INTERNAL_S6_FEATURES \
    (APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO)

#ifndef APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN
#define APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN 2048u
#endif
/* Same ring addressing as the onset bins; see the note there. */
#ifndef APTA_INTERNAL_GLOBAL_BIN_CAPACITY
#define APTA_INTERNAL_GLOBAL_BIN_CAPACITY 16384u
#endif
#ifndef APTA_INTERNAL_GLOBAL_WINDOW_BINS
#define APTA_INTERNAL_GLOBAL_WINDOW_BINS 128u
#endif
#ifndef APTA_INTERNAL_GLOBAL_MIN_BINS
#define APTA_INTERNAL_GLOBAL_MIN_BINS 64u
#endif
#ifndef APTA_INTERNAL_GLOBAL_STABLE_BINS
#define APTA_INTERNAL_GLOBAL_STABLE_BINS 256u
#endif

/* A2: the S6 equivalent. Global bins hold 2048 frames, so a 1024-frame process
 * call advances the range by at most half a bin and 32 bins is about 1.5 s at
 * 44.1 kHz. The global grid does not need updating more often than that. */
#ifndef APTA_INTERNAL_S6_REFRESH_MIN_NEW_BINS
#define APTA_INTERNAL_S6_REFRESH_MIN_NEW_BINS 32u
#endif
#define APTA_INTERNAL_GLOBAL_MAX_SEGMENTS \
    APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS
#define APTA_INTERNAL_GLOBAL_MAX_BEATS \
    APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS

/*
 * C3: invariants. These hold at the default settings and must keep holding for
 * any override. Each one guards something that fails silently rather than
 * loudly if it is violated.
 */

/* Nothing may be zero: these divide, size arrays, or bound loops. */
_Static_assert(APTA_INTERNAL_MAX_REGION_REQUESTS >= 1u,
               "at least one region-request slot is required");
_Static_assert(APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN >= 1u,
               "overview frames per column must be non-zero");
_Static_assert(APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN >= 1u,
               "detail frames per column must be non-zero");
_Static_assert(APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE >= 1u,
               "detail columns per tile must be non-zero");
_Static_assert(APTA_INTERNAL_MAX_DETAIL_TILES >= 1u,
               "at least one detail tile is required");
_Static_assert(APTA_INTERNAL_PROCESS_CHUNK_FRAMES >= 1u,
               "process chunk must be non-zero");
_Static_assert(APTA_INTERNAL_ONSET_FRAMES_PER_BIN >= 1u,
               "onset frames per bin must be non-zero");
_Static_assert(APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN >= 1u,
               "global frames per bin must be non-zero");
_Static_assert(APTA_INTERNAL_S4_LAGS_PER_STEP >= 1u,
               "S4 lags per scheduler step must be non-zero");
_Static_assert(APTA_INTERNAL_GLOBAL_WINDOW_BINS >= 1u,
               "global window must be non-zero");

/* A push must be drainable in whole chunks, and a chunk must fit a push. */
_Static_assert(APTA_INTERNAL_PROCESS_CHUNK_FRAMES <=
                   APTA_INTERNAL_MAX_PUSH_FRAMES,
               "process chunk cannot exceed the maximum push");

/* The onset ring must be able to hold the evidence run the estimator waits
 * for, and the run it calls stable. apta_s4_find_evidence() bounds the range
 * it returns by the capacity, so a capacity below the minimum means the
 * estimator can never start. */
_Static_assert(APTA_INTERNAL_MIN_TEMPO_BINS <=
                   APTA_INTERNAL_ONSET_BIN_CAPACITY,
               "onset bin capacity must hold the minimum tempo window");
_Static_assert(APTA_INTERNAL_STABLE_TEMPO_BINS <=
                   APTA_INTERNAL_ONSET_BIN_CAPACITY,
               "onset bin capacity must hold the stable tempo window");
_Static_assert(APTA_INTERNAL_MIN_TEMPO_BINS <=
                   APTA_INTERNAL_STABLE_TEMPO_BINS,
               "the stable window cannot be shorter than the minimum window");

/* Same for the global ring, which additionally slices into windows. */
_Static_assert(APTA_INTERNAL_GLOBAL_MIN_BINS <=
                   APTA_INTERNAL_GLOBAL_BIN_CAPACITY,
               "global bin capacity must hold the minimum window");
_Static_assert(APTA_INTERNAL_GLOBAL_STABLE_BINS <=
                   APTA_INTERNAL_GLOBAL_BIN_CAPACITY,
               "global bin capacity must hold a stable window");
_Static_assert(APTA_INTERNAL_GLOBAL_WINDOW_BINS <=
                   APTA_INTERNAL_GLOBAL_BIN_CAPACITY,
               "an analysis window must fit the global ring");

/* Candidate capacity is published in the public headers and serialized. */
_Static_assert(APTA_INTERNAL_MAX_TEMPO_CANDIDATES >= 1u,
               "at least one tempo candidate is required");
_Static_assert(APTA_INTERNAL_MAX_TEMPO_CANDIDATES <=
                   APTA_REFERENCE_TEMPO_MAX_CANDIDATES,
               "candidate capacity exceeds the documented public maximum");
_Static_assert(APTA_INTERNAL_GLOBAL_MAX_SEGMENTS <=
                   APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS,
               "segment capacity exceeds the documented public maximum");
_Static_assert(APTA_INTERNAL_GLOBAL_MAX_BEATS <=
                   APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS,
               "beat capacity exceeds the documented public maximum");

/* A2's refresh gates must be able to fire. */
_Static_assert(APTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS >= 1u,
               "the S4 refresh gate must allow progress");
_Static_assert(APTA_INTERNAL_S6_REFRESH_MIN_NEW_BINS >= 1u,
               "the S6 refresh gate must allow progress");

typedef struct {
    void *raw_memory;
    size_t allocated_size;
    size_t requested_size;
} apta_allocation_header_t;

static inline int apta_internal_size_array_fits(
    size_t base_size,
    size_t element_count,
    size_t element_size)
{
    return element_size == 0u ||
           element_count <= (SIZE_MAX - base_size) / element_size;
}

typedef struct {
    uint32_t request_id;
    apta_region_request_t request;
    apta_request_state_t state;
    uint32_t diagnostic_code;
    uint32_t scheduler_skip_count;
    uint32_t reserved32;
    uint64_t scheduler_enqueue_serial;
} apta_internal_request_t;

typedef struct {
    uint32_t effective_priority;
    uint32_t request_id;
    uint64_t soft_deadline_monotonic_ns;
    uint64_t enqueue_serial;
} apta_internal_schedule_score_t;

typedef struct {
    apta_metadata_view_t view;
    uint8_t *storage;
    size_t storage_size;
    uint32_t present;
    uint32_t reserved32;
} apta_internal_metadata_t;

typedef struct {
    apta_source_frame_t first_frame;
    apta_source_frame_t end_frame;
} apta_internal_range_t;

typedef struct apta_internal_pcm_node {
    struct apta_internal_pcm_node *next;
    apta_source_frame_t first_frame;
    uint32_t frame_count;
    uint32_t processed_frames;
    float samples[];
} apta_internal_pcm_node_t;

typedef struct {
    uint32_t column_index;
    uint32_t sample_count;
    /* A3: sum of squared sample magnitudes scaled by
     * APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE. Bounded by 1024 * (2^23)^2 = 2^56
     * for an overview column, a 256x margin inside uint64_t.
     * apta_*_quantize_rms() divides out both the count and the scale.
     *
     * The scale is 2^23 rather than the 2^15 used for onset magnitudes because
     * this feeds published column values. A sample decoded from 16-bit PCM is
     * exactly k/32768, so k * 2^8 is representable without loss and the
     * conversion introduces no quantization error at all for such sources.
     * A 15-bit scale here moved some published rms values by one count. */
    uint64_t sum_squares;
    float minimum;
    float maximum;
    uint8_t clipped;
    uint8_t complete;
    uint16_t reserved16;
} apta_internal_waveform_accumulator_t;

typedef struct {
    uint32_t tile_index;
    uint32_t complete_count;
    uint64_t access_serial;
    uint8_t occupied;
    uint8_t reserved8[7];
    apta_internal_waveform_accumulator_t
        accumulators[APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE];
} apta_internal_detail_tile_t;

/* A3: sum of 15-bit sample magnitudes, not a floating-point sum. The target is
 * RV32IMAFC with no hardware double, and this accumulates once per source
 * sample. S6 bins hold the most samples, 2048, so the sum is bounded by
 * 2048 * 32768 = 67,108,864 -- a 64x margin inside uint32_t. Readers divide by
 * sample_count and by 32768 to recover the normalized energy. */
#define APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE 32768.0f
#define APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE 8388608.0f

/* C1: three-band split corners, in hertz. Derived into one-pole coefficients
 * from the session's sample rate, never hard-coded in samples. */
#ifndef APTA_INTERNAL_BAND_LOW_HZ
#define APTA_INTERNAL_BAND_LOW_HZ 200.0f
#endif
#ifndef APTA_INTERNAL_BAND_HIGH_HZ
#define APTA_INTERNAL_BAND_HIGH_HZ 2000.0f
#endif
#define APTA_INTERNAL_BAND_COUNT 3u

/* B3: relative contributions to the experimental novelty sum. They remain
 * overridable for controlled development-corpus experiments. The first
 * candidate is deliberately unnormalised: the earlier normalized prototype
 * regressed severely on the available synthetic corpus. */
#ifndef APTA_INTERNAL_ONSET_WEIGHT_LOW
#define APTA_INTERNAL_ONSET_WEIGHT_LOW 1.0f
#endif
#ifndef APTA_INTERNAL_ONSET_WEIGHT_MID
#define APTA_INTERNAL_ONSET_WEIGHT_MID 1.0f
#endif
#ifndef APTA_INTERNAL_ONSET_WEIGHT_HIGH
#define APTA_INTERNAL_ONSET_WEIGHT_HIGH 1.0f
#endif
#ifndef APTA_INTERNAL_ONSET_MULTIBAND_MIX
#define APTA_INTERNAL_ONSET_MULTIBAND_MIX 0.25f
#endif

/* C1/B3: one shared portable filterbank implementation. Overview and onset
 * each own state because they advance at different points in the pipeline. */
typedef struct {
    float low_state;
    float mid_state;
    float low_coefficient;
    float mid_coefficient;
} apta_internal_band_filter_t;

void apta_internal_band_filter_init(
    apta_internal_band_filter_t *filter,
    uint32_t sample_rate);
void apta_internal_band_filter_reset(apta_internal_band_filter_t *filter);
void apta_internal_band_filter_split(
    apta_internal_band_filter_t *filter,
    float sample,
    float out_bands[APTA_INTERNAL_BAND_COUNT]);

/*
 * B1: preferred-tempo prior.
 *
 * Autocorrelation of a periodic novelty function peaks at integer multiples
 * and divisors of the true period, frequently more strongly than at the period
 * itself. Raw argmax has nothing to resolve that with. A log-normal prior
 * centred on the range most DJ material occupies weights the score before
 * selection.
 *
 * Centre and width are overridable so a consumer with different repertoire can
 * retune without forking. Width is in natural-log units: 0.55 puts a half- or
 * double-tempo candidate at about 0.45 of the weight of one at the centre, and
 * a third or triple at about 0.14.
 */
#ifndef APTA_INTERNAL_TEMPO_PRIOR_CENTRE_MILLIBPM
#define APTA_INTERNAL_TEMPO_PRIOR_CENTRE_MILLIBPM 125000u
#endif
#ifndef APTA_INTERNAL_TEMPO_PRIOR_WIDTH
#define APTA_INTERNAL_TEMPO_PRIOR_WIDTH 0.55f
#endif

/*
 * B1: how close an octave sibling has to come before it counts as ambiguity.
 *
 * A sibling scoring below this fraction of the winner does not reduce
 * confidence at all; between here and parity, confidence falls to zero. The
 * knee matters: a clean four-to-the-floor track always has a half-tempo
 * sibling with a substantial score, so a scaling that starts penalising as
 * soon as any sibling exists collapses confidence on ordinary material and
 * makes threshold gating useless. Ambiguous has to mean "nearly as good", not
 * "present".
 */
#ifndef APTA_INTERNAL_TEMPO_AMBIGUITY_KNEE
#define APTA_INTERNAL_TEMPO_AMBIGUITY_KNEE 0.85f
#endif

/*
 * How many beats the sub-bin period refinement measures across.
 *
 * The integer lag search resolves the period to one onset bin, 5.8 ms, which is
 * 1.6 BPM near 128 and enough for the published grid to slip half a beat inside
 * two minutes. Correlating across N beats and dividing divides the error too.
 *
 * Sixteen is four bars in common time. Raising it buys hundredths of a BPM and
 * assumes the tempo is held over more of the track; lowering it gives the drift
 * back. The refinement falls back to whatever fits when the evidence is shorter
 * than this many beats, so early estimates are coarser rather than absent.
 */
#ifndef APTA_INTERNAL_TEMPO_REFINE_MAX_BEATS
#define APTA_INTERNAL_TEMPO_REFINE_MAX_BEATS 16u
#endif

/*
 * When the global estimator may promote one of the local estimator's own
 * candidates over the local winner.
 *
 * The two engines fail differently: S6's long windows make it robust about
 * which tempo region is right, S4's fine bins make it precise once the region
 * is settled. Measured over 68 real tracks, S4 is wrong on 25, and on 11 of
 * those the correct answer was already in its candidate list, beaten by a
 * margin -- the runner-up scored 82 to 98 percent of the winner.
 *
 * Promotion is deliberately weak. It selects a candidate S4 already produced
 * and never computes a new tempo, because an earlier attempt that rescaled the
 * winner toward S6 by a metrical ratio could invent a value neither engine
 * proposed -- 240.02 against a truth of 120.00 -- and lost more than it gained.
 *
 * ENDORSE_TOLERANCE: how close S6 must be to a candidate to endorse it, as a
 * fraction of the candidate. One percent is tighter than S6's own resolution,
 * so agreement means S6 landed squarely on the candidate rather than near it.
 *
 * ENDORSE_MIN_SCORE: the endorsed candidate must already have scored this much
 * of the winner, out of 65535. Below it S6 can promote something S4's own
 * evidence rejected, which is where the regressions were.
 *
 * Both were chosen on that corpus, which is a risk worth naming: the gain sits
 * on a plateau rather than a peak -- every tolerance from 0.3 to 2 percent and
 * every threshold from 0 to 58000 improves on the baseline -- but a second
 * library has not confirmed them.
 */
#ifndef APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE
#define APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE 0.01f
#endif
#ifndef APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE
#define APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE 55000u
#endif

/*
 * One onset bin, shared by the local and global rings.
 *
 * `bin_index` is 32 bits rather than 64 because the width decides the size of
 * the largest allocation the library makes. A 64-bit field forces eight-byte
 * alignment and pads the struct to 24 bytes for 17 bytes of data; 32 bits packs
 * it to 16. Across S6's 16,384-entry ring that is 131,072 bytes, and on an
 * ESP32-P4 the difference between an S6 workspace that fits in internal SRAM
 * and one that spills to PSRAM -- measured at 3.5 times the cost per call, see
 * section 29 of the S4 status document.
 *
 * The range is not a constraint in practice. At the local ring's 256 frames per
 * bin, 2^32 bins is 1.1e12 frames, or 6.9 million hours at 44.1 kHz. It is
 * still checked rather than assumed: both writers reject a frame position that
 * would not round-trip.
 */
typedef struct {
    uint32_t bin_index;
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    /* B3: S4's quantized band sums and broadband anchor share storage with
     * S6's unchanged 32-bit broadband accumulator. A local and global bin
     * never inhabit the same ring, so the union preserves both algorithms
     * while keeping the dominant allocation at 16 bytes per entry. */
    union {
        struct {
            uint16_t band_sums[APTA_INTERNAL_BAND_COUNT];
            uint16_t broadband_sum;
        } multiband;
        uint32_t sum_absolute;
    } sums;
    uint16_t sample_count;
    uint8_t occupied;
    uint8_t reserved8;
#else
    uint32_t sum_absolute;
    uint32_t sample_count;
    uint8_t occupied;
    uint8_t reserved8[3];
#endif
} apta_internal_onset_bin_t;

#ifdef APTA_INTERNAL_MULTIBAND_ONSET
#define APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE 255u
_Static_assert(APTA_INTERNAL_ONSET_FRAMES_PER_BIN *
                       APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE <=
                   UINT16_MAX,
               "S4 band sum must fit in uint16_t");
_Static_assert(APTA_INTERNAL_ONSET_FRAMES_PER_BIN <= UINT16_MAX,
               "S4 sample count must fit in uint16_t");
_Static_assert(APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN <= UINT16_MAX,
               "S6 sample count must fit in uint16_t");
#endif

#ifdef APTA_INTERNAL_MULTIBAND_ONSET
#define APTA_INTERNAL_ONSET_SUM_ABSOLUTE(bin) ((bin)->sums.sum_absolute)
#else
#define APTA_INTERNAL_ONSET_SUM_ABSOLUTE(bin) ((bin)->sum_absolute)
#endif

_Static_assert(sizeof(apta_internal_onset_bin_t) <= 16u,
               "the onset bin is the unit of the library's largest allocation; "
               "growing it past 16 bytes pushes the S6 workspace out of "
               "internal SRAM on the intended target");

/* The largest bin index either ring can store. Both process_sample() entry
 * points reject a frame beyond it rather than wrapping into a bin that would
 * silently alias an earlier one. */
#define APTA_INTERNAL_MAX_BIN_INDEX UINT32_MAX

typedef struct apta_internal_result_pool_control
    apta_internal_result_pool_control_t;
typedef struct apta_internal_s6_session_state
    apta_internal_s6_session_state_t;
typedef struct apta_internal_s6_result_state
    apta_internal_s6_result_state_t;

struct apta_context {
    apta_allocator_t allocator;
    apta_logger_t logger;
    apta_clock_t clock;

    apta_feature_mask_t capabilities;
    uint64_t memory_limit_bytes;
    uint32_t flags;

    atomic_size_t allocated_bytes;
    atomic_uint session_count;
    atomic_uint result_count;
    atomic_uint_fast64_t lineage_counter;
};

struct apta_result {
    apta_context_t *context;
    atomic_uint reference_count;
    apta_internal_result_pool_control_t *result_pool;
    uint32_t result_pool_slot_index;
    uint32_t result_flags;
    apta_result_info_t info;
    apta_source_info_t source_info;

    apta_source_frame_t total_source_frames;
    uint32_t source_sample_rate;
    uint32_t source_channel_count;
    apta_channel_layout_t source_channel_layout;

    apta_internal_metadata_t metadata;

    apta_waveform_overview_view_t overview;
    apta_waveform_span_t *overview_spans;
    apta_waveform_column_t *overview_columns;

    uint32_t detail_tile_count;
    apta_waveform_tile_view_t *detail_tiles;
    apta_waveform_column_t *detail_columns;

    apta_tempo_view_t tempo;
    apta_tempo_candidate_t *tempo_candidates;

    apta_grid_view_t local_grid;
    apta_frame_range_t *local_grid_coverage;
    apta_grid_segment_t *local_grid_segments;

    apta_key_view_t key;
    apta_key_candidate_t *key_candidates;

    apta_meter_view_t meter;
    apta_meter_segment_t *meter_segments;

    uint32_t quality_count;
    uint32_t reserved_quality_count;
    apta_quality_view_t *quality;

    apta_internal_s6_result_state_t *s6;
};

struct apta_session {
    apta_context_t *context;
    apta_session_config_t config;

    atomic_uint state;
    atomic_uint cancel_requested;
    atomic_flag process_lock;
    atomic_flag result_lock;

    /* Absolute deadline for the current process call. Zero means that the
     * caller supplied no usable soft-time budget. Internal analysis stages
     * share it so chained waveform/S4/S6 work cannot each consume a fresh
     * copy of the same budget. */
    uint64_t process_deadline_ns;

    apta_result_t *current_result;
    apta_internal_result_pool_control_t *result_pool;
    apta_generation_t generation;
    uint64_t lineage_id_high;
    uint64_t lineage_id_low;

    apta_internal_metadata_t metadata;

    uint32_t has_pull_source;
    apta_pcm_source_t pull_source;

    uint32_t has_focus;
    apta_focus_t focus;

    uint32_t end_of_input_signalled;
    apta_source_frame_t final_end_frame;

    uint32_t next_request_id;
    uint64_t next_scheduler_enqueue_serial;
    apta_internal_request_t requests[APTA_INTERNAL_MAX_REGION_REQUESTS];

    apta_internal_pcm_node_t *pcm_head;
    apta_internal_pcm_node_t *pcm_tail;
    uint64_t queued_pcm_frames;

    apta_internal_range_t *accepted_ranges;
    uint32_t accepted_range_count;
    uint32_t accepted_range_capacity;
    apta_source_frame_t greatest_accepted_end;
    apta_source_frame_t maximum_accepted_end;

    apta_internal_waveform_accumulator_t *overview_accumulators;
    uint32_t overview_accumulator_count;
    uint32_t overview_accumulator_capacity;
    uint32_t overview_complete_count;
    uint32_t overview_frames_per_column;

    /* C1: three-band split for the overview.
     *
     * The per-column sums live in a parallel array rather than inside
     * apta_internal_waveform_accumulator_t so that a session which did not ask
     * for bands pays nothing: the accumulator array is the dominant workspace
     * term, and three more uint32 per entry would grow it by half.
     * APTA_INTERNAL_BAND_COUNT entries per column, low then mid then high.
     *
     * The filter is two one-pole low-passes: low is the 200 Hz output, mid is
     * the difference between the 2 kHz and 200 Hz outputs, high is what is
     * left. Two multiply-adds per sample, no double. State is carried in the
     * session, which is itself the workspace base when one is configured. */
    uint32_t *overview_band_sums;
    apta_internal_band_filter_t overview_band_filter;
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    apta_internal_band_filter_t onset_band_filter;
    apta_source_frame_t onset_band_next_frame;
    uint8_t onset_band_filter_valid;
    uint8_t onset_band_reserved8[7];
#endif

    apta_internal_detail_tile_t detail_tiles[APTA_INTERNAL_MAX_DETAIL_TILES];
    uint64_t detail_access_serial;
    uint64_t detail_mutation_serial;
    uint64_t detail_published_serial;

    apta_internal_onset_bin_t *onset_bins;
    uint32_t onset_bin_capacity;
    /* A1: precomputed onset flux for the evidence range of the last refresh,
     * indexed linearly as flux[bin_index - evidence_first]. Unlike onset_bins
     * this is not a ring: apta_s4_find_evidence() returns a contiguous run of
     * at most onset_bin_capacity bins, so linear offsets always fit, and
     * avoiding the modulo keeps a hardware divide out of the lag loop. */
    float *onset_flux;
    uint32_t onset_flux_capacity;
    /* Incremental longest contiguous run of complete onset bins. Sequential
     * input extends this in O(1), including ordinary ring wrap. Gaps or an
     * unexpected overwrite mark it dirty and the next refresh rebuilds it with
     * the conservative full scan. End-of-input also rebuilds once because the
     * final partial bin becomes complete only when its true length is known. */
    uint64_t s4_evidence_first;
    uint64_t s4_evidence_end;
    uint32_t s4_evidence_valid;
    uint32_t s4_evidence_dirty;
    /* A2: evidence_end of the last refresh that actually ran the
     * autocorrelation, and the estimate it produced. A gated refresh reloads
     * the estimate and recomputes everything derived from the current ranges,
     * so focus movement keeps republishing while the expensive loops are
     * skipped. Zero before the first estimate. */
    uint64_t s4_refreshed_evidence_end;
    float s4_cached_scores[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    uint32_t s4_cached_lags[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    uint32_t s4_cached_phase;
    /* B1: the octave-family ambiguity that produced the cached estimate. It is
     * cached rather than recomputed because the family scan reads onset_flux,
     * which a gated pass has not refilled: the array is indexed from the
     * evidence start of the refresh that filled it, and that start moves once
     * the track is longer than the onset ring. */
    float s4_cached_ambiguity;
    /* Contrast between novelty on the predicted beats and novelty between
     * them. Cached with the estimate for the same reason as the ambiguity:
     * a gated pass has not refilled the flux array it is computed from. */
    float s4_cached_grid_fit;
    /* Sub-bin position of each candidate's correlation peak, in bins, within
     * [-0.5, 0.5] of the integer lag beside it. The lag search is an integer
     * argmax, so without this the reachable tempi are fixed by the bin size:
     * 1.6 BPM apart near 128, which is enough for the published grid to slip
     * half a beat inside two minutes. Cached with the estimate for the same
     * reason as the ambiguity and the grid fit. */
    float s4_cached_lag_offsets[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    /* Phase 7 cooperative refresh. The flux array is the immutable evidence
     * snapshot for this generation; these fields retain the ordered argmax
     * state while small lag batches are processed across process() calls.
     * Published/cached state is not changed until the generation commits. */
    uint64_t s4_refresh_evidence_first;
    uint64_t s4_refresh_evidence_end;
    float s4_refresh_best_scores[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    uint32_t s4_refresh_best_lags[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    uint32_t s4_refresh_minimum_lag;
    uint32_t s4_refresh_maximum_lag;
    uint32_t s4_refresh_next_lag;
    uint8_t s4_refresh_active;
    uint8_t s4_refresh_pending;
    uint8_t s4_refresh_reserved8[2];
    /* The global estimator's most recent nominal tempo, or zero before it has
     * one. S6 runs after S4 within a process call -- the layering is waveform,
     * then S4, then S6 -- so S4 reads the previous generation's value. Analysis
     * is progressive and S6 settles well before the end of a track, so the
     * published result converges on an endorsement from a current estimate. */
    uint32_t s6_nominal_tempo_millibpm;
    uint32_t tempo_candidate_count;
    apta_tempo_value_t tempo_value;
    apta_tempo_candidate_t tempo_candidates[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    apta_grid_segment_t local_grid_segment;
    apta_frame_range_t local_grid_requested_range;
    apta_frame_range_t local_grid_evidence_range;
    apta_frame_range_t local_grid_applicability_range;
    apta_frame_range_t local_grid_coverage_range;
    uint32_t has_tempo;
    uint32_t has_local_grid;
    uint32_t local_grid_locked;
    uint32_t tempo_candidate_set_id;
    uint32_t local_grid_segment_id;
    uint64_t s4_mutation_serial;
    uint64_t s4_published_serial;
#ifdef APTA_INTERNAL_PROFILE_S4
    apta_internal_s4_profile_t s4_profile;
#endif

    apta_internal_s6_session_state_t *s6;
};

int apta_internal_api_version_is_compatible(uint32_t api_version);

int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version);

int apta_internal_source_fingerprint_is_valid(
    apta_source_fingerprint_kind_t kind,
    const uint8_t fingerprint[APTA_SOURCE_FINGERPRINT_SIZE]);

int apta_internal_source_identity_is_valid(
    const apta_session_config_t *config);

int apta_internal_is_power_of_two(size_t value);

void *apta_internal_context_allocate(
    apta_context_t *context,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags);

void apta_internal_context_deallocate(
    apta_context_t *context,
    void *memory);

int apta_internal_result_allocation_bytes(
    const apta_result_t *result,
    uint64_t *allocation_bytes_out);

int apta_internal_result_allocation_fits(
    const apta_result_t *result,
    uint64_t additional_bytes,
    uint64_t maximum_allocation_bytes);

void apta_internal_result_init_absent_views(apta_result_t *result);

void apta_internal_log(
    apta_context_t *context,
    apta_log_level_t level,
    uint32_t diagnostic_code,
    const char *message);

apta_status_t apta_internal_publish_result(
    apta_session_t *session,
    apta_feature_mask_t changed_features);

void apta_internal_result_retain(apta_result_t *result);
void apta_internal_result_release(apta_result_t *result);

apta_status_t apta_internal_session_transition(
    apta_session_t *session,
    apta_session_state_t new_state);

apta_status_t apta_internal_metadata_copy_from_input(
    apta_context_t *context,
    const apta_metadata_t *input,
    apta_internal_metadata_t *metadata_out);

apta_status_t apta_internal_metadata_copy_from_view(
    apta_context_t *context,
    const apta_metadata_view_t *input,
    apta_internal_metadata_t *metadata_out);

void apta_internal_metadata_cleanup(
    apta_context_t *context,
    apta_internal_metadata_t *metadata);

int apta_internal_metadata_is_present(
    const apta_internal_metadata_t *metadata);

apta_status_t apta_internal_scheduler_register_request(
    apta_session_t *session,
    apta_internal_request_t *request);

void apta_internal_scheduler_score_request(
    const apta_internal_request_t *request,
    apta_internal_schedule_score_t *score_out);

int apta_internal_scheduler_score_better(
    const apta_internal_schedule_score_t *candidate,
    const apta_internal_schedule_score_t *current);

void apta_internal_scheduler_note_choice(
    apta_session_t *session,
    uint32_t selected_request_id);

apta_status_t apta_internal_waveform_accept_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

apta_status_t apta_internal_waveform_accept_pcm_base(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

apta_status_t apta_internal_waveform_process_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

apta_status_t apta_internal_waveform_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_waveform_build_overview_snapshot(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_detail_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample);

void apta_internal_detail_refresh_completed(apta_session_t *session);
void apta_internal_detail_update_request_states(apta_session_t *session);

int apta_internal_detail_range_complete(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame);

int apta_internal_detail_range_has_output(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame);

apta_status_t apta_internal_detail_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_s4_prepare(apta_session_t *session);
int apta_internal_overview_resolution_is_valid(
    const apta_session_config_t *config);

apta_status_t apta_internal_waveform_seed_column(
    apta_session_t *session,
    uint32_t column_index,
    float minimum,
    float maximum,
    uint64_t sum_squares,
    uint32_t sample_count,
    int clipped);

void apta_internal_waveform_init_bands(apta_session_t *session);

apta_status_t apta_internal_waveform_grow_band_sums(
    apta_session_t *session,
    uint32_t capacity);

apta_status_t apta_internal_s4_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    const float bands[APTA_INTERNAL_BAND_COUNT]);
#else
    float sample);
#endif
apta_status_t apta_internal_s4_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out);
int apta_internal_s4_refresh_pending(const apta_session_t *session);
apta_feature_mask_t apta_internal_s4_pending_features(
    const apta_session_t *session);
void apta_internal_s4_mark_published(apta_session_t *session);
apta_status_t apta_internal_s4_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);
void apta_internal_s4_cleanup_session(apta_session_t *session);
void apta_internal_s4_cleanup_result(apta_result_t *result);

apta_status_t apta_internal_s6_prepare(apta_session_t *session);
apta_status_t apta_internal_s6_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample);
apta_status_t apta_internal_s6_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out);
int apta_internal_s6_refresh_pending(const apta_session_t *session);
int apta_internal_analysis_pending(const apta_session_t *session);
apta_feature_mask_t apta_internal_s6_pending_features(
    const apta_session_t *session);
void apta_internal_s6_mark_published(apta_session_t *session);
apta_status_t apta_internal_s6_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);
void apta_internal_s6_cleanup_session(apta_session_t *session);
void apta_internal_s6_cleanup_result(apta_result_t *result);

void apta_internal_waveform_cleanup_session(apta_session_t *session);
void apta_internal_waveform_cleanup_result(apta_result_t *result);
void apta_internal_waveform_cleanup_result_base(apta_result_t *result);

uint32_t apta_internal_crc32c(const uint8_t *data, size_t size);

#endif /* APTA_INTERNAL_H */
