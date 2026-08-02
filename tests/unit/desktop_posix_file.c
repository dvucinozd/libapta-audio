// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/desktop/apta_posix_file.h>

#include "desktop_wav_fixture.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    static const uint8_t payload[] = {
        0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u
    };
    char path[APTA_TEST_TEMP_PATH_CAPACITY];
    FILE *writer;
    apta_posix_file_t *file = NULL;
    uint8_t output[8] = {0};
    uint64_t size = 0u;
    size_t read_bytes = 0u;

    CHECK(apta_test_make_temp_path(path, sizeof(path)));
    writer = fopen(path, "wb");
    CHECK(writer != NULL);
    CHECK(fwrite(payload, 1u, sizeof(payload), writer) == sizeof(payload));
    CHECK(fclose(writer) == 0);

    CHECK(apta_posix_file_open_read(path, &file) == APTA_STATUS_OK);
    CHECK(file != NULL);
    CHECK(apta_posix_file_get_size(file, &size) == APTA_STATUS_OK);
    CHECK(size == sizeof(payload));

    CHECK(apta_posix_file_read_at(
              file, 2u, output, 3u, &read_bytes) == APTA_STATUS_OK);
    CHECK(read_bytes == 3u);
    CHECK(memcmp(output, payload + 2u, 3u) == 0);

    memset(output, 0, sizeof(output));
    CHECK(apta_posix_file_read_at(
              file, 5u, output, sizeof(output), &read_bytes) == APTA_STATUS_OK);
    CHECK(read_bytes == 2u);
    CHECK(memcmp(output, payload + 5u, 2u) == 0);

    CHECK(apta_posix_file_read_at(
              file, size, output, 1u, &read_bytes) == APTA_STATUS_OK);
    CHECK(read_bytes == 0u);
    CHECK(apta_posix_file_read_at(
              file, size + 1u, output, 1u, &read_bytes) == APTA_ERROR_SOURCE);

    apta_posix_file_close(file);
    file = NULL;
    CHECK(apta_test_remove_path(path));
    CHECK(apta_posix_file_open_read(path, &file) == APTA_ERROR_SOURCE);
    CHECK(file == NULL);
    return 0;
}
