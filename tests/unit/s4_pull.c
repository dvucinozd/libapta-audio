// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 288000u
#define BLOCK_FRAMES 4096u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    int16_t samples[BLOCK_FRAMES];
    uint32_t reads;
    uint32_t releases;
} source_state_t;

static apta_status_t APTA_CALL read_frames(
    void *user_data,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    apta_pcm_block_t *block_out)
{
    source_state_t *state = (source_state_t *)user_data;
    uint32_t count;
    uint32_t index;

    state->reads += 1u;
    if (first_frame >= TOTAL_FRAMES) {
        return APTA_STATUS_END_OF_INPUT;
    }
    count = (uint32_t)(TOTAL_FRAMES - first_frame);
    if (count > requested_frames) {
        count = requested_frames;
    }
    if (count > BLOCK_FRAMES) {
        count = BLOCK_FRAMES;
    }
    for (index = 0u; index < count; ++index) {
        state->samples[index] =
            ((first_frame + index) % BEAT_FRAMES) < 128u
                ? (int16_t)30000
                : 0;
    }

    apta_pcm_block_init(block_out);
    block_out->data = state->samples;
    block_out->first_frame = first_frame;
    block_out->frame_count = count;
    return APTA_STATUS_OK;
}

static void APTA_CALL release_frames(
    void *user_data,
    apta_pcm_block_t *block)
{
    source_state_t *state = (source_state_t *)user_data;
    (void)block;
    state->releases += 1u;
}

static uint64_t APTA_CALL get_total_frames(void *user_data)
{
    (void)user_data;
    return TOTAL_FRAMES;
}

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_source_t source;
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    source_state_t source_state;
    apta_status_t status = APTA_STATUS_MORE_WORK;
    uint32_t attempts;

    memset(&source_state, 0, sizeof(source_state));
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_pcm_source_init(&source);
    source.user_data = &source_state;
    source.read_frames = read_frames;
    source.release_frames = release_frames;
    source.get_total_frames = get_total_frames;
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    for (attempts = 0u; attempts < 256u; ++attempts) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK ||
              status == APTA_STATUS_WOULD_BLOCK);
    }
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(source_state.reads > 0u);
    CHECK(source_state.releases + 1u == source_state.reads ||
          source_state.releases == source_state.reads);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.tempo_millibpm >= 124500u);
    CHECK(tempo.selected.tempo_millibpm <= 125500u);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
