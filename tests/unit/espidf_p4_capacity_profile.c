// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#include "../../src/core/apta_result_pool_layout.h"

#define P4_SAMPLE_RATE 48000u
#define P4_TRACK_SECONDS 1800u
#define P4_TOTAL_FRAMES ((apta_source_frame_t)P4_SAMPLE_RATE * P4_TRACK_SECONDS)
#define P4_OVERVIEW_FRAMES_PER_COLUMN 32768u
#define P4_OVERVIEW_COLUMN_BUDGET 4096u
#define P4_EXPLICIT_BEAT_RECORD_BUDGET 9216u
#define P4_WORKSPACE_BUDGET_BYTES (1536u * 1024u)
#define P4_RESULT_POOL_BUDGET_BYTES (2048u * 1024u)
#define P4_COMBINED_BUDGET_BYTES \
    (P4_WORKSPACE_BUDGET_BYTES + P4_RESULT_POOL_BUDGET_BYTES)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    apta_session_config_t config;
    apta_memory_requirements_t workspace;
    apta_memory_requirements_t pool;
    apta_internal_result_pool_layout_t layout;
    uint64_t explicit_beat_records;
    uint64_t combined_minimum;

    apta_session_config_init(&config);
    config.input_mode = APTA_INPUT_MODE_PUSH;
    config.source_sample_rate = P4_SAMPLE_RATE;
    config.channel_count = 2u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    config.total_frames = P4_TOTAL_FRAMES;
    config.overview_frames_per_column = P4_OVERVIEW_FRAMES_PER_COLUMN;
    config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL |
        APTA_FEATURE_WAVEFORM_3BAND |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_CALIBRATED_QUALITY |
        APTA_FEATURE_GRID_LOCKING |
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT;
    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&workspace);
    CHECK(apta_query_workspace_requirements(&config, &workspace) ==
          APTA_STATUS_OK);

    apta_memory_requirements_init(&pool);
    CHECK(apta_query_memory_requirements(&config, &pool) == APTA_STATUS_OK);
    CHECK((pool.flags & APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL) != 0u);

    CHECK(apta_internal_result_pool_calculate_layout(&config, &layout) ==
          APTA_STATUS_OK);

    /* 30 minutes at the coarsest useful power-of-two overview resolution must
     * remain under the 4,096-column DJ profile ceiling. */
    CHECK(layout.overview_column_capacity <= P4_OVERVIEW_COLUMN_BUDGET);
    CHECK(layout.overview_column_capacity > 0u);

    /* One mutable S6 beat store plus two immutable bounded-result slots:
     * 3 * 3,072 = 9,216 explicit beat records resident at worst case. */
    CHECK(layout.global_grid_beat_capacity == APTA_INTERNAL_GLOBAL_MAX_BEATS);
    CHECK(layout.slot_count == APTA_INTERNAL_RESULT_SLOT_COUNT);
    explicit_beat_records =
        (uint64_t)APTA_INTERNAL_GLOBAL_MAX_BEATS *
        ((uint64_t)layout.slot_count + 1u);
    CHECK(explicit_beat_records == P4_EXPLICIT_BEAT_RECORD_BUDGET);

    CHECK(workspace.minimum_bytes <= P4_WORKSPACE_BUDGET_BYTES);
    CHECK(workspace.recommended_bytes <= P4_WORKSPACE_BUDGET_BYTES);
    CHECK(pool.minimum_bytes <= P4_RESULT_POOL_BUDGET_BYTES);
    CHECK(pool.recommended_bytes <= P4_RESULT_POOL_BUDGET_BYTES);
    combined_minimum =
        (uint64_t)workspace.minimum_bytes + (uint64_t)pool.minimum_bytes;
    CHECK(combined_minimum <= P4_COMBINED_BUDGET_BYTES);

    printf(
        "APTA_P4_CAPACITY seconds=%u total_frames=%llu overview_fpc=%u "
        "overview_columns=%u mutable_beats=%u result_slots=%u "
        "resident_beat_records=%llu workspace_min=%zu workspace_rec=%zu "
        "pool_min=%zu pool_rec=%zu combined_min=%llu\n",
        P4_TRACK_SECONDS,
        (unsigned long long)P4_TOTAL_FRAMES,
        P4_OVERVIEW_FRAMES_PER_COLUMN,
        layout.overview_column_capacity,
        APTA_INTERNAL_GLOBAL_MAX_BEATS,
        layout.slot_count,
        (unsigned long long)explicit_beat_records,
        workspace.minimum_bytes,
        workspace.recommended_bytes,
        pool.minimum_bytes,
        pool.recommended_bytes,
        (unsigned long long)combined_minimum);

    return 0;
}
