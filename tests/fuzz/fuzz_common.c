// SPDX-License-Identifier: Apache-2.0
#include "fuzz_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define APTA_FUZZ_MAX_CONTAINER_BYTES UINT64_C(1048576)
#define APTA_FUZZ_MAX_ALLOCATION_BYTES UINT64_C(2097152)
#define APTA_FUZZ_CONTEXT_MEMORY_BYTES UINT64_C(4194304)
#define APTA_FUZZ_MAX_DIAGNOSTICS 8u
#define APTA_FUZZ_PCM_FRAMES 128u

static apta_feature_mask_t apta_fuzz_all_features(void)
{
    return APTA_FEATURE_WAVEFORM_OVERVIEW |
           APTA_FEATURE_WAVEFORM_DETAIL |
           APTA_FEATURE_WAVEFORM_3BAND |
           APTA_FEATURE_BPM |
           APTA_FEATURE_LOCAL_BEATGRID |
           APTA_FEATURE_GLOBAL_BEATGRID |
           APTA_FEATURE_DYNAMIC_TEMPO |
           APTA_FEATURE_CONFIDENCE |
           APTA_FEATURE_GRID_LOCKING;
}

static uint32_t apta_fuzz_u32(
    const uint8_t *data,
    size_t size,
    size_t offset,
    uint32_t fallback)
{
    if (data == NULL || offset > size || size - offset < 4u) {
        return fallback;
    }
    return (uint32_t)data[offset] |
           ((uint32_t)data[offset + 1u] << 8u) |
           ((uint32_t)data[offset + 2u] << 16u) |
           ((uint32_t)data[offset + 3u] << 24u);
}

static uint64_t apta_fuzz_u64(
    const uint8_t *data,
    size_t size,
    size_t offset,
    uint64_t fallback)
{
    uint64_t low;
    uint64_t high;
    if (data == NULL || offset > size || size - offset < 8u) {
        return fallback;
    }
    low = apta_fuzz_u32(data, size, offset, 0u);
    high = apta_fuzz_u32(data, size, offset + 4u, 0u);
    return low | (high << 32u);
}

static apta_context_t *apta_fuzz_create_context(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;

    apta_context_config_init(&config);
    config.requested_capabilities = apta_fuzz_all_features();
    config.memory_limit_bytes = APTA_FUZZ_CONTEXT_MEMORY_BYTES;
    if (apta_context_create(&config, &context) != APTA_STATUS_OK) {
        return NULL;
    }
    return context;
}

static void apta_fuzz_touch_common(const apta_result_t *result)
{
    apta_result_info_t info;
    apta_source_info_t source;
    uint32_t index;
    uint32_t count;

    apta_result_info_init(&info);
    (void)apta_result_get_info(result, &info);
    apta_source_info_init(&source);
    (void)apta_result_get_source_info(result, &source);

    count = apta_result_get_diagnostic_count(result);
    if (count > APTA_FUZZ_MAX_DIAGNOSTICS) {
        count = APTA_FUZZ_MAX_DIAGNOSTICS;
    }
    for (index = 0u; index < count; ++index) {
        apta_diagnostic_view_t diagnostic;
        apta_diagnostic_view_init(&diagnostic);
        (void)apta_result_get_diagnostic(result, index, &diagnostic);
    }
}

static void apta_fuzz_touch_metadata(const apta_result_t *result)
{
    apta_metadata_view_t view;
    apta_metadata_view_init(&view);
    (void)apta_result_get_metadata(result, &view);
}

static void apta_fuzz_touch_waveform(const apta_result_t *result)
{
    apta_waveform_overview_view_t overview;
    apta_waveform_tile_view_t tile;

    apta_waveform_overview_view_init(&overview);
    (void)apta_result_get_waveform_overview(result, 0u, &overview);
    apta_waveform_tile_view_init(&tile);
    (void)apta_result_get_waveform_tile(result, 1u, 0u, &tile);
}

static void apta_fuzz_touch_tempo_local(const apta_result_t *result)
{
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;

    apta_tempo_view_init(&tempo);
    (void)apta_result_get_tempo(result, NULL, &tempo);
    apta_grid_view_init(&grid);
    (void)apta_result_get_beatgrid(
        result,
        APTA_FEATURE_LOCAL_BEATGRID,
        NULL,
        &grid);
}

static void apta_fuzz_touch_global_revision(const apta_result_t *result)
{
    apta_grid_view_t grid;
    apta_grid_revision_view_t revision;

    apta_grid_view_init(&grid);
    (void)apta_result_get_beatgrid(
        result,
        APTA_FEATURE_GLOBAL_BEATGRID,
        NULL,
        &grid);
    apta_grid_revision_view_init(&revision);
    (void)apta_result_get_grid_revision(result, &revision);
}

