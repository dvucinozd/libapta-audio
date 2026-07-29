// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_DESKTOP_DECODER_H
#define APTA_DESKTOP_DECODER_H

#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_source.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t sample_rate;
    uint16_t channel_count;
    uint16_t bits_per_sample;
    apta_sample_format_t sample_format;
    apta_channel_layout_t channel_layout;
    apta_source_frame_t total_frames;

    uint32_t flags;
    uint32_t reserved32[3];
    uint64_t reserved64[2];
} apta_decoder_info_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    apta_status_t (APTA_CALL *read_frames)(
        void *user_data,
        apta_source_frame_t first_frame,
        uint32_t requested_frames,
        apta_pcm_block_t *block_out);

    void (APTA_CALL *release_frames)(
        void *user_data,
        apta_pcm_block_t *block);

    uint64_t (APTA_CALL *get_total_frames)(void *user_data);
    void (APTA_CALL *destroy)(void *user_data);
} apta_decoder_t;

APTA_API void APTA_CALL
apta_decoder_info_init(apta_decoder_info_t *info);

APTA_API void APTA_CALL
apta_decoder_init(apta_decoder_t *decoder);

APTA_API apta_status_t APTA_CALL
apta_decoder_make_pcm_source(
    const apta_decoder_t *decoder,
    apta_pcm_source_t *source_out);

APTA_API void APTA_CALL
apta_decoder_close(apta_decoder_t *decoder);

APTA_API apta_status_t APTA_CALL
apta_wav_decoder_open_path(
    const char *path,
    apta_decoder_t *decoder_out,
    apta_decoder_info_t *info_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_DESKTOP_DECODER_H */
