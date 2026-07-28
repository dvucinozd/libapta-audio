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

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    apta_metadata_view_t view;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    int16_t pcm[256] = {0};
    char producer[] = "libapta";
    char comments[] = "metadata-owned";
    const uint8_t source_id[] = {0x00u, 0x11u, 0x22u, 0x33u};
    const char invalid_utf8[] = {(char)0xC0, (char)0xAF};
    uint32_t accepted = 0u;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_metadata_init(&metadata);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    metadata.producer_name.data = invalid_utf8;
    metadata.producer_name.size = sizeof(invalid_utf8);
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = producer;
    metadata.producer_name.size = 1u;
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_metadata_init(&metadata);
    metadata.reserved32[0] = 1u;
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_metadata_init(&metadata);
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK(view.flags == 0u);
    CHECK(view.application_source_id_kind == APTA_METADATA_SOURCE_ID_NONE);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_set_metadata(session, NULL) == APTA_STATUS_OK);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) ==
          APTA_STATUS_NOT_AVAILABLE);
    apta_result_release(result);
    result = NULL;

    apta_metadata_init(&metadata);
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
        APTA_METADATA_FLAG_COMMENTS_PRESENT;
    metadata.producer_name.data = producer;
    metadata.producer_name.size = (uint32_t)strlen(producer);
    metadata.creation_unix_time = UINT64_C(1785268800);
    metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_BYTES;
    metadata.application_source_id.data = source_id;
    metadata.application_source_id.size = sizeof(source_id);
    metadata.comments.data = comments;
    metadata.comments.size = (uint32_t)strlen(comments);
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);

    memset(producer, 'X', sizeof(producer) - 1u);
    memset(comments, 'Y', sizeof(comments) - 1u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK(view.producer_name.size == 7u);
    CHECK(memcmp(view.producer_name.data, "libapta", 7u) == 0);
    CHECK(view.creation_unix_time == UINT64_C(1785268800));
    CHECK(view.application_source_id_kind == APTA_METADATA_SOURCE_ID_BYTES);
    CHECK(view.application_source_id.size == sizeof(source_id));
    CHECK(memcmp(view.application_source_id.data, source_id, sizeof(source_id)) == 0);
    CHECK(view.comments.size == 14u);
    CHECK(memcmp(view.comments.data, "metadata-owned", 14u) == 0);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 256u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 256u);
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_INVALID_STATE);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK(memcmp(view.producer_name.data, "libapta", 7u) == 0);
    CHECK(memcmp(view.comments.data, "metadata-owned", 14u) == 0);

    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
