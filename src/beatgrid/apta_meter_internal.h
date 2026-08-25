// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_METER_INTERNAL_H
#define APTA_METER_INTERNAL_H

#include "../core/apta_internal.h"
#include "../core/apta_result_pool.h"

#define APTA_INTERNAL_METER_MIN_BEATS 12u
#define APTA_INTERNAL_METER_STABLE_BEATS 24u
#define APTA_INTERNAL_METER_MAX_BEATS 128u
#define APTA_INTERNAL_METER_TRIPLE_MIN_CONFIDENCE 50u

typedef struct {
    uint16_t numerator;
    uint16_t denominator;
    uint32_t downbeat_phase;
    apta_confidence_value_t confidence;
    float score;
    float runner_up_score;
} apta_internal_meter_selection_t;

apta_status_t apta_internal_meter_select(
    const float *beat_strengths,
    uint32_t beat_count,
    apta_internal_meter_selection_t *selection_out);

int apta_internal_meter_refresh_pending(const apta_session_t *session);

apta_status_t apta_internal_meter_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out);

apta_feature_mask_t apta_internal_meter_pending_features(
    const apta_session_t *session);

void apta_internal_meter_mark_published(apta_session_t *session);

apta_status_t apta_internal_meter_build_snapshot(
    const apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_meter_pool_build(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result);

#ifdef APTA_INTERNAL_METER_TRACE
/* Diagnostic trace of the last meter refresh. Returns the collected
 * broadband (and accent, when stored) per-beat series, the sampling lag and
 * the first sampled evidence bin. Any output pointer may be NULL. */
void apta_internal_meter_trace_get(
    const apta_session_t *session,
    const float **broad_out,
    const float **accent_out,
    uint32_t *beat_count_out,
    uint32_t *lag_out,
    uint64_t *first_beat_bin_out);
#endif

#endif /* APTA_METER_INTERNAL_H */
