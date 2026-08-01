// SPDX-License-Identifier: Apache-2.0
/* A5: apta_query_workspace_requirements() reports what a configuration needs,
 * and apta_session_create() enforces it instead of a constant floor. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define FULL_FEATURES                                                        \
    (APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL |         \
     APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID |                        \
     APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO |             \
     APTA_FEATURE_CONFIDENCE | APTA_FEATURE_GRID_LOCKING)

#define FIVE_MINUTES (44100u * 300u)

static void configure(
    apta_session_config_t *config,
    apta_feature_mask_t features,
    uint64_t total_frames)
{
    apta_session_config_init(config);
    config->source_sample_rate = 44100u;
    config->channel_count = 2u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    config->total_frames = total_frames;
    config->requested_features = features;
    config->flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t config;
    apta_memory_requirements_t required;
    apta_memory_requirements_t smaller;
    apta_memory_requirements_t zeroed;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    void *workspace;
    size_t workspace_size;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = FULL_FEATURES;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    configure(&config, FULL_FEATURES, FIVE_MINUTES);

    /* Output structs must be initialized, as for the pool query. */
    memset(&zeroed, 0, sizeof(zeroed));
    CHECK(apta_query_workspace_requirements(&config, &zeroed) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(apta_query_workspace_requirements(NULL, &required) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_memory_requirements_init(&required);
    CHECK(apta_query_workspace_requirements(&config, &required) ==
          APTA_STATUS_OK);

    /* The figure must be credible: far more than the old constant floor, which
     * accepted 12 KiB for this configuration, and not an absurd amount. The
     * bounds are deliberately wide so they hold at any supported geometry --
     * what they catch is a query that reports a constant or a nonsense value,
     * not a specific number. The exact figure is checked below by creating a
     * session at exactly it. */
    CHECK(required.minimum_bytes > (size_t)256u * 1024u);
    CHECK(required.minimum_bytes <
          (size_t)16u * 1024u * 1024u * APTA_TEST_WORKSPACE_SCALE);
    CHECK(required.recommended_bytes >= required.minimum_bytes);
    CHECK(required.required_alignment >= sizeof(void *));

    /* It must scale with duration; the overview accumulators dominate. */
    apta_memory_requirements_init(&smaller);
    configure(&config, FULL_FEATURES, 44100u * 10u);
    CHECK(apta_query_workspace_requirements(&config, &smaller) ==
          APTA_STATUS_OK);
    CHECK(smaller.minimum_bytes < required.minimum_bytes);

    /* Fewer features must not cost more. */
    apta_memory_requirements_init(&smaller);
    configure(&config, APTA_FEATURE_WAVEFORM_OVERVIEW, FIVE_MINUTES);
    CHECK(apta_query_workspace_requirements(&config, &smaller) ==
          APTA_STATUS_OK);
    CHECK(smaller.minimum_bytes < required.minimum_bytes);

    /* The bug this task fixes: a 12 KiB workspace was accepted for a
     * five-minute full-feature session and failed much later inside
     * process(). It must be rejected at creation. */
    configure(&config, FULL_FEATURES, FIVE_MINUTES);
    workspace_size = (required.minimum_bytes + 63u) & ~(size_t)63u;
    workspace = aligned_alloc(64u, workspace_size);
    CHECK(workspace != NULL);
    memset(workspace, 0, workspace_size);

    config.static_workspace = workspace;
    config.static_workspace_size = 12u * 1024u;
    CHECK(apta_session_create(context, &config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);

    /* One byte below the reported requirement is still rejected. */
    config.static_workspace_size = required.minimum_bytes - 1u;
    CHECK(apta_session_create(context, &config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);

    /* Exactly the reported requirement is accepted. */
    config.static_workspace_size = required.minimum_bytes;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(session != NULL);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    free(workspace);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
