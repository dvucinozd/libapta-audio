// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define FIXTURE_SIZE 664u
#define MKEY_DIRECTORY 136u
#define MTRD_DIRECTORY 176u
#define CONF_DIRECTORY 216u
#define MKEY_PAYLOAD 352u
#define MTRD_PAYLOAD 424u
#define CONF_PAYLOAD 584u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

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

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4u) << 32u);
}

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *p, uint64_t value)
{
    put_u32(p, (uint32_t)value);
    put_u32(p + 4u, (uint32_t)(value >> 32u));
}

static int load_fixture(uint8_t *bytes)
{
    FILE *file = fopen(APTA_DJ_GOLDEN_HEX_PATH, "rb");
    int high = -1;
    size_t size = 0u;
    int character;
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
            if (size == FIXTURE_SIZE) {
                fclose(file);
                return 0;
            }
            bytes[size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    return high < 0 && size == FIXTURE_SIZE;
}

static void refresh_section(uint8_t *bytes, size_t directory)
{
    const uint64_t offset = get_u64(bytes + directory + 8u);
    const uint64_t size = get_u64(bytes + directory + 16u);
    put_u32(bytes + directory + 32u,
            crc32c(bytes + (size_t)offset, (size_t)size));
}

static apta_status_t parse_case(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *bytes,
    size_t size)
{
    const apta_result_t *result = NULL;
    const apta_status_t status = apta_result_parse(
        context, options, bytes, size, &result);
    if (result != NULL) apta_result_release(result);
    return status;
}

static void write_unknown_entry(
    uint8_t *entry,
    const char fourcc[4],
    uint64_t offset,
    uint8_t payload)
{
    memset(entry, 0, 40u);
    memcpy(entry, fourcc, 4u);
    put_u16(entry + 4u, 1u);
    put_u64(entry + 8u, offset);
    put_u64(entry + 16u, 1u);
    put_u64(entry + 24u, 1u);
    put_u32(entry + 32u, crc32c(&payload, 1u));
}

static size_t add_unknown_sections(
    const uint8_t *source,
    uint8_t *output,
    size_t capacity)
{
    static const uint32_t old_entries[4] = {96u, 136u, 176u, 216u};
    static const uint32_t new_indices[4] = {0u, 2u, 4u, 6u};
    uint32_t index;
    if (capacity < 801u) return 0u;
    memset(output, 0, 801u);
    memcpy(output, source, 96u);
    memcpy(output + 376u, source + 256u, FIXTURE_SIZE - 256u);
    for (index = 0u; index < 4u; ++index) {
        uint8_t *entry = output + 96u + (size_t)new_indices[index] * 40u;
        memcpy(entry, source + old_entries[index], 40u);
        put_u64(entry + 8u, get_u64(entry + 8u) + 120u);
    }
    output[784] = 0x11u;
    output[792] = 0x22u;
    output[800] = 0x33u;
    write_unknown_entry(output + 136u, "U001", 784u, output[784]);
    write_unknown_entry(output + 216u, "U002", 792u, output[792]);
    write_unknown_entry(output + 296u, "U003", 800u, output[800]);
    put_u32(output + 20u, 7u);
    put_u64(output + 32u, 801u);
    put_u32(output + 92u, crc32c(output, 92u));
    return 801u;
}

int main(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_parse_options_t permissive;
    uint8_t original[FIXTURE_SIZE];
    uint8_t mutated[FIXTURE_SIZE];
    uint8_t unknown[801];
    size_t prefix;

    CHECK(load_fixture(original));
    apta_context_config_init(&config);
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(parse_case(context, NULL, original, sizeof(original)) ==
          APTA_STATUS_OK);

    for (prefix = 0u; prefix < sizeof(original); ++prefix) {
        CHECK(parse_case(context, NULL, original, prefix) < 0);
    }

    memcpy(mutated, original, sizeof(mutated));
    mutated[MKEY_PAYLOAD + 4u] ^= 1u;
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    mutated[MTRD_PAYLOAD + 4u] ^= 1u;
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    mutated[CONF_PAYLOAD + 4u] ^= 1u;
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    memcpy(mutated, original, sizeof(mutated));
    put_u32(mutated + MKEY_PAYLOAD + 12u, UINT32_MAX);
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_LIMIT_EXCEEDED);
    memcpy(mutated, original, sizeof(mutated));
    put_u32(mutated + MTRD_PAYLOAD + 12u, UINT32_MAX);
    refresh_section(mutated, MTRD_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_LIMIT_EXCEEDED);
    memcpy(mutated, original, sizeof(mutated));
    put_u32(mutated + CONF_PAYLOAD + 4u, UINT32_MAX);
    refresh_section(mutated, CONF_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_LIMIT_EXCEEDED);
    memcpy(mutated, original, sizeof(mutated));
    put_u64(mutated + MKEY_DIRECTORY + 16u, UINT64_MAX);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    memcpy(mutated, original, sizeof(mutated));
    put_u16(mutated + MKEY_DIRECTORY + 6u, 1u);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u16(mutated + MKEY_DIRECTORY + 4u, 2u);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_UNSUPPORTED);

    memcpy(mutated, original, sizeof(mutated));
    memcpy(mutated + MTRD_DIRECTORY, "MKEY", 4u);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    memcpy(mutated + MKEY_DIRECTORY, "MTRD", 4u);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    memcpy(mutated + MKEY_DIRECTORY, "CONF", 4u);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    memcpy(mutated, original, sizeof(mutated));
    mutated[MKEY_PAYLOAD + 5u] = 3u;
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u16(mutated + MKEY_PAYLOAD + 6u, 101u);
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    mutated[MKEY_PAYLOAD + 3u] = 101u;
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u16(mutated + MKEY_PAYLOAD + 56u + 4u, 62000u);
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    memcpy(mutated + MKEY_PAYLOAD + 56u,
           mutated + MKEY_PAYLOAD + 40u, 4u);
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u64(mutated + MKEY_PAYLOAD + 24u, 120u);
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    memcpy(mutated, original, sizeof(mutated));
    put_u16(mutated + MTRD_PAYLOAD + 6u, 3u);
    refresh_section(mutated, MTRD_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u64(mutated + MTRD_PAYLOAD + 104u, 47000u);
    refresh_section(mutated, MTRD_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u32(mutated + MTRD_PAYLOAD + 104u + 44u, 4u);
    refresh_section(mutated, MTRD_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    memcpy(mutated, original, sizeof(mutated));
    put_u64(mutated + CONF_PAYLOAD + 48u,
            APTA_FEATURE_MUSICAL_KEY);
    refresh_section(mutated, CONF_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    memcpy(mutated, original, sizeof(mutated));
    put_u64(mutated + CONF_PAYLOAD + 16u, APTA_FEATURE_BPM);
    refresh_section(mutated, CONF_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    memcpy(mutated, original, sizeof(mutated));
    mutated[MKEY_PAYLOAD + 36u] = 1u;
    refresh_section(mutated, MKEY_DIRECTORY);
    CHECK(parse_case(context, NULL, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);
    apta_parse_options_init(&permissive);
    permissive.flags = 0u;
    CHECK(parse_case(context, &permissive, mutated, sizeof(mutated)) ==
          APTA_ERROR_CORRUPT_DATA);

    CHECK(add_unknown_sections(original, unknown, sizeof(unknown)) ==
          sizeof(unknown));
    CHECK(parse_case(context, NULL, unknown, sizeof(unknown)) ==
          APTA_STATUS_OK);

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
