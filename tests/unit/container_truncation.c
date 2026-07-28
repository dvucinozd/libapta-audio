// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define FIXTURE_CAPACITY 2048u

typedef struct {
    uint8_t bytes[FIXTURE_CAPACITY];
    size_t size;
} fixture_t;

static int build_fixture(
    apta_context_t *context,
    int include_detail,
    int include_metadata,
    fixture_t *fixture)
{
    apta_session_config_t session_config;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    int16_t pcm[1024] = {0};
    uint64_t required = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    int success = 0;

    memset(fixture, 0, sizeof(*fixture));

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (include_detail) {
        session_config.requested_features |= APTA_FEATURE_WAVEFORM_DETAIL;
    }

    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }

    if (include_metadata) {
        apta_metadata_init(&metadata);
        metadata.flags =
            APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
            APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
            APTA_METADATA_FLAG_COMMENTS_PRESENT;
        metadata.producer_name.data = "truncation";
        metadata.producer_name.size = 10u;
        metadata.creation_unix_time = UINT64_C(1700000000);
        metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_BYTES;
        metadata.application_source_id.data =
            (const uint8_t *)"fixture";
        metadata.application_source_id.size = 7u;
        metadata.comments.data = "all-prefixes";
        metadata.comments.size = 12u;
        if (apta_session_set_metadata(session, &metadata) != APTA_STATUS_OK) {
            goto cleanup;
        }
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 1024u;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK ||
        accepted != 1024u ||
        apta_session_signal_end_of_input(session, 1024u) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    if (apta_session_process(session, &budget, NULL) !=
        APTA_STATUS_END_OF_INPUT) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL ||
        apta_result_query_serialized_size(result, NULL, &required) !=
            APTA_STATUS_OK ||
        required == 0u ||
        required >= FIXTURE_CAPACITY ||
        apta_result_serialize(
            result,
            NULL,
            fixture->bytes,
            FIXTURE_CAPACITY,
            &written) != APTA_STATUS_OK ||
        written != (size_t)required) {
        goto cleanup;
    }

    fixture->size = written;
    success = 1;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    return success;
}

static int verify_fixture(
    apta_context_t *context,
    fixture_t *fixture)
{
    const apta_result_t *result = NULL;
    size_t prefix;

    if (apta_result_parse(
            context,
            NULL,
            fixture->bytes,
            fixture->size,
            &result) != APTA_STATUS_OK ||
        result == NULL) {
        return 0;
    }
    apta_result_release(result);
    result = NULL;

    for (prefix = 0u; prefix < fixture->size; ++prefix) {
        apta_status_t status = apta_result_parse(
            context,
            NULL,
            fixture->bytes,
            prefix,
            &result);
        if (status != APTA_ERROR_CORRUPT_DATA || result != NULL) {
            if (result != NULL) {
                apta_result_release(result);
                result = NULL;
            }
            fprintf(stderr,
                    "Unexpected prefix result at %zu/%zu: %d\n",
                    prefix,
                    fixture->size,
                    (int)status);
            return 0;
        }
    }

    fixture->bytes[fixture->size] = 0u;
    if (apta_result_parse(
            context,
            NULL,
            fixture->bytes,
            fixture->size + 1u,
            &result) != APTA_ERROR_CORRUPT_DATA ||
        result != NULL) {
        if (result != NULL) {
            apta_result_release(result);
        }
        return 0;
    }

    return 1;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    fixture_t wovr;
    fixture_t wdtl;
    fixture_t meta;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    CHECK(build_fixture(context, 0, 0, &wovr));
    CHECK(build_fixture(context, 1, 0, &wdtl));
    CHECK(build_fixture(context, 1, 1, &meta));

    CHECK(verify_fixture(context, &wovr));
    CHECK(verify_fixture(context, &wdtl));
    CHECK(verify_fixture(context, &meta));

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
