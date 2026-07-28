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
            (uint32_t)sizeof(grid.requested_range)) {
        return 1;
    }

    if (overview.confidence != APTA_CONFIDENCE_UNKNOWN ||
        tile.confidence != APTA_CONFIDENCE_UNKNOWN ||
        tempo.selected.confidence != APTA_CONFIDENCE_UNKNOWN ||
        grid.confidence != APTA_CONFIDENCE_UNKNOWN ||
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
