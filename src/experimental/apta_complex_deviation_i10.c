// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_session_workspace.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

#define APTA_I10_TWO_PI 6.28318530717958647692f
#define APTA_I10_MIN_FREQUENCY_HZ 40u
#define APTA_I10_MAX_FREQUENCY_HZ 16000u
#define APTA_I10_NYQUIST_NUMERATOR 45u
#define APTA_I10_NYQUIST_DENOMINATOR 100u
#define APTA_I10_MAGNITUDE_FLOOR 1.0e-20f

_Static_assert(APTA_INTERNAL_I10_FFT_SIZE == 512u,
               "the frozen I10 FFT size is 512");
_Static_assert(APTA_INTERNAL_I10_HOP_FRAMES == 128u,
               "the frozen I10 hop is 128 source frames");
_Static_assert((APTA_INTERNAL_I10_FFT_SIZE &
                (APTA_INTERNAL_I10_FFT_SIZE - 1u)) == 0u,
               "I10 radix-2 FFT size must be a power of two");
_Static_assert(
    sizeof(apta_internal_complex_deviation_i10_state_t) +
            APTA_INTERNAL_ONSET_BIN_CAPACITY * sizeof(uint16_t) <=
        24u * 1024u,
    "I10 conditional persistent state must fit the frozen 24 KiB ceiling");

static void apta_i10_reset_run(
    apta_internal_complex_deviation_i10_state_t *state)
{
    state->run_sample_count = 0u;
    state->write_index = 0u;
    state->history_count = 0u;
}

static void apta_i10_fft(
    float real[APTA_INTERNAL_I10_FFT_SIZE],
    float imaginary[APTA_INTERNAL_I10_FFT_SIZE])
{
    uint32_t index;
    uint32_t reversed = 0u;
    uint32_t length;

    for (index = 1u; index < APTA_INTERNAL_I10_FFT_SIZE; ++index) {
        uint32_t bit = APTA_INTERNAL_I10_FFT_SIZE >> 1u;

        while ((reversed & bit) != 0u) {
            reversed ^= bit;
            bit >>= 1u;
        }
        reversed ^= bit;
        if (index < reversed) {
            const float real_swap = real[index];
            const float imaginary_swap = imaginary[index];

            real[index] = real[reversed];
            imaginary[index] = imaginary[reversed];
            real[reversed] = real_swap;
            imaginary[reversed] = imaginary_swap;
        }
    }

    for (length = 2u;
         length <= APTA_INTERNAL_I10_FFT_SIZE;
         length <<= 1u) {
        const float angle = -APTA_I10_TWO_PI / (float)length;
        const float step_real = cosf(angle);
        const float step_imaginary = sinf(angle);
        const uint32_t half = length >> 1u;
        uint32_t first;

        for (first = 0u;
             first < APTA_INTERNAL_I10_FFT_SIZE;
             first += length) {
            float twiddle_real = 1.0f;
            float twiddle_imaginary = 0.0f;
            uint32_t offset;

            for (offset = 0u; offset < half; ++offset) {
                const uint32_t even = first + offset;
                const uint32_t odd = even + half;
                const float odd_real =
                    real[odd] * twiddle_real -
                    imaginary[odd] * twiddle_imaginary;
                const float odd_imaginary =
                    real[odd] * twiddle_imaginary +
                    imaginary[odd] * twiddle_real;
                const float even_real = real[even];
                const float even_imaginary = imaginary[even];
                const float next_twiddle_real =
                    twiddle_real * step_real -
                    twiddle_imaginary * step_imaginary;

                real[even] = even_real + odd_real;
                imaginary[even] = even_imaginary + odd_imaginary;
                real[odd] = even_real - odd_real;
                imaginary[odd] = even_imaginary - odd_imaginary;
                twiddle_imaginary =
                    twiddle_real * step_imaginary +
                    twiddle_imaginary * step_real;
                twiddle_real = next_twiddle_real;
            }
        }
    }
}

