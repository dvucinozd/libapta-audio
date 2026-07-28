// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_WAVEFORM_DETAIL_INTERNAL_H
#define APTA_WAVEFORM_DETAIL_INTERNAL_H

#include "../core/apta_internal.h"

apta_status_t apta_internal_detail_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

apta_status_t apta_internal_detail_accept_replay(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

#endif /* APTA_WAVEFORM_DETAIL_INTERNAL_H */