static void apta_fuzz_roundtrip(
    apta_context_t *context,
    const apta_result_t *result)
{
    apta_serialize_options_t options;
    uint64_t required = 0u;
    uint8_t *encoded = NULL;
    size_t written = 0u;
    const apta_result_t *reparsed = NULL;
    apta_parse_options_t parse_options;

    apta_serialize_options_init(&options);
    options.flags = APTA_SERIALIZE_CANONICAL;
    options.maximum_output_bytes = APTA_FUZZ_MAX_CONTAINER_BYTES;
    if (apta_result_query_serialized_size(
            result,
            &options,
            &required) != APTA_STATUS_OK ||
        required == 0u ||
        required > APTA_FUZZ_MAX_CONTAINER_BYTES ||
        required > SIZE_MAX) {
        return;
    }

    encoded = (uint8_t *)malloc((size_t)required);
    if (encoded == NULL) {
        return;
    }
    if (apta_result_serialize(
            result,
            &options,
            encoded,
            (size_t)required,
            &written) == APTA_STATUS_OK &&
        written == (size_t)required) {
        apta_parse_options_init(&parse_options);
        parse_options.flags = APTA_PARSE_STRICT;
        parse_options.maximum_file_bytes = APTA_FUZZ_MAX_CONTAINER_BYTES;
        parse_options.maximum_section_count = 64u;
        parse_options.maximum_overview_spans = 4096u;
        parse_options.maximum_waveform_columns = 65536u;
        parse_options.maximum_allocation_bytes =
            APTA_FUZZ_MAX_ALLOCATION_BYTES;
        (void)apta_result_parse(
            context,
            &parse_options,
            encoded,
            written,
            &reparsed);
    }
    if (reparsed != NULL) {
        apta_result_release(reparsed);
    }
    free(encoded);
}

static void apta_fuzz_touch_result(
    apta_context_t *context,
    const apta_result_t *result,
    apta_fuzz_parser_mode_t mode)
{
    apta_fuzz_touch_common(result);

    switch (mode) {
    case APTA_FUZZ_METADATA:
        apta_fuzz_touch_metadata(result);
        break;
    case APTA_FUZZ_WAVEFORM:
        apta_fuzz_touch_waveform(result);
        break;
    case APTA_FUZZ_TEMP_LGRD:
        apta_fuzz_touch_tempo_local(result);
        break;
    case APTA_FUZZ_GGRD_REVN:
        apta_fuzz_touch_global_revision(result);
        break;
    case APTA_FUZZ_ROUNDTRIP:
        apta_fuzz_touch_metadata(result);
        apta_fuzz_touch_waveform(result);
        apta_fuzz_touch_tempo_local(result);
        apta_fuzz_touch_global_revision(result);
        apta_fuzz_roundtrip(context, result);
        break;
    case APTA_FUZZ_CONTAINER:
        apta_fuzz_touch_metadata(result);
        apta_fuzz_touch_waveform(result);
        apta_fuzz_touch_tempo_local(result);
        apta_fuzz_touch_global_revision(result);
        break;
    case APTA_FUZZ_HEADER_DIRECTORY:
    default:
        break;
    }
}

int apta_fuzz_parser_input(
    const uint8_t *data,
    size_t size,
    apta_fuzz_parser_mode_t mode)
{
    apta_context_t *context;
    apta_parse_options_t options;
    const apta_result_t *result = NULL;
    unsigned int pass;

    if (size > APTA_FUZZ_MAX_CONTAINER_BYTES) {
        return 0;
    }

    context = apta_fuzz_create_context();
    if (context == NULL) {
        return 0;
    }

    for (pass = 0u; pass < 2u; ++pass) {
        apta_parse_options_init(&options);
        options.flags = pass == 0u ? APTA_PARSE_STRICT : 0u;
        options.maximum_file_bytes = APTA_FUZZ_MAX_CONTAINER_BYTES;
        options.maximum_section_count =
            mode == APTA_FUZZ_HEADER_DIRECTORY ? 32u : 64u;
        options.maximum_overview_spans = 4096u;
        options.maximum_waveform_columns = 65536u;
        options.maximum_allocation_bytes =
            APTA_FUZZ_MAX_ALLOCATION_BYTES;

        result = NULL;
        (void)apta_result_parse(
            context,
            &options,
            data,
            size,
            &result);
        if (result != NULL) {
            apta_fuzz_touch_result(context, result, mode);
            apta_result_release(result);
        }
    }

    (void)apta_context_destroy(context);
    return 0;
}

