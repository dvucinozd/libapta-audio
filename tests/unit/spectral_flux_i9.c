// SPDX-License-Identifier: Apache-2.0
#include "../../src/core/apta_internal.h"

#include <math.h>
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

#define SAMPLE_RATE 48000u
#define TWO_PI 6.28318530717958647692

typedef struct {
    apta_context_t *context;
    apta_session_t *session;
} fixture_t;

static int fixture_create(fixture_t *fixture)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    apta_context_config_t context_config;
    apta_session_config_t session_config;

    fixture->context = NULL;
    fixture->session = NULL;
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = capabilities;
    if (apta_context_create(&context_config, &fixture->context) !=
        APTA_STATUS_OK) {
        return 0;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 8192u;
    session_config.requested_features = capabilities;
    if (apta_session_create(
            fixture->context, &session_config, &fixture->session) !=
        APTA_STATUS_OK) {
        apta_context_destroy(fixture->context);
        fixture->context = NULL;
        return 0;
    }
    return apta_internal_s4_prepare(fixture->session) == APTA_STATUS_OK;
}

static void fixture_destroy(fixture_t *fixture)
{
    if (fixture->session != NULL) {
        (void)apta_session_destroy(fixture->session);
    }
    apta_context_destroy(fixture->context);
    fixture->session = NULL;
    fixture->context = NULL;
}

static int feed_sample(
    apta_session_t *session,
    apta_source_frame_t frame,
    float sample)
{
    const float bands[APTA_INTERNAL_BAND_COUNT] = {sample, 0.0f, 0.0f};

    return apta_internal_s4_process_sample(session, frame, bands) ==
           APTA_STATUS_OK;
}

static int feed_sine(
    apta_session_t *session,
    apta_source_frame_t first,
    uint32_t count,
    float frequency,
    float amplitude)
{
    uint32_t offset;

    for (offset = 0u; offset < count; ++offset) {
        const double phase =
            TWO_PI * (double)frequency * (double)offset / SAMPLE_RATE;
        if (!feed_sample(
                session,
                first + offset,
                amplitude * (float)sin(phase))) {
            return 0;
        }
    }
    return 1;
}

static uint16_t maximum_flux(
    const apta_session_t *session,
    uint32_t first_bin,
    uint32_t end_bin)
{
    uint16_t maximum = 0u;
    uint32_t bin;

    for (bin = first_bin; bin < end_bin; ++bin) {
        const uint16_t value = session->spectral_flux_i9.bin_flux[
            bin % APTA_INTERNAL_ONSET_BIN_CAPACITY];
        if (value > maximum) {
            maximum = value;
        }
    }
    return maximum;
}

static int test_silence_and_partial_window(void)
{
    fixture_t fixture;
    uint32_t frame;

    CHECK(fixture_create(&fixture));
    CHECK(sizeof(apta_internal_onset_bin_t) == 16u);
    CHECK(fixture.session->spectral_flux_i9.initialized);
    CHECK(fixture.session->spectral_flux_i9.bin_flux != NULL);
    for (frame = 0u; frame < 511u; ++frame) {
        CHECK(feed_sample(fixture.session, frame, 0.0f));
    }
    CHECK(maximum_flux(fixture.session, 0u, 4u) == 0u);
    for (; frame < 1024u; ++frame) {
        CHECK(feed_sample(fixture.session, frame, 0.0f));
    }
    CHECK(maximum_flux(fixture.session, 0u, 4u) == 0u);
    fixture_destroy(&fixture);
    return 0;
}

static int test_stationary_and_frequency_change(void)
{
    fixture_t fixture;
    uint16_t stationary_maximum;
    uint16_t changed_maximum;

    CHECK(fixture_create(&fixture));
    CHECK(feed_sine(fixture.session, 0u, 1024u, 750.0f, 0.5f));
    stationary_maximum = maximum_flux(fixture.session, 0u, 4u);
    CHECK(stationary_maximum <= 16u);

    CHECK(feed_sine(fixture.session, 1024u, 1024u, 3000.0f, 0.5f));
    changed_maximum = maximum_flux(fixture.session, 4u, 8u);
    CHECK(changed_maximum > 1000u);
    CHECK(changed_maximum > stationary_maximum);
    fixture_destroy(&fixture);
    return 0;
}

