// SPDX-License-Identifier: Apache-2.0
/*
 * Frozen APTA-1.0-era consumer: intentionally does not include or link
 * libapta. It recognizes only the 1.0 FourCC registry and skips every other
 * optional section after validating its common v1 frame and CRC32C.
 */
#include <stdint.h>
#include <stdio.h>
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

int main(void)
{
    uint8_t bytes[EXPECTED_SIZE];
    uint32_t count;
    uint64_t directory_offset;
    uint32_t known = 0u;
    uint32_t skipped = 0u;
    uint32_t index;

    if (!load_hex(bytes) || memcmp(bytes, "APTA", 4u) != 0 ||
        get_u16(bytes + 4u) != 96u || get_u16(bytes + 6u) != 1u ||
        get_u64(bytes + 32u) != sizeof(bytes) ||
        crc32c(bytes, 92u) != get_u32(bytes + 92u)) {
        return 1;
    }
    count = get_u32(bytes + 20u);
    directory_offset = get_u64(bytes + 24u);
    if (directory_offset > sizeof(bytes) ||
        count > (sizeof(bytes) - (size_t)directory_offset) / 40u) {
        return 1;
    }
    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * 40u;
        const uint64_t offset = get_u64(entry + 8u);
        const uint64_t stored_size = get_u64(entry + 16u);
        const uint16_t flags = get_u16(entry + 6u);
        if ((flags & ~1u) != 0u || get_u32(entry + 36u) != 0u ||
            get_u64(entry + 24u) != stored_size ||
            offset > sizeof(bytes) || stored_size > sizeof(bytes) - offset ||
            crc32c(bytes + (size_t)offset, (size_t)stored_size) !=
                get_u32(entry + 32u)) {
            return 1;
        }
        if (is_v1_known(entry)) {
            if (get_u16(entry + 4u) != 1u) return 1;
            ++known;
        } else {
            if ((flags & 1u) != 0u) return 1;
            ++skipped;
        }
    }
    if (known != 1u || skipped != 3u) return 1;
    puts("frozen APTA 1.0 consumer: WOVR consumed; 3 optional sections skipped");
    return 0;
}
