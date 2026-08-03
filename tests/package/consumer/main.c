// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>

#include <apta/apta.h>

int main(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;

    apta_context_config_init(&config);
    if (apta_context_create(&config, &context) != APTA_STATUS_OK || context == NULL) {
        return 1;
    }
    return apta_context_destroy(context) == APTA_STATUS_OK ? 0 : 2;
}
