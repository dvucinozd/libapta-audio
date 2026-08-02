// SPDX-License-Identifier: Apache-2.0
#include "../../src/core/apta_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static void measure_sine(
    float frequency,
    float sums[APTA_INTERNAL_BAND_COUNT])
{
    const uint32_t sample_rate = 48000u;
    const uint32_t sample_count = 48000u;
    apta_internal_band_filter_t filter;
    uint32_t frame;

    apta_internal_band_filter_init(&filter, sample_rate);
    for (frame = 0u; frame < sample_count; ++frame) {
        float bands[APTA_INTERNAL_BAND_COUNT];
        const float phase =
            6.2831853f * frequency * (float)frame / (float)sample_rate;
        uint32_t band;

        apta_internal_band_filter_split(&filter, sinf(phase), bands);
        if (frame < sample_count / 2u) {
            continue;
        }
        for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
            sums[band] += fabsf(bands[band]);
        }
    }
}

int main(void)
{
    apta_internal_band_filter_t filter;
    float low_tone[APTA_INTERNAL_BAND_COUNT] = {0.0f, 0.0f, 0.0f};
    float high_tone[APTA_INTERNAL_BAND_COUNT] = {0.0f, 0.0f, 0.0f};

    CHECK(sizeof(apta_internal_onset_bin_t) == 16u);
    CHECK(APTA_INTERNAL_ONSET_FRAMES_PER_BIN *
              APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE <= UINT16_MAX);

    apta_internal_band_filter_init(&filter, 48000u);
    CHECK(filter.low_coefficient > 0.0f);
    CHECK(filter.low_coefficient < filter.mid_coefficient);
    CHECK(filter.mid_coefficient < 1.0f);
    apta_internal_band_filter_split(&filter, 1.0f, low_tone);
    apta_internal_band_filter_reset(&filter);
    CHECK(filter.low_state == 0.0f);
    CHECK(filter.mid_state == 0.0f);

    low_tone[0] = low_tone[1] = low_tone[2] = 0.0f;
    measure_sine(100.0f, low_tone);
    measure_sine(5000.0f, high_tone);
    CHECK(low_tone[0] > low_tone[1]);
    CHECK(low_tone[0] > low_tone[2]);
    CHECK(high_tone[2] > high_tone[0]);
    CHECK(high_tone[2] > high_tone[1]);

    return 0;
}
