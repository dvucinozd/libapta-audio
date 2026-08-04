// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>

#include <apta/apta.h>

int main(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_status_t status;

    apta_context_config_init(&config);
    status = apta_context_create(&config, &context);
    if (status != APTA_STATUS_OK) {
        fprintf(stderr, "apta_context_create failed: %d\n", (int)status);
        return 1;
    }

    printf("libapta package %s, API %u.%u.%u, capabilities 0x%llx\n",
           APTA_PACKAGE_VERSION_STRING,
           (unsigned)APTA_API_VERSION_MAJOR,
           (unsigned)APTA_API_VERSION_MINOR,
           (unsigned)APTA_API_VERSION_PATCH,
           (unsigned long long)apta_context_get_capabilities(context));

    return apta_context_destroy(context) == APTA_STATUS_OK ? 0 : 1;
}
