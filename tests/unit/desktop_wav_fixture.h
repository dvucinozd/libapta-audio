// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TEST_DESKTOP_WAV_FIXTURE_H
#define APTA_TEST_DESKTOP_WAV_FIXTURE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#define APTA_TEST_TEMP_PATH_CAPACITY 512u

typedef double (*apta_test_sample_fn)(
    uint64_t frame,
    uint16_t channel,
    void *user_data);

static int apta_test_put_u16(FILE *file, uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u)
    };
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int apta_test_put_u24(FILE *file, uint32_t value)
{
    uint8_t bytes[3] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u)
    };
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int apta_test_put_u32(FILE *file, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 24u)
    };
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes);
}

static int apta_test_make_temp_path(char *path, size_t capacity)
{
#if defined(_WIN32)
    char directory[MAX_PATH + 1u];
    DWORD directory_size;

    if (path == NULL || capacity < MAX_PATH + 1u) {
        return 0;
    }
    directory_size = GetTempPathA((DWORD)sizeof(directory), directory);
    if (directory_size == 0u || directory_size >= sizeof(directory)) {
        return 0;
    }
    return GetTempFileNameA(directory, "apt", 0u, path) != 0u;
#else
    int descriptor;
    if (path == NULL || capacity < 32u) {
        return 0;
    }
    (void)snprintf(path, capacity, "/tmp/libapta-s5-XXXXXX");
    descriptor = mkstemp(path);
    if (descriptor < 0) {
        return 0;
    }
    return close(descriptor) == 0;
#endif
}

static int apta_test_remove_path(const char *path)
{
#if defined(_WIN32)
    wchar_t wide[MAX_PATH + 1u];
    int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, (int)(MAX_PATH + 1u));
    return count > 0 && DeleteFileW(wide) != 0;
#else
    return remove(path) == 0;
#endif
}

static int apta_test_make_unicode_temp_path(char *path, size_t capacity)
{
    static const char suffix[] = "-\xC5\xBE.apta";
    size_t path_size;

    if (!apta_test_make_temp_path(path, capacity)) {
        return 0;
    }
    if (!apta_test_remove_path(path)) {
        return 0;
    }
    path_size = strlen(path);
    if (capacity < sizeof(suffix) ||
        path_size > capacity - sizeof(suffix)) {
        return 0;
    }
    memcpy(path + path_size, suffix, sizeof(suffix));
    return 1;
}

static double apta_test_default_sample(
    uint64_t frame,
    uint16_t channel,
    void *user_data)
{
    (void)user_data;
    return ((frame + channel) & 1u) != 0u ? 0.5 : -0.5;
}

static int apta_test_write_sample(
    FILE *file,
    uint16_t format,
    uint16_t bits,
    double sample)
{
    if (sample > 1.0) {
        sample = 1.0;
    } else if (sample < -1.0) {
        sample = -1.0;
    }

    if (format == 3u && bits == 32u) {
        float value = (float)sample;
        uint32_t encoded;
        memcpy(&encoded, &value, sizeof(encoded));
        return apta_test_put_u32(file, encoded);
    }
    if (format != 1u) {
        return 0;
    }
    if (bits == 16u) {
        int32_t value = (int32_t)(sample * 32767.0);
        return apta_test_put_u16(file, (uint16_t)(int16_t)value);
    }
    if (bits == 24u) {
        int32_t value = (int32_t)(sample * 8388607.0);
        return apta_test_put_u24(file, (uint32_t)value & 0xFFFFFFu);
    }
    if (bits == 32u) {
        int64_t value = (int64_t)(sample * 2147483647.0);
        return apta_test_put_u32(file, (uint32_t)(int32_t)value);
    }
    return 0;
}

static int apta_test_write_wav(
    const char *path,
    uint16_t format,
    uint16_t bits,
    uint16_t channels,
    uint32_t sample_rate,
    uint32_t frame_count,
    int extensible,
    apta_test_sample_fn sample_fn,
    void *user_data)
{
    FILE *file;
    uint32_t bytes_per_sample = bits / 8u;
    uint32_t block_align = channels * bytes_per_sample;
    uint32_t data_size = frame_count * block_align;
    uint32_t fmt_size = extensible ? 40u : 16u;
    uint32_t riff_size = 4u + 8u + fmt_size + 8u + data_size;
    uint32_t frame;
    uint16_t channel;

    if (sample_fn == NULL) {
        sample_fn = apta_test_default_sample;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }

    if (fwrite("RIFF", 1u, 4u, file) != 4u ||
        !apta_test_put_u32(file, riff_size) ||
        fwrite("WAVEfmt ", 1u, 8u, file) != 8u ||
        !apta_test_put_u32(file, fmt_size) ||
        !apta_test_put_u16(file, extensible ? 0xFFFEu : format) ||
        !apta_test_put_u16(file, channels) ||
        !apta_test_put_u32(file, sample_rate) ||
        !apta_test_put_u32(file, sample_rate * block_align) ||
        !apta_test_put_u16(file, (uint16_t)block_align) ||
        !apta_test_put_u16(file, bits)) {
        fclose(file);
        return 0;
    }

    if (extensible) {
        uint8_t guid[16] = {
            (uint8_t)format, (uint8_t)(format >> 8u), 0u, 0u,
            0u, 0u, 0x10u, 0u, 0x80u, 0u, 0u, 0xAAu,
            0u, 0x38u, 0x9Bu, 0x71u
        };
        if (!apta_test_put_u16(file, 22u) ||
            !apta_test_put_u16(file, bits) ||
            !apta_test_put_u32(file, channels == 1u ? 0x4u : 0x3u) ||
            fwrite(guid, 1u, sizeof(guid), file) != sizeof(guid)) {
            fclose(file);
            return 0;
        }
    }

    if (fwrite("data", 1u, 4u, file) != 4u ||
        !apta_test_put_u32(file, data_size)) {
        fclose(file);
        return 0;
    }
    for (frame = 0u; frame < frame_count; ++frame) {
        for (channel = 0u; channel < channels; ++channel) {
            if (!apta_test_write_sample(
                    file,
                    format,
                    bits,
                    sample_fn(frame, channel, user_data))) {
                fclose(file);
                return 0;
            }
        }
    }
    return fclose(file) == 0;
}

#endif /* APTA_TEST_DESKTOP_WAV_FIXTURE_H */
