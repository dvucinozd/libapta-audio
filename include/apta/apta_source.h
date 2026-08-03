// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_SOURCE_H
#define APTA_SOURCE_H

#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_PCM_BLOCK_FLAG_NONE       0u
#define APTA_PCM_BLOCK_FLAG_DISCONTINUITY (1u << 0)

#define APTA_FOCUS_FLAG_NONE           0u
#define APTA_REGION_REQUEST_FLAG_NONE  0u
#define APTA_WORK_BUDGET_FLAG_NONE     0u

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    const void *data;
    const void *planes[8];

    apta_source_frame_t first_frame;
    uint32_t frame_count;
    uint32_t flags;
    uint32_t reserved32[3];
} apta_pcm_block_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    apta_status_t (APTA_CALL *read_frames)(
        void *user_data,
        apta_source_frame_t first_frame,
        uint32_t requested_frames,
        apta_pcm_block_t *block_out);

    void (APTA_CALL *release_frames)(
        void *user_data,
        apta_pcm_block_t *block);

    uint64_t (APTA_CALL *get_total_frames)(void *user_data);
} apta_pcm_source_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_source_frame_t playhead_frame;
    uint64_t lookbehind_frames;
    uint64_t lookahead_frames;

    apta_feature_mask_t feature_mask;
    uint8_t priority;
    uint8_t flags;
    uint16_t reserved16;
    uint32_t reserved32[2];
} apta_focus_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t range;
    apta_feature_mask_t feature_mask;

    uint64_t soft_deadline_monotonic_ns;
    uint32_t request_id;

    uint8_t priority;
    uint8_t flags;
    uint16_t reserved16;
    uint32_t reserved32[2];
} apta_region_request_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t range;
    apta_feature_mask_t feature_mask;

    uint8_t priority;
    uint8_t flags;
    uint16_t reserved16;
    uint32_t request_token;
    uint32_t reserved32[2];
} apta_pcm_request_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t maximum_input_frames;
    uint32_t maximum_steps;
    uint32_t soft_time_budget_us;
    uint32_t flags;
} apta_work_budget_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t consumed_input_frames;
    uint32_t completed_steps;
    uint32_t active_request_id;
    uint32_t flags;

    apta_generation_t published_generation;
    apta_feature_mask_t changed_features;
} apta_progress_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t request_id;
    apta_request_state_t state;

    apta_frame_range_t requested_range;
    apta_feature_mask_t requested_features;
    apta_feature_mask_t satisfied_features;

    uint32_t progress_permille;
    uint32_t diagnostic_code;
    uint32_t flags;
    uint32_t reserved32[3];
} apta_request_progress_t;

APTA_API void APTA_CALL
apta_pcm_block_init(apta_pcm_block_t *block);

APTA_API void APTA_CALL
apta_pcm_source_init(apta_pcm_source_t *source);

APTA_API void APTA_CALL
apta_focus_init(apta_focus_t *focus);

APTA_API void APTA_CALL
apta_region_request_init(apta_region_request_t *request);

APTA_API void APTA_CALL
apta_pcm_request_init(apta_pcm_request_t *request);

APTA_API void APTA_CALL
apta_work_budget_init(apta_work_budget_t *budget);

APTA_API apta_status_t APTA_CALL
apta_session_set_source(
    apta_session_t *session,
    const apta_pcm_source_t *source);

APTA_API apta_status_t APTA_CALL
apta_session_push_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

APTA_API apta_status_t APTA_CALL
apta_session_signal_end_of_input(
    apta_session_t *session,
    apta_source_frame_t final_end_frame);

/*
 * Seed a session's overview coverage from a previously parsed result, so a
 * partially analysed track can be continued instead of rescanned.
 *
 * Callable only in APTA_SESSION_CREATED, like apta_session_set_source(), and
 * subject to the same host-serialization rule as every other mutating call.
 *
 * The result must carry APTA_FEATURE_WAVEFORM_OVERVIEW, and its column
 * geometry, known source sample rate, channel layout/count and known track
 * length must match the session, or APTA_ERROR_CONFLICT is returned. A length
 * that is unknown on either side is not a conflict.
 *
 * When both sides carry a source fingerprint, kind and bytes must match. A
 * missing identity is accepted by default because equal geometry does not prove
 * equal audio; hosts that require an identity set
 * APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING. See
 * docs/api/APTA-SESSION-SEEDING-0.1.md.
 *
 * Only waveform coverage is seeded. Tempo and beatgrid engines rebuild their
 * own evidence from the PCM that follows, because the onset timeline they need
 * is not in the container.
 */
APTA_API apta_status_t APTA_CALL
apta_session_seed_from_result(
    apta_session_t *session,
    const apta_result_t *result);

APTA_API apta_status_t APTA_CALL
apta_session_set_focus(
    apta_session_t *session,
    const apta_focus_t *focus);

APTA_API apta_status_t APTA_CALL
apta_session_request_region(
    apta_session_t *session,
    const apta_region_request_t *request,
    uint32_t *request_id_out);

APTA_API apta_status_t APTA_CALL
apta_session_cancel_region_request(
    apta_session_t *session,
    uint32_t request_id);

APTA_API apta_status_t APTA_CALL
apta_session_get_request_progress(
    const apta_session_t *session,
    uint32_t request_id,
    apta_request_progress_t *progress_out);

APTA_API apta_status_t APTA_CALL
apta_session_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

APTA_API apta_status_t APTA_CALL
apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out);

APTA_API void APTA_CALL
apta_session_request_cancel(apta_session_t *session);

APTA_API uint32_t APTA_CALL
apta_session_is_cancel_requested(const apta_session_t *session);

APTA_API apta_session_state_t APTA_CALL
apta_session_get_state(const apta_session_t *session);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_SOURCE_H */
