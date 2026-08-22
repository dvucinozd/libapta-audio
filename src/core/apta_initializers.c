// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_windows_exports.h"

#include <string.h>

#define APTA_INIT_STRUCT(value)                  \
    do {                                         \
        memset((value), 0, sizeof(*(value)));    \
        (value)->struct_size = (uint32_t)sizeof(*(value)); \
        (value)->api_version = APTA_API_VERSION; \
    } while (0)

void APTA_CALL apta_frame_range_init(apta_frame_range_t *range)
{
    if (range != NULL) {
        APTA_INIT_STRUCT(range);
    }
}

void APTA_CALL apta_memory_requirements_init(
    apta_memory_requirements_t *requirements)
{
    if (requirements != NULL) {
        APTA_INIT_STRUCT(requirements);
    }
}

void APTA_CALL apta_progress_init(apta_progress_t *progress)
{
    if (progress != NULL) {
        APTA_INIT_STRUCT(progress);
    }
}

void APTA_CALL apta_request_progress_init(apta_request_progress_t *progress)
{
    if (progress != NULL) {
        APTA_INIT_STRUCT(progress);
        apta_frame_range_init(&progress->requested_range);
    }
}

void APTA_CALL apta_source_info_init(apta_source_info_t *info)
{
    if (info != NULL) {
        APTA_INIT_STRUCT(info);
        info->total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    }
}

void APTA_CALL apta_result_info_init(apta_result_info_t *info)
{
    if (info != NULL) {
        APTA_INIT_STRUCT(info);
    }
}

void APTA_CALL apta_result_provenance_init(
    apta_result_provenance_t *provenance)
{
    if (provenance != NULL) {
        APTA_INIT_STRUCT(provenance);
    }
}

void APTA_CALL apta_diagnostic_view_init(apta_diagnostic_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        apta_frame_range_init(&view->affected_range);
    }
}

void APTA_CALL apta_waveform_overview_view_init(
    apta_waveform_overview_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        view->level.struct_size = (uint32_t)sizeof(view->level);
        view->level.api_version = APTA_API_VERSION;
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
    }
}

void APTA_CALL apta_waveform_tile_view_init(
    apta_waveform_tile_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        apta_frame_range_init(&view->source_range);
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
    }
}

void APTA_CALL apta_tempo_view_init(apta_tempo_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        view->selected.struct_size = (uint32_t)sizeof(view->selected);
        view->selected.api_version = APTA_API_VERSION;
        apta_frame_range_init(&view->selected.evidence_range);
        apta_frame_range_init(&view->selected.applicability_range);
        view->selected.confidence = APTA_CONFIDENCE_UNKNOWN;
    }
}

void APTA_CALL apta_grid_view_init(apta_grid_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        apta_frame_range_init(&view->requested_range);
        apta_frame_range_init(&view->evidence_range);
        apta_frame_range_init(&view->applicability_range);
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
    }
}

void APTA_CALL apta_output_stream_init(apta_output_stream_t *stream)
{
    if (stream != NULL) {
        APTA_INIT_STRUCT(stream);
    }
}

void APTA_CALL apta_input_stream_init(apta_input_stream_t *stream)
{
    if (stream != NULL) {
        APTA_INIT_STRUCT(stream);
    }
}

void APTA_CALL apta_stream_parse_options_init(
    apta_stream_parse_options_t *options)
{
    if (options != NULL) {
        APTA_INIT_STRUCT(options);
        options->flags = APTA_PARSE_STRICT;
        options->maximum_section_count = 64u;
        options->maximum_overview_spans = 65536u;
        options->maximum_waveform_columns = 16777216u;
        options->requested_features = APTA_FEATURE_ALL_KNOWN;
        options->maximum_input_bytes = UINT64_C(268435456);
        options->maximum_section_bytes = UINT64_C(268435456);
        options->maximum_allocation_bytes = UINT64_C(268435456);
        options->maximum_scratch_bytes = UINT64_C(65536);
    }
}

void APTA_CALL apta_key_view_init(apta_key_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        apta_frame_range_init(&view->applicability_range);
        view->tonic = APTA_KEY_TONIC_UNKNOWN;
        view->mode = APTA_KEY_MODE_UNKNOWN;
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
        view->state = APTA_FEATURE_ABSENT;
    }
}

void APTA_CALL apta_meter_view_init(apta_meter_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
        view->state = APTA_FEATURE_ABSENT;
    }
}

void APTA_CALL apta_quality_view_init(apta_quality_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        view->evidence_coverage_permille =
            APTA_EVIDENCE_COVERAGE_UNKNOWN;
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
        view->state = APTA_FEATURE_ABSENT;
    }
}

void APTA_CALL apta_grid_revision_view_init(apta_grid_revision_view_t *view)
{
    if (view != NULL) {
        APTA_INIT_STRUCT(view);
        apta_frame_range_init(&view->affected_range);
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
        view->state = APTA_GRID_REVISION_NONE;
    }
}
