// SPDX-License-Identifier: Apache-2.0
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    return apta_fuzz_state_input(data, size);
}
