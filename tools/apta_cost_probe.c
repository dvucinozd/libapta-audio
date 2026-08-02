// SPDX-License-Identifier: Apache-2.0
/* Per-feature CPU cost and opt-in S4 stage profiling. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <apta/apta.h>

#include "apta_internal_profile.h"

#ifndef APTA_INTERNAL_PROFILE_S4
#error "apta-cost-probe requires APTA_ENABLE_INTERNAL_PROFILING=ON"
#endif

#define BLOCK_FRAMES 1024u
#define WS_BYTES (4u * 1024u * 1024u)

typedef struct {
    const char *name;
    unsigned long calls;
    double total_ms;
    double per_call_us;
    double realtime_multiple;
    apta_internal_s4_profile_t s4;
} measurement_t;

static int16_t g_pcm[BLOCK_FRAMES * 2u];
static uint64_t g_total_frames;
static uint32_t g_beat_frames;
static void *g_workspace;

static void *host_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    (void)user_data;
    (void)alignment;
    (void)flags;
    return malloc(size);
}

static void host_deallocate(void *user_data, void *memory)
{
    (void)user_data;
    free(memory);
}

static void *host_reallocate(
    void *user_data,
    void *memory,
    size_t new_size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    (void)user_data;
    (void)alignment;
    (void)flags;
    return realloc(memory, new_size);
}

/* CPU time is sufficient for this single-threaded measurement tool and is
 * available in C11 on every supported desktop compiler. The library itself
 * only consumes the context clock callback, so ESP-IDF profiling uses the
 * port's esp_timer-backed monotonic clock instead. */
static uint64_t host_monotonic_time_ns(void *user_data)
{
    clock_t ticks = clock();
    uint64_t seconds;
    uint64_t remainder;

    (void)user_data;
    if (ticks == (clock_t)-1 || CLOCKS_PER_SEC <= 0) {
        return 0u;
    }
    seconds = (uint64_t)ticks / (uint64_t)CLOCKS_PER_SEC;
    remainder = (uint64_t)ticks % (uint64_t)CLOCKS_PER_SEC;
    return seconds * UINT64_C(1000000000) +
           remainder * UINT64_C(1000000000) /
               (uint64_t)CLOCKS_PER_SEC +
           1u;
}

static apta_status_t source_read(
    void *user_data,
    apta_source_frame_t first,
    uint32_t requested,
    apta_pcm_block_t *block_out)
{
    uint32_t index;
    uint32_t count;

    (void)user_data;
    if (first >= g_total_frames) {
        return APTA_STATUS_END_OF_INPUT;
    }
    count = requested > BLOCK_FRAMES ? BLOCK_FRAMES : requested;
    if (first + count > g_total_frames) {
        count = (uint32_t)(g_total_frames - first);
    }
    for (index = 0u; index < count; ++index) {
        uint64_t phase = (first + index) % g_beat_frames;
        int16_t value = phase < 160u ? (int16_t)28000 : (int16_t)0;

        g_pcm[index * 2u] = value;
        g_pcm[index * 2u + 1u] = value;
    }
    apta_pcm_block_init(block_out);
    block_out->data = g_pcm;
    block_out->first_frame = first;
    block_out->frame_count = count;
    return APTA_STATUS_OK;
}

static void source_release(void *user_data, apta_pcm_block_t *block)
{
    (void)user_data;
    (void)block;
}

static uint64_t source_total_frames(void *user_data)
{
    (void)user_data;
    return g_total_frames;
}

