// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>

#include <apta/apta.h>

int main(void)
{
    printf("package=%s\n", APTA_PACKAGE_VERSION_STRING);
    printf("api=%u.%u.%u\n",
           APTA_API_VERSION_MAJOR,
           APTA_API_VERSION_MINOR,
           APTA_API_VERSION_PATCH);
    printf("spec=%u.%u\n",
           APTA_SPEC_VERSION_MAJOR,
           APTA_SPEC_VERSION_MINOR);
    printf("container=%u\n", APTA_CONTAINER_VERSION);
    return 0;
}
