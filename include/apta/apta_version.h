// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_VERSION_H
#define APTA_VERSION_H

#define APTA_SPEC_VERSION_MAJOR 0u
#define APTA_SPEC_VERSION_MINOR 1u

#define APTA_API_VERSION_MAJOR 0u
#define APTA_API_VERSION_MINOR 1u
#define APTA_API_VERSION_PATCH 0u

#define APTA_API_VERSION_ENCODE(major, minor, patch) \
    ((((major) & 0x3ffu) << 22) | (((minor) & 0x3ffu) << 12) | ((patch) & 0xfffu))

#define APTA_API_VERSION \
    APTA_API_VERSION_ENCODE( \
        APTA_API_VERSION_MAJOR, \
        APTA_API_VERSION_MINOR, \
        APTA_API_VERSION_PATCH)

#endif /* APTA_VERSION_H */
