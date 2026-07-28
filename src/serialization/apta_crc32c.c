// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

uint32_t apta_internal_crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc;
    size_t index;

    if (data == NULL && size != 0u) {
        return 0u;
    }

    crc = UINT32_C(0xFFFFFFFF);
    for (index = 0u; index < size; ++index) {
        uint32_t bit;

        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0x82F63B78) & mask);
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}