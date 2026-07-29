// SPDX-License-Identifier: Apache-2.0
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <apta/apta.h>
#include <apta/apta_espidf.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APTA_EXAMPLE_SAMPLE_RATE 48000u
#define APTA_EXAMPLE_TOTAL_FRAMES (APTA_EXAMPLE_SAMPLE_RATE * 8u)
#define APTA_EXAMPLE_BLOCK_FRAMES 1024u
#define APTA_EXAMPLE_BEAT_FRAMES 23040u

static const char *TAG = "apta-example";
static int16_t pcm_block[APTA_EXAMPLE_BLOCK_FRAMES];

typedef struct {
    uint64_t call_count;
    uint64_t total_us;
    uint64_t maximum_us;
} process_stats_t;

static apta_status_t process_once(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    process_stats_t *stats)
{
    int64_t before = esp_timer_get_time();
    apta_status_t status = apta_session_process(session, budget, NULL);
    int64_t after = esp_timer_get_time();
    uint64_t elapsed = after > before ? (uint64_t)(after - before) : 0u;

    stats->call_count += 1u;
    stats->total_us += elapsed;
    if (elapsed > stats->maximum_us) {
        stats->maximum_us = elapsed;
    }
    return status;
}

static void generate_click_block(uint32_t first_frame, uint32_t frame_count)
{
    uint32_t index;
    for (index = 0u; index < frame_count; ++index) {
        uint32_t phase = (first_frame + index) % APTA_EXAMPLE_BEAT_FRAMES;
        pcm_block[index] = phase < 128u ? (int16_t)30000 : 0;
    }
}

void app_main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_espidf_port_t port;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_work_budget_t budget;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    process_stats_t stats = {0u, 0u, 0u};
    uint32_t first_frame = 0u;
    size_t free_before;
    size_t free_after;
    apta_status_t status = APTA_STATUS_OK;

    free_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    apta_espidf_port_init(&port);
    port.log_tag = TAG;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    if (apta_espidf_bind_context_config(&port, &context_config) < 0 ||
        apta_context_create(&context_config, &context) < 0) {
        ESP_LOGE(TAG, "context creation failed");
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PUSH;
    session_config.source_sample_rate = APTA_EXAMPLE_SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = APTA_EXAMPLE_TOTAL_FRAMES;
    session_config.requested_features = features;
    if (apta_session_create(context, &session_config, &session) < 0) {
        ESP_LOGE(TAG, "session creation failed");
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = APTA_EXAMPLE_BLOCK_FRAMES;
    budget.maximum_steps = 8u;

    while (first_frame < APTA_EXAMPLE_TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t accepted = 0u;
        uint32_t count = APTA_EXAMPLE_TOTAL_FRAMES - first_frame;
        if (count > APTA_EXAMPLE_BLOCK_FRAMES) {
            count = APTA_EXAMPLE_BLOCK_FRAMES;
        }

        generate_click_block(first_frame, count);
        apta_pcm_block_init(&block);
        block.data = pcm_block;
        block.first_frame = first_frame;
        block.frame_count = count;
        status = apta_session_push_pcm(session, &block, &accepted);
        if (status < 0 || accepted != count) {
            ESP_LOGE(TAG, "push failed: status=%" PRId32 " accepted=%" PRIu32,
                     status, accepted);
            goto cleanup;
        }

        status = process_once(session, &budget, &stats);
        if (status < 0) {
            ESP_LOGE(TAG, "process failed: status=%" PRId32, status);
            goto cleanup;
        }
        first_frame += count;
        taskYIELD();
    }

    status = apta_session_signal_end_of_input(session, APTA_EXAMPLE_TOTAL_FRAMES);
    if (status < 0) {
        ESP_LOGE(TAG, "end-of-input failed: status=%" PRId32, status);
        goto cleanup;
    }
    do {
        status = process_once(session, &budget, &stats);
        taskYIELD();
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    if (status != APTA_STATUS_END_OF_INPUT) {
        ESP_LOGE(TAG, "drain failed: status=%" PRId32, status);
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    apta_tempo_view_init(&tempo);
    apta_grid_view_init(&grid);
    if (result == NULL ||
        apta_result_get_tempo(result, NULL, &tempo) < 0 ||
        apta_result_get_beatgrid(
            result,
            APTA_FEATURE_LOCAL_BEATGRID,
            NULL,
            &grid) < 0) {
        ESP_LOGE(TAG, "final result unavailable");
        goto cleanup;
    }

    ESP_LOGI(TAG,
             "tempo=%" PRIu32 " millibpm confidence=%u grid_segments=%" PRIu32,
             tempo.selected.tempo_millibpm,
             (unsigned)tempo.selected.confidence,
             grid.segment_count);
    ESP_LOGI(TAG,
             "process_calls=%" PRIu64 " average_us=%" PRIu64 " max_us=%" PRIu64,
             stats.call_count,
             stats.call_count != 0u ? stats.total_us / stats.call_count : 0u,
             stats.maximum_us);
    ESP_LOGI(TAG,
             "port_dsp_backend=%" PRIu32,
             apta_espidf_dsp_backend());

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    if (context != NULL) {
        (void)apta_context_destroy(context);
    }

    free_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG,
             "free_heap_before=%u free_heap_after=%u delta=%d",
             (unsigned)free_before,
             (unsigned)free_after,
             (int)free_after - (int)free_before);
    vTaskDelete(NULL);
}
