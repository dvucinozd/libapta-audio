// SPDX-License-Identifier: Apache-2.0
#include "sdkconfig.h"

#if CONFIG_APTA_P4_HARDWARE_EVIDENCE

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <apta/apta.h>
#include <apta/apta_espidf.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb_device_uac.h"

#define APTA_EVIDENCE_SAMPLE_RATE 48000u
#define APTA_EVIDENCE_DURATION_SECONDS 1800u
#define APTA_EVIDENCE_TOTAL_FRAMES \
    ((uint64_t)APTA_EVIDENCE_SAMPLE_RATE * APTA_EVIDENCE_DURATION_SECONDS)
#define APTA_EVIDENCE_BLOCK_FRAMES 1024u
#define APTA_EVIDENCE_QUEUE_BLOCKS 16u
#define APTA_EVIDENCE_DEADLINE_US 21334u
#define APTA_EVIDENCE_TIMING_BUCKET_US 100u
#define APTA_EVIDENCE_TIMING_BUCKETS 2048u
#define APTA_EVIDENCE_STREAM_TIMEOUT_MS 250u
#define APTA_EVIDENCE_PROGRESS_SECONDS 60u

static const char *TAG = "apta-wp8";

typedef struct {
    uint32_t frame_count;
    int16_t samples[APTA_EVIDENCE_BLOCK_FRAMES];
} audio_block_t;

typedef struct {
    uint64_t call_count;
    uint64_t total_us;
    uint64_t maximum_us;
    uint64_t deadline_miss_count;
    uint32_t timing_histogram[APTA_EVIDENCE_TIMING_BUCKETS];
} process_stats_t;

typedef struct {
    portMUX_TYPE lock;
    uint32_t active_callbacks;
    uint64_t enqueued_frames;
    uint64_t input_bytes;
    uint64_t input_callbacks;
    uint64_t input_drop_count;
    uint64_t allocation_failure_count;
    int64_t first_audio_us;
    int64_t last_audio_us;
    bool stream_started;
    bool input_closed;
    bool failed;
} harness_state_t;

static StaticQueue_t audio_queue_control;
static uint8_t audio_queue_storage[
    APTA_EVIDENCE_QUEUE_BLOCKS * sizeof(audio_block_t)];
static QueueHandle_t audio_queue;
static audio_block_t callback_block;
static harness_state_t harness = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};
static process_stats_t process_stats;

static apta_feature_mask_t evidence_features(void)
{
    return APTA_FEATURE_WAVEFORM_OVERVIEW |
           APTA_FEATURE_WAVEFORM_DETAIL |
           APTA_FEATURE_WAVEFORM_3BAND |
           APTA_FEATURE_BPM |
           APTA_FEATURE_LOCAL_BEATGRID |
           APTA_FEATURE_GLOBAL_BEATGRID |
           APTA_FEATURE_DYNAMIC_TEMPO |
           APTA_FEATURE_CONFIDENCE |
           APTA_FEATURE_GRID_LOCKING |
           APTA_FEATURE_MUSICAL_KEY |
           APTA_FEATURE_METER_DOWNBEAT |
           APTA_FEATURE_CALIBRATED_QUALITY;
}

static void fail_input(void)
{
    portENTER_CRITICAL(&harness.lock);
    harness.input_drop_count += 1u;
    harness.failed = true;
    portEXIT_CRITICAL(&harness.lock);
}

