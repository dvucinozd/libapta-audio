// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_VERSION_H
#define APTA_VERSION_H

#define APTA_SPEC_VERSION_MAJOR 0u
#define APTA_SPEC_VERSION_MINOR 1u

/*
 * 0.2.0 adds public API surface without changing any struct size:
 *
 *   - apta_query_workspace_requirements();
 *   - apta_session_config_t.overview_frames_per_column, taken from reserved
 *     space;
 *   - APTA_FEATURE_WAVEFORM_3BAND becomes a supported capability;
 *   - APTA_TEMPO_RELATION_THIRD, _TRIPLE, _QUARTER, _QUADRUPLE;
 *   - APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY.
 *
 * Note that apta_internal_validate_struct() compares api_version for exact
 * equality, so a minor bump is a hard compatibility break rather than an
 * additive marker: a caller compiled against 0.1.0 headers is rejected with
 * APTA_ERROR_INCOMPATIBLE_VERSION until it is recompiled. That is acceptable
 * while the project withholds any stable API/ABI claim, and the check is left
 * as it is rather than quietly relaxed.
 */
#define APTA_API_VERSION_MAJOR 0u
#define APTA_API_VERSION_MINOR 2u
#define APTA_API_VERSION_PATCH 0u

#define APTA_API_VERSION_ENCODE(major, minor, patch) \
    ((((major) & 0x3ffu) << 22) | (((minor) & 0x3ffu) << 12) | ((patch) & 0xfffu))

#define APTA_API_VERSION \
    APTA_API_VERSION_ENCODE( \
        APTA_API_VERSION_MAJOR, \
        APTA_API_VERSION_MINOR, \
        APTA_API_VERSION_PATCH)

#endif /* APTA_VERSION_H */
