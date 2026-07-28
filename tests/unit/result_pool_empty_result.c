// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#include "apta_result_pool.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = 4096u;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config->flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
}

static int check_result_info(
    const apta_result_t *result,
    apta_generation_t generation,
    apta_session_state_t session_state,
    apta_feature_mask_t changed_features,
    uint64_t lineage_high,
    uint64_t lineage_low)
{
    apta_result_info_t info;

    apta_result_info_init(&info);
    if (apta_result_get_info(result, &info) != APTA_STATUS_OK ||
        info.generation != generation ||
        info.session_state != session_state ||
        info.changed_features != changed_features ||
        info.available_features != 0u ||
        info.lineage_id_high != lineage_high ||
        info.lineage_id_low != lineage_low ||
        apta_result_get_generation(result) != generation ||
        apta_result_get_available_features(result) != 0u) {
        return 0;
    }
    return 1;
}

int main(void)
{
    apta_session_config_t session_config;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_internal_result_pool_control_t *pool = NULL;
    apta_result_t *first = NULL;
    apta_result_t *second = NULL;
    apta_result_t *third = NULL;
    apta_result_t *first_address;

    configure_session(&session_config);
    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_internal_result_pool_create(
              context,
              &session_config,
              &pool) == APTA_STATUS_OK);

    CHECK(apta_internal_result_pool_create_empty_result(
              pool,
              &session_config,
              7u,
              APTA_SESSION_CREATED,
              0u,
              11u,
              22u,
              &first) == APTA_STATUS_OK);
    CHECK(first != NULL);
    CHECK(check_result_info(
        first,
        7u,
        APTA_SESSION_CREATED,
        0u,
        11u,
        22u));

    apta_internal_result_retain(first);
    apta_result_release(first);
    CHECK(check_result_info(
        first,
        7u,
        APTA_SESSION_CREATED,
        0u,
        11u,
        22u));

    CHECK(apta_internal_result_pool_create_empty_result(
              pool,
              &session_config,
              8u,
              APTA_SESSION_ACTIVE,
              APTA_FEATURE_WAVEFORM_OVERVIEW,
              11u,
              22u,
              &second) == APTA_STATUS_OK);
    CHECK(second != NULL);
    CHECK(second != first);
    CHECK(check_result_info(
        second,
        8u,
        APTA_SESSION_ACTIVE,
        APTA_FEATURE_WAVEFORM_OVERVIEW,
        11u,
        22u));

    CHECK(apta_internal_result_pool_create_empty_result(
              pool,
              &session_config,
              9u,
              APTA_SESSION_DRAINING,
              0u,
              11u,
              22u,
              &third) == APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(third == NULL);

    first_address = first;
    apta_result_release(first);
    first = NULL;

    CHECK(apta_internal_result_pool_create_empty_result(
              pool,
              &session_config,
              9u,
              APTA_SESSION_DRAINING,
              0u,
              11u,
              22u,
              &third) == APTA_STATUS_OK);
    CHECK(third == first_address);
    CHECK(check_result_info(
        third,
        9u,
        APTA_SESSION_DRAINING,
        0u,
        11u,
        22u));

    apta_internal_result_pool_release(pool);
    pool = NULL;
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(second);
    second = NULL;
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(third);
    third = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
