// SPDX-License-Identifier: Apache-2.0
/*
 * Frozen APTA-1.0-era consumer: intentionally does not include or link
 * libapta. It recognizes only the 1.0 FourCC registry and skips every other
 * optional section after validating its common v1 frame and CRC32C.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED_SIZE 664u

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4u) << 32u);
}

static uint32_t crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;
    for (index = 0u; index < size; ++index) {
        uint32_t bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^
                  ((crc & 1u) != 0u ? UINT32_C(0x82f63b78) : 0u);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static int is_v1_known(const uint8_t *fourcc)
{
    return memcmp(fourcc, "WOVR", 4u) == 0 ||
           memcmp(fourcc, "WDTL", 4u) == 0 ||
           memcmp(fourcc, "META", 4u) == 0 ||
           memcmp(fourcc, "TEMP", 4u) == 0 ||
           memcmp(fourcc, "LGRD", 4u) == 0 ||
           memcmp(fourcc, "GGRD", 4u) == 0 ||
           memcmp(fourcc, "REVN", 4u) == 0;
}

static int load_hex(uint8_t *bytes)
{
    FILE *file = fopen(APTA_DJ_GOLDEN_HEX_PATH, "rb");
    int high = -1;
    int character;
    size_t size = 0u;
    if (file == NULL) return 0;
    while ((character = fgetc(file)) != EOF) {
        int value;
        if (character >= '0' && character <= '9') value = character - '0';
        else if (character >= 'a' && character <= 'f') {
            value = character - 'a' + 10;
        } else if (character >= 'A' && character <= 'F') {
            value = character - 'A' + 10;
        } else continue;
        if (high < 0) high = value;
        else {
            if (size == EXPECTED_SIZE) {
                fclose(file);
                return 0;
            }
            bytes[size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    return high < 0 && size == EXPECTED_SIZE;
}

static int consume(const uint8_t *bytes, size_t size)
{
    uint32_t count;
    uint16_t header_size;
    uint64_t directory_offset;
    uint64_t directory_size;
    uint64_t directory_end;
    uint32_t known = 0u;
    uint32_t skipped = 0u;
    uint32_t wovr_count = 0u;
    uint32_t index;

    if (bytes == NULL || size < 96u || memcmp(bytes, "APTA", 4u) != 0 ||
        get_u16(bytes + 6u) != 1u || get_u64(bytes + 32u) != size ||
        crc32c(bytes, 92u) != get_u32(bytes + 92u)) {
        return 0;
    }
    header_size = get_u16(bytes + 4u);
    count = get_u32(bytes + 20u);
    directory_offset = get_u64(bytes + 24u);
    if (header_size < 96u || header_size > size ||
        directory_offset < header_size || (directory_offset & 7u) != 0u ||
        count > UINT64_MAX / 40u) {
        return 0;
    }
    directory_size = (uint64_t)count * 40u;
    if (directory_offset > size || directory_size > size - directory_offset) {
        return 0;
    }
    directory_end = directory_offset + directory_size;
    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * 40u;
        const uint64_t offset = get_u64(entry + 8u);
        const uint64_t stored_size = get_u64(entry + 16u);
        const uint16_t flags = get_u16(entry + 6u);
        uint32_t prior;
        if ((flags & ~1u) != 0u || get_u32(entry + 36u) != 0u ||
            get_u64(entry + 24u) != stored_size ||
            (offset & 7u) != 0u || offset < directory_end ||
            offset > size || stored_size > size - offset ||
            crc32c(bytes + (size_t)offset, (size_t)stored_size) !=
                get_u32(entry + 32u)) {
            return 0;
        }
        for (prior = 0u; prior < index; ++prior) {
            const uint8_t *previous = bytes + (size_t)directory_offset +
                                      (size_t)prior * 40u;
            const uint64_t previous_offset = get_u64(previous + 8u);
            const uint64_t previous_size = get_u64(previous + 16u);
            if (stored_size != 0u && previous_size != 0u &&
                offset < previous_offset + previous_size &&
                previous_offset < offset + stored_size) {
                return 0;
            }
        }
        if (is_v1_known(entry)) {
            if (get_u16(entry + 4u) != 1u) return 0;
            if (memcmp(entry, "WOVR", 4u) == 0) {
                const uint8_t *payload = bytes + (size_t)offset;
                uint32_t span_count;
                uint64_t span_offset;
                uint64_t column_offset;
                if (flags != 1u || stored_size < 48u ||
                    get_u32(payload + 4u) == 0u ||
                    get_u32(payload + 16u) == 0u ||
                    get_u32(payload + 20u) == 0u) return 0;
                span_count = get_u32(payload + 20u);
                span_offset = get_u64(payload + 24u);
                column_offset = get_u64(payload + 32u);
                if (span_count > (stored_size - 48u) / 32u ||
                    span_offset < 48u ||
                    span_offset > stored_size ||
                    (uint64_t)span_count * 32u >
                        stored_size - span_offset ||
                    column_offset < 48u || column_offset > stored_size) {
                    return 0;
                }
                ++wovr_count;
            } else if (flags != 0u) {
                return 0;
            }
            ++known;
        } else {
            if ((flags & 1u) != 0u) return 0;
            ++skipped;
        }
    }
    return known == 1u && wovr_count == 1u && skipped == 3u;
}

static int load_binary(const char *path, uint8_t **bytes_out, size_t *size_out)
{
    FILE *file;
    long length;
    uint8_t *bytes;
    if (path == NULL || bytes_out == NULL || size_out == NULL) return 0;
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) return 0;
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, file) !=
                             (size_t)length) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    *bytes_out = bytes;
    *size_out = (size_t)length;
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t fixture[EXPECTED_SIZE];
    uint8_t *allocated = NULL;
    const uint8_t *bytes = fixture;
    size_t size = sizeof(fixture);
    int valid;
    if (argc == 2) {
        if (!load_binary(argv[1], &allocated, &size)) return 1;
        bytes = allocated;
    } else if (argc != 1 || !load_hex(fixture)) {
        return 1;
    }
    valid = consume(bytes, size);
    free(allocated);
    if (!valid) return 1;
    puts("frozen APTA 1.0 consumer: WOVR consumed; 3 optional sections skipped");
    return 0;
}
