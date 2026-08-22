// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_KEY_INTERNAL_H
#define APTA_KEY_INTERNAL_H

#include "../core/apta_internal.h"
#include "../core/apta_result_pool.h"

#define APTA_INTERNAL_KEY_OCTAVES 3u
#define APTA_INTERNAL_KEY_PITCH_CLASSES 12u
#define APTA_INTERNAL_KEY_BIN_COUNT \
    (APTA_INTERNAL_KEY_OCTAVES * APTA_INTERNAL_KEY_PITCH_CLASSES)
#define APTA_INTERNAL_KEY_CANDIDATE_COUNT 3u
#define APTA_INTERNAL_KEY_DECIMATION 4u
#define APTA_INTERNAL_KEY_STABLE_WINDOWS 4u
#define APTA_INTERNAL_KEY_PUBLISH_INTERVAL 4u

typedef struct {
    float coefficients[APTA_INTERNAL_KEY_BIN_COUNT];
    float q1[APTA_INTERNAL_KEY_BIN_COUNT];
    float q2[APTA_INTERNAL_KEY_BIN_COUNT];
    float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES];
    float decimation_sum;
    uint32_t decimation_count;
    uint32_t window_samples;
    uint32_t window_target_samples;
    uint32_t completed_windows;
    uint32_t selected_windows;
    apta_source_frame_t evidence_first_frame;
    apta_source_frame_t evidence_end_frame;
    uint32_t initialized;
} apta_internal_key_analysis_t;

void apta_internal_key_feed_sample(
    apta_session_t *session,
    float sample,
    apta_source_frame_t source_frame);

int apta_internal_key_refresh_pending(const apta_session_t *session);

apta_status_t apta_internal_key_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out);

apta_feature_mask_t apta_internal_key_pending_features(
    const apta_session_t *session);

void apta_internal_key_mark_published(apta_session_t *session);

apta_status_t apta_internal_key_build_snapshot(
    const apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_key_pool_build(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_key_select_chroma(
    const float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES],
    uint32_t completed_windows,
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT],
    apta_key_view_t *view_out);

#endif /* APTA_KEY_INTERNAL_H */