static int measure(
    const char *name,
    apta_feature_mask_t features,
    uint32_t rate,
    uint32_t seconds,
    measurement_t *measurement_out)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_pcm_source_t source;
    apta_work_budget_t budget;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_status_t status;
    unsigned long calls = 0ul;
    clock_t started_at;
    clock_t finished_at;

    if (name == NULL || measurement_out == NULL) {
        return 0;
    }
    memset(measurement_out, 0, sizeof(*measurement_out));
    measurement_out->name = name;

    g_total_frames = (uint64_t)rate * seconds;
    g_beat_frames = (uint32_t)((uint64_t)rate * 60u / 128u);

    apta_context_config_init(&context_config);
    context_config.allocator.allocate = host_allocate;
    context_config.allocator.deallocate = host_deallocate;
    context_config.allocator.reallocate = host_reallocate;
    context_config.clock.monotonic_time_ns = host_monotonic_time_ns;
    context_config.requested_capabilities = features;
    if (apta_context_create(&context_config, &context) < 0) {
        return 0;
    }

    memset(g_workspace, 0, WS_BYTES);
    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = rate;
    session_config.channel_count = 2u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    session_config.total_frames = g_total_frames;
    session_config.requested_features = features;
    session_config.static_workspace = g_workspace;
    session_config.static_workspace_size = WS_BYTES;
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    if (apta_session_create(context, &session_config, &session) < 0) {
        (void)apta_context_destroy(context);
        return 0;
    }

    apta_pcm_source_init(&source);
    source.read_frames = source_read;
    source.release_frames = source_release;
    source.get_total_frames = source_total_frames;
    if (apta_session_set_source(session, &source) < 0) {
        (void)apta_session_destroy(session);
        (void)apta_context_destroy(context);
        return 0;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 16u;

    apta_internal_s4_profile_reset(session);
    started_at = clock();
    for (;;) {
        status = apta_session_process(session, &budget, NULL);
        calls += 1ul;
        if (status == APTA_STATUS_END_OF_INPUT || status < 0) {
            break;
        }
        if (calls > 200000ul) {
            status = APTA_ERROR_LIMIT_EXCEEDED;
            break;
        }
    }
    finished_at = clock();

    measurement_out->calls = calls;
    measurement_out->total_ms =
        1000.0 * (double)(finished_at - started_at) /
        (double)CLOCKS_PER_SEC;
    measurement_out->per_call_us = calls != 0ul
        ? measurement_out->total_ms * 1000.0 / (double)calls
        : 0.0;
    measurement_out->realtime_multiple = measurement_out->total_ms > 0.0
        ? (double)seconds * 1000.0 / measurement_out->total_ms
        : 0.0;
    apta_internal_s4_profile_snapshot(session, &measurement_out->s4);

    (void)apta_session_destroy(session);
    (void)apta_context_destroy(context);
    return status >= 0 || status == APTA_STATUS_END_OF_INPUT;
}

static double ns_to_us(uint64_t nanoseconds)
{
    return (double)nanoseconds / 1000.0;
}

static void print_text(const measurement_t *measurement)
{
    const apta_internal_s4_profile_t *profile = &measurement->s4;

    printf("%-34s calls=%6lu total=%9.1f ms per_call=%9.1f us x_rt=%6.1f\n",
           measurement->name,
           measurement->calls,
           measurement->total_ms,
           measurement->per_call_us,
           measurement->realtime_multiple);
    if (profile->process_calls != 0u) {
        printf("  s4 calls=%llu scans=%llu gated=%llu bins=%llu "
               "cache_hits=%llu full_scans=%llu "
               "find=%8.1f us flux=%8.1f sweep=%9.1f refine=%8.1f "
               "family=%8.1f phase=%8.1f publish=%8.1f\n",
               (unsigned long long)profile->process_calls,
               (unsigned long long)profile->refresh_scans,
               (unsigned long long)profile->gated_calls,
               (unsigned long long)profile->evidence_bins_scanned,
               (unsigned long long)profile->evidence_cache_hits,
               (unsigned long long)profile->evidence_full_scans,
               ns_to_us(profile->find_evidence_ns),
               ns_to_us(profile->flux_ns),
               ns_to_us(profile->lag_sweep_ns),
               ns_to_us(profile->refinement_ns),
               ns_to_us(profile->family_scan_ns),
               ns_to_us(profile->phase_search_ns),
               ns_to_us(profile->publication_ns));
    }
}

