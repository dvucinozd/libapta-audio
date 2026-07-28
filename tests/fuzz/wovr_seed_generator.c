// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

static int write_result_seed(
    const char *path,
    apta_context_t *context,
    apta_source_frame_t first_frame,
    uint32_t frame_count,
    apta_source_frame_t total_frames,
    int signal_end_of_input,
    apta_feature_mask_t requested_features,
    int include_metadata)
{
    apta_session_config_t session_config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_serialize_options_t serialize_options;
    apta_metadata_t metadata;
    int16_t pcm[2048] = {0};
    uint8_t output[2048];
    uint64_t required_size = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    FILE *file = NULL;
    int success = 0;

    if (frame_count > 2048u) {
        return 0;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = total_frames;
    session_config.requested_features = requested_features;

    if (apta_session_create(context, &session_config, &session) != APTA_STATUS_OK) {
        goto cleanup;
    }

    if (include_metadata) {
        apta_metadata_init(&metadata);
        metadata.flags =
            APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
            APTA_METADATA_FLAG_BACKEND_NAME_PRESENT |
            APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
            APTA_METADATA_FLAG_COMMENTS_PRESENT;
        metadata.producer_name.data = "libapta";
        metadata.producer_name.size = 7u;
        metadata.backend_name.data = "fuzz-seed";
        metadata.backend_name.size = 9u;
        metadata.creation_unix_time = UINT64_C(1700000000);
        metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_TEXT;
        metadata.application_source_id.data =
            (const uint8_t *)"valid-meta";
        metadata.application_source_id.size = 10u;
        metadata.comments.data = "canonical META seed";
        metadata.comments.size = 19u;
        if (apta_session_set_metadata(session, &metadata) != APTA_STATUS_OK) {
            goto cleanup;
        }
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = first_frame;
    block.frame_count = frame_count;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK ||
        accepted != frame_count) {
        goto cleanup;
    }

    if (signal_end_of_input &&
        apta_session_signal_end_of_input(
            session,
            first_frame + frame_count) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = frame_count;
    budget.maximum_steps = (frame_count + 255u) / 256u;
    if (apta_session_process(session, &budget, NULL) < 0) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        goto cleanup;
    }

    apta_serialize_options_init(&serialize_options);
    if (apta_result_query_serialized_size(
            result,
            &serialize_options,
            &required_size) != APTA_STATUS_OK ||
        required_size > sizeof(output)) {
        goto cleanup;
    }

    if (apta_result_serialize(
            result,
            &serialize_options,
            output,
            sizeof(output),
            &written) != APTA_STATUS_OK ||
        written != (size_t)required_size) {
        goto cleanup;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        goto cleanup;
    }
    if (fwrite(output, 1u, written, file) != written) {
        goto cleanup;
    }

    success = 1;

cleanup:
    if (file != NULL) {
        (void)fclose(file);
    }
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    return success;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    int success;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        return 1;
    }

    success =
        write_result_seed(
            "valid-final.apta",
            context,
            0u,
            2048u,
            2048u,
            1,
            APTA_FEATURE_WAVEFORM_OVERVIEW,
            0) &&
        write_result_seed(
            "valid-sparse-partial.apta",
            context,
            2048u,
            1024u,
            APTA_TOTAL_FRAMES_UNKNOWN,
            0,
            APTA_FEATURE_WAVEFORM_OVERVIEW,
            0) &&
        write_result_seed(
            "valid-wdtl.apta",
            context,
            0u,
            1024u,
            1024u,
            1,
            APTA_FEATURE_WAVEFORM_OVERVIEW |
                APTA_FEATURE_WAVEFORM_DETAIL,
            0) &&
        write_result_seed(
            "valid-meta.apta",
            context,
            0u,
            1024u,
            1024u,
            1,
            APTA_FEATURE_WAVEFORM_OVERVIEW |
                APTA_FEATURE_WAVEFORM_DETAIL,
            1);

    if (apta_context_destroy(context) != APTA_STATUS_OK) {
        return 1;
    }
    return success ? 0 : 1;
}