static int test_impulse_and_trace_alignment(void)
{
    fixture_t fixture;
    uint32_t frame;
    float traced = 0.0f;

    CHECK(fixture_create(&fixture));
    for (frame = 0u; frame < 1024u; ++frame) {
        CHECK(feed_sample(fixture.session, frame, 0.0f));
    }
    CHECK(feed_sample(fixture.session, 1024u, 1.0f));
    for (frame = 1025u; frame < 1536u; ++frame) {
        CHECK(feed_sample(fixture.session, frame, 0.0f));
    }
    /* The first frame that observes the impulse is centred 255 frames behind
     * the newest sample, so its trace contribution belongs to bin 3. */
    CHECK(maximum_flux(fixture.session, 3u, 5u) > 60000u);

    fixture.session->s4_refresh_evidence_first = 3u;
    fixture.session->s4_refresh_evidence_end = 5u;
    CHECK(apta_internal_spectral_flux_i9_trace_at(
        fixture.session, 0u, &traced));
    CHECK(traced > 0.9f && traced <= 1.0f);
    CHECK(!apta_internal_spectral_flux_i9_trace_at(
        fixture.session, 2u, &traced));
    CHECK(!apta_internal_spectral_flux_i9_trace_at(
        fixture.session, 0u, NULL));
    fixture_destroy(&fixture);
    return 0;
}

static int test_amplitude_only_rise(void)
{
    fixture_t fixture;
    uint16_t quiet_maximum;
    uint16_t louder_maximum;
    uint16_t settled_maximum;

    CHECK(fixture_create(&fixture));
    CHECK(feed_sine(fixture.session, 0u, 1024u, 750.0f, 0.1f));
    quiet_maximum = maximum_flux(fixture.session, 0u, 4u);
    CHECK(feed_sine(fixture.session, 1024u, 1024u, 750.0f, 0.8f));
    louder_maximum = maximum_flux(fixture.session, 4u, 8u);
    settled_maximum = maximum_flux(fixture.session, 6u, 8u);

    /* A frame straddling the level edge is a transient, but steady normalized
     * spectral shape must settle back to the stationary floor. */
    CHECK(quiet_maximum <= 16u);
    CHECK(louder_maximum > quiet_maximum);
    CHECK(settled_maximum <= 16u);
    fixture_destroy(&fixture);
    return 0;
}

static int test_gap_reset_and_ring_replacement(void)
{
    fixture_t fixture;
    uint32_t frame;
    const uint32_t gap_first = 4096u;
    const uint32_t reference_bin = 17u;

    CHECK(fixture_create(&fixture));
    CHECK(feed_sine(fixture.session, 0u, 1024u, 750.0f, 0.5f));
    CHECK(feed_sine(
        fixture.session, gap_first, 512u, 3000.0f, 0.5f));
    CHECK(fixture.session->spectral_flux_i9.bin_flux[reference_bin] == 0u);

    fixture.session->spectral_flux_i9.bin_flux[0] = 1234u;
    apta_internal_spectral_flux_i9_reset_bin(
        fixture.session, APTA_INTERNAL_ONSET_BIN_CAPACITY);
    CHECK(fixture.session->spectral_flux_i9.bin_flux[0] == 0u);

    for (frame = gap_first + 512u; frame < gap_first + 768u; ++frame) {
        CHECK(feed_sample(fixture.session, frame, 0.0f));
    }
    fixture_destroy(&fixture);
    return 0;
}

int main(void)
{
    CHECK(test_silence_and_partial_window() == 0);
    CHECK(test_stationary_and_frequency_change() == 0);
    CHECK(test_impulse_and_trace_alignment() == 0);
    CHECK(test_amplitude_only_rise() == 0);
    CHECK(test_gap_reset_and_ring_replacement() == 0);
    return 0;
}
