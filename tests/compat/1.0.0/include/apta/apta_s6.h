// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_S6_H
#define APTA_S6_H

#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS 8u

/*
 * Beats the reference implementation will materialise for a global grid.
 *
 * Reduced from 4096 in API 0.3.0. At 128 BPM this is 24 minutes of beats,
 * against an analysis ring that covers 12.7, so the array is still the larger
 * of the two. The 1024 entries removed are 40,960 bytes, and on an ESP32-P4
 * that is what brings the S6 workspace inside internal SRAM instead of PSRAM --
 * measured at roughly three and a half times the cost per call. See section 30
 * of docs/status/S4-TEMPO-LOCAL-GRID-STATUS.md.
 *
 * A host that sizes its own storage from this constant needs no change. A host
 * that hard-coded 4096 and expects that many beats from a long track will see
 * fewer.
 */
#define APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS    3072u

#define APTA_GRID_REVISION_NONE    0u
#define APTA_GRID_REVISION_PENDING 1u
#define APTA_GRID_REVISION_APPLIED 2u

#define APTA_GRID_REVISION_FLAG_CONFLICTS_LOCKED_RANGE (1u << 0)
#define APTA_GRID_REVISION_FLAG_DYNAMIC_TEMPO          (1u << 1)
#define APTA_GRID_REVISION_FLAG_DEGRADED               (1u << 2)

typedef uint32_t apta_grid_revision_state_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t revision_id;
    uint32_t previous_revision_id;
    apta_grid_revision_state_t state;
    apta_confidence_value_t confidence;
    uint8_t reserved8[3];

    apta_frame_range_t affected_range;
    apta_grid_representation_t proposed_representation;
    uint32_t proposed_segment_count;
    uint32_t proposed_beat_count;
    uint32_t flags;

    uint32_t reserved32[4];
    uint64_t reserved64[2];
} apta_grid_revision_view_t;

APTA_API void APTA_CALL
apta_grid_revision_view_init(apta_grid_revision_view_t *view);

APTA_API apta_status_t APTA_CALL
apta_result_get_grid_revision(
    const apta_result_t *result,
    apta_grid_revision_view_t *view_out);

APTA_API apta_status_t APTA_CALL
apta_session_apply_grid_revision(
    apta_session_t *session,
    uint32_t revision_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_S6_H */
