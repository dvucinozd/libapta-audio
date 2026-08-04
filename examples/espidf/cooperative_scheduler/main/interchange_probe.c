// SPDX-License-Identifier: Apache-2.0
/*
 * P6 build-time ESP-IDF integration probe.
 *
 * The hosted CI builds this object into every supported firmware image. It is
 * deliberately not called by the demonstration application because hosted CI
 * cannot execute ESP32 firmware. The retained P6 report therefore classifies
 * this as firmware-build evidence, not a hardware runtime result.
 */
#include <stdint.h>

#include <apta/apta.h>

#include "apta_interchange_fixture.inc"

int apta_espidf_interchange_probe(apta_context_t *context)
{
    const apta_feature_mask_t expected_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_parse_options_t options;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    apta_source_info_t source;
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    apta_grid_revision_view_t revision;
    int success = 0;

    if (context == NULL ||
        apta_interchange_fixture_size != 1032u) {
        return 0;
    }

    apta_parse_options_init(&options);
    options.flags = APTA_PARSE_STRICT;
    if (apta_result_parse(
            context,
            &options,
            apta_interchange_fixture,
            apta_interchange_fixture_size,
            &result) != APTA_STATUS_OK ||
        result == NULL) {
        goto cleanup;
    }

    apta_result_info_init(&info);
    apta_source_info_init(&source);
    apta_tempo_view_init(&tempo);
    apta_grid_view_init(&grid);
    apta_grid_revision_view_init(&revision);
    if (apta_result_get_info(result, &info) != APTA_STATUS_OK ||
        apta_result_get_source_info(result, &source) != APTA_STATUS_OK ||
        apta_result_get_tempo(result, NULL, &tempo) != APTA_STATUS_OK ||
        apta_result_get_beatgrid(
            result,
            APTA_FEATURE_GLOBAL_BEATGRID,
            NULL,
            &grid) != APTA_STATUS_OK ||
        apta_result_get_grid_revision(result, &revision) != APTA_STATUS_OK) {
        goto cleanup;
    }

    success =
        info.container_version == 1u &&
        info.available_features == expected_features &&
        source.total_frames == 1024u &&
        source.sample_rate == 48000u &&
        source.channel_count == 1u &&
        tempo.selected.tempo_millibpm == 120000u &&
        grid.representation == APTA_GRID_REPRESENTATION_SEGMENTS &&
        grid.segment_count == 1u &&
        revision.state == APTA_GRID_REVISION_APPLIED &&
        revision.revision_id == 1u;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    return success;
}
