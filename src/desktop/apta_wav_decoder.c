// SPDX-License-Identifier: Apache-2.0
#include <apta/desktop/apta_decoder.h>
#include <apta/desktop/apta_posix_file.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define APTA_WAV_FORMAT_PCM        0x0001u
#define APTA_WAV_FORMAT_IEEE_FLOAT 0x0003u
#define APTA_WAV_FORMAT_EXTENSIBLE 0xFFFEu

typedef struct {
    apta_posix_file_t *file;
    uint64_t data_offset;
    uint64_t data_size;
    uint64_t total_frames;
    uint32_t sample_rate;
    uint16_t channel_count;
    uint16_t bits_per_sample;
    uint16_t block_align;
    apta_sample_format_t sample_format;
    apta_channel_layout_t channel_layout;
    uint8_t *buffer;
    size_t buffer_capacity;
    uint32_t borrowed;
} apta_wav_decoder_state_t;

static uint16_t apta_wav_get_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t apta_wav_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static apta_status_t apta_wav_read_exact(
    apta_posix_file_t *file,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    size_t read_bytes = 0u;
    apta_status_t status = apta_posix_file_read_at(
        file,
        offset,
        buffer,
        size,
        &read_bytes);
    if (status < 0) {
        return status;
    }
    return read_bytes == size ? APTA_STATUS_OK : APTA_ERROR_CORRUPT_DATA;
}

static int apta_wav_is_extensible_guid(
    const uint8_t guid[16],
    uint16_t *format_out)
{
    static const uint8_t suffix[14] = {
        0x00u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x80u,
        0x00u, 0x00u, 0xAAu, 0x00u, 0x38u, 0x9Bu, 0x71u
    };

    if (memcmp(guid + 2u, suffix, sizeof(suffix)) != 0) {
        return 0;
    }
    *format_out = apta_wav_get_u16(guid);
    return 1;
}

static apta_status_t apta_wav_select_format(
    uint16_t wave_format,
    uint16_t bits_per_sample,
    apta_sample_format_t *sample_format_out)
{
    if (wave_format == APTA_WAV_FORMAT_PCM) {
        if (bits_per_sample == 16u) {
            *sample_format_out = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
            return APTA_STATUS_OK;
        }
        if (bits_per_sample == 24u) {
            *sample_format_out = APTA_SAMPLE_S24_3LE_INTERLEAVED;
            return APTA_STATUS_OK;
        }
        if (bits_per_sample == 32u) {
            *sample_format_out = APTA_SAMPLE_S32_NATIVE_INTERLEAVED;
            return APTA_STATUS_OK;
        }
    } else if (wave_format == APTA_WAV_FORMAT_IEEE_FLOAT &&
               bits_per_sample == 32u) {
        *sample_format_out = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
        return APTA_STATUS_OK;
    }
    return APTA_ERROR_UNSUPPORTED;
}

static apta_status_t apta_wav_convert_native(
    apta_wav_decoder_state_t *state,
    size_t byte_count)
{
    size_t offset;

    if (state->sample_format == APTA_SAMPLE_S24_3LE_INTERLEAVED) {
        return APTA_STATUS_OK;
    }
    if (state->bits_per_sample == 16u) {
        for (offset = 0u; offset < byte_count; offset += 2u) {
            uint16_t value = apta_wav_get_u16(state->buffer + offset);
            memcpy(state->buffer + offset, &value, sizeof(value));
        }
        return APTA_STATUS_OK;
    }
    if (state->bits_per_sample == 32u) {
        for (offset = 0u; offset < byte_count; offset += 4u) {
            uint32_t value = apta_wav_get_u32(state->buffer + offset);
            memcpy(state->buffer + offset, &value, sizeof(value));
        }
        return APTA_STATUS_OK;
    }
    return APTA_ERROR_INTERNAL;
}

