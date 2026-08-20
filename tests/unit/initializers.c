// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK_PREFIX(value)                                                  \
    do {                                                                     \
        if ((value).struct_size != (uint32_t)sizeof(value) ||                \
            (value).api_version != APTA_API_VERSION) {                       \
            fprintf(stderr, "Invalid prefix at %s:%d\n", __FILE__, __LINE__); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    apta_frame_range_t range;
    apta_memory_requirements_t memory;
    apta_progress_t progress;
    apta_request_progress_t request_progress;
    apta_result_info_t result_info;
    apta_diagnostic_view_t diagnostic;
    apta_waveform_overview_view_t overview;
    apta_waveform_tile_view_t tile;
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    apta_result_builder_options_t builder_options;
    apta_result_builder_info_t builder_info;
    apta_result_provenance_t provenance;
    apta_waveform_detail_input_t detail_input;
    apta_metadata_t metadata;
    apta_metadata_view_t metadata_view;
    apta_serialize_options_t serialization;
    apta_parse_options_t parsing;

    apta_frame_range_init(&range);
    apta_memory_requirements_init(&memory);
    apta_progress_init(&progress);
    apta_request_progress_init(&request_progress);
    apta_result_info_init(&result_info);
    apta_diagnostic_view_init(&diagnostic);
    apta_waveform_overview_view_init(&overview);
    apta_waveform_tile_view_init(&tile);
    apta_tempo_view_init(&tempo);
    apta_grid_view_init(&grid);
    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_quality_view_init(&quality);
    apta_result_builder_options_init(&builder_options);
    apta_result_builder_info_init(&builder_info);
    apta_result_provenance_init(&provenance);
    apta_waveform_detail_input_init(&detail_input);
    apta_metadata_init(&metadata);
    apta_metadata_view_init(&metadata_view);
    apta_serialize_options_init(&serialization);
    apta_parse_options_init(&parsing);

    CHECK_PREFIX(range);
    CHECK_PREFIX(memory);
    CHECK_PREFIX(progress);
    CHECK_PREFIX(request_progress);
    CHECK_PREFIX(result_info);
    CHECK_PREFIX(diagnostic);
    CHECK_PREFIX(overview);
    CHECK_PREFIX(tile);
    CHECK_PREFIX(tempo);
    CHECK_PREFIX(grid);
    CHECK_PREFIX(key);
    CHECK_PREFIX(meter);
    CHECK_PREFIX(quality);
    CHECK_PREFIX(builder_options);
    CHECK_PREFIX(builder_info);
    CHECK_PREFIX(provenance);
    CHECK_PREFIX(detail_input);
    CHECK_PREFIX(metadata);
    CHECK_PREFIX(metadata_view);
    CHECK_PREFIX(serialization);
    CHECK_PREFIX(parsing);

    if (request_progress.requested_range.struct_size !=
            (uint32_t)sizeof(request_progress.requested_range) ||
        diagnostic.affected_range.struct_size !=
            (uint32_t)sizeof(diagnostic.affected_range) ||
        overview.level.struct_size != (uint32_t)sizeof(overview.level) ||
        tile.source_range.struct_size !=
            (uint32_t)sizeof(tile.source_range) ||
        tempo.selected.evidence_range.struct_size !=
            (uint32_t)sizeof(tempo.selected.evidence_range) ||
        grid.requested_range.struct_size !=
            (uint32_t)sizeof(grid.requested_range) ||
        key.applicability_range.struct_size !=
            (uint32_t)sizeof(key.applicability_range)) {
        return 1;
    }

    if (overview.confidence != APTA_CONFIDENCE_UNKNOWN ||
        tile.confidence != APTA_CONFIDENCE_UNKNOWN ||
        tempo.selected.confidence != APTA_CONFIDENCE_UNKNOWN ||
        grid.confidence != APTA_CONFIDENCE_UNKNOWN ||
        key.tonic != APTA_KEY_TONIC_UNKNOWN ||
        key.mode != APTA_KEY_MODE_UNKNOWN ||
        key.confidence != APTA_CONFIDENCE_UNKNOWN ||
        key.state != APTA_FEATURE_ABSENT ||
        meter.confidence != APTA_CONFIDENCE_UNKNOWN ||
        meter.state != APTA_FEATURE_ABSENT ||
        quality.evidence_coverage_permille !=
            APTA_EVIDENCE_COVERAGE_UNKNOWN ||
        quality.confidence != APTA_CONFIDENCE_UNKNOWN ||
        quality.state != APTA_FEATURE_ABSENT ||
        builder_options.maximum_allocation_bytes == 0u ||
        builder_info.generation == 0u ||
        provenance.origin != APTA_RESULT_PROVENANCE_UNSPECIFIED ||
        detail_input.tile_count != 0u ||
        metadata.flags != 0u ||
        metadata.application_source_id_kind !=
            APTA_METADATA_SOURCE_ID_NONE ||
        metadata_view.flags != 0u ||
        serialization.flags != APTA_SERIALIZE_CANONICAL ||
        parsing.flags != APTA_PARSE_STRICT ||
        parsing.maximum_section_count == 0u ||
        parsing.maximum_overview_spans == 0u ||
        parsing.maximum_waveform_columns == 0u ||
        parsing.maximum_file_bytes == 0u ||
        parsing.maximum_allocation_bytes == 0u) {
        return 1;
    }

    return 0;
}