typedef union {
    uint8_t bytes[APTA_FUZZ_PCM_FRAMES * 2u * sizeof(float)];
    int16_t s16[APTA_FUZZ_PCM_FRAMES * 2u];
    int32_t s32[APTA_FUZZ_PCM_FRAMES * 2u];
    float f32[APTA_FUZZ_PCM_FRAMES * 2u];
} apta_fuzz_pcm_storage_t;

int apta_fuzz_pcm_input(const uint8_t *data, size_t size)
{
    apta_context_t *context;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_progress_t progress;
    apta_fuzz_pcm_storage_t storage;
    uint32_t frame_count;
    uint32_t accepted = 0u;
    uint32_t sample_selector;
    uint32_t channel_count;
    size_t copy_size;

    if (data == NULL || size == 0u) {
        return 0;
    }

    memset(&storage, 0, sizeof(storage));
    copy_size = size < sizeof(storage) ? size : sizeof(storage);
    memcpy(storage.bytes, data, copy_size);

    context = apta_fuzz_create_context();
    if (context == NULL) {
        return 0;
    }

    channel_count = (uint32_t)(data[0] & 1u) + 1u;
    sample_selector = size > 1u ? data[1] % 5u : 0u;
    frame_count = size > 2u
                      ? ((uint32_t)data[2] % APTA_FUZZ_PCM_FRAMES) + 1u
                      : 1u;

    apta_session_config_init(&config);
    config.input_mode = APTA_INPUT_MODE_PUSH;
    config.source_sample_rate =
        8000u + (apta_fuzz_u32(data, size, 3u, 40000u) % 184001u);
    config.channel_count = (uint16_t)channel_count;
    config.channel_layout =
        channel_count == 1u ? APTA_CHANNEL_LAYOUT_MONO
                            : APTA_CHANNEL_LAYOUT_STEREO;
    config.total_frames =
        (size > 7u && (data[7] & 1u) != 0u)
            ? APTA_TOTAL_FRAMES_UNKNOWN
            : (apta_source_frame_t)APTA_FUZZ_PCM_FRAMES;
    config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;

    switch (sample_selector) {
    case 1u:
        config.sample_format = APTA_SAMPLE_S24_3LE_INTERLEAVED;
        break;
    case 2u:
        config.sample_format = APTA_SAMPLE_S32_NATIVE_INTERLEAVED;
        break;
    case 3u:
        config.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
        break;
    case 4u:
        config.sample_format = APTA_SAMPLE_F32_NATIVE_PLANAR;
        break;
    case 0u:
    default:
        config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
        break;
    }

    if (apta_session_create(context, &config, &session) == APTA_STATUS_OK) {
        apta_pcm_block_init(&block);
        block.first_frame = apta_fuzz_u64(data, size, 8u, 0u);
        block.frame_count = frame_count;
        block.flags =
            size > 16u && (data[16] & 1u) != 0u
                ? APTA_PCM_BLOCK_FLAG_DISCONTINUITY
                : 0u;
        if (config.sample_format == APTA_SAMPLE_F32_NATIVE_PLANAR) {
            block.planes[0] = storage.f32;
            block.planes[1] =
                channel_count == 2u ? storage.f32 + APTA_FUZZ_PCM_FRAMES
                                    : NULL;
            block.data = size > 17u && (data[17] & 1u) != 0u
                             ? storage.f32
                             : NULL;
        } else {
            block.data = size > 17u && (data[17] & 1u) != 0u
                             ? NULL
                             : storage.bytes;
        }
        if (size > 18u && (data[18] & 1u) != 0u) {
            block.flags |= UINT32_C(0x80000000);
        }

        (void)apta_session_push_pcm(session, &block, &accepted);
        if (size > 19u && (data[19] & 1u) != 0u) {
            (void)apta_session_signal_end_of_input(
                session,
                apta_fuzz_u64(
                    data,
                    size,
                    20u,
                    block.first_frame + accepted));
        }

        apta_work_budget_init(&budget);
        budget.maximum_input_frames = frame_count;
        budget.maximum_steps =
            (apta_fuzz_u32(data, size, 28u, 4u) % 16u) + 1u;
        apta_progress_init(&progress);
        (void)apta_session_process(session, &budget, &progress);
        (void)apta_session_destroy(session);
    }

    (void)apta_context_destroy(context);
    return 0;
}

