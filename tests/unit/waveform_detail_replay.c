// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define DETAIL_TILE_FRAMES 16384u
#define PUSH_FRAMES 4096u

static int push_and_process(
    apta_session_t *session,
    apta_source_frame_t first_frame,
    const int16_t pcm[PUSH_FRAMES])
{
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    uint32_t accepted = 0u;
    apta_status_t status;

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = first_frame;
    block.frame_count = PUSH_FRAMES;
    status = apta_session_push_pcm(session, &block, &accepted);
    if (status != APTA_STATUS_OK || accepted != PUSH_FRAMES) {
        return 0;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = PUSH_FRAMES;
    budget.maximum_steps = 16u;
    status = apta_session_process(session, &budget, NULL);
    return status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_focus_t focus;
    apta_pcm_request_t request;
    const apta_result_t *result = NULL;
    apta_waveform_tile_view_t tile;
    int16_t pcm[PUSH_FRAMES] = {0};
    uint32_t tile_index;
    uint32_t block_index;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 5u * DETAIL_TILE_FRAMES;
    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    /* Protect the first four tiles while they are populated. */
    apta_focus_init(&focus);
    focus.playhead_frame = 2u * DETAIL_TILE_FRAMES;
    focus.lookbehind_frames = 2u * DETAIL_TILE_FRAMES;
    focus.lookahead_frames = 2u * DETAIL_TILE_FRAMES;
    focus.feature_mask = APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_set_focus(session, &focus) == APTA_STATUS_OK);

    for (tile_index = 0u; tile_index < 4u; ++tile_index) {
        for (block_index = 0u; block_index < 4u; ++block_index) {
            CHECK(push_and_process(
                session,
                (apta_source_frame_t)tile_index * DETAIL_TILE_FRAMES +
                    (apta_source_frame_t)block_index * PUSH_FRAMES,
                pcm));
        }
    }

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    for (tile_index = 0u; tile_index < 4u; ++tile_index) {
        apta_waveform_tile_view_init(&tile);
        CHECK(apta_result_get_waveform_tile(result, 1u, tile_index, &tile) ==
              APTA_STATUS_OK);
        CHECK(tile.column_count == 64u);
        CHECK(tile.state == APTA_FEATURE_STABLE);
    }
    apta_result_release(result);
    result = NULL;

    /*
     * Tile 4 is background work while all four resident tiles are protected.
     * Overview accepts it, but bounded detail cache intentionally skips it.
     */
    CHECK(push_and_process(session, 4u * DETAIL_TILE_FRAMES, pcm));

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 4u, &tile) ==
          APTA_STATUS_NOT_AVAILABLE);
    apta_result_release(result);
    result = NULL;

    /* Moving focus makes tile 4 protected and requests a detail-only replay. */
    apta_focus_init(&focus);
    focus.playhead_frame = 4u * DETAIL_TILE_FRAMES +
                           DETAIL_TILE_FRAMES / 2u;
    focus.lookbehind_frames = DETAIL_TILE_FRAMES / 2u;
    focus.lookahead_frames = DETAIL_TILE_FRAMES / 2u;
    focus.feature_mask = APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_set_focus(session, &focus) == APTA_STATUS_OK);

    apta_pcm_request_init(&request);
    CHECK(apta_session_next_pcm_request(session, &request) == APTA_STATUS_OK);
    CHECK(request.feature_mask == APTA_FEATURE_WAVEFORM_DETAIL);
    CHECK(request.range.first_frame == 4u * DETAIL_TILE_FRAMES);
    CHECK(request.range.end_frame ==
          4u * DETAIL_TILE_FRAMES + PUSH_FRAMES);

    /* The same range already belongs to overview; this push is detail replay. */
    CHECK(push_and_process(session, request.range.first_frame, pcm));

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 4u, &tile) ==
          APTA_STATUS_OK);
    CHECK(tile.first_column_index == 256u);
    CHECK(tile.column_count == 16u);
    CHECK(tile.state == APTA_FEATURE_PARTIAL);

    /* Oldest formerly protected tile is the deterministic LRU victim. */
    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 0u, &tile) ==
          APTA_STATUS_NOT_AVAILABLE);
    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 1u, &tile) ==
          APTA_STATUS_OK);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
