// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_S4_H
#define APTA_S4_H

#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_GRID_FLAG_LOCKED (1u << 8)

#define APTA_REFERENCE_TEMPO_MIN_MILLIBPM 40000u
#define APTA_REFERENCE_TEMPO_MAX_MILLIBPM 300000u
#define APTA_REFERENCE_TEMPO_MAX_CANDIDATES 3u

APTA_API apta_status_t APTA_CALL
apta_session_lock_grid_range(
    apta_session_t *session,
    const apta_frame_range_t *range);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_S4_H */