static apta_status_t apta_i10_finish_frame(
    apta_session_t *session,
    apta_source_frame_t source_frame)
{
    apta_internal_complex_deviation_i10_state_t *state =
        session->complex_deviation_i10;
    float residual_sum = 0.0f;
    float magnitude_sum = 0.0f;
    float deviation = 0.0f;
    apta_source_frame_t centre_frame;
    uint64_t bin_index;
    uint32_t slot;
    uint32_t index;

    for (index = 0u; index < APTA_INTERNAL_I10_FFT_SIZE; ++index) {
        const uint32_t sample_index =
            (state->write_index + index) &
            (APTA_INTERNAL_I10_FFT_SIZE - 1u);

        state->fft_real[index] =
            state->samples[sample_index] * state->window[index];
        state->fft_imaginary[index] = 0.0f;
    }
    apta_i10_fft(state->fft_real, state->fft_imaginary);

    if (state->history_count >= 2u) {
        for (index = state->minimum_spectrum_bin;
             index <= state->maximum_spectrum_bin;
             ++index) {
            const float current_real = state->fft_real[index];
            const float current_imaginary = state->fft_imaginary[index];
            const float previous_real = state->previous_real[0][index];
            const float previous_imaginary =
                state->previous_imaginary[0][index];
            const float previous_previous_real =
                state->previous_real[1][index];
            const float previous_previous_imaginary =
                state->previous_imaginary[1][index];
            const float current_magnitude =
                hypotf(current_real, current_imaginary);
            const float previous_magnitude =
                hypotf(previous_real, previous_imaginary);
            const float previous_previous_magnitude =
                hypotf(previous_previous_real, previous_previous_imaginary);
            float prediction_real = 0.0f;
            float prediction_imaginary = 0.0f;

            if (previous_magnitude > APTA_I10_MAGNITUDE_FLOOR &&
                previous_previous_magnitude > APTA_I10_MAGNITUDE_FLOOR) {
                const float previous_unit_real =
                    previous_real / previous_magnitude;
                const float previous_unit_imaginary =
                    previous_imaginary / previous_magnitude;
                const float previous_previous_unit_real =
                    previous_previous_real / previous_previous_magnitude;
                const float previous_previous_unit_imaginary =
                    previous_previous_imaginary /
                    previous_previous_magnitude;
                const float rotation_real =
                    previous_unit_real * previous_previous_unit_real +
                    previous_unit_imaginary *
                        previous_previous_unit_imaginary;
                const float rotation_imaginary =
                    previous_unit_imaginary *
                        previous_previous_unit_real -
                    previous_unit_real *
                        previous_previous_unit_imaginary;

                prediction_real =
                    previous_real * rotation_real -
                    previous_imaginary * rotation_imaginary;
                prediction_imaginary =
                    previous_real * rotation_imaginary +
                    previous_imaginary * rotation_real;
            }
            residual_sum += hypotf(
                current_real - prediction_real,
                current_imaginary - prediction_imaginary);
            magnitude_sum += current_magnitude + previous_magnitude;
        }
        deviation = residual_sum /
                    (APTA_I10_MAGNITUDE_FLOOR + magnitude_sum);
    }

    for (index = state->minimum_spectrum_bin;
         index <= state->maximum_spectrum_bin;
         ++index) {
        state->previous_real[1][index] = state->previous_real[0][index];
        state->previous_imaginary[1][index] =
            state->previous_imaginary[0][index];
        state->previous_real[0][index] = state->fft_real[index];
        state->previous_imaginary[0][index] =
            state->fft_imaginary[index];
    }
    if (state->history_count < 2u) {
        state->history_count += 1u;
        deviation = 0.0f;
    }
    if (!isfinite(deviation)) {
        return APTA_ERROR_INTERNAL;
    }
    deviation = fminf(1.0f, fmaxf(0.0f, deviation));

    centre_frame =
        source_frame - (APTA_INTERNAL_I10_FFT_SIZE / 2u - 1u);
    bin_index = centre_frame / APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    if (bin_index > APTA_INTERNAL_MAX_BIN_INDEX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    slot = (uint32_t)(bin_index % APTA_INTERNAL_ONSET_BIN_CAPACITY);
    if (session->onset_bins == NULL ||
        !session->onset_bins[slot].occupied ||
        session->onset_bins[slot].bin_index != (uint32_t)bin_index) {
        return APTA_ERROR_INTERNAL;
    }
    {
        const uint16_t quantized =
            (uint16_t)floorf(deviation * 65535.0f + 0.5f);

        if (quantized > state->bin_deviation[slot]) {
            state->bin_deviation[slot] = quantized;
        }
    }
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_complex_deviation_i10_prepare(
    apta_session_t *session)
{
    apta_internal_complex_deviation_i10_state_t *state;
    uint64_t upper_frequency;
    uint32_t sample_rate;
    uint32_t index;
    size_t bytes;

    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    state = session->complex_deviation_i10;
    if (state != NULL && state->initialized) {
        return APTA_STATUS_OK;
    }
    sample_rate = session->config.source_sample_rate;
    if (sample_rate == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    state = (apta_internal_complex_deviation_i10_state_t *)
        apta_internal_session_allocate(
            session,
            sizeof(*state),
            alignof(apta_internal_complex_deviation_i10_state_t),
            APTA_MEMORY_PERSISTENT);
    if (state == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(state, 0, sizeof(*state));
    session->complex_deviation_i10 = state;

    bytes = (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY * sizeof(uint16_t);
    state->bin_deviation = (uint16_t *)apta_internal_session_allocate(
        session,
        bytes,
        alignof(uint16_t),
        APTA_MEMORY_PERSISTENT);
    if (state->bin_deviation == NULL) {
        apta_internal_context_deallocate(session->context, state);
        session->complex_deviation_i10 = NULL;
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(state->bin_deviation, 0, bytes);

    for (index = 0u; index < APTA_INTERNAL_I10_FFT_SIZE; ++index) {
        state->window[index] =
            0.5f - 0.5f * cosf(
                APTA_I10_TWO_PI * (float)index /
                (float)APTA_INTERNAL_I10_FFT_SIZE);
    }
    state->minimum_spectrum_bin = (uint32_t)(
        ((uint64_t)APTA_I10_MIN_FREQUENCY_HZ *
         APTA_INTERNAL_I10_FFT_SIZE + sample_rate - 1u) /
        sample_rate);
    if (state->minimum_spectrum_bin == 0u) {
        state->minimum_spectrum_bin = 1u;
    }
    upper_frequency =
        (uint64_t)sample_rate * APTA_I10_NYQUIST_NUMERATOR /
        APTA_I10_NYQUIST_DENOMINATOR;
    if (upper_frequency > APTA_I10_MAX_FREQUENCY_HZ) {
        upper_frequency = APTA_I10_MAX_FREQUENCY_HZ;
    }
    state->maximum_spectrum_bin = (uint32_t)(
        upper_frequency * APTA_INTERNAL_I10_FFT_SIZE / sample_rate);
    if (state->maximum_spectrum_bin >= APTA_INTERNAL_I10_SPECTRUM_BINS) {
        state->maximum_spectrum_bin =
            APTA_INTERNAL_I10_SPECTRUM_BINS - 1u;
    }
    if (state->maximum_spectrum_bin < state->minimum_spectrum_bin) {
        apta_internal_context_deallocate(
            session->context, state->bin_deviation);
        apta_internal_context_deallocate(session->context, state);
        session->complex_deviation_i10 = NULL;
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    apta_i10_reset_run(state);
    state->initialized = 1u;
    return APTA_STATUS_OK;
}

void apta_internal_complex_deviation_i10_reset_bin(
    apta_session_t *session,
    uint64_t bin_index)
{
    apta_internal_complex_deviation_i10_state_t *state;

    if (session == NULL) {
        return;
    }
    state = session->complex_deviation_i10;
    if (state == NULL || !state->initialized ||
        state->bin_deviation == NULL) {
        return;
    }
    state->bin_deviation[
        (uint32_t)(bin_index % APTA_INTERNAL_ONSET_BIN_CAPACITY)] = 0u;
}

apta_status_t apta_internal_complex_deviation_i10_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample)
{
    apta_internal_complex_deviation_i10_state_t *state;

    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    state = session->complex_deviation_i10;
    if (state == NULL || !state->initialized ||
        state->bin_deviation == NULL) {
        return APTA_ERROR_INTERNAL;
    }
    if (!isfinite(sample)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (state->has_next_source_frame &&
        source_frame != state->next_source_frame) {
        apta_i10_reset_run(state);
    }
    state->next_source_frame = source_frame + 1u;
    state->has_next_source_frame = 1u;

    state->samples[state->write_index] =
        fminf(1.0f, fmaxf(-1.0f, sample));
    state->write_index =
        (state->write_index + 1u) & (APTA_INTERNAL_I10_FFT_SIZE - 1u);
    state->run_sample_count += 1u;

    if (state->run_sample_count < APTA_INTERNAL_I10_FFT_SIZE ||
        ((state->run_sample_count - APTA_INTERNAL_I10_FFT_SIZE) %
         APTA_INTERNAL_I10_HOP_FRAMES) != 0u) {
        return APTA_STATUS_OK;
    }
    return apta_i10_finish_frame(session, source_frame);
}

int apta_internal_complex_deviation_i10_trace_at(
    const apta_session_t *session,
    uint32_t offset,
    float *deviation_out)
{
    const apta_internal_complex_deviation_i10_state_t *state;
    const apta_internal_onset_bin_t *bin;
    uint64_t count;
    uint64_t bin_index;
    uint32_t slot;

    if (session == NULL || deviation_out == NULL ||
        session->s4_refresh_evidence_end <
            session->s4_refresh_evidence_first) {
        return 0;
    }
    state = session->complex_deviation_i10;
    if (state == NULL || !state->initialized ||
        state->bin_deviation == NULL ||
        session->onset_bins == NULL) {
        return 0;
    }
    count = session->s4_refresh_evidence_end -
            session->s4_refresh_evidence_first;
    if ((uint64_t)offset >= count) {
        return 0;
    }
    bin_index = session->s4_refresh_evidence_first + offset;
    slot = (uint32_t)(bin_index % APTA_INTERNAL_ONSET_BIN_CAPACITY);
    bin = &session->onset_bins[slot];
    if (!bin->occupied || bin->bin_index != (uint32_t)bin_index ||
        bin->sample_count == 0u) {
        return 0;
    }
    *deviation_out = (float)state->bin_deviation[slot] / 65535.0f;
    return 1;
}

void apta_internal_complex_deviation_i10_cleanup(apta_session_t *session)
{
    apta_internal_complex_deviation_i10_state_t *state;

    if (session == NULL) {
        return;
    }
    state = session->complex_deviation_i10;
    if (state == NULL) {
        return;
    }
    apta_internal_context_deallocate(session->context, state->bin_deviation);
    apta_internal_context_deallocate(session->context, state);
    session->complex_deviation_i10 = NULL;
}
