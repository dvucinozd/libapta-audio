// SPDX-License-Identifier: Apache-2.0
/* Reproducible workspace and bounded-result-pool figures for documentation. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define FULL_FEATURES                                                        \
    (APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL |         \
     APTA_FEATURE_WAVEFORM_3BAND | APTA_FEATURE_MUSICAL_KEY |                \
     APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID |                        \
     APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO |             \
     APTA_FEATURE_CONFIDENCE | APTA_FEATURE_GRID_LOCKING |                   \
     APTA_FEATURE_METER_DOWNBEAT | APTA_FEATURE_CALIBRATED_QUALITY)

typedef struct {
    const char *name;
    const char *description;
    uint32_t sample_rate;
    uint16_t channel_count;
    apta_channel_layout_t channel_layout;
    uint64_t total_frames;
    apta_feature_mask_t features;
    size_t supplied_workspace;
    uint32_t overview_frames_per_column;
} probe_row_t;

typedef struct {
    size_t workspace;
    size_t recommended_workspace;
    size_t result_pool;
    size_t alignment;
} probe_result_t;

static int query_row(const probe_row_t *row, probe_result_t *result_out)
{
    apta_session_config_t config;
    apta_memory_requirements_t workspace;
    apta_memory_requirements_t result_pool;

    if (row == NULL || result_out == NULL) {
        return 0;
    }
    apta_session_config_init(&config);
    config.input_mode = APTA_INPUT_MODE_PUSH;
    config.source_sample_rate = row->sample_rate;
    config.channel_count = row->channel_count;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = row->channel_layout;
    config.total_frames = row->total_frames;
    config.requested_features = row->features;
    config.overview_frames_per_column = row->overview_frames_per_column;
    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&workspace);
    if (apta_query_workspace_requirements(&config, &workspace) !=
        APTA_STATUS_OK) {
        return 0;
    }
    apta_memory_requirements_init(&result_pool);
    if (apta_query_memory_requirements(&config, &result_pool) !=
        APTA_STATUS_OK) {
        return 0;
    }

    result_out->workspace = workspace.minimum_bytes;
    result_out->recommended_workspace = workspace.recommended_bytes;
    result_out->result_pool = result_pool.minimum_bytes;
    result_out->alignment = workspace.required_alignment;
    return 1;
}

static void print_number(size_t value)
{
    char digits[32];
    size_t length = 0u;
    size_t position;

    do {
        digits[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && length < sizeof(digits));

    for (position = length; position > 0u; --position) {
        putchar(digits[position - 1u]);
        if (position > 1u && (position - 1u) % 3u == 0u) {
            putchar(',');
        }
    }
}

static int print_text_rows(const probe_row_t *rows, size_t count)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        probe_result_t result;

        if (!query_row(&rows[index], &result)) {
            fprintf(stderr, "query failed: %s\n", rows[index].name);
            return 0;
        }
        printf("APTA_WORKSPACE name=%s sample_rate=%u channels=%u "
               "total_frames=%llu queried_workspace=%zu "
               "recommended_workspace=%zu supplied_workspace=%zu "
               "result_pool=%zu alignment=%zu\n",
               rows[index].name,
               rows[index].sample_rate,
               (unsigned)rows[index].channel_count,
               (unsigned long long)rows[index].total_frames,
               result.workspace,
               result.recommended_workspace,
               rows[index].supplied_workspace,
               result.result_pool,
               result.alignment);
    }
    return 1;
}

static int print_markdown_rows(const probe_row_t *rows, size_t count)
{
    size_t index;

    puts("| Profile | Source | Queried workspace | Profile workspace | "
         "Queried result pool | Planned workspace + pool | Alignment |");
    puts("|---|---:|---:|---:|---:|---:|---:|");
    for (index = 0u; index < count; ++index) {
        probe_result_t result;

        if (!query_row(&rows[index], &result)) {
            fprintf(stderr, "query failed: %s\n", rows[index].name);
            return 0;
        }
        printf("| `%s` | %s | ", rows[index].name, rows[index].description);
        print_number(result.workspace);
        fputs(" B | ", stdout);
        if (rows[index].supplied_workspace != 0u) {
            print_number(rows[index].supplied_workspace);
            fputs(" B", stdout);
        } else {
            fputs("—", stdout);
        }
        fputs(" | ", stdout);
        print_number(result.result_pool);
        fputs(" B | ", stdout);
        print_number((rows[index].supplied_workspace != 0u
                          ? rows[index].supplied_workspace
                          : result.workspace) +
                     result.result_pool);
        fputs(" B | ", stdout);
        print_number(result.alignment);
        puts(" B |");
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const probe_row_t profiles[] = {
        {
            "WAVEFORM_8S", "8.0 s / 384,000 frames", 48000u, 1u,
            APTA_CHANNEL_LAYOUT_MONO, 384000u,
            APTA_FEATURE_WAVEFORM_OVERVIEW, 131072u, 0u
        },
        {
            "PERFORMANCE_LOCAL_6S", "6.0 s / 288,000 frames", 48000u, 1u,
            APTA_CHANNEL_LAYOUT_MONO, 288000u,
            APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM |
                APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_CONFIDENCE,
            262144u, 0u
        },
        {
            "GLOBAL_DYNAMIC_10_9S", "10.92 s / 524,288 frames", 48000u, 1u,
            APTA_CHANNEL_LAYOUT_MONO, 524288u,
            APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM |
                APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO |
                APTA_FEATURE_CONFIDENCE,
            1572864u, 0u
        }
    };
    static const probe_row_t durations[] = {
        {
            "FULL_30S", "30 s / 1,323,000 frames", 44100u, 2u,
            APTA_CHANNEL_LAYOUT_STEREO, UINT64_C(44100) * 30u,
            FULL_FEATURES, 0u, 0u
        },
        {
            "FULL_5MIN", "5 min / 13,230,000 frames", 44100u, 2u,
            APTA_CHANNEL_LAYOUT_STEREO, UINT64_C(44100) * 300u,
            FULL_FEATURES, 0u, 0u
        },
        {
            "FULL_12MIN", "12 min / 31,752,000 frames", 44100u, 2u,
            APTA_CHANNEL_LAYOUT_STEREO, UINT64_C(44100) * 720u,
            FULL_FEATURES, 0u, 0u
        },
        {
            "FULL_P4_30MIN", "30 min / 86,400,000 frames", 48000u, 2u,
            APTA_CHANNEL_LAYOUT_STEREO, UINT64_C(48000) * 1800u,
            FULL_FEATURES, 0u, 32768u
        }
    };
    int markdown = 0;

    if (argc == 2 && strcmp(argv[1], "--markdown") == 0) {
        markdown = 1;
    } else if (argc != 1) {
        fprintf(stderr, "usage: apta-workspace-probe [--markdown]\n");
        return 2;
    }

    if (markdown) {
        puts("Published ESP-IDF profiles:\n");
        if (!print_markdown_rows(
                profiles, sizeof(profiles) / sizeof(profiles[0]))) {
            return 1;
        }
        puts("\nFull-feature duration sweep:\n");
        if (!print_markdown_rows(
                durations, sizeof(durations) / sizeof(durations[0]))) {
            return 1;
        }
    } else {
        if (!print_text_rows(profiles, sizeof(profiles) / sizeof(profiles[0])) ||
            !print_text_rows(durations, sizeof(durations) / sizeof(durations[0]))) {
            return 1;
        }
    }
    return 0;
}