static apta_status_t APTA_CALL apta_wav_read_frames(
    void *user_data,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    apta_pcm_block_t *block_out)
{
    apta_wav_decoder_state_t *state =
        (apta_wav_decoder_state_t *)user_data;
    uint64_t available_frames;
    uint32_t frame_count;
    uint64_t byte_offset;
    size_t byte_count;
    uint8_t *new_buffer;
    apta_status_t status;

    if (state == NULL || block_out == NULL || requested_frames == 0u ||
        block_out->struct_size != sizeof(*block_out) ||
        block_out->api_version != APTA_API_VERSION) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (state->borrowed != 0u) {
        return APTA_ERROR_BUSY;
    }
    if (first_frame >= state->total_frames) {
        return APTA_STATUS_END_OF_INPUT;
    }

    available_frames = state->total_frames - first_frame;
    frame_count = available_frames < requested_frames
                      ? (uint32_t)available_frames
                      : requested_frames;
    if ((uint64_t)frame_count > SIZE_MAX / state->block_align) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    byte_count = (size_t)frame_count * state->block_align;
    if (first_frame > (UINT64_MAX - state->data_offset) / state->block_align) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    byte_offset = state->data_offset + first_frame * state->block_align;

    if (byte_count > state->buffer_capacity) {
        new_buffer = (uint8_t *)realloc(state->buffer, byte_count);
        if (new_buffer == NULL) {
            return APTA_ERROR_OUT_OF_MEMORY;
        }
        state->buffer = new_buffer;
        state->buffer_capacity = byte_count;
    }

    status = apta_wav_read_exact(
        state->file,
        byte_offset,
        state->buffer,
        byte_count);
    if (status < 0) {
        return status == APTA_ERROR_CORRUPT_DATA ? APTA_ERROR_SOURCE : status;
    }
    status = apta_wav_convert_native(state, byte_count);
    if (status < 0) {
        return status;
    }

    apta_pcm_block_init(block_out);
    block_out->data = state->buffer;
    block_out->first_frame = first_frame;
    block_out->frame_count = frame_count;
    state->borrowed = 1u;
    return APTA_STATUS_OK;
}

static void APTA_CALL apta_wav_release_frames(
    void *user_data,
    apta_pcm_block_t *block)
{
    apta_wav_decoder_state_t *state =
        (apta_wav_decoder_state_t *)user_data;
    if (state == NULL) {
        return;
    }
    state->borrowed = 0u;
    if (block != NULL) {
        apta_pcm_block_init(block);
    }
}

static uint64_t APTA_CALL apta_wav_get_total_frames(void *user_data)
{
    const apta_wav_decoder_state_t *state =
        (const apta_wav_decoder_state_t *)user_data;
    return state != NULL ? state->total_frames : APTA_TOTAL_FRAMES_UNKNOWN;
}

static void APTA_CALL apta_wav_destroy(void *user_data)
{
    apta_wav_decoder_state_t *state =
        (apta_wav_decoder_state_t *)user_data;
    if (state == NULL) {
        return;
    }
    apta_posix_file_close(state->file);
    free(state->buffer);
    free(state);
}

