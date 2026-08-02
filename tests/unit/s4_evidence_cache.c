// SPDX-License-Identifier: Apache-2.0
/* A2 follow-up: incremental discovery of the contiguous onset-evidence run. */
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#include "apta_internal.h"

#define SAMPLE_RATE 48000u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int create_session(
    apta_context_t *context,
    apta_session_t **session_out)
{
    apta_session_config_t config;

    apta_session_config_init(&config);
    config.source_sample_rate = SAMPLE_RATE;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = UINT64_C(6000) *
                          APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    if (apta_session_create(context, &config, session_out) != APTA_STATUS_OK) {
        return 1;
    }
    if (apta_internal_s4_prepare(*session_out) != APTA_STATUS_OK) {
        (void)apta_session_destroy(*session_out);
        *session_out = NULL;
        return 1;
    }
    return 0;
}

static int fill_bin(
    apta_session_t *session,
    uint64_t bin_index,
    uint32_t sample_count)
{
    uint32_t sample;

    for (sample = 0u; sample < sample_count; ++sample) {
        apta_source_frame_t frame =
            bin_index * APTA_INTERNAL_ONSET_FRAMES_PER_BIN + sample;
        apta_status_t status =
            apta_internal_s4_process_sample(session, frame, 0.5f);
        if (status != APTA_STATUS_OK) {
            fprintf(stderr,
                    "process_sample failed: bin=%llu sample=%u status=%d\n",
                    (unsigned long long)bin_index,
                    sample,
                    (int)status);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    uint64_t bin;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(create_session(context, &session) == 0);

    for (bin = 0u; bin < APTA_INTERNAL_MIN_TEMPO_BINS; ++bin) {
        CHECK(fill_bin(
                  session,
                  bin,
                  APTA_INTERNAL_ONSET_FRAMES_PER_BIN) == 0);
    }
    CHECK(session->s4_evidence_valid != 0u);
    CHECK(session->s4_evidence_dirty == 0u);
    CHECK(session->s4_evidence_first == 0u);
    CHECK(session->s4_evidence_end == APTA_INTERNAL_MIN_TEMPO_BINS);
    CHECK(apta_internal_s4_refresh(session) == APTA_STATUS_OK);
    CHECK(session->s4_evidence_first == 0u);
    CHECK(session->s4_evidence_end == APTA_INTERNAL_MIN_TEMPO_BINS);

    /* Fill through one ordinary ring wrap. Replacing the oldest cached bin and
     * completing the newest one advances both ends without a dirty rebuild. */
    for (bin = APTA_INTERNAL_MIN_TEMPO_BINS;
         bin <= APTA_INTERNAL_ONSET_BIN_CAPACITY;
         ++bin) {
        CHECK(fill_bin(
                  session,
                  bin,
                  APTA_INTERNAL_ONSET_FRAMES_PER_BIN) == 0);
    }
    CHECK(session->s4_evidence_dirty == 0u);
    CHECK(session->s4_evidence_first == 1u);
    CHECK(session->s4_evidence_end ==
          (uint64_t)APTA_INTERNAL_ONSET_BIN_CAPACITY + 1u);

    /* An out-of-order overwrite inside the run splits it. The cache marks
     * itself dirty and the conservative scan finds the longer surviving side. */
    CHECK(fill_bin(session, 5000u, APTA_INTERNAL_ONSET_FRAMES_PER_BIN) == 0);
    CHECK(session->s4_evidence_dirty != 0u);
    CHECK(apta_internal_s4_refresh(session) == APTA_STATUS_OK);
    CHECK(session->s4_evidence_dirty == 0u);
    CHECK(session->s4_evidence_first == 905u);
    CHECK(session->s4_evidence_end ==
          (uint64_t)APTA_INTERNAL_ONSET_BIN_CAPACITY + 1u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    /* Adjacent out-of-order input extends the cached run backwards in O(1). */
    CHECK(create_session(context, &session) == 0);
    CHECK(fill_bin(session, 1u, APTA_INTERNAL_ONSET_FRAMES_PER_BIN) == 0);
    CHECK(fill_bin(session, 0u, APTA_INTERNAL_ONSET_FRAMES_PER_BIN) == 0);
    CHECK(session->s4_evidence_dirty == 0u);
    CHECK(session->s4_evidence_first == 0u);
    CHECK(session->s4_evidence_end == 2u);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    /* The last partial bin becomes complete only after final_end_frame is
     * known. End-of-input deliberately forces one full rebuild to include it. */
    CHECK(create_session(context, &session) == 0);
    CHECK(fill_bin(session, 0u, APTA_INTERNAL_ONSET_FRAMES_PER_BIN) == 0);
    CHECK(fill_bin(session, 1u, 17u) == 0);
    CHECK(session->s4_evidence_end == 1u);
    session->end_of_input_signalled = 1u;
    session->final_end_frame = APTA_INTERNAL_ONSET_FRAMES_PER_BIN + 17u;
    CHECK(apta_internal_s4_refresh(session) == APTA_STATUS_OK);
    CHECK(session->s4_evidence_first == 0u);
    CHECK(session->s4_evidence_end == 2u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
