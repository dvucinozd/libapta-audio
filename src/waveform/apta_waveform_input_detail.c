// SPDX-License-Identifier: Apache-2.0
#include "apta_waveform_detail_internal.h"

#include <stdint.h>

apta_status_t apta_internal_waveform_accept_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    const int detail_enabled =
        (session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u;
    const int s4_enabled =
        (session->config.requested_features &
         APTA_INTERNAL_S4_FEATURES) != 0u;
    const int s6_enabled =
        (session->config.requested_features &
         APTA_INTERNAL_S6_FEATURES) != 0u;
    apta_status_t status;
    apta_internal_pcm_node_t *node;
    uint32_t frame;

    if (detail_enabled && block->frame_count != 0u) {
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

    if (s4_enabled) {
        status = apta_internal_s4_prepare(session);
        if (status < 0) {
            *accepted_frames_out = 0u;
            return status;
        }
    }
    if (s6_enabled) {
        status = apta_internal_s6_prepare(session);
        if (status < 0) {
            *accepted_frames_out = 0u;
            return status;
        }
    }

    status = apta_internal_waveform_accept_pcm_base(
        session,
        block,
        accepted_frames_out);

    if (status == APTA_ERROR_CONFLICT && detail_enabled) {
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
        (!detail_enabled && !s4_enabled && !s6_enabled)) {
        return status;
    }

    /*
     * The overview layer owns a copied, normalized PCM node at this point.
     * Derived detail/onset work therefore cannot invalidate the accepted push.
     */
    node = session->pcm_tail;
    if (node == NULL || node->frame_count != *accepted_frames_out ||
        node->first_frame != block->first_frame) {
        return status;
    }

    for (frame = 0u; frame < *accepted_frames_out; ++frame) {
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
        float onset_bands[APTA_INTERNAL_BAND_COUNT];
        const apta_source_frame_t source_frame = node->first_frame + frame;

        if (s4_enabled) {
            if (!session->onset_band_filter_valid ||
                session->onset_band_next_frame != source_frame) {
                /* A seek or gap starts a new causal run. This avoids leaking
                 * filter history across unrelated accepted ranges. */
                apta_internal_band_filter_reset(&session->onset_band_filter);
            }
            apta_internal_band_filter_split(
                &session->onset_band_filter,
                node->samples[frame],
                onset_bands);
            session->onset_band_next_frame = source_frame + 1u;
            session->onset_band_filter_valid = 1u;
        }
#endif
        if (detail_enabled) {
            const apta_status_t detail_status =
                apta_internal_detail_process_sample(
                    session,
                    node->first_frame + frame,
                    node->samples[frame]);
            if (detail_status != APTA_STATUS_OK) {
                break;
            }
        }
        if (s4_enabled) {
            const apta_status_t s4_status = apta_internal_s4_process_sample(
                session,
                node->first_frame + frame,
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
                onset_bands);
#else
                node->samples[frame]);
#endif
            if (s4_status != APTA_STATUS_OK) {
                break;
            }
        }
        if (s6_enabled) {
            const apta_status_t s6_status = apta_internal_s6_process_sample(
                session,
                node->first_frame + frame,
                node->samples[frame]);
            if (s6_status != APTA_STATUS_OK) {
                break;
            }
        }
    }

    if (detail_enabled) {
        apta_internal_detail_refresh_completed(session);
    }
    return status;
}
