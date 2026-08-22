// SPDX-License-Identifier: Apache-2.0
#include "apta_key_internal.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

static const float apta_key_frequencies[APTA_INTERNAL_KEY_BIN_COUNT] = {
    130.8128f, 138.5913f, 146.8324f, 155.5635f, 164.8138f, 174.6141f,
    184.9972f, 195.9977f, 207.6523f, 220.0000f, 233.0819f, 246.9417f,
    261.6256f, 277.1826f, 293.6648f, 311.1270f, 329.6276f, 349.2282f,
    369.9944f, 391.9954f, 415.3047f, 440.0000f, 466.1638f, 493.8833f,
    523.2511f, 554.3653f, 587.3295f, 622.2540f, 659.2551f, 698.4565f,
    739.9888f, 783.9909f, 830.6094f, 880.0000f, 932.3275f, 987.7666f
};

static const float apta_major_profile[APTA_INTERNAL_KEY_PITCH_CLASSES] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

static const float apta_minor_profile[APTA_INTERNAL_KEY_PITCH_CLASSES] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

static float apta_key_profile_score(
    const float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES],
    uint32_t tonic,
    const float profile[APTA_INTERNAL_KEY_PITCH_CLASSES])
{
    float dot = 0.0f;
    float chroma_norm = 0.0f;
    float profile_norm = 0.0f;
    uint32_t pitch;

    for (pitch = 0u; pitch < APTA_INTERNAL_KEY_PITCH_CLASSES; ++pitch) {
        const uint32_t rotated =
            (pitch + APTA_INTERNAL_KEY_PITCH_CLASSES - tonic) %
            APTA_INTERNAL_KEY_PITCH_CLASSES;
        dot += chroma[pitch] * profile[rotated];
        chroma_norm += chroma[pitch] * chroma[pitch];
        profile_norm += profile[rotated] * profile[rotated];
    }
    if (chroma_norm <= 1e-20f || profile_norm <= 1e-20f) {
        return 0.0f;
    }
    return dot / sqrtf(chroma_norm * profile_norm);
}

static void apta_key_insert_candidate(
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT],
    float scores[APTA_INTERNAL_KEY_CANDIDATE_COUNT],
    uint8_t tonic,
    apta_key_mode_t mode,
    float score)
{
    uint32_t position;
    uint32_t move;

    for (position = 0u; position < APTA_INTERNAL_KEY_CANDIDATE_COUNT; ++position) {
        if (score > scores[position]) {
            break;
        }
    }
    if (position == APTA_INTERNAL_KEY_CANDIDATE_COUNT) {
        return;
    }
    for (move = APTA_INTERNAL_KEY_CANDIDATE_COUNT - 1u;
         move > position;
         --move) {
        scores[move] = scores[move - 1u];
        candidates[move] = candidates[move - 1u];
    }
    memset(&candidates[position], 0, sizeof(candidates[position]));
    candidates[position].tonic = tonic;
    candidates[position].mode = mode;
    candidates[position].tuning_offset_cents = 0;
    candidates[position].score = (uint16_t)fminf(
        65535.0f,
        fmaxf(0.0f, score * 65535.0f + 0.5f));
    scores[position] = score;
}

