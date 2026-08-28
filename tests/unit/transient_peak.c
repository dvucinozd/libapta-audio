// SPDX-License-Identifier: Apache-2.0
#include "../../src/core/apta_internal.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr,                                                  \
                    "check failed at %s:%d: %s\n",                          \
                    __FILE__,                                                \
                    __LINE__,                                                \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static apta_status_t process_magnitude(
    apta_session_t *session,
    apta_source_frame_t frame,
    float magnitude)
{
    const float bands[APTA_INTERNAL_BAND_COUNT] = {
        magnitude, 0.0f, 0.0f};

    return apta_internal_s4_process_sample(session, frame, bands);
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_internal_onset_bin_t *bin;
    apta_source_frame_t replacement_frame;

    CHECK(sizeof(apta_internal_onset_bin_t) == 16u);
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames =
        ((uint64_t)APTA_INTERNAL_ONSET_BIN_CAPACITY + 1u) *
        APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    session_config.requested_features = capabilities;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(apta_internal_s4_prepare(session) == APTA_STATUS_OK);

    CHECK(process_magnitude(session, 0u, 0.0f) == APTA_STATUS_OK);
    bin = &session->onset_bins[0];
    CHECK(bin->reserved8 == 0u);
    CHECK(process_magnitude(session, 1u, 0.5f) == APTA_STATUS_OK);
    CHECK(bin->reserved8 == 127u);
    CHECK(process_magnitude(session, 2u, 0.25f) == APTA_STATUS_OK);
    CHECK(bin->reserved8 == 127u);
    CHECK(process_magnitude(session, 3u, 1.0f) == APTA_STATUS_OK);
    CHECK(bin->reserved8 == 255u);
    CHECK(process_magnitude(session, 4u, 0.75f) == APTA_STATUS_OK);
    CHECK(bin->reserved8 == 255u);

    replacement_frame =
        (apta_source_frame_t)session->onset_bin_capacity *
        APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    CHECK(process_magnitude(session, replacement_frame, 0.25f) ==
          APTA_STATUS_OK);
    CHECK(bin->bin_index == session->onset_bin_capacity);
    CHECK(bin->sample_count == 1u);
    CHECK(bin->reserved8 == 63u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    apta_context_destroy(context);
    return 0;
}
