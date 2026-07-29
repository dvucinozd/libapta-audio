// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_wav_fixture.h"

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 384000u

static double click_sample(
    uint64_t frame,
    uint16_t channel,
    void *user_data)
{
    (void)channel;
    (void)user_data;
    return (frame % BEAT_FRAMES) < 128u ? 0.9 : 0.0;
}

static int truncate_file(const char *input_path, const char *output_path)
{
    FILE *input = NULL;
    FILE *output = NULL;
    long size;
    long remaining;
    uint8_t buffer[4096];
    int success = 0;

    input = fopen(input_path, "rb");
    if (input == NULL || fseek(input, 0L, SEEK_END) != 0) {
        goto cleanup;
    }
    size = ftell(input);
    if (size <= 1L || fseek(input, 0L, SEEK_SET) != 0) {
        goto cleanup;
    }
    output = fopen(output_path, "wb");
    if (output == NULL) {
        goto cleanup;
    }
    remaining = size - 1L;
    while (remaining > 0L) {
        size_t requested = remaining > (long)sizeof(buffer)
                               ? sizeof(buffer)
                               : (size_t)remaining;
        size_t read_bytes = fread(buffer, 1u, requested, input);
        if (read_bytes != requested ||
            fwrite(buffer, 1u, read_bytes, output) != read_bytes) {
            goto cleanup;
        }
        remaining -= (long)read_bytes;
    }
    success = 1;

cleanup:
    if (output != NULL && fclose(output) != 0) {
        success = 0;
    }
    if (input != NULL && fclose(input) != 0) {
        success = 0;
    }
    return success;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "wav") == 0) {
        return apta_test_write_wav(
                   argv[2],
                   1u,
                   16u,
                   1u,
                   SAMPLE_RATE,
                   TOTAL_FRAMES,
                   0,
                   click_sample,
                   NULL)
                   ? 0
                   : 1;
    }
    if (argc == 4 && strcmp(argv[1], "truncate") == 0) {
        return truncate_file(argv[2], argv[3]) ? 0 : 1;
    }
    fprintf(stderr,
            "Usage: %s wav OUTPUT.wav | truncate INPUT.apta OUTPUT.apta\n",
            argc > 0 ? argv[0] : "desktop-cli-fixture");
    return 2;
}
