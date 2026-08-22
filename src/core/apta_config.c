// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_result_pool_layout.h"
#include "apta_session_workspace.h"
#include "../beatgrid/apta_s6_internal.h"

#include <stdalign.h>
#include <string.h>

#define APTA_INIT_STRUCT(value)              \
    do {                                     \
        memset((value), 0, sizeof(*(value))); \
        (value)->struct_size = (uint32_t)sizeof(*(value)); \
        (value)->api_version = APTA_API_VERSION;            \
    } while (0)

void APTA_CALL apta_allocator_init(apta_allocator_t *allocator)
{
    if (allocator != NULL) {
        APTA_INIT_STRUCT(allocator);
    }
}

void APTA_CALL apta_logger_init(apta_logger_t *logger)
{
    if (logger != NULL) {
        APTA_INIT_STRUCT(logger);
    }
}

void APTA_CALL apta_clock_init(apta_clock_t *clock)
{
    if (clock != NULL) {
        APTA_INIT_STRUCT(clock);
    }
}

void APTA_CALL apta_context_config_init(apta_context_config_t *config)
{
    if (config != NULL) {
        APTA_INIT_STRUCT(config);
        apta_allocator_init(&config->allocator);
        apta_logger_init(&config->logger);
        apta_clock_init(&config->clock);
    }
}

void APTA_CALL apta_session_config_init(apta_session_config_t *config)
{
    if (config != NULL) {
        APTA_INIT_STRUCT(config);
        config->input_mode = APTA_INPUT_MODE_PUSH;
        config->total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    }
}

void APTA_CALL apta_pcm_block_init(apta_pcm_block_t *block)
{
    if (block != NULL) {
        APTA_INIT_STRUCT(block);
    }
}

void APTA_CALL apta_pcm_source_init(apta_pcm_source_t *source)
{
    if (source != NULL) {
        APTA_INIT_STRUCT(source);
    }
}

void APTA_CALL apta_focus_init(apta_focus_t *focus)
{
    if (focus != NULL) {
        APTA_INIT_STRUCT(focus);
        focus->priority = APTA_PRIORITY_INTERACTIVE;
    }
}

void APTA_CALL apta_region_request_init(apta_region_request_t *request)
{
    if (request != NULL) {
        APTA_INIT_STRUCT(request);
        request->priority = APTA_PRIORITY_NORMAL;
        request->range.struct_size = (uint32_t)sizeof(request->range);
        request->range.api_version = APTA_API_VERSION;
    }
}

void APTA_CALL apta_pcm_request_init(apta_pcm_request_t *request)
{
    if (request != NULL) {
        APTA_INIT_STRUCT(request);
        request->range.struct_size = (uint32_t)sizeof(request->range);
        request->range.api_version = APTA_API_VERSION;
    }
}

void APTA_CALL apta_work_budget_init(apta_work_budget_t *budget)
{
    if (budget != NULL) {
        APTA_INIT_STRUCT(budget);
    }
}

static size_t apta_memory_base_requirement(void)
{
    return sizeof(apta_session_t) + sizeof(apta_result_t);
}