static void apta_fuzz_push_fixed_pcm(
    apta_session_t *session,
    apta_source_frame_t first_frame,
    uint32_t count,
    uint32_t flags)
{
    int16_t samples[64] = {0};
    apta_pcm_block_t block;
    uint32_t accepted = 0u;

    if (count > 64u) {
        count = 64u;
    }
    apta_pcm_block_init(&block);
    block.data = samples;
    block.first_frame = first_frame;
    block.frame_count = count;
    block.flags = flags;
    (void)apta_session_push_pcm(session, &block, &accepted);
}

int apta_fuzz_state_input(const uint8_t *data, size_t size)
{
    apta_context_t *context;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    uint32_t request_id = 0u;
    size_t index;

    if (data == NULL || size == 0u) {
        return 0;
    }

    context = apta_fuzz_create_context();
    if (context == NULL) {
        return 0;
    }

    apta_session_config_init(&config);
    config.input_mode = APTA_INPUT_MODE_PUSH;
    config.source_sample_rate = 48000u;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = 512u;
    config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;

    if (apta_session_create(context, &config, &session) != APTA_STATUS_OK) {
        (void)apta_context_destroy(context);
        return 0;
    }

    for (index = 0u; index < size && index < 96u; ++index) {
        const uint8_t opcode = data[index] % 11u;
        switch (opcode) {
        case 0u:
            apta_fuzz_push_fixed_pcm(
                session,
                (apta_source_frame_t)(index * 32u),
                ((uint32_t)data[index] % 64u) + 1u,
                (data[index] & 0x80u) != 0u
                    ? APTA_PCM_BLOCK_FLAG_DISCONTINUITY
                    : 0u);
            break;
        case 1u:
            (void)apta_session_signal_end_of_input(
                session,
                (apta_source_frame_t)(
                    apta_fuzz_u32(data, size, index, 512u)));
            break;
        case 2u: {
            apta_work_budget_t budget;
            apta_progress_t progress;
            apta_work_budget_init(&budget);
            budget.maximum_input_frames =
                (apta_fuzz_u32(data, size, index, 64u) % 128u) + 1u;
            budget.maximum_steps =
                (apta_fuzz_u32(data, size, index + 1u, 4u) % 16u) + 1u;
            apta_progress_init(&progress);
            (void)apta_session_process(session, &budget, &progress);
            break;
        }
        case 3u: {
            apta_focus_t focus;
            apta_focus_init(&focus);
            focus.playhead_frame =
                apta_fuzz_u64(data, size, index, (uint64_t)index);
            focus.lookbehind_frames =
                apta_fuzz_u32(data, size, index + 1u, 64u);
            focus.lookahead_frames =
                apta_fuzz_u32(data, size, index + 2u, 64u);
            focus.feature_mask = apta_fuzz_all_features();
            focus.priority = data[index];
            (void)apta_session_set_focus(session, &focus);
            break;
        }
        case 4u: {
            apta_region_request_t request;
            apta_region_request_init(&request);
            request.range.first_frame =
                apta_fuzz_u32(data, size, index, 0u) % 512u;
            request.range.end_frame =
                apta_fuzz_u32(data, size, index + 1u, 512u) % 1024u;
            request.feature_mask = apta_fuzz_all_features();
            request.priority = data[index];
            (void)apta_session_request_region(
                session,
                &request,
                &request_id);
            break;
        }
        case 5u:
            (void)apta_session_cancel_region_request(
                session,
                request_id != 0u
                    ? request_id
                    : apta_fuzz_u32(data, size, index, 1u));
            break;
        case 6u: {
            apta_request_progress_t progress;
            apta_request_progress_init(&progress);
            (void)apta_session_get_request_progress(
                session,
                request_id != 0u
                    ? request_id
                    : apta_fuzz_u32(data, size, index, 1u),
                &progress);
            break;
        }
        case 7u:
            apta_session_request_cancel(session);
            (void)apta_session_is_cancel_requested(session);
            break;
        case 8u: {
            apta_pcm_request_t request;
            apta_pcm_request_init(&request);
            (void)apta_session_next_pcm_request(session, &request);
            break;
        }
        case 9u:
            (void)apta_session_apply_grid_revision(
                session,
                apta_fuzz_u32(data, size, index, 1u));
            break;
        case 10u:
        default: {
            const apta_result_t *result =
                apta_session_acquire_result(session);
            if (result != NULL) {
                apta_fuzz_touch_common(result);
                apta_result_release(result);
            }
            (void)apta_session_get_state(session);
            break;
        }
        }
    }

    (void)apta_session_destroy(session);
    (void)apta_context_destroy(context);
    return 0;
}
