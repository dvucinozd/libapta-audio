// SPDX-License-Identifier: Apache-2.0
/* Development trace must expose exact normalized multiband onset evidence. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"
#include "apta_s4_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",               \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_internal_onset_bin_t *bin;
    float bands[APTA_INTERNAL_BAND_COUNT];
    float broadband = 0.0f;
    uint32_t band;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames =
        UINT64_C(64) * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    session_config.requested_features = capabilities;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(apta_internal_s4_prepare(session) == APTA_STATUS_OK);

    bin = &session->onset_bins[7u % session->onset_bin_capacity];
    memset(bin, 0, sizeof(*bin));
    bin->occupied = 1u;
    bin->bin_index = 7u;
    bin->sample_count = APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    bin->sums.multiband.band_sums[0] = 16320u;
    bin->sums.multiband.band_sums[1] = 8160u;
    bin->sums.multiband.band_sums[2] = 4080u;
    bin->sums.multiband.broadband_sum = 28560u;
#ifdef APTA_INTERNAL_TRANSIENT_LATTICE_I8
    bin->reserved8 = 191u;
#endif
    session->s4_refresh_evidence_first = 7u;
    session->s4_refresh_evidence_end = 8u;

    CHECK(apta_internal_s4_trace_energy_at(
              session, 0u, bands, &broadband));
    CHECK(fabsf(bands[0] - 0.25f) < 1.0e-6f);
    CHECK(fabsf(bands[1] - 0.125f) < 1.0e-6f);
    CHECK(fabsf(bands[2] - 0.0625f) < 1.0e-6f);
    CHECK(fabsf(broadband - 0.4375f) < 1.0e-6f);
#ifdef APTA_INTERNAL_TRANSIENT_LATTICE_I8
    {
        float peak = 0.0f;

        CHECK(apta_internal_s4_trace_peak_at(session, 0u, &peak));
        CHECK(fabsf(peak - 191.0f / 255.0f) < 1.0e-6f);
        CHECK(!apta_internal_s4_trace_peak_at(session, 1u, &peak));
        CHECK(!apta_internal_s4_trace_peak_at(session, 0u, NULL));
    }
#endif
    CHECK(!apta_internal_s4_trace_energy_at(
              session, 1u, bands, &broadband));
    CHECK(!apta_internal_s4_trace_energy_at(
              session, 0u, NULL, &broadband));
    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        CHECK(isfinite(bands[band]));
    }

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    apta_context_destroy(context);
    return 0;
}
