// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <apta/apta.h>

#define TOTAL_FRAMES 131072u
#define BLOCK_FRAMES 4096u
#define BEAT_FRAMES 24576u

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    int16_t samples[BLOCK_FRAMES];
    uint8_t *output = NULL;
    uint64_t required = 0u;
    size_t written = 0u;
    uint32_t first = 0u;
    apta_status_t status;
    FILE *file = NULL;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = ((first + index) % BEAT_FRAMES) < 192u
                                 ? (int16_t)30000
                                 : 0;
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        if (apta_session_push_pcm(session, &block, &accepted) !=
                APTA_STATUS_OK ||
            accepted != count) {
            goto cleanup;
        }
        status = apta_session_process(session, &budget, NULL);
        if (status < 0) {
            goto cleanup;
        }
        first += count;
    }
    if (apta_session_signal_end_of_input(session, TOTAL_FRAMES) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    if (status != APTA_STATUS_END_OF_INPUT) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL ||
        apta_result_query_serialized_size(result, NULL, &required) !=
            APTA_STATUS_OK ||
        required == 0u || required > 4096u) {
        goto cleanup;
    }
    output = (uint8_t *)malloc((size_t)required);
    if (output == NULL ||
        apta_result_serialize(
            result,
            NULL,
            output,
            (size_t)required,
            &written) != APTA_STATUS_OK ||
        written != (size_t)required) {
        goto cleanup;
    }

    file = fopen("valid-s6.apta", "wb");
    if (file == NULL || fwrite(output, 1u, written, file) != written) {
        goto cleanup;
    }
    success = 1;

cleanup:
    if (file != NULL) {
        (void)fclose(file);
    }
    free(output);
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    if (context != NULL && apta_context_destroy(context) != APTA_STATUS_OK) {
        success = 0;
    }
    return success ? 0 : 1;
}
