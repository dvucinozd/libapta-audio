// SPDX-License-Identifier: Apache-2.0
#include "apta_waveform_detail_internal.h"

#include <stdint.h>

apta_status_t apta_internal_waveform_accept_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    apta_status_t status;
    apta_internal_pcm_node_t *node;
    uint32_t frame;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u &&
        block->frame_count != 0u) {
        const apta_source_frame_t last_frame =
            block->first_frame + (apta_source_frame_t)block->frame_count - 1u;
        const uint64_t tile_index =
            last_frame / APTA_INTERNAL_DETAIL_TILE_FRAMES;

        if (tile_index >
            UINT32_MAX / APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE) {
            *accepted_frames_out = 0u;
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
    }

    status = apta_internal_waveform_accept_pcm_base(
        session,
        block,
        accepted_frames_out);

    if (status == APTA_ERROR_CONFLICT &&
        (session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        const apta_status_t replay_status =
            apta_internal_detail_accept_replay(
                session,
                block,
                accepted_frames_out);

        if (replay_status != APTA_STATUS_NOT_AVAILABLE) {
            return replay_status;
        }
    }

    if (status < 0 || *accepted_frames_out == 0u ||
        (session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return status;
    }

    /*
     * The overview layer has accepted ownership of the PCM at this point.
     * Detail-cache pressure or degradation MUST NOT retroactively change the
     * accepted frame count or turn an accepted push into an error.
     */
    node = session->pcm_tail;
    if (node == NULL || node->frame_count != *accepted_frames_out ||
        node->first_frame != block->first_frame) {
        return status;
    }

    for (frame = 0u; frame < *accepted_frames_out; ++frame) {
        const apta_status_t detail_status =
            apta_internal_detail_process_sample(
                session,
                node->first_frame + frame,
                node->samples[frame]);

        if (detail_status < 0) {
            /* Preserve the already-committed PCM acceptance contract. */
            break;
        }
    }

    apta_internal_detail_refresh_completed(session);
    return status;
}
