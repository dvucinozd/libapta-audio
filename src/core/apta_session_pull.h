// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_SESSION_PULL_H
#define APTA_SESSION_PULL_H

#include "apta_internal.h"

apta_status_t apta_internal_source_validate_pcm_block(
    const apta_session_t *session,
    const apta_pcm_block_t *block);

apta_status_t apta_internal_session_signal_end_of_input(
    apta_session_t *session,
    apta_source_frame_t final_end_frame);

apta_status_t apta_internal_pull_pcm_before_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    uint32_t *pulled_frames_out);

#endif /* APTA_SESSION_PULL_H */
