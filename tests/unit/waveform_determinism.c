// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int drain_session(apta_session_t *session)
{
    apta_work_budget_t budget;
    apta_status_t status;
    uint32_t guard;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 300u;
    budget.maximum_steps = 2u;

    for (guard = 0u; guard < 100u; ++guard) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            return 0;
        }
        if (status < 0 || status == APTA_STATUS_WOULD_BLOCK) {
            return 1;
        }
    }

    return 1;
}

static int analyze(
    const int16_t *pcm,
    uint32_t block_frames,
    apta_waveform_column_t output[2])
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    uint32_t first_frame;
    int rc = 1;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 44100u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 2048u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_session_create(context, &session_config, &session) != APTA_STATUS_OK) {
        goto cleanup;
    }

    for (first_frame = 0u; first_frame < 2048u; first_frame += block_frames) {
        uint32_t accepted = 0u;
        uint32_t frames = block_frames;
        apta_status_t status;

        if (frames > 2048u - first_frame) {
            frames = 2048u - first_frame;
        }

        apta_pcm_block_init(&block);
        block.data = &pcm[first_frame];
        block.first_frame = first_frame;
        block.frame_count = frames;
        status = apta_session_push_pcm(session, &block, &accepted);
        if ((status != APTA_STATUS_OK && status != APTA_STATUS_MORE_WORK) ||
            accepted != frames) {
            goto cleanup;
        }
    }

    if (apta_session_signal_end_of_input(session, 2048u) != APTA_STATUS_OK ||
        drain_session(session) != 0) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        goto cleanup;
    }

    apta_waveform_overview_view_init(&overview);
    if (apta_result_get_waveform_overview(result, 0u, &overview) !=
            APTA_STATUS_OK ||
        overview.state != APTA_FEATURE_FINAL ||
        overview.span_count != 1u ||
        overview.spans[0].column_count != 2u) {
        goto cleanup;
    }

    output[0] = overview.spans[0].columns[0];
    output[1] = overview.spans[0].columns[1];
    rc = 0;

cleanup:
    apta_result_release(result);
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    if (context != NULL) {
        (void)apta_context_destroy(context);
    }
    return rc;
}

int main(void)
{
    int16_t pcm[2048];
    apta_waveform_column_t single_block[2];
    apta_waveform_column_t split_blocks[2];
    uint32_t index;

    for (index = 0u; index < 1024u; ++index) {
        pcm[index] = (index & 1u) == 0u ? INT16_MIN : INT16_MAX;
    }
    for (; index < 2048u; ++index) {
        pcm[index] = (int16_t)(index - 1536u);
    }

    CHECK(analyze(pcm, 2048u, single_block) == 0);
    CHECK(analyze(pcm, 257u, split_blocks) == 0);
    CHECK(memcmp(single_block, split_blocks, sizeof(single_block)) == 0);
    return 0;
}