static void print_json(const measurement_t *measurements, size_t count)
{
    size_t index;

    puts("[");
    for (index = 0u; index < count; ++index) {
        const measurement_t *measurement = &measurements[index];
        const apta_internal_s4_profile_t *profile = &measurement->s4;

        printf("  {\"name\":\"%s\",\"calls\":%lu,\"total_ms\":%.3f,"
               "\"per_call_us\":%.3f,\"realtime_multiple\":%.3f,"
               "\"s4\":{\"process_calls\":%llu,\"refresh_scans\":%llu,"
               "\"gated_calls\":%llu,\"evidence_bins_scanned\":%llu,"
               "\"evidence_cache_hits\":%llu,\"evidence_full_scans\":%llu,"
               "\"find_evidence_us\":%.3f,\"flux_us\":%.3f,"
               "\"lag_sweep_us\":%.3f,\"refinement_us\":%.3f,"
               "\"family_scan_us\":%.3f,\"phase_search_us\":%.3f,"
               "\"publication_us\":%.3f}}%s\n",
               measurement->name,
               measurement->calls,
               measurement->total_ms,
               measurement->per_call_us,
               measurement->realtime_multiple,
               (unsigned long long)profile->process_calls,
               (unsigned long long)profile->refresh_scans,
               (unsigned long long)profile->gated_calls,
               (unsigned long long)profile->evidence_bins_scanned,
               (unsigned long long)profile->evidence_cache_hits,
               (unsigned long long)profile->evidence_full_scans,
               ns_to_us(profile->find_evidence_ns),
               ns_to_us(profile->flux_ns),
               ns_to_us(profile->lag_sweep_ns),
               ns_to_us(profile->refinement_ns),
               ns_to_us(profile->family_scan_ns),
               ns_to_us(profile->phase_search_ns),
               ns_to_us(profile->publication_ns),
               index + 1u == count ? "" : ",");
    }
    puts("]");
}

static int parse_seconds(const char *text, uint32_t *seconds_out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || seconds_out == NULL) {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0ul || value > 3600ul) {
        return 0;
    }
    *seconds_out = (uint32_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    const apta_feature_mask_t overview = APTA_FEATURE_WAVEFORM_OVERVIEW;
    const apta_feature_mask_t overview_confidence =
        overview | APTA_FEATURE_CONFIDENCE;
    const apta_feature_mask_t bpm =
        overview_confidence | APTA_FEATURE_BPM;
    const apta_feature_mask_t local = bpm | APTA_FEATURE_LOCAL_BEATGRID;
    const apta_feature_mask_t global = local | APTA_FEATURE_GLOBAL_BEATGRID;
    const apta_feature_mask_t dynamic = global | APTA_FEATURE_DYNAMIC_TEMPO;
    const apta_feature_mask_t full =
        dynamic | APTA_FEATURE_WAVEFORM_DETAIL | APTA_FEATURE_GRID_LOCKING;
    const struct {
        const char *name;
        apta_feature_mask_t features;
    } rows[] = {
        {"overview", overview},
        {"overview+confidence", overview_confidence},
        {"+BPM", bpm},
        {"+local grid", local},
        {"+global grid", global},
        {"+dynamic tempo", dynamic},
        {"+detail+locking (full)", full}
    };
    measurement_t measurements[sizeof(rows) / sizeof(rows[0])];
    uint32_t seconds = 300u;
    int json = 0;
    int argument;
    size_t row;

    for (argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[argument], "--seconds") == 0 &&
                   argument + 1 < argc) {
            argument += 1;
            if (!parse_seconds(argv[argument], &seconds)) {
                fprintf(stderr, "invalid --seconds value: %s\n", argv[argument]);
                return 2;
            }
        } else {
            fprintf(stderr, "usage: apta-cost-probe [--json] [--seconds N]\n");
            return 2;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    g_workspace = malloc(WS_BYTES);
    if (g_workspace == NULL) {
        fputs("workspace allocation failed\n", stderr);
        return 1;
    }

    for (row = 0u; row < sizeof(rows) / sizeof(rows[0]); ++row) {
        if (!measure(rows[row].name,
                     rows[row].features,
                     44100u,
                     seconds,
                     &measurements[row])) {
            fprintf(stderr, "measurement failed: %s\n", rows[row].name);
            free(g_workspace);
            return 1;
        }
    }

    if (json) {
        print_json(measurements, sizeof(measurements) / sizeof(measurements[0]));
    } else {
        printf("%u s @ 44.1 kHz, %u-frame blocks\n", seconds, BLOCK_FRAMES);
        for (row = 0u; row < sizeof(measurements) / sizeof(measurements[0]); ++row) {
            print_text(&measurements[row]);
        }
    }

    free(g_workspace);
    return 0;
}
