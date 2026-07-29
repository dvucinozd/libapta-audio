// SPDX-License-Identifier: Apache-2.0
#include <apta/desktop/apta_decoder.h>

#include <string.h>

void APTA_CALL apta_decoder_info_init(apta_decoder_info_t *info)
{
    if (info == NULL) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->struct_size = (uint32_t)sizeof(*info);
    info->api_version = APTA_API_VERSION;
    info->total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
}

void APTA_CALL apta_decoder_init(apta_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->struct_size = (uint32_t)sizeof(*decoder);
    decoder->api_version = APTA_API_VERSION;
}

apta_status_t APTA_CALL apta_decoder_make_pcm_source(
    const apta_decoder_t *decoder,
    apta_pcm_source_t *source_out)
{
    if (decoder == NULL || source_out == NULL ||
        decoder->struct_size != sizeof(*decoder) ||
        decoder->api_version != APTA_API_VERSION ||
        decoder->user_data == NULL || decoder->read_frames == NULL ||
        decoder->release_frames == NULL || decoder->get_total_frames == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    apta_pcm_source_init(source_out);
    source_out->user_data = decoder->user_data;
    source_out->read_frames = decoder->read_frames;
    source_out->release_frames = decoder->release_frames;
    source_out->get_total_frames = decoder->get_total_frames;
    return APTA_STATUS_OK;
}

void APTA_CALL apta_decoder_close(apta_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    if (decoder->user_data != NULL && decoder->destroy != NULL) {
        decoder->destroy(decoder->user_data);
    }
    apta_decoder_init(decoder);
}