apta_status_t apta_internal_key_select_chroma(
    const float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES],
    uint32_t completed_windows,
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT],
    apta_key_view_t *view_out)
{
    float scores[APTA_INTERNAL_KEY_CANDIDATE_COUNT] = {-1.0f, -1.0f, -1.0f};
    float energy = 0.0f;
    float separation;
    uint32_t tonic;
    uint32_t pitch;
    uint32_t confidence;

    if (chroma == NULL || candidates == NULL || view_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    for (pitch = 0u; pitch < APTA_INTERNAL_KEY_PITCH_CLASSES; ++pitch) {
        if (!isfinite(chroma[pitch]) || chroma[pitch] < 0.0f) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        energy += chroma[pitch];
    }
    if (completed_windows == 0u || energy <= 1e-12f) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    memset(candidates, 0,
           sizeof(*candidates) * APTA_INTERNAL_KEY_CANDIDATE_COUNT);
    for (tonic = 0u; tonic < APTA_INTERNAL_KEY_PITCH_CLASSES; ++tonic) {
        apta_key_insert_candidate(
            candidates,
            scores,
            (uint8_t)tonic,
            APTA_KEY_MODE_MAJOR,
            apta_key_profile_score(chroma, tonic, apta_major_profile));
        apta_key_insert_candidate(
            candidates,
            scores,
            (uint8_t)tonic,
            APTA_KEY_MODE_MINOR,
            apta_key_profile_score(chroma, tonic, apta_minor_profile));
    }

    /* The wire contract orders candidates by the encoded uint16 score, not by
     * the higher-precision float used for ranking. Distinct float scores can
     * round to the same 16-bit value (and flat/broadband material can produce
     * exact ties), which made a native MKEY result impossible to serialize.
     * Preserve the float ranking and resolve only encoded ties by one LSB. */
    for (pitch = 1u; pitch < APTA_INTERNAL_KEY_CANDIDATE_COUNT; ++pitch) {
        if (candidates[pitch].score >= candidates[pitch - 1u].score) {
            candidates[pitch].score =
                candidates[pitch - 1u].score > 0u
                    ? (uint16_t)(candidates[pitch - 1u].score - 1u)
                    : 0u;
        }
    }

    separation = scores[0] > scores[1] ? scores[0] - scores[1] : 0.0f;
    confidence = 25u +
                 (completed_windows >= 8u ? 40u : completed_windows * 5u) +
                 (uint32_t)fminf(35.0f, separation * 350.0f + 0.5f);
    if (confidence > APTA_CONFIDENCE_MAX) {
        confidence = APTA_CONFIDENCE_MAX;
    }
    candidates[0].confidence = (apta_confidence_value_t)confidence;
    candidates[1].confidence = confidence > 10u
                                   ? (apta_confidence_value_t)(confidence - 10u)
                                   : 0u;
    candidates[2].confidence = confidence > 20u
                                   ? (apta_confidence_value_t)(confidence - 20u)
                                   : 0u;

    apta_key_view_init(view_out);
    view_out->mode = candidates[0].mode;
    view_out->tonic = candidates[0].tonic;
    view_out->tuning_offset_cents = 0;
    view_out->confidence = candidates[0].confidence;
    view_out->state = completed_windows >= APTA_INTERNAL_KEY_STABLE_WINDOWS
                          ? APTA_FEATURE_STABLE
                          : APTA_FEATURE_PROVISIONAL;
    view_out->candidate_count = APTA_INTERNAL_KEY_CANDIDATE_COUNT;
    view_out->candidates = candidates;
    return APTA_STATUS_OK;
}

static void apta_key_reset_window(apta_internal_key_analysis_t *analysis)
{
    memset(analysis->q1, 0, sizeof(analysis->q1));
    memset(analysis->q2, 0, sizeof(analysis->q2));
    analysis->decimation_sum = 0.0f;
    analysis->decimation_count = 0u;
    analysis->window_samples = 0u;
}

static void apta_key_initialize(
    apta_internal_key_analysis_t *analysis,
    uint32_t source_sample_rate)
{
    const float decimated_rate =
        (float)source_sample_rate / (float)APTA_INTERNAL_KEY_DECIMATION;
    uint32_t bin;

    memset(analysis, 0, sizeof(*analysis));
    if (decimated_rate <= 2.0f * apta_key_frequencies[APTA_INTERNAL_KEY_BIN_COUNT - 1u]) {
        return;
    }
    for (bin = 0u; bin < APTA_INTERNAL_KEY_BIN_COUNT; ++bin) {
        analysis->coefficients[bin] =
            2.0f * cosf(6.28318530718f * apta_key_frequencies[bin] /
                        decimated_rate);
    }
    analysis->window_target_samples =
        source_sample_rate / APTA_INTERNAL_KEY_DECIMATION;
    if (analysis->window_target_samples == 0u) {
        return;
    }
    analysis->initialized = 1u;
}

static void apta_key_finish_window(apta_internal_key_analysis_t *analysis)
{
    uint32_t bin;

    for (bin = 0u; bin < APTA_INTERNAL_KEY_BIN_COUNT; ++bin) {
        const float q1 = analysis->q1[bin];
        const float q2 = analysis->q2[bin];
        float energy = q1 * q1 + q2 * q2 -
                       analysis->coefficients[bin] * q1 * q2;
        if (!isfinite(energy) || energy < 0.0f) {
            energy = 0.0f;
        }
        analysis->chroma[bin % APTA_INTERNAL_KEY_PITCH_CLASSES] += energy;
    }
    analysis->completed_windows += 1u;
    apta_key_reset_window(analysis);
}

void apta_internal_key_feed_sample(
    apta_session_t *session,
    float sample,
    apta_source_frame_t source_frame)
{
    apta_internal_key_analysis_t *analysis;
    float decimated_sample;
    uint32_t bin;

    if (session == NULL ||
        (session->config.requested_features & APTA_FEATURE_MUSICAL_KEY) == 0u) {
        return;
    }
    analysis = &session->key_analysis;
    if (!analysis->initialized) {
        apta_key_initialize(analysis, session->config.source_sample_rate);
        if (!analysis->initialized) {
            return;
        }
    }

    if (analysis->has_next_source_frame &&
        source_frame != analysis->next_source_frame) {
        apta_key_reset_window(analysis);
    }
    if (analysis->completed_windows == 0u && analysis->window_samples == 0u &&
        analysis->decimation_count == 0u) {
        analysis->evidence_first_frame = source_frame;
    }
    analysis->next_source_frame = source_frame + 1u;
    analysis->has_next_source_frame = 1u;
    analysis->evidence_end_frame = source_frame + 1u;

    analysis->decimation_sum += sample;
    analysis->decimation_count += 1u;
    if (analysis->decimation_count < APTA_INTERNAL_KEY_DECIMATION) {
        return;
    }
    decimated_sample =
        analysis->decimation_sum / (float)APTA_INTERNAL_KEY_DECIMATION;
    analysis->decimation_sum = 0.0f;
    analysis->decimation_count = 0u;

    for (bin = 0u; bin < APTA_INTERNAL_KEY_BIN_COUNT; ++bin) {
        const float q0 = decimated_sample +
                         analysis->coefficients[bin] * analysis->q1[bin] -
                         analysis->q2[bin];
        analysis->q2[bin] = analysis->q1[bin];
        analysis->q1[bin] = q0;
    }
    analysis->window_samples += 1u;
    if (analysis->window_samples >= analysis->window_target_samples) {
        apta_key_finish_window(analysis);
    }
}

int apta_internal_key_refresh_pending(const apta_session_t *session)
{
    const apta_internal_key_analysis_t *analysis;

    if (session == NULL ||
        (session->config.requested_features & APTA_FEATURE_MUSICAL_KEY) == 0u) {
        return 0;
    }
    analysis = &session->key_analysis;
    if (analysis->completed_windows == analysis->selected_windows) {
        return 0;
    }
    return analysis->selected_windows == 0u ||
           analysis->completed_windows >=
               analysis->selected_windows + APTA_INTERNAL_KEY_PUBLISH_INTERVAL ||
           session->end_of_input_signalled;
}

apta_status_t apta_internal_key_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out)
{
    apta_key_candidate_t next_candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT];
    apta_key_view_t next_view;
    apta_status_t status;
    int changed;

    if (session == NULL || completed_steps_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *completed_steps_out = 0u;
    if (!apta_internal_key_refresh_pending(session)) {
        return APTA_STATUS_OK;
    }
    if (step_limit == 0u) {
        return APTA_STATUS_MORE_WORK;
    }
    *completed_steps_out = 1u;

    status = apta_internal_key_select_chroma(
        session->key_analysis.chroma,
        session->key_analysis.completed_windows,
        next_candidates,
        &next_view);
    session->key_analysis.selected_windows =
        session->key_analysis.completed_windows;
    if (status == APTA_STATUS_NOT_AVAILABLE) {
        return APTA_STATUS_OK;
    }
    if (status < 0) {
        return status;
    }
    apta_frame_range_init(&next_view.applicability_range);
    next_view.applicability_range.first_frame =
        session->key_analysis.evidence_first_frame;
    next_view.applicability_range.end_frame =
        session->key_analysis.evidence_end_frame;

    changed = !session->has_key ||
              session->key_value.mode != next_view.mode ||
              session->key_value.tonic != next_view.tonic ||
              session->key_value.state != next_view.state ||
              session->key_value.confidence != next_view.confidence ||
              session->key_candidates[0].score != next_candidates[0].score;
    session->key_value = next_view;
    memcpy(
        session->key_candidates,
        next_candidates,
        sizeof(next_candidates));
    session->key_value.candidates = session->key_candidates;
    session->has_key = 1u;
    if (changed) {
        session->key_mutation_serial += 1u;
    }
    return APTA_STATUS_OK;
}