static size_t apta_memory_waveform_recommendation(
    apta_feature_mask_t requested_features)
{
    const size_t result_generations = 2u * sizeof(apta_result_t);
    const size_t accepted_ranges =
        8u * sizeof(apta_internal_range_t);
    const size_t accumulators =
        16u * sizeof(apta_internal_waveform_accumulator_t);
    const size_t pcm_queue =
        sizeof(apta_internal_pcm_node_t) +
        (size_t)APTA_INTERNAL_MAX_PUSH_FRAMES * sizeof(float);
    const size_t overview_snapshot_columns =
        8u * sizeof(apta_waveform_column_t);
    const size_t overview_snapshot_spans =
        2u * sizeof(apta_waveform_span_t);
    size_t detail_snapshot = 0u;
    size_t s4_session = 0u;
    size_t s4_snapshots = 0u;
    size_t s6_session = 0u;
    size_t s6_snapshots = 0u;
    size_t meter_snapshots = 0u;
    size_t key_snapshots = 0u;

    if ((requested_features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        detail_snapshot =
            (size_t)APTA_INTERNAL_MAX_DETAIL_TILES *
                sizeof(apta_waveform_tile_view_t) +
            (size_t)APTA_INTERNAL_MAX_DETAIL_TILES *
                APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE *
                sizeof(apta_waveform_column_t);
    }
    if ((requested_features & APTA_INTERNAL_S4_FEATURES) != 0u) {
        s4_session =
            (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY *
                sizeof(apta_internal_onset_bin_t) +
            /* A1: precomputed flux array, one float per onset bin. */
            (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY * sizeof(float);
        s4_snapshots =
            2u * ((size_t)APTA_INTERNAL_MAX_TEMPO_CANDIDATES *
                      sizeof(apta_tempo_candidate_t) +
                  sizeof(apta_frame_range_t) +
                  sizeof(apta_grid_segment_t));
    }
    if ((requested_features & APTA_FEATURE_METER_DOWNBEAT) != 0u) {
        meter_snapshots = 2u * sizeof(apta_meter_segment_t);
    }
    if ((requested_features & APTA_FEATURE_MUSICAL_KEY) != 0u) {
        key_snapshots = 2u * APTA_INTERNAL_KEY_CANDIDATE_COUNT * sizeof(apta_key_candidate_t);
    }
    if ((requested_features & APTA_INTERNAL_S6_FEATURES) != 0u) {
        s6_session =
            sizeof(apta_internal_s6_session_state_t) +
            (size_t)APTA_INTERNAL_GLOBAL_BIN_CAPACITY *
                sizeof(apta_internal_onset_bin_t) +
            /* A1: precomputed flux array, one float per global bin. */
            (size_t)APTA_INTERNAL_GLOBAL_BIN_CAPACITY * sizeof(float) +
            (size_t)APTA_INTERNAL_GLOBAL_MAX_BEATS *
                sizeof(apta_beat_t);
        s6_snapshots =
            2u * (sizeof(apta_internal_s6_result_state_t) +
                  sizeof(apta_frame_range_t) +
                  (size_t)APTA_INTERNAL_GLOBAL_MAX_SEGMENTS *
                      sizeof(apta_grid_segment_t) +
                  (size_t)APTA_INTERNAL_GLOBAL_MAX_BEATS *
                      sizeof(apta_beat_t));
    }

    return sizeof(apta_session_t) +
           result_generations +
           accepted_ranges +
           accumulators +
           pcm_queue +
           overview_snapshot_columns +
           overview_snapshot_spans +
           detail_snapshot +
           s4_session +
           s4_snapshots +
           s6_session +
           s6_snapshots +
           meter_snapshots +
           key_snapshots;
}

/* C2: zero means the library default. Otherwise a power of two in range; the
 * column index is a division by this value and the result-pool layout is sized
 * from it, so a rogue value is a configuration error rather than something to
 * clamp silently.
 *
 * Shared, because session creation validates through
 * apta_session_config_is_valid() and apta_workspace_config_is_valid() while
 * the memory query validates here, and those are three separate paths a new
 * config field has to be added to. */
int apta_internal_overview_resolution_is_valid(
    const apta_session_config_t *config)
{
    if (config->overview_frames_per_column == 0u) {
        return 1;
    }
    return config->overview_frames_per_column >= 64u &&
           config->overview_frames_per_column <= 65536u &&
           apta_internal_is_power_of_two(config->overview_frames_per_column);
}

apta_status_t APTA_CALL apta_query_memory_requirements_base(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out)
{
    const apta_feature_mask_t supported_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL |
        /* C1: three-band overview waveform. */
        APTA_FEATURE_WAVEFORM_3BAND |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING |
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT;
    const apta_feature_mask_t waveform_dependency =
        APTA_FEATURE_WAVEFORM_DETAIL |
        /* C1: bands qualify the overview, so they require it, like DETAIL. */
        APTA_FEATURE_WAVEFORM_3BAND |
        /* A4: CONFIDENCE is no longer an S4 feature, but it still needs
         * something to qualify, and every feature it can qualify depends on
         * the overview. Naming it explicitly keeps CONFIDENCE-alone rejected
         * while allowing WAVEFORM_OVERVIEW | CONFIDENCE. */
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT |
        APTA_INTERNAL_S4_FEATURES |
        APTA_INTERNAL_S6_FEATURES;
    apta_internal_result_pool_layout_t pool_layout;
    apta_status_t status;

    if (config == NULL || requirements_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (!apta_internal_validate_struct(
            requirements_out,
            sizeof(*requirements_out),
            requirements_out->struct_size,
            requirements_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if ((config->flags &
         ~(APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS |
           APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING)) != 0u ||
        !apta_internal_source_identity_is_valid(config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_overview_resolution_is_valid(config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->requested_features & ~supported_features) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if ((config->requested_features & waveform_dependency) != 0u &&
        (config->requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->requested_features &
         APTA_FEATURE_LOCAL_BEATGRID) != 0u &&
        (config->requested_features & APTA_FEATURE_BPM) == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->requested_features &
         APTA_FEATURE_GLOBAL_BEATGRID) != 0u &&
        (config->requested_features & APTA_FEATURE_BPM) == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->requested_features &
         APTA_FEATURE_DYNAMIC_TEMPO) != 0u &&
        (config->requested_features &
         APTA_FEATURE_GLOBAL_BEATGRID) == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->requested_features & APTA_FEATURE_GRID_LOCKING) != 0u &&
        (config->requested_features &
         APTA_FEATURE_LOCAL_BEATGRID) == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->requested_features & APTA_FEATURE_METER_DOWNBEAT) != 0u &&
        (config->requested_features & APTA_FEATURE_LOCAL_BEATGRID) == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (config->input_mode == APTA_INPUT_MODE_PULL &&
        (config->requested_features & supported_features) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    if ((config->flags &
         APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        status = apta_internal_result_pool_calculate_layout(
            config,
            &pool_layout);
        if (status < 0) {
            return status;
        }

        requirements_out->minimum_bytes = pool_layout.total_bytes;
        requirements_out->recommended_bytes = pool_layout.total_bytes;
        requirements_out->required_alignment = APTA_INTERNAL_MAX_ALIGNMENT;
        requirements_out->flags =
            APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL;
        memset(
            requirements_out->reserved32,
            0,
            sizeof(requirements_out->reserved32));
        return APTA_STATUS_OK;
    }

    requirements_out->minimum_bytes = apta_memory_base_requirement();
    requirements_out->recommended_bytes =
        (config->requested_features & supported_features) != 0u
            ? apta_memory_waveform_recommendation(
                  config->requested_features)
            : requirements_out->minimum_bytes;
    requirements_out->required_alignment = APTA_INTERNAL_MAX_ALIGNMENT;
    requirements_out->flags = 0u;
    memset(requirements_out->reserved32, 0, sizeof(requirements_out->reserved32));

    return APTA_STATUS_OK;
}

/* A5: this is a new symbol rather than a flag on apta_query_memory_requirements
 * deliberately. That entry point is wrapped by the compile-time renaming scheme
 * in both CMakeLists.txt and ports/espidf/CMakeLists.txt; a fresh symbol with a
 * single definition needs no entry in either. */
apta_status_t APTA_CALL apta_query_workspace_requirements(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out)
{
    size_t required;

    if (config == NULL || requirements_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_internal_validate_struct(
            requirements_out,
            sizeof(*requirements_out),
            requirements_out->struct_size,
            requirements_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if ((config->flags &
         ~(APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS |
           APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING)) != 0u ||
        !apta_internal_source_identity_is_valid(config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    required = apta_internal_session_workspace_requirement(config);
    if (required == SIZE_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    requirements_out->minimum_bytes = required;
    /* Headroom for allocator slack: the block splitter refuses to split a
     * remainder smaller than a header plus prefix plus alignment, so a request
     * can strand that much. */
    requirements_out->recommended_bytes =
        required + required / 16u + APTA_INTERNAL_MAX_ALIGNMENT;
    requirements_out->required_alignment = APTA_INTERNAL_MAX_ALIGNMENT;
    requirements_out->flags = 0u;
    memset(
        requirements_out->reserved32,
        0,
        sizeof(requirements_out->reserved32));

    return APTA_STATUS_OK;
}
