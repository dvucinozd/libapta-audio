// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/desktop/apta_file.h>

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
    static const uint8_t first_payload[] = {
        0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u
    };
    static const uint8_t replacement[] = {0xA1u, 0xB2u, 0xC3u};
    char path[APTA_TEST_TEMP_PATH_CAPACITY];
    apta_file_t *file = NULL;
    uint8_t output[8] = {0};
    uint64_t size = 0u;
    size_t read_bytes = 0u;

    CHECK(apta_test_make_unicode_temp_path(path, sizeof(path)));
    CHECK(apta_file_write_atomic(
              path, first_payload, sizeof(first_payload)) == APTA_STATUS_OK);
    CHECK(apta_file_open_read(path, &file) == APTA_STATUS_OK);
    CHECK(apta_file_get_size(file, &size) == APTA_STATUS_OK);
    CHECK(size == sizeof(first_payload));
    CHECK(apta_file_read_at(
              file, 2u, output, 3u, &read_bytes) == APTA_STATUS_OK);
    CHECK(read_bytes == 3u);
    CHECK(memcmp(output, first_payload + 2u, 3u) == 0);
    CHECK(apta_file_read_at(
              file, size + 1u, output, 1u, &read_bytes) == APTA_ERROR_SOURCE);
    apta_file_close(file);
    file = NULL;

    CHECK(apta_file_write_atomic(
              path, replacement, sizeof(replacement)) == APTA_STATUS_OK);
    CHECK(apta_file_open_read(path, &file) == APTA_STATUS_OK);
    CHECK(apta_file_get_size(file, &size) == APTA_STATUS_OK);
    CHECK(size == sizeof(replacement));
    CHECK(apta_file_read_at(
              file, 0u, output, sizeof(output), &read_bytes) == APTA_STATUS_OK);
    CHECK(read_bytes == sizeof(replacement));
    CHECK(memcmp(output, replacement, sizeof(replacement)) == 0);
    apta_file_close(file);

    CHECK(apta_test_remove_path(path));
    CHECK(apta_file_open_read(path, &file) == APTA_ERROR_SOURCE);
    CHECK(file == NULL);
    return 0;
}
