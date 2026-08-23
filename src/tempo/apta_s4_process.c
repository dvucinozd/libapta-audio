// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_period_refine.h"
#include "../core/apta_tempo_ensemble.h"
#include "../core/apta_tempo_prior.h"
#include "../core/apta_tempo_relation.h"
#include "../beatgrid/apta_s6_internal.h"

#include <string.h>

apta_status_t apta_internal_waveform_process_s4_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

static int apta_s4_range_changed(
    const apta_frame_range_t *before,
    const apta_frame_range_t *after)
{
    return before->first_frame != after->first_frame ||
           before->end_frame != after->end_frame;
}

static uint32_t apta_s4_ensemble_lag_from_tempo(
    const apta_session_t *session,
    uint32_t tempo_millibpm)
{
    uint64_t numerator;
    uint64_t denominator;

    if (session == NULL || tempo_millibpm == 0u) {
        return 0u;
    }
    numerator = (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    denominator = (uint64_t)tempo_millibpm * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    if (denominator == 0u) {
        return 0u;
    }
    return (uint32_t)((numerator + denominator / 2u) / denominator);
}

static uint32_t apta_s4_ensemble_tempo_from_lag(
    const apta_session_t *session,
    uint32_t lag)
{
    uint64_t numerator;
    uint64_t denominator;

    if (session == NULL || lag == 0u) {
        return 0u;
    }
    numerator = (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    denominator = (uint64_t)lag * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    return denominator != 0u
               ? (uint32_t)((numerator + denominator / 2u) / denominator)
               : 0u;
}

static void apta_s4_ensemble_period_from_tempo(
    const apta_session_t *session,
    uint32_t tempo_millibpm,
    apta_frame_period_t *period)
{
    uint64_t numerator;
    uint64_t remainder;

    memset(period, 0, sizeof(*period));
    if (tempo_millibpm == 0u) {
        return;
    }
    numerator = (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    period->whole_frames = numerator / tempo_millibpm;
    remainder = numerator % tempo_millibpm;
    period->fraction_q32 = (uint32_t)((remainder << 32) / tempo_millibpm);
}

static uint32_t apta_s4_ensemble_beat_count(
    const apta_grid_segment_t *segment)
{
    uint64_t period;
    uint64_t first;
    uint64_t end;
    uint64_t anchor;
    uint64_t count;

    if (segment == NULL ||
        segment->applicability_range.end_frame <=
            segment->applicability_range.first_frame ||
        segment->frames_per_beat.whole_frames == 0u) {
        return 0u;
    }

    period = segment->frames_per_beat.whole_frames +
             (segment->frames_per_beat.fraction_q32 != 0u ? 1u : 0u);
    first = segment->applicability_range.first_frame;
    end = segment->applicability_range.end_frame;
    anchor = segment->anchor_position.whole_frame;

    if (anchor < first) {
        uint64_t delta = first - anchor;
        uint64_t steps = (delta + period - 1u) / period;
        if (steps > (UINT64_MAX - anchor) / period) {
            return 0u;
        }
        anchor += steps * period;
    }
    if (anchor >= end) {
        return 0u;
    }

    count = 1u + (end - 1u - anchor) / period;
    return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

static float apta_s4_ensemble_grid_fit(
    const float *flux,
    uint32_t span,
    uint32_t lag,
    uint32_t phase)
{
    float on_sum = 0.0f;
    float off_sum = 0.0f;
    uint32_t on_count = 0u;
    uint32_t off_count = 0u;
    uint32_t offset;

    if (flux == NULL || span == 0u || lag == 0u) {
        return 0.0f;
    }
    for (offset = 0u; offset < span; ++offset) {
        const uint32_t from_phase = (offset + lag - (phase % lag)) % lag;
        const uint32_t distance =
            from_phase < lag - from_phase ? from_phase : lag - from_phase;

        if (distance <= 1u) {
            on_sum += flux[offset];
            on_count += 1u;
        } else {
            off_sum += flux[offset];
            off_count += 1u;
        }
    }
    if (on_count == 0u || off_count == 0u) {
        return 0.0f;
    }
    {
        const float on_mean = on_sum / (float)on_count;
        const float off_mean = off_sum / (float)off_count;
        const float sum = on_mean + off_mean;

        return sum > 1e-12f ? (on_mean - off_mean) / sum : 0.0f;
    }
}

static float apta_s4_ensemble_best_grid_fit(
    const float *flux,
    uint32_t span,
    uint32_t lag,
    uint32_t *phase_out)
{
    float best_score = -1.0f;
    uint32_t best_phase = 0u;
    uint32_t phase;

    if (phase_out != NULL) {
        *phase_out = 0u;
    }
    if (flux == NULL || span == 0u || lag == 0u || lag >= span) {
        return 0.0f;
    }
    for (phase = 0u; phase < lag; ++phase) {
        float score = 0.0f;
        uint32_t offset;

        for (offset = phase; offset < span; offset += lag) {
            score += flux[offset];
        }
        if (score > best_score) {
            best_score = score;
            best_phase = phase;
        }
    }
    if (phase_out != NULL) {
        *phase_out = best_phase;
    }
    return apta_s4_ensemble_grid_fit(flux, span, lag, best_phase);
}

static uint32_t apta_s4_ensemble_normalized_score(
    const apta_session_t *session,
    uint32_t lag,
    uint32_t tempo_millibpm)
{
    uint32_t span;
    float score;
    float normalized;

    if (session == NULL || session->onset_flux == NULL ||
        session->s4_refresh_evidence_end <= session->s4_refresh_evidence_first ||
        session->s4_cached_scores[0] <= 0.0f) {
        return 0u;
    }
    span = (uint32_t)(session->s4_refresh_evidence_end -
                      session->s4_refresh_evidence_first);
    if (lag == 0u || lag >= span) {
        return 0u;
    }
    score = apta_internal_correlation_at_lag(session->onset_flux, span, lag) *
            apta_internal_tempo_prior(tempo_millibpm);
    if (score <= 0.0f) {
        return 0u;
    }
    normalized = score / session->s4_cached_scores[0] * 65535.0f;
    if (normalized >= 65535.0f) {
        return 65535u;
    }
    return normalized > 0.0f ? (uint32_t)(normalized + 0.5f) : 0u;
}

static int apta_s4_ensemble_may_need_work(const apta_session_t *session)
{
    uint32_t selected;
    uint32_t proposed;
    float difference;

    if (session == NULL || !session->has_tempo || !session->has_local_grid ||
        session->local_grid_locked || session->s4_refresh_active ||
        session->s6_nominal_tempo_millibpm == 0u || session->onset_flux == NULL ||
        session->tempo_candidate_count == 0u ||
        session->s4_refresh_evidence_end <= session->s4_refresh_evidence_first) {
        return 0;
    }
    selected = session->tempo_value.tempo_millibpm;
    proposed = session->s6_nominal_tempo_millibpm;
    if (selected == 0u) {
        return 0;
    }
    difference = (float)proposed - (float)selected;
    if (difference < 0.0f) {
        difference = -difference;
    }
    if (difference / (float)proposed <= APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE) {
        return 0;
    }
    if (apta_internal_tempo_relation(selected, proposed) !=
        APTA_TEMPO_RELATION_INDEPENDENT) {
        return 1;
    }
    return session->s6 != NULL &&
           session->s6->confidence != APTA_CONFIDENCE_UNKNOWN &&
           session->local_grid_segment.confidence != APTA_CONFIDENCE_UNKNOWN &&
           session->s6->confidence > session->local_grid_segment.confidence;
}

static void apta_s4_ensemble_relate_candidates(apta_session_t *session)
{
    uint32_t index;
    const uint32_t selected = session->tempo_value.tempo_millibpm;

    for (index = 0u; index < session->tempo_candidate_count; ++index) {
        const apta_tempo_relation_t relation = apta_internal_tempo_relation(
            selected, session->tempo_candidates[index].tempo_millibpm);

        session->tempo_candidates[index].relation_to_selected = relation;
        if (index != 0u && relation != APTA_TEMPO_RELATION_INDEPENDENT) {
            session->tempo_value.flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
            session->local_grid_segment.flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
        }
        if (index != 0u && relation == APTA_TEMPO_RELATION_HALF) {
            session->tempo_value.flags |= APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY;
            session->local_grid_segment.flags |= APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY;
        }
        if (index != 0u && relation == APTA_TEMPO_RELATION_DOUBLE) {
            session->tempo_value.flags |= APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY;
            session->local_grid_segment.flags |= APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY;
        }
    }
}

static apta_status_t apta_s4_apply_tempo_grid_ensemble(
    apta_session_t *session,
    uint32_t previous_selected_tempo,
    uint32_t previous_candidate_set_id,
    uint32_t *did_work_out)
{
    uint32_t s6_tempo;
    uint32_t proposed_tempo;
    uint32_t selected_tempo;
    uint32_t proposed_lag;
    uint32_t selected_lag;
    uint32_t normalized_score;
    uint32_t proposed_phase = 0u;
    uint32_t selected_phase = 0u;
    uint32_t span;
    uint32_t entry;
    uint32_t existing = UINT32_MAX;
    float proposed_fit;
    float selected_fit;
    float proposed_offset;
    float difference;
    apta_tempo_relation_t relation;
    apta_tempo_candidate_t promoted;

    if (did_work_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *did_work_out = 0u;
    if (!apta_s4_ensemble_may_need_work(session)) {
        return APTA_STATUS_OK;
    }

    s6_tempo = session->s6_nominal_tempo_millibpm;
    selected_tempo = session->tempo_value.tempo_millibpm;
    span = (uint32_t)(session->s4_refresh_evidence_end -
                      session->s4_refresh_evidence_first);
    proposed_lag = apta_s4_ensemble_lag_from_tempo(session, s6_tempo);
    selected_lag = apta_s4_ensemble_lag_from_tempo(session, selected_tempo);
    if (span == 0u ||
        proposed_lag < session->s4_refresh_minimum_lag ||
        proposed_lag > session->s4_refresh_maximum_lag ||
        selected_lag < session->s4_refresh_minimum_lag ||
        selected_lag > session->s4_refresh_maximum_lag ||
        proposed_lag >= span || selected_lag >= span) {
        return APTA_STATUS_OK;
    }

    /* Everything below this point is bounded by the S4 evidence-ring capacity
     * and is charged as one cooperative scheduler step by the caller. */
    *did_work_out = 1u;

    /* S6 chooses the metrical region; S4 remains the precision authority.
     * Refine the S4 lag on the fine onset evidence instead of publishing the
     * coarser S6 nominal value directly. */
    proposed_offset = apta_internal_refine_lag(
        session->onset_flux,
        span,
        proposed_lag,
        APTA_INTERNAL_TEMPO_REFINE_MAX_BEATS);
    proposed_tempo = apta_internal_tempo_with_offset(
        apta_s4_ensemble_tempo_from_lag(session, proposed_lag),
        proposed_lag,
        proposed_offset);
    if (proposed_tempo < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
        proposed_tempo > APTA_REFERENCE_TEMPO_MAX_MILLIBPM) {
        return APTA_STATUS_OK;
    }

    difference = (float)proposed_tempo - (float)s6_tempo;
    if (difference < 0.0f) {
        difference = -difference;
    }
    if (difference / (float)s6_tempo >
        APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE) {
        return APTA_STATUS_OK;
    }
    difference = (float)proposed_tempo - (float)selected_tempo;
    if (difference < 0.0f) {
        difference = -difference;
    }
    if (difference / (float)proposed_tempo <=
        APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE) {
        return APTA_STATUS_OK;
    }

    normalized_score = apta_s4_ensemble_normalized_score(
        session, proposed_lag, proposed_tempo);
    selected_fit = apta_s4_ensemble_best_grid_fit(
        session->onset_flux, span, selected_lag, &selected_phase);
    proposed_fit = apta_s4_ensemble_best_grid_fit(
        session->onset_flux, span, proposed_lag, &proposed_phase);
    for (entry = 0u; entry < session->tempo_candidate_count; ++entry) {
        const uint32_t candidate =
            session->tempo_candidates[entry].tempo_millibpm;
        const uint32_t candidate_difference = candidate > proposed_tempo
                                                  ? candidate - proposed_tempo
                                                  : proposed_tempo - candidate;
        if ((uint64_t)candidate_difference * 100u <=
            (uint64_t)proposed_tempo) {
            existing = entry;
            break;
        }
    }

    relation = apta_internal_tempo_relation(selected_tempo, proposed_tempo);
    if (relation == APTA_TEMPO_RELATION_INDEPENDENT) {
        if (!apta_internal_tempo_ensemble_should_promote_close(
                existing != UINT32_MAX,
                session->local_grid_segment.confidence,
                session->s6 != NULL ? session->s6->confidence
                                    : APTA_CONFIDENCE_UNKNOWN,
                normalized_score,
                selected_fit,
                proposed_fit)) {
            return APTA_STATUS_OK;
        }
    } else if (!apta_internal_tempo_ensemble_should_promote(
                   relation, normalized_score, selected_fit, proposed_fit)) {
        return APTA_STATUS_OK;
    }

    if (existing != UINT32_MAX) {
        promoted = session->tempo_candidates[existing];
        if (normalized_score > promoted.score) {
            promoted.score = (uint16_t)normalized_score;
            promoted.confidence = (apta_confidence_value_t)(
                25u + (normalized_score * 70u) / 65535u);
        }
        for (entry = existing; entry > 0u; --entry) {
            session->tempo_candidates[entry] =
                session->tempo_candidates[entry - 1u];
        }
    } else {
        memset(&promoted, 0, sizeof(promoted));
        promoted.tempo_millibpm = proposed_tempo;
        promoted.score = (uint16_t)normalized_score;
        promoted.confidence = (apta_confidence_value_t)(
            25u + (normalized_score * 70u) / 65535u);
        if (session->tempo_candidate_count < APTA_INTERNAL_MAX_TEMPO_CANDIDATES) {
            session->tempo_candidate_count += 1u;
        }
        for (entry = session->tempo_candidate_count - 1u; entry > 0u; --entry) {
            session->tempo_candidates[entry] =
                session->tempo_candidates[entry - 1u];
        }
    }
    if (session->tempo_candidate_count > 1u) {
        promoted.score = apta_internal_tempo_promotion_score(
            promoted.score, session->tempo_candidates[1].score);
    }
    session->tempo_candidates[0] = promoted;

    session->tempo_value.tempo_millibpm = promoted.tempo_millibpm;
    if (relation != APTA_TEMPO_RELATION_INDEPENDENT) {
        session->tempo_value.flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
        session->local_grid_segment.flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
    }
    if (promoted.confidence < session->tempo_value.confidence) {
        session->tempo_value.confidence = promoted.confidence;
    }
    session->local_grid_segment.confidence = session->tempo_value.confidence;
    session->local_grid_segment.nominal_tempo_millibpm = promoted.tempo_millibpm;
    apta_s4_ensemble_period_from_tempo(
        session,
        promoted.tempo_millibpm,
        &session->local_grid_segment.frames_per_beat);
    session->local_grid_segment.anchor_position.whole_frame =
        (session->s4_refresh_evidence_first + proposed_phase) *
        APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    session->local_grid_segment.anchor_position.fraction_q32 = 0u;
    session->local_grid_segment.anchor_ordinal = 0;
    session->local_grid_segment.beat_count =
        apta_s4_ensemble_beat_count(&session->local_grid_segment);

    if (previous_selected_tempo != 0u &&
        session->tempo_value.tempo_millibpm == previous_selected_tempo &&
        previous_candidate_set_id != 0u) {
        session->tempo_candidate_set_id = previous_candidate_set_id;
    } else if (previous_selected_tempo != 0u &&
               session->tempo_value.tempo_millibpm != previous_selected_tempo &&
               session->tempo_candidate_set_id == previous_candidate_set_id) {
        session->tempo_candidate_set_id += 1u;
        if (session->tempo_candidate_set_id == 0u) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
    }
    session->tempo_value.candidate_set_id = session->tempo_candidate_set_id;
    session->local_grid_segment.revision = session->tempo_candidate_set_id;
    apta_s4_ensemble_relate_candidates(session);
    session->s4_mutation_serial += 1u;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out)
{
    apta_progress_t local_progress;
    apta_progress_t *work_progress = progress_out;
    apta_feature_mask_t saved_focus_mask;
    apta_feature_mask_t saved_request_masks[APTA_INTERNAL_MAX_REGION_REQUESTS];
    apta_frame_range_t old_requested = session->local_grid_requested_range;
    apta_frame_range_t old_applicability =
        session->local_grid_applicability_range;
    uint64_t old_mutation_serial = session->s4_mutation_serial;
    uint32_t previous_selected_tempo =
        session->has_tempo ? session->tempo_value.tempo_millibpm : 0u;
    uint32_t previous_candidate_set_id = session->tempo_candidate_set_id;
    apta_status_t status;
    apta_status_t refresh_status;
    apta_status_t ensemble_status;
    apta_feature_mask_t pending;
    uint32_t slot;
    uint32_t refresh_steps = 0u;
    uint32_t ensemble_step = 0u;
    uint32_t remaining_steps;

    if (work_progress == NULL) {
        apta_progress_init(&local_progress);
        work_progress = &local_progress;
    }

    saved_focus_mask = session->focus.feature_mask;
    if ((saved_focus_mask & APTA_INTERNAL_S4_FEATURES) != 0u) {
        session->focus.feature_mask |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    }
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        saved_request_masks[slot] =
            session->requests[slot].request.feature_mask;
        if ((saved_request_masks[slot] & APTA_INTERNAL_S4_FEATURES) != 0u) {
            session->requests[slot].request.feature_mask |=
                APTA_FEATURE_WAVEFORM_OVERVIEW;
        }
    }

    status = apta_internal_waveform_process_s4_base(
        session,
        budget,
        work_progress,
        did_work_out,
        published_output_out);

    session->focus.feature_mask = saved_focus_mask;
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        session->requests[slot].request.feature_mask = saved_request_masks[slot];
    }

    if (status < 0) {
        return status;
    }

    remaining_steps = budget->maximum_steps == 0u
                          ? UINT32_MAX
                          : work_progress->completed_steps <
                                    budget->maximum_steps
                                ? budget->maximum_steps -
                                      work_progress->completed_steps
                                : 0u;
    if (remaining_steps != UINT32_MAX && remaining_steps > 1u &&
        (session->config.requested_features & APTA_INTERNAL_S6_FEATURES) != 0u) {
        /* S6 is the downstream consumer in the wrapper chain. Without an
         * explicit share, a long S4 generation can consume every remaining
         * step and leave the global grid one evidence generation behind. */
        remaining_steps /= 2u;
    }
    if (session->process_deadline_ns != 0u &&
        session->context->clock.monotonic_time_ns != NULL &&
        session->context->clock.monotonic_time_ns(
            session->context->clock.user_data) >=
            session->process_deadline_ns) {
        remaining_steps = 0u;
    }
    refresh_status = apta_internal_s4_refresh(
        session,
        remaining_steps,
        &refresh_steps);
    if (refresh_status < 0) {
        return refresh_status;
    }
    work_progress->completed_steps += refresh_steps;
    if (refresh_steps != 0u) {
        *did_work_out = 1u;
        if (status == APTA_STATUS_WOULD_BLOCK) {
            status = APTA_STATUS_OK;
        }
    }
    if (refresh_status == APTA_STATUS_MORE_WORK) {
        status = APTA_STATUS_MORE_WORK;
    } else if (apta_s4_ensemble_may_need_work(session)) {
        int ensemble_step_available =
            budget->maximum_steps == 0u ||
            work_progress->completed_steps < budget->maximum_steps;

        if (ensemble_step_available && session->process_deadline_ns != 0u &&
            session->context->clock.monotonic_time_ns != NULL &&
            session->context->clock.monotonic_time_ns(
                session->context->clock.user_data) >=
                session->process_deadline_ns) {
            ensemble_step_available = 0;
        }
        if (!ensemble_step_available) {
            status = APTA_STATUS_MORE_WORK;
        } else {
            ensemble_status = apta_s4_apply_tempo_grid_ensemble(
                session,
                previous_selected_tempo,
                previous_candidate_set_id,
                &ensemble_step);
            if (ensemble_status < 0) {
                return ensemble_status;
            }
            if (ensemble_step != 0u) {
                work_progress->completed_steps += 1u;
                *did_work_out = 1u;
                if (status == APTA_STATUS_WOULD_BLOCK) {
                    status = APTA_STATUS_OK;
                }
            }
        }
    }
    if (session->has_local_grid &&
        session->s4_mutation_serial == old_mutation_serial &&
        (apta_s4_range_changed(
             &old_requested,
             &session->local_grid_requested_range) ||
         apta_s4_range_changed(
             &old_applicability,
             &session->local_grid_applicability_range))) {
        session->s4_mutation_serial += 1u;
    }

    pending = apta_internal_s4_pending_features(session);
    if (pending != 0u) {
        apta_status_t publish_status =
            apta_internal_publish_result(session, pending);
        if (publish_status < 0) {
            return publish_status;
        }
        apta_internal_s4_mark_published(session);
        *published_output_out = 1u;
        work_progress->changed_features |= pending;
        work_progress->published_generation = session->generation;
        if (status == APTA_STATUS_WOULD_BLOCK) {
            status = APTA_STATUS_OK;
        }
    }

    return status;
}
