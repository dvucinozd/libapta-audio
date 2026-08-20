// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_INITIALIZERS_H
#define APTA_INITIALIZERS_H

#include <apta/apta_config.h>
#include <apta/apta_result.h>
#include <apta/apta_s6.h>
#include <apta/apta_source.h>

#ifdef __cplusplus
extern "C" {
#endif

APTA_API void APTA_CALL
apta_frame_range_init(apta_frame_range_t *range);

APTA_API void APTA_CALL
apta_memory_requirements_init(apta_memory_requirements_t *requirements);

APTA_API void APTA_CALL
apta_progress_init(apta_progress_t *progress);

APTA_API void APTA_CALL
apta_request_progress_init(apta_request_progress_t *progress);

APTA_API void APTA_CALL
apta_source_info_init(apta_source_info_t *info);

APTA_API void APTA_CALL
apta_result_info_init(apta_result_info_t *info);

APTA_API void APTA_CALL
apta_diagnostic_view_init(apta_diagnostic_view_t *view);

APTA_API void APTA_CALL
apta_waveform_overview_view_init(apta_waveform_overview_view_t *view);

APTA_API void APTA_CALL
apta_waveform_tile_view_init(apta_waveform_tile_view_t *view);

APTA_API void APTA_CALL
apta_tempo_view_init(apta_tempo_view_t *view);

APTA_API void APTA_CALL
apta_grid_view_init(apta_grid_view_t *view);

APTA_API void APTA_CALL
apta_key_view_init(apta_key_view_t *view);

APTA_API void APTA_CALL
apta_meter_view_init(apta_meter_view_t *view);

APTA_API void APTA_CALL
apta_quality_view_init(apta_quality_view_t *view);

APTA_API void APTA_CALL
apta_grid_revision_view_init(apta_grid_revision_view_t *view);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_INITIALIZERS_H */
