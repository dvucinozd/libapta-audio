// SPDX-License-Identifier: Apache-2.0
/* Private WP4 measurement instrument; not a test or installed deliverable. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "apta_internal.h"
#include "apta_key_internal.h"

#define SAMPLE_RATE 48000u
#define BLOCK_FRAMES 1024u

static int parse_seconds(const char *text, uint32_t *seconds_out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || seconds_out == NULL) {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0ul || value > 3600ul) {
        return 0;
    }
    *seconds_out = (uint32_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    apta_session_t session;
    float samples[BLOCK_FRAMES];
    uint64_t total_frames;
    uint64_t frame;
    clock_t started;
    clock_t finished;
    double elapsed_ms;
    double checksum = 0.0;
    uint32_t seconds = 60u;
    uint32_t index;

    if (argc == 2) {
        if (!parse_seconds(argv[1], &seconds)) {
            fputs("usage: apta_key_feed_benchmark [seconds]\n", stderr);
            return 2;
        }
    } else if (argc != 1) {
        fputs("usage: apta_key_feed_benchmark [seconds]\n", stderr);
        return 2;
    }

    for (index = 0u; index < BLOCK_FRAMES; ++index) {
        const int32_t centered = (int32_t)((index * 1103515245u + 12345u) >> 16) -
                                 32768;
        samples[index] = (float)centered / 65536.0f;
    }

    memset(&session, 0, sizeof(session));
    session.config.source_sample_rate = SAMPLE_RATE;
    session.config.requested_features = APTA_FEATURE_MUSICAL_KEY;
    total_frames = (uint64_t)SAMPLE_RATE * seconds;

    started = clock();
    for (frame = 0u; frame < total_frames; ++frame) {
        apta_internal_key_feed_sample(
            &session,
            samples[frame % BLOCK_FRAMES],
            frame);
    }
    finished = clock();
    if (started == (clock_t)-1 || finished == (clock_t)-1 || finished < started) {
        fputs("clock failed\n", stderr);
        return 1;
    }

    for (index = 0u; index < APTA_INTERNAL_KEY_PITCH_CLASSES; ++index) {
        checksum += session.key_analysis
                       .chroma[APTA_INTERNAL_KEY_BASE_VARIANT][index];
    }
    elapsed_ms = 1000.0 * (double)(finished - started) /
                 (double)CLOCKS_PER_SEC;
    printf("seconds=%u elapsed_ms=%.3f checksum=%.9f\n",
           seconds,
           elapsed_ms,
           checksum);
    return checksum > 0.0 ? 0 : 1;
}