apta_feature_mask_t apta_internal_key_pending_features(
    const apta_session_t *session)
{
    if (session == NULL || !session->has_key ||
        session->key_mutation_serial == session->key_published_serial) {
        return 0u;
    }
    return APTA_FEATURE_MUSICAL_KEY;
}

void apta_internal_key_mark_published(apta_session_t *session)
{
    if (session != NULL) {
        session->key_published_serial = session->key_mutation_serial;
    }
}

static void apta_key_snapshot_view(
    const apta_session_t *session,
    apta_key_view_t *view,
    apta_key_candidate_t *candidates,
    int completed)
{
    *view = session->key_value;
    memcpy(
        candidates,
        session->key_candidates,
        sizeof(session->key_candidates));
    view->candidates = candidates;
    if (completed) {
        view->state = APTA_FEATURE_FINAL;
    }
}

apta_status_t apta_internal_key_build_snapshot(
    const apta_session_t *session,
    apta_result_t *result)
{
    const int completed =
        atomic_load_explicit(&session->state, memory_order_acquire) ==
        APTA_SESSION_COMPLETED;

    if (session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!session->has_key ||
        (session->config.requested_features & APTA_FEATURE_MUSICAL_KEY) == 0u) {
        return APTA_STATUS_OK;
    }
    result->key_candidates =
        (apta_key_candidate_t *)apta_internal_context_allocate(
            session->context,
            sizeof(session->key_candidates),
            alignof(apta_key_candidate_t),
            APTA_MEMORY_PERSISTENT);
    if (result->key_candidates == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    apta_key_snapshot_view(
        session,
        &result->key,
        result->key_candidates,
        completed);
    result->info.available_features |= APTA_FEATURE_MUSICAL_KEY;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_key_pool_build(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    const apta_internal_result_pool_layout_t *layout;
    uint8_t *storage;
    const int completed =
        atomic_load_explicit(&session->state, memory_order_acquire) ==
        APTA_SESSION_COMPLETED;

    if (pool == NULL || session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!session->has_key ||
        (session->config.requested_features & APTA_FEATURE_MUSICAL_KEY) == 0u) {
        return APTA_STATUS_OK;
    }
    layout = apta_internal_result_pool_get_layout(pool);
    storage = (uint8_t *)apta_internal_result_pool_get_slot_storage(
        pool, result->result_pool_slot_index);
    if (layout == NULL || storage == NULL ||
        layout->key_candidate_capacity < APTA_INTERNAL_KEY_CANDIDATE_COUNT) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    result->key_candidates = (apta_key_candidate_t *)(void *)(
        storage + layout->key_candidates_offset);
    apta_key_snapshot_view(
        session,
        &result->key,
        result->key_candidates,
        completed);
    result->info.available_features |= APTA_FEATURE_MUSICAL_KEY;
    return APTA_STATUS_OK;
}