static esp_err_t usb_audio_output(
    uint8_t *data,
    size_t length,
    void *context)
{
    uint64_t remaining;
    uint32_t frame_count;
    int64_t now;
    bool closed;
    (void)context;

    if (data == NULL || length == 0u ||
        (length % sizeof(int16_t)) != 0u) {
        fail_input();
        return ESP_FAIL;
    }
    frame_count = (uint32_t)(length / sizeof(int16_t));
    if (frame_count > APTA_EVIDENCE_BLOCK_FRAMES) {
        fail_input();
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&harness.lock);
    closed = harness.input_closed;
    if (!closed) {
        harness.active_callbacks += 1u;
    }
    remaining = APTA_EVIDENCE_TOTAL_FRAMES - harness.enqueued_frames;
    portEXIT_CRITICAL(&harness.lock);
    if (closed) {
        return ESP_OK;
    }
    if (remaining == 0u) {
        portENTER_CRITICAL(&harness.lock);
        harness.active_callbacks -= 1u;
        portEXIT_CRITICAL(&harness.lock);
        return ESP_OK;
    }
    if ((uint64_t)frame_count > remaining) {
        frame_count = (uint32_t)remaining;
    }

    callback_block.frame_count = frame_count;
    memcpy(callback_block.samples, data, frame_count * sizeof(int16_t));
    if (xQueueSend(audio_queue, &callback_block, 0u) != pdPASS) {
        portENTER_CRITICAL(&harness.lock);
        harness.input_drop_count += 1u;
        harness.failed = true;
        harness.active_callbacks -= 1u;
        portEXIT_CRITICAL(&harness.lock);
        return ESP_FAIL;
    }

    now = esp_timer_get_time();
    portENTER_CRITICAL(&harness.lock);
    if (!harness.stream_started) {
        harness.first_audio_us = now;
        harness.stream_started = true;
    }
    harness.last_audio_us = now;
    harness.enqueued_frames += frame_count;
    harness.input_bytes += (uint64_t)frame_count * sizeof(int16_t);
    harness.input_callbacks += 1u;
    if (harness.enqueued_frames == APTA_EVIDENCE_TOTAL_FRAMES) {
        harness.input_closed = true;
    }
    harness.active_callbacks -= 1u;
    portEXIT_CRITICAL(&harness.lock);
    return ESP_OK;
}

static uint32_t process_p99_upper_us(const process_stats_t *stats)
{
    const uint64_t target = (stats->call_count * 99u + 99u) / 100u;
    uint64_t cumulative = 0u;
    uint32_t index;

    if (stats->call_count == 0u) {
        return 0u;
    }

    for (index = 0u; index < APTA_EVIDENCE_TIMING_BUCKETS; ++index) {
        cumulative += stats->timing_histogram[index];
        if (cumulative >= target) {
            return (index + 1u) * APTA_EVIDENCE_TIMING_BUCKET_US;
        }
    }
    return APTA_EVIDENCE_TIMING_BUCKETS *
           APTA_EVIDENCE_TIMING_BUCKET_US;
}

static apta_status_t process_once(
    apta_session_t *session,
    const apta_work_budget_t *budget)
{
    const int64_t before = esp_timer_get_time();
    const apta_status_t status =
        apta_session_process(session, budget, NULL);
    const int64_t after = esp_timer_get_time();
    const uint64_t elapsed =
        after > before ? (uint64_t)(after - before) : 0u;
    uint64_t bucket = elapsed / APTA_EVIDENCE_TIMING_BUCKET_US;

    process_stats.call_count += 1u;
    process_stats.total_us += elapsed;
    if (elapsed > process_stats.maximum_us) {
        process_stats.maximum_us = elapsed;
    }
    if (elapsed > APTA_EVIDENCE_DEADLINE_US) {
        process_stats.deadline_miss_count += 1u;
    }
    if (bucket >= APTA_EVIDENCE_TIMING_BUCKETS) {
        bucket = APTA_EVIDENCE_TIMING_BUCKETS - 1u;
    }
    process_stats.timing_histogram[bucket] += 1u;
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        portENTER_CRITICAL(&harness.lock);
        harness.allocation_failure_count += 1u;
        harness.failed = true;
        portEXIT_CRITICAL(&harness.lock);
    }
    return status;
}

static uint32_t count_overview_columns(const apta_result_t *result)
{
    apta_waveform_overview_view_t overview;
    uint32_t columns = 0u;
    uint32_t index;

    apta_waveform_overview_view_init(&overview);
    if (apta_result_get_waveform_overview(result, 0u, &overview) < 0) {
        return 0u;
    }
    for (index = 0u; index < overview.span_count; ++index) {
        columns += overview.spans[index].column_count;
    }
    return columns;
}

static uint32_t count_resident_beats(const apta_result_t *result)
{
    apta_grid_view_t grid;

    apta_grid_view_init(&grid);
    if (apta_result_get_beatgrid(
            result, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &grid) < 0) {
        return 0u;
    }
    return grid.beat_count;
}

