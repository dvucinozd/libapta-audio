// SPDX-License-Identifier: Apache-2.0
/* WP1 iteration 1: synthetic transient novelty and onset-ring rollover. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",               \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void set_bin(
    apta_session_t *session,
    uint64_t bin_index,
    float low,
    float mid,
    float high)
{
    const float bands[APTA_INTERNAL_BAND_COUNT] = {low, mid, high};
    apta_internal_onset_bin_t *bin =
        &session->onset_bins[bin_index % session->onset_bin_capacity];
    float broadband = low + mid + high;
    uint32_t band;

    if (broadband > 1.0f) {
        broadband = 1.0f;
    }
    memset(bin, 0, sizeof(*bin));
    bin->occupied = 1u;
    bin->bin_index = (uint32_t)bin_index;
    bin->sample_count = APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        bin->sums.multiband.band_sums[band] = (uint16_t)(
            bands[band] * (float)APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE *
            (float)APTA_INTERNAL_ONSET_FRAMES_PER_BIN);
    }
    bin->sums.multiband.broadband_sum = (uint16_t)(
        broadband * (float)APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE *
        (float)APTA_INTERNAL_ONSET_FRAMES_PER_BIN);
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    uint64_t first;
    uint64_t end;
    uint64_t index;
    uint32_t completed_steps = 0u;
    apta_status_t status;
    float rollover_flux;
    float reference_flux;

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
        (uint64_t)(APTA_INTERNAL_ONSET_BIN_CAPACITY * 2u) *
        APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    session_config.requested_features = capabilities;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(apta_internal_s4_prepare(session) == APTA_STATUS_OK);

    /* Start just before the modulo boundary, then retain one complete run
     * across it. This exercises production ring addressing, not a test-only
     * copy of the novelty function. */
    first = session->onset_bin_capacity - 8u;
    end = first + APTA_INTERNAL_MIN_TEMPO_BINS;
    for (index = first; index < end; ++index) {
        set_bin(session, index, 0.0f, 0.0f, 0.0f);
    }

    /* Identical isolated low-band impulses on each side of rollover. */
    set_bin(session, first + 9u, 0.20f, 0.0f, 0.0f);
    set_bin(session, first + 40u, 0.20f, 0.0f, 0.0f);

    /* Sustained tone: only its leading edge is novel. */
    for (index = first + 64u; index <= first + 70u; ++index) {
        set_bin(session, index, 0.0f, 0.18f, 0.0f);
    }

    /* Syncopated attacks. */
    set_bin(session, first + 96u, 0.12f, 0.08f, 0.0f);
    set_bin(session, first + 101u, 0.12f, 0.08f, 0.0f);
    set_bin(session, first + 108u, 0.12f, 0.08f, 0.0f);

    /* Kick/snare alternation and off-beat hats. */
    set_bin(session, first + 128u, 0.20f, 0.0f, 0.0f);
    set_bin(session, first + 136u, 0.0f, 0.20f, 0.0f);
    set_bin(session, first + 144u, 0.20f, 0.0f, 0.0f);
    set_bin(session, first + 152u, 0.0f, 0.20f, 0.0f);
    set_bin(session, first + 132u, 0.0f, 0.0f, 0.12f);
    set_bin(session, first + 140u, 0.0f, 0.0f, 0.12f);
    set_bin(session, first + 148u, 0.0f, 0.0f, 0.12f);

    session->s4_evidence_first = first;
    session->s4_evidence_end = end;
    session->s4_evidence_valid = 1u;
    session->s4_evidence_dirty = 0u;
    status = apta_internal_s4_refresh(session, 1u, &completed_steps);
    CHECK(status == APTA_STATUS_MORE_WORK);
    CHECK(completed_steps == 1u);
    CHECK(session->s4_refresh_evidence_first == first);
    CHECK(session->s4_refresh_evidence_end == end);

    /* Silence and the body of a sustained tone carry no novelty. */
    CHECK(session->onset_flux[20u] == 0.0f);
    CHECK(session->onset_flux[64u] > 0.0f);
    CHECK(session->onset_flux[65u] == 0.0f);
    CHECK(session->onset_flux[70u] == 0.0f);

    CHECK(session->onset_flux[96u] > 0.0f);
    CHECK(session->onset_flux[101u] > 0.0f);
    CHECK(session->onset_flux[108u] > 0.0f);
    CHECK(session->onset_flux[128u] > 0.0f);
    CHECK(session->onset_flux[132u] > 0.0f);
    CHECK(session->onset_flux[136u] > 0.0f);
    CHECK(session->onset_flux[140u] > 0.0f);
    CHECK(session->onset_flux[144u] > 0.0f);
    CHECK(session->onset_flux[148u] > 0.0f);
    CHECK(session->onset_flux[152u] > 0.0f);

    rollover_flux = session->onset_flux[9u];
    reference_flux = session->onset_flux[40u];
    CHECK(rollover_flux > 0.0f);
    CHECK(fabsf(rollover_flux - reference_flux) < 1.0e-6f);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    apta_context_destroy(context);
    return 0;
}