apta_status_t APTA_CALL apta_wav_decoder_open_path(
    const char *path,
    apta_decoder_t *decoder_out,
    apta_decoder_info_t *info_out)
{
    apta_wav_decoder_state_t *state = NULL;
    apta_posix_file_t *file = NULL;
    uint8_t riff[12];
    uint64_t file_size;
    uint64_t riff_end;
    uint64_t cursor;
    uint64_t data_offset = 0u;
    uint64_t data_size = 0u;
    uint32_t sample_rate = 0u;
    uint32_t byte_rate = 0u;
    uint16_t channel_count = 0u;
    uint16_t block_align = 0u;
    uint16_t bits_per_sample = 0u;
    uint16_t wave_format = 0u;
    uint32_t found_fmt = 0u;
    uint32_t found_data = 0u;
    apta_sample_format_t sample_format = 0u;
    apta_status_t status;

    if (path == NULL || decoder_out == NULL || info_out == NULL ||
        decoder_out->struct_size != sizeof(*decoder_out) ||
        decoder_out->api_version != APTA_API_VERSION ||
        info_out->struct_size != sizeof(*info_out) ||
        info_out->api_version != APTA_API_VERSION) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    apta_decoder_init(decoder_out);
    apta_decoder_info_init(info_out);

    status = apta_posix_file_open_read(path, &file);
    if (status < 0) {
        return status;
    }
    status = apta_posix_file_get_size(file, &file_size);
    if (status < 0 || file_size < sizeof(riff)) {
        apta_posix_file_close(file);
        return status < 0 ? status : APTA_ERROR_CORRUPT_DATA;
    }
    status = apta_wav_read_exact(file, 0u, riff, sizeof(riff));
    if (status < 0 || memcmp(riff, "RIFF", 4u) != 0 ||
        memcmp(riff + 8u, "WAVE", 4u) != 0) {
        apta_posix_file_close(file);
        return status < 0 ? status : APTA_ERROR_UNSUPPORTED;
    }
    riff_end = (uint64_t)apta_wav_get_u32(riff + 4u) + 8u;
    if (riff_end < sizeof(riff) || riff_end > file_size) {
        apta_posix_file_close(file);
        return APTA_ERROR_CORRUPT_DATA;
    }

    cursor = 12u;
    while (cursor + 8u <= riff_end) {
        uint8_t chunk_header[8];
        uint32_t chunk_size;
        uint64_t payload_offset;
        uint64_t next_offset;

        status = apta_wav_read_exact(file, cursor, chunk_header, sizeof(chunk_header));
        if (status < 0) {
            apta_posix_file_close(file);
            return status;
        }
        chunk_size = apta_wav_get_u32(chunk_header + 4u);
        payload_offset = cursor + 8u;
        if ((uint64_t)chunk_size > riff_end - payload_offset) {
            apta_posix_file_close(file);
            return APTA_ERROR_CORRUPT_DATA;
        }
        next_offset = payload_offset + chunk_size + (chunk_size & 1u);
        if (next_offset < payload_offset || next_offset > riff_end) {
            apta_posix_file_close(file);
            return APTA_ERROR_CORRUPT_DATA;
        }

        if (memcmp(chunk_header, "fmt ", 4u) == 0) {
            uint8_t fmt[40] = {0};
            uint16_t parsed_format;
            size_t read_size;

            if (found_fmt != 0u || chunk_size < 16u) {
                apta_posix_file_close(file);
                return APTA_ERROR_CORRUPT_DATA;
            }
            read_size = chunk_size < sizeof(fmt) ? chunk_size : sizeof(fmt);
            status = apta_wav_read_exact(file, payload_offset, fmt, read_size);
            if (status < 0) {
                apta_posix_file_close(file);
                return status;
            }
            parsed_format = apta_wav_get_u16(fmt);
            channel_count = apta_wav_get_u16(fmt + 2u);
            sample_rate = apta_wav_get_u32(fmt + 4u);
            byte_rate = apta_wav_get_u32(fmt + 8u);
            block_align = apta_wav_get_u16(fmt + 12u);
            bits_per_sample = apta_wav_get_u16(fmt + 14u);

            if (parsed_format == APTA_WAV_FORMAT_EXTENSIBLE) {
                uint16_t subformat;
                uint16_t valid_bits;
                if (chunk_size < 40u || apta_wav_get_u16(fmt + 16u) < 22u ||
                    !apta_wav_is_extensible_guid(fmt + 24u, &subformat)) {
                    apta_posix_file_close(file);
                    return APTA_ERROR_UNSUPPORTED;
                }
                valid_bits = apta_wav_get_u16(fmt + 18u);
                if (valid_bits == 0u || valid_bits > bits_per_sample) {
                    apta_posix_file_close(file);
                    return APTA_ERROR_CORRUPT_DATA;
                }
                parsed_format = subformat;
            }
            wave_format = parsed_format;
            found_fmt = 1u;
        } else if (memcmp(chunk_header, "data", 4u) == 0) {
            if (found_data != 0u) {
                apta_posix_file_close(file);
                return APTA_ERROR_CORRUPT_DATA;
            }
            data_offset = payload_offset;
            data_size = chunk_size;
            found_data = 1u;
        }

        cursor = next_offset;
    }

    if (found_fmt == 0u || found_data == 0u || channel_count == 0u ||
        channel_count > 2u || sample_rate == 0u || sample_rate > 768000u ||
        bits_per_sample == 0u || (bits_per_sample & 7u) != 0u ||
        block_align != channel_count * (bits_per_sample / 8u) ||
        block_align == 0u || data_size % block_align != 0u ||
        (uint64_t)byte_rate != (uint64_t)sample_rate * block_align) {
        apta_posix_file_close(file);
        return APTA_ERROR_CORRUPT_DATA;
    }
    status = apta_wav_select_format(
        wave_format,
        bits_per_sample,
        &sample_format);
    if (status < 0) {
        apta_posix_file_close(file);
        return status;
    }

    state = (apta_wav_decoder_state_t *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        apta_posix_file_close(file);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    state->file = file;
    state->data_offset = data_offset;
    state->data_size = data_size;
    state->total_frames = data_size / block_align;
    state->sample_rate = sample_rate;
    state->channel_count = channel_count;
    state->bits_per_sample = bits_per_sample;
    state->block_align = block_align;
    state->sample_format = sample_format;
    state->channel_layout = channel_count == 1u
                                ? APTA_CHANNEL_LAYOUT_MONO
                                : APTA_CHANNEL_LAYOUT_STEREO;

    decoder_out->user_data = state;
    decoder_out->read_frames = apta_wav_read_frames;
    decoder_out->release_frames = apta_wav_release_frames;
    decoder_out->get_total_frames = apta_wav_get_total_frames;
    decoder_out->destroy = apta_wav_destroy;

    info_out->sample_rate = sample_rate;
    info_out->channel_count = channel_count;
    info_out->bits_per_sample = bits_per_sample;
    info_out->sample_format = sample_format;
    info_out->channel_layout = state->channel_layout;
    info_out->total_frames = state->total_frames;
    return APTA_STATUS_OK;
}
