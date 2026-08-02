// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static int patch_bytes(
    const char *path,
    long offset,
    const void *data,
    size_t size)
{
    FILE *file = fopen(path, "r+b");
    if (file == NULL) {
        return 0;
    }
    if (fseek(file, offset, SEEK_SET) != 0 ||
        fwrite(data, 1u, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static apta_status_t open_status(const char *path)
{
    apta_decoder_t decoder;
    apta_decoder_info_t info;
    apta_status_t status;

    apta_decoder_init(&decoder);
    apta_decoder_info_init(&info);
    status = apta_wav_decoder_open_path(path, &decoder, &info);
    if (status >= 0) {
        apta_decoder_close(&decoder);
    }
    return status;
}

int main(void)
{
    char path[APTA_TEST_TEMP_PATH_CAPACITY];
    FILE *file;
    uint8_t patch4[4];
    uint8_t patch2[2];

    CHECK(apta_test_make_temp_path(path, sizeof(path)));
    file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite("not-a-wave", 1u, 10u, file) == 10u);
    CHECK(fclose(file) == 0);
    CHECK(open_status(path) < 0);

    CHECK(apta_test_write_wav(path, 1u, 16u, 1u, 48000u, 8u, 0, NULL, NULL));
    memcpy(patch4, "RIFX", 4u);
    CHECK(patch_bytes(path, 0L, patch4, sizeof(patch4)));
    CHECK(open_status(path) == APTA_ERROR_UNSUPPORTED);

    CHECK(apta_test_write_wav(path, 1u, 16u, 1u, 48000u, 8u, 0, NULL, NULL));
    patch4[0] = 0xFFu;
    patch4[1] = 0xFFu;
    patch4[2] = 0xFFu;
    patch4[3] = 0x7Fu;
    CHECK(patch_bytes(path, 4L, patch4, sizeof(patch4)));
    CHECK(open_status(path) == APTA_ERROR_CORRUPT_DATA);

    CHECK(apta_test_write_wav(path, 1u, 16u, 1u, 48000u, 8u, 0, NULL, NULL));
    patch2[0] = 1u;
    patch2[1] = 0u;
    CHECK(patch_bytes(path, 32L, patch2, sizeof(patch2)));
    CHECK(open_status(path) == APTA_ERROR_CORRUPT_DATA);

    CHECK(apta_test_write_wav(path, 1u, 16u, 1u, 48000u, 8u, 0, NULL, NULL));
    patch4[0] = 0x00u;
    patch4[1] = 0x00u;
    patch4[2] = 0x01u;
    patch4[3] = 0x00u;
    CHECK(patch_bytes(path, 40L, patch4, sizeof(patch4)));
    CHECK(open_status(path) == APTA_ERROR_CORRUPT_DATA);

    CHECK(apta_test_write_wav(path, 1u, 16u, 1u, 48000u, 8u, 0, NULL, NULL));
    patch2[0] = 8u;
    patch2[1] = 0u;
    CHECK(patch_bytes(path, 34L, patch2, sizeof(patch2)));
    patch2[0] = 1u;
    patch2[1] = 0u;
    CHECK(patch_bytes(path, 32L, patch2, sizeof(patch2)));
    patch4[0] = 0x80u;
    patch4[1] = 0xBBu;
    patch4[2] = 0x00u;
    patch4[3] = 0x00u;
    CHECK(patch_bytes(path, 28L, patch4, sizeof(patch4)));
    CHECK(open_status(path) == APTA_ERROR_UNSUPPORTED);

    CHECK(apta_test_remove_path(path));
    return 0;
}
