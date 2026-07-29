// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <apta/desktop/apta_decoder.h>

#include "desktop_wav_fixture.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int run_case(
    uint16_t format,
    uint16_t bits,
    uint16_t channels,
    int extensible,
    apta_sample_format_t expected_format)
{
    char path[64];
    apta_decoder_t decoder;
    apta_decoder_info_t info;
    apta_pcm_source_t source;
    apta_pcm_block_t block;
    apta_pcm_block_t second;

    CHECK(apta_test_make_temp_path(path));
    CHECK(apta_test_write_wav(
        path,
        format,
        bits,
        channels,
        48000u,
        8u,
        extensible,
        NULL,
        NULL));

    apta_decoder_init(&decoder);
    apta_decoder_info_init(&info);
    CHECK(apta_wav_decoder_open_path(path, &decoder, &info) == APTA_STATUS_OK);
    CHECK(info.sample_rate == 48000u);
    CHECK(info.channel_count == channels);
    CHECK(info.bits_per_sample == bits);
    CHECK(info.sample_format == expected_format);
    CHECK(info.channel_layout ==
          (channels == 1u ? APTA_CHANNEL_LAYOUT_MONO
                          : APTA_CHANNEL_LAYOUT_STEREO));
    CHECK(info.total_frames == 8u);

    apta_pcm_source_init(&source);
    CHECK(apta_decoder_make_pcm_source(&decoder, &source) == APTA_STATUS_OK);
    CHECK(source.get_total_frames(source.user_data) == 8u);

    apta_pcm_block_init(&block);
    CHECK(source.read_frames(source.user_data, 2u, 3u, &block) ==
          APTA_STATUS_OK);
    CHECK(block.data != NULL);
    CHECK(block.first_frame == 2u);
    CHECK(block.frame_count == 3u);

    apta_pcm_block_init(&second);
    CHECK(source.read_frames(source.user_data, 0u, 1u, &second) ==
          APTA_ERROR_BUSY);
    source.release_frames(source.user_data, &block);

    apta_pcm_block_init(&block);
    CHECK(source.read_frames(source.user_data, 7u, 4u, &block) ==
          APTA_STATUS_OK);
    CHECK(block.first_frame == 7u);
    CHECK(block.frame_count == 1u);
    source.release_frames(source.user_data, &block);

    apta_pcm_block_init(&block);
    CHECK(source.read_frames(source.user_data, 8u, 1u, &block) ==
          APTA_STATUS_END_OF_INPUT);

    apta_decoder_close(&decoder);
    CHECK(decoder.user_data == NULL);
    CHECK(unlink(path) == 0);
    return 0;
}

int main(void)
{
    CHECK(run_case(
              1u,
              16u,
              1u,
              0,
              APTA_SAMPLE_S16_NATIVE_INTERLEAVED) == 0);
    CHECK(run_case(
              1u,
              24u,
              2u,
              0,
              APTA_SAMPLE_S24_3LE_INTERLEAVED) == 0);
    CHECK(run_case(
              1u,
              32u,
              1u,
              0,
              APTA_SAMPLE_S32_NATIVE_INTERLEAVED) == 0);
    CHECK(run_case(
              3u,
              32u,
              2u,
              0,
              APTA_SAMPLE_F32_NATIVE_INTERLEAVED) == 0);
    CHECK(run_case(
              1u,
              16u,
              2u,
              1,
              APTA_SAMPLE_S16_NATIVE_INTERLEAVED) == 0);
    return 0;
}
