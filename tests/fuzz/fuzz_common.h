// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_FUZZ_COMMON_H
#define APTA_FUZZ_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    APTA_FUZZ_CONTAINER = 0,
    APTA_FUZZ_HEADER_DIRECTORY = 1,
    APTA_FUZZ_METADATA = 2,
    APTA_FUZZ_WAVEFORM = 3,
    APTA_FUZZ_TEMP_LGRD = 4,
    APTA_FUZZ_GGRD_REVN = 5,
    APTA_FUZZ_ROUNDTRIP = 6
} apta_fuzz_parser_mode_t;

int apta_fuzz_parser_input(
    const uint8_t *data,
    size_t size,
    apta_fuzz_parser_mode_t mode);

int apta_fuzz_pcm_input(const uint8_t *data, size_t size);
int apta_fuzz_state_input(const uint8_t *data, size_t size);

#endif /* APTA_FUZZ_COMMON_H */