void app_main(void)
{
    apta_espidf_port_t port;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_memory_requirements_t workspace_requirement;
    apta_memory_requirements_t pool_requirement;
    apta_work_budget_t budget;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    void *workspace = NULL;
    uint64_t first_frame = 0u;
    uint64_t next_progress_frame =
        (uint64_t)APTA_EVIDENCE_PROGRESS_SECONDS *
        APTA_EVIDENCE_SAMPLE_RATE;
    uint32_t overview_columns = 0u;
    uint32_t resident_beats = 0u;
    size_t internal_before = 0u;
    size_t internal_minimum = 0u;
    size_t internal_after = 0u;
    size_t psram_before = 0u;
    size_t psram_minimum = 0u;
    size_t psram_after = 0u;
    uint64_t input_frames = 0u;
    uint64_t input_bytes = 0u;
    uint64_t input_callbacks = 0u;
    uint64_t input_drop_count = 0u;
    uint64_t allocation_failure_count = 0u;
    uint32_t duration_seconds = 0u;
    int64_t wall_duration_us = 0u;
    bool stream_started = false;
    bool stream_completed = false;
    bool analysis_completed = false;
    bool completed = false;
    bool monitor_started = false;
    apta_status_t status = APTA_STATUS_OK;

    memset(&process_stats, 0, sizeof(process_stats));
    audio_queue = xQueueCreateStatic(
        APTA_EVIDENCE_QUEUE_BLOCKS,
        sizeof(audio_block_t),
        audio_queue_storage,
        &audio_queue_control);
    if (audio_queue == NULL) {
        harness.allocation_failure_count += 1u;
        ESP_LOGE(TAG, "static audio queue creation failed");
        goto finish;
    }

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PUSH;
    session_config.source_sample_rate = APTA_EVIDENCE_SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = APTA_EVIDENCE_TOTAL_FRAMES;
    session_config.requested_features = evidence_features();
    session_config.overview_frames_per_column = 32768u;
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&workspace_requirement);
    apta_memory_requirements_init(&pool_requirement);
    if (apta_query_workspace_requirements(
            &session_config, &workspace_requirement) < 0 ||
        apta_query_memory_requirements(
            &session_config, &pool_requirement) < 0) {
        harness.allocation_failure_count += 1u;
        ESP_LOGE(TAG, "memory requirement query failed");
        goto finish;
    }
    workspace = heap_caps_aligned_alloc(
        workspace_requirement.required_alignment,
        workspace_requirement.minimum_bytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (workspace == NULL) {
        harness.allocation_failure_count += 1u;
        ESP_LOGE(TAG, "PSRAM workspace allocation failed");
        goto finish;
    }
    memset(workspace, 0, workspace_requirement.minimum_bytes);
    session_config.static_workspace = workspace;
    session_config.static_workspace_size = workspace_requirement.minimum_bytes;

    apta_espidf_port_init(&port);
    port.log_tag = TAG;
    port.persistent_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    port.temporary_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = evidence_features();
    if (apta_espidf_bind_context_config(&port, &context_config) < 0 ||
        apta_context_create(&context_config, &context) < 0 ||
        apta_session_create(context, &session_config, &session) < 0) {
        harness.allocation_failure_count += 1u;
        ESP_LOGE(TAG, "bounded APTA setup failed");
        goto finish;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = APTA_EVIDENCE_BLOCK_FRAMES;
    budget.maximum_steps = 8u;

    internal_before = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    psram_before = heap_caps_get_free_size(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (heap_caps_monitor_local_minimum_free_size_start() != ESP_OK) {
        harness.allocation_failure_count += 1u;
        ESP_LOGE(TAG, "local heap monitor start failed");
        goto finish;
    }
    monitor_started = true;
    {
        uac_device_config_t uac = {
            .output_cb = usb_audio_output,
            .cb_ctx = NULL,
        };
        if (uac_device_init(&uac) != ESP_OK) {
            harness.allocation_failure_count += 1u;
            ESP_LOGE(TAG, "USB UAC initialization failed");
            goto finish;
        }
    }
    ESP_LOGI(TAG,
             "ready: real UAC PCM required, frames=%" PRIu64
             " workspace=%u result_pool=%u",
             APTA_EVIDENCE_TOTAL_FRAMES,
             (unsigned)workspace_requirement.minimum_bytes,
             (unsigned)pool_requirement.minimum_bytes);

    while (first_frame < APTA_EVIDENCE_TOTAL_FRAMES) {
        audio_block_t block;
        uint32_t accepted = 0u;
        bool stream_started;
        bool failed;

        if (xQueueReceive(
                audio_queue,
                &block,
                pdMS_TO_TICKS(APTA_EVIDENCE_STREAM_TIMEOUT_MS)) != pdPASS) {
            portENTER_CRITICAL(&harness.lock);
            stream_started = harness.stream_started;
            failed = harness.failed;
            portEXIT_CRITICAL(&harness.lock);
            if (failed) {
                goto finish;
            }
            if (stream_started) {
                fail_input();
                ESP_LOGE(TAG, "USB audio stream paused or disconnected");
                goto finish;
            }
            continue;
        }
        status = apta_session_push_pcm(
            session,
            &(apta_pcm_block_t){
                .struct_size = sizeof(apta_pcm_block_t),
                .api_version = APTA_API_VERSION,
                .data = block.samples,
                .first_frame = first_frame,
                .frame_count = block.frame_count,
            },
            &accepted);
        if (status < 0 || accepted != block.frame_count) {
            fail_input();
            ESP_LOGE(TAG,
                     "APTA push failed at frame=%" PRIu64
                     " status=%" PRId32 " accepted=%u expected=%u",
                     first_frame,
                     status,
                     (unsigned)accepted,
                     (unsigned)block.frame_count);
            goto finish;
        }
        status = process_once(session, &budget);
        if (status < 0) {
            ESP_LOGE(TAG,
                     "APTA process failed at frame=%" PRIu64
                     " status=%" PRId32,
                     first_frame,
                     status);
            goto finish;
        }
        first_frame += block.frame_count;
        if (first_frame >= next_progress_frame) {
            ESP_LOGI(TAG,
                     "progress seconds=%" PRIu64
                     " input_drops=%" PRIu64
                     " deadline_misses=%" PRIu64,
                     first_frame / APTA_EVIDENCE_SAMPLE_RATE,
                     harness.input_drop_count,
                     process_stats.deadline_miss_count);
            next_progress_frame +=
                (uint64_t)APTA_EVIDENCE_PROGRESS_SECONDS *
                APTA_EVIDENCE_SAMPLE_RATE;
        }
    }

    status = apta_session_signal_end_of_input(
        session, APTA_EVIDENCE_TOTAL_FRAMES);
    if (status < 0) {
        ESP_LOGE(TAG, "end-of-input failed: status=%" PRId32, status);
        goto finish;
    }
    do {
        status = process_once(session, &budget);
        vTaskDelay(1u);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    if (status != APTA_STATUS_END_OF_INPUT) {
        ESP_LOGE(TAG, "drain failed: status=%" PRId32, status);
        goto finish;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        ESP_LOGE(TAG, "final result unavailable");
        goto finish;
    }
    overview_columns = count_overview_columns(result);
    resident_beats = count_resident_beats(result);
    if (overview_columns == 0u || resident_beats == 0u) {
        ESP_LOGE(TAG, "final result geometry unavailable");
        goto finish;
    }
    analysis_completed = true;

finish:
    portENTER_CRITICAL(&harness.lock);
    harness.input_closed = true;
    portEXIT_CRITICAL(&harness.lock);
    for (;;) {
        uint32_t active_callbacks;

        portENTER_CRITICAL(&harness.lock);
        active_callbacks = harness.active_callbacks;
        portEXIT_CRITICAL(&harness.lock);
        if (active_callbacks == 0u) {
            break;
        }
        vTaskDelay(1u);
    }
    if (monitor_started) {
        internal_minimum = heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        psram_minimum = heap_caps_get_minimum_free_size(
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        (void)heap_caps_monitor_local_minimum_free_size_stop();
    }
    internal_after = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    psram_after = heap_caps_get_free_size(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    portENTER_CRITICAL(&harness.lock);
    input_frames = harness.enqueued_frames;
    input_bytes = harness.input_bytes;
    input_callbacks = harness.input_callbacks;
    input_drop_count = harness.input_drop_count;
    allocation_failure_count = harness.allocation_failure_count;
    stream_started = harness.stream_started;
    wall_duration_us = stream_started
        ? harness.last_audio_us - harness.first_audio_us
        : 0u;
    portEXIT_CRITICAL(&harness.lock);
    duration_seconds =
        (uint32_t)(first_frame / APTA_EVIDENCE_SAMPLE_RATE);
    stream_completed = stream_started &&
                       input_frames == APTA_EVIDENCE_TOTAL_FRAMES &&
                       first_frame == APTA_EVIDENCE_TOTAL_FRAMES;
    completed = analysis_completed && stream_completed &&
                input_drop_count == 0u &&
                allocation_failure_count == 0u &&
                process_stats.deadline_miss_count == 0u;
    ESP_LOGI(TAG,
             "APTA_P4_EVIDENCE {\"sample_rate_hz\":48000,"
             "\"overview_frames_per_column\":32768,"
             "\"duration_seconds\":%u,\"wall_duration_us\":%" PRId64 ","
             "\"workspace_bytes\":%u,\"result_pool_bytes\":%u,"
             "\"overview_columns\":%u,\"resident_beat_records\":%u,"
             "\"internal_heap_free_before_bytes\":%u,"
             "\"internal_heap_min_free_bytes\":%u,"
             "\"internal_heap_free_after_bytes\":%u,"
             "\"psram_free_before_bytes\":%u,"
             "\"psram_min_free_bytes\":%u,"
             "\"psram_free_after_bytes\":%u,"
             "\"input_frames\":%" PRIu64 ",\"input_bytes\":%" PRIu64 ","
             "\"input_callbacks\":%" PRIu64 ","
             "\"processed_frames\":%" PRIu64 ","
             "\"process_calls\":%" PRIu64 ","
             "\"process_call_average_us\":%" PRIu64 ","
             "\"process_call_p99_us\":%u,"
             "\"process_call_max_us\":%" PRIu64 ","
             "\"allocation_failure_count\":%" PRIu64 ","
             "\"process_deadline_miss_count\":%" PRIu64 ","
             "\"input_drop_count\":%" PRIu64 ","
             "\"features\":[\"waveform_overview\",\"waveform_detail\","
             "\"waveform_3band\",\"bpm\",\"local_beatgrid\","
             "\"global_beatgrid\",\"dynamic_tempo\",\"confidence\","
             "\"grid_locking\",\"meter_downbeat\",\"musical_key\","
             "\"calibrated_quality\"],"
             "\"usb_stream_started\":%s,"
             "\"usb_stream_completed\":%s,"
             "\"usb_audio_coexistence_passed\":%s,"
             "\"test_completed\":%s}",
             duration_seconds,
             wall_duration_us,
             (unsigned)workspace_requirement.minimum_bytes,
             (unsigned)pool_requirement.minimum_bytes,
             (unsigned)overview_columns,
             (unsigned)resident_beats,
             (unsigned)internal_before,
             (unsigned)internal_minimum,
             (unsigned)internal_after,
             (unsigned)psram_before,
             (unsigned)psram_minimum,
             (unsigned)psram_after,
             input_frames,
             input_bytes,
             input_callbacks,
             first_frame,
             process_stats.call_count,
             process_stats.call_count != 0u
                 ? process_stats.total_us / process_stats.call_count
                 : 0u,
             process_p99_upper_us(&process_stats),
             process_stats.maximum_us,
             allocation_failure_count,
             process_stats.deadline_miss_count,
             input_drop_count,
             stream_started ? "true" : "false",
             stream_completed ? "true" : "false",
             completed ? "true" : "false",
             completed ? "true" : "false");

    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    if (context != NULL) {
        (void)apta_context_destroy(context);
    }
    heap_caps_free(workspace);
    ESP_LOGI(TAG, "WP8 harness stopped; reboot required for another run");
    vTaskDelete(NULL);
}

#endif /* CONFIG_APTA_P4_HARDWARE_EVIDENCE */
