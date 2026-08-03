// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_VERSION_H
#define APTA_VERSION_H

#define APTA_PACKAGE_VERSION_MAJOR 1u
#define APTA_PACKAGE_VERSION_MINOR 0u
#define APTA_PACKAGE_VERSION_PATCH 0u
#define APTA_PACKAGE_VERSION_PRERELEASE "rc.1"
#define APTA_PACKAGE_VERSION_STRING "1.0.0-rc.1"

#define APTA_SPEC_VERSION_MAJOR 1u
#define APTA_SPEC_VERSION_MINOR 0u

#define APTA_API_VERSION_MAJOR 1u
#define APTA_API_VERSION_MINOR 0u
#define APTA_API_VERSION_PATCH 0u

#define APTA_CONTAINER_VERSION 1u

#define APTA_API_VERSION_ENCODE(major, minor, patch) \
    ((((major) & 0x3ffu) << 22) | (((minor) & 0x3ffu) << 12) | ((patch) & 0xfffu))

#define APTA_API_VERSION_GET_MAJOR(version) (((version) >> 22) & 0x3ffu)
#define APTA_API_VERSION_GET_MINOR(version) (((version) >> 12) & 0x3ffu)
#define APTA_API_VERSION_GET_PATCH(version) ((version) & 0xfffu)

#define APTA_API_VERSION \
    APTA_API_VERSION_ENCODE( \
        APTA_API_VERSION_MAJOR, \
        APTA_API_VERSION_MINOR, \
        APTA_API_VERSION_PATCH)

#endif /* APTA_VERSION_H */
