// SPDX-License-Identifier: Apache-2.0
#include "apta_tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t get_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static uint64_t get_u64(const uint8_t *data)
{
    return (uint64_t)get_u32(data) | ((uint64_t)get_u32(data + 4u) << 32u);
}

static void print_usage(FILE *stream)
{
    fputs(
        "Usage: apta-inspect INPUT.apta [--json] [--section SECTION]\n"
        "\n"
        "SECTION: WOVR, WDTL, META, TEMP, LGRD, GGRD, REVN\n"
        "Options:\n"
        "  --json\n"
        "  --section SECTION\n"
        "  --help\n"
        "  --version\n",
        stream);
}

/* The list lives in apta_tool_common.c so a test can pin it against a
 * container that actually carries every section. */
static int section_is_valid(const char *section)
{
    return section == NULL || apta_tool_section_is_known(section);
}

static int include_section(const char *filter, const char *section)
{
    return filter == NULL || strcmp(filter, section) == 0;
}

static void print_text_view(const char *name, const apta_utf8_view_t *view)
{
    printf("%s: ", name);
    if (view->data != NULL) {
        (void)fwrite(view->data, 1u, view->size, stdout);
    }
    fputc('\n', stdout);
}

static void print_human(
    const apta_tool_buffer_t *file,
    const apta_result_t *result,
    const char *section)
{
    apta_result_info_t info;
    apta_feature_mask_t features = apta_result_get_available_features(result);

    apta_result_info_init(&info);
    (void)apta_result_get_info(result, &info);
    if (section == NULL) {
        printf("file-size: %zu\n", file->size);
        printf("container-version: %u\n", get_u16(file->data + 6u));
        printf("specification: %u.%u\n",
               get_u16(file->data + 8u), get_u16(file->data + 10u));
        printf("source-frames: %llu\n",
               (unsigned long long)get_u64(file->data + 40u));
        printf("sample-rate: %u\n", get_u32(file->data + 48u));
        printf("channels: %u\n", (unsigned)get_u16(file->data + 52u));
        printf("generation: %llu\n", (unsigned long long)info.generation);
        printf("session-state: %s\n", apta_tool_session_state_name(info.session_state));
        fputs("features: ", stdout);
        apta_tool_print_feature_list(stdout, features);
        fputc('\n', stdout);
    }

    if (include_section(section, "WOVR")) {
        apta_waveform_overview_view_t overview;
        apta_status_t status;
        uint64_t columns = 0u;
        uint32_t index;
        apta_waveform_overview_view_init(&overview);
        status = apta_result_get_waveform_overview(result, 0u, &overview);
        if (status == APTA_STATUS_OK) {
            for (index = 0u; index < overview.span_count; ++index) {
                columns += overview.spans[index].column_count;
            }
            printf("WOVR: state=%s confidence=%u spans=%u columns=%llu frames-per-column=%u\n",
                   apta_tool_feature_state_name(overview.state),
                   (unsigned)overview.confidence,
                   overview.span_count,
                   (unsigned long long)columns,
                   overview.level.frames_per_column);
        } else {
            printf("WOVR: unavailable\n");
        }
    }
    if (include_section(section, "WDTL")) {
        printf("WDTL: %s\n",
               (features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u
                   ? "present"
                   : "unavailable");
    }
    if (include_section(section, "META")) {
        apta_metadata_view_t metadata;
        apta_status_t status;
        apta_metadata_view_init(&metadata);
        status = apta_result_get_metadata(result, &metadata);
        if (status == APTA_STATUS_OK) {
            fputs("META:\n", stdout);
            if ((metadata.flags & APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT) != 0u) {
                print_text_view("  producer", &metadata.producer_name);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT) != 0u) {
                print_text_view("  producer-version", &metadata.producer_version_string);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_BACKEND_NAME_PRESENT) != 0u) {
                print_text_view("  backend", &metadata.backend_name);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT) != 0u) {
                print_text_view("  backend-version", &metadata.backend_version);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_CREATION_TIME_PRESENT) != 0u) {
                printf("  creation-unix-time: %llu\n",
                       (unsigned long long)metadata.creation_unix_time);
            }
        } else {
            printf("META: unavailable\n");
        }
    }
    if (include_section(section, "TEMP")) {
        apta_tempo_view_t tempo;
        apta_status_t status;
        apta_tempo_view_init(&tempo);
        status = apta_result_get_tempo(result, NULL, &tempo);
        if (status == APTA_STATUS_OK) {
            printf("TEMP: bpm=%.3f millibpm=%u state=%s confidence=%u candidates=%u\n",
                   tempo.selected.tempo_millibpm / 1000.0,
                   tempo.selected.tempo_millibpm,
                   apta_tool_feature_state_name(tempo.selected.state),
                   (unsigned)tempo.selected.confidence,
                   tempo.candidate_count);
        } else {
            printf("TEMP: unavailable\n");
        }
    }
    if (include_section(section, "LGRD")) {
        apta_grid_view_t grid;
        apta_status_t status;
        apta_grid_view_init(&grid);
        status = apta_result_get_beatgrid(
            result, APTA_FEATURE_LOCAL_BEATGRID, NULL, &grid);
        if (status == APTA_STATUS_OK) {
            printf("LGRD: state=%s confidence=%u segments=%u coverage-ranges=%u",
                   apta_tool_feature_state_name(grid.state),
                   (unsigned)grid.confidence,
                   grid.segment_count,
                   grid.coverage_range_count);
            if (grid.segment_count != 0u) {
                printf(" anchor=%llu period=%llu+%u/2^32 beats=%u",
                       (unsigned long long)grid.segments[0].anchor_position.whole_frame,
                       (unsigned long long)grid.segments[0].frames_per_beat.whole_frames,
                       grid.segments[0].frames_per_beat.fraction_q32,
                       grid.segments[0].beat_count);
            }
            fputc('\n', stdout);
        } else {
            printf("LGRD: unavailable\n");
        }
    }
    if (include_section(section, "GGRD")) {
        apta_grid_view_t grid;
        apta_status_t status;
        apta_grid_view_init(&grid);
        status = apta_result_get_beatgrid(
            result, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &grid);
        if (status == APTA_STATUS_OK) {
            printf("GGRD: state=%s confidence=%u segments=%u coverage-ranges=%u",
                   apta_tool_feature_state_name(grid.state),
                   (unsigned)grid.confidence,
                   grid.segment_count,
                   grid.coverage_range_count);
            if (grid.segment_count != 0u) {
                /* The nominal tempo is printed here and not for LGRD because
                 * the global grid is where a per-segment tempo can differ from
                 * the published one. */
                printf(" tempo=%.3f anchor=%llu period=%llu+%u/2^32 beats=%u",
                       grid.segments[0].nominal_tempo_millibpm / 1000.0,
                       (unsigned long long)grid.segments[0].anchor_position.whole_frame,
                       (unsigned long long)grid.segments[0].frames_per_beat.whole_frames,
                       grid.segments[0].frames_per_beat.fraction_q32,
                       grid.segments[0].beat_count);
            }
            fputc('\n', stdout);
        } else {
            printf("GGRD: unavailable\n");
        }
    }
    if (include_section(section, "REVN")) {
        apta_grid_revision_view_t revision;
        apta_grid_revision_view_init(&revision);
        if (apta_result_get_grid_revision(result, &revision) ==
            APTA_STATUS_OK) {
            printf("REVN: revision=%u previous=%u state=%s confidence=%u"
                   " segments=%u beats=%u flags=0x%08x\n",
                   revision.revision_id,
                   revision.previous_revision_id,
                   apta_tool_grid_revision_state_name(revision.state),
                   (unsigned)revision.confidence,
                   revision.proposed_segment_count,
                   revision.proposed_beat_count,
                   revision.flags);
        } else {
            printf("REVN: unavailable\n");
        }
    }
    if (section == NULL) {
        /* Diagnostics are not a container section, so they have no --section
         * filter. They were also not printed at all: a result carrying a
         * warning or an error passed through every tool in silence. */
        const uint32_t count = apta_result_get_diagnostic_count(result);
        uint32_t index;

        printf("diagnostics: %u\n", count);
        for (index = 0u; index < count; ++index) {
            apta_diagnostic_view_t diagnostic;
            apta_diagnostic_view_init(&diagnostic);
            if (apta_result_get_diagnostic(result, index, &diagnostic) !=
                APTA_STATUS_OK) {
                continue;
            }
            printf("  [%s] code=%u features=0x%08lx range=%llu..%llu",
                   apta_tool_diagnostic_severity_name(diagnostic.severity),
                   diagnostic.code,
                   (unsigned long)diagnostic.affected_features,
                   (unsigned long long)diagnostic.affected_range.first_frame,
                   (unsigned long long)diagnostic.affected_range.end_frame);
            if (diagnostic.message != NULL) {
                printf(" %s", diagnostic.message);
            }
            fputc('\n', stdout);
        }
    }
}

static void print_json_text_field(
    const char *name,
    const apta_utf8_view_t *view,
    int *first)
{
    printf("%s\"%s\":", *first ? "" : ",", name);
    apta_tool_json_string(stdout, view->data != NULL ? view->data : "", view->size);
    *first = 0;
}

static void print_json(
    const apta_tool_buffer_t *file,
    const apta_result_t *result,
    const char *section)
{
    apta_result_info_t info;
    apta_feature_mask_t features = apta_result_get_available_features(result);
    int first_section = 1;

    apta_result_info_init(&info);
    (void)apta_result_get_info(result, &info);
    fputc('{', stdout);
    if (section == NULL) {
        printf("\"file_size\":%zu,\"container_version\":%u,"
               "\"specification_major\":%u,\"specification_minor\":%u,"
               "\"source_frames\":%llu,\"sample_rate\":%u,\"channels\":%u,"
               "\"generation\":%llu,\"session_state\":",
               file->size,
               get_u16(file->data + 6u),
               get_u16(file->data + 8u),
               get_u16(file->data + 10u),
               (unsigned long long)get_u64(file->data + 40u),
               get_u32(file->data + 48u),
               (unsigned)get_u16(file->data + 52u),
               (unsigned long long)info.generation);
        apta_tool_json_string(
            stdout,
            apta_tool_session_state_name(info.session_state),
            strlen(apta_tool_session_state_name(info.session_state)));
        printf(",\"available_features\":%llu",
               (unsigned long long)features);
        first_section = 0;
    }

    if (include_section(section, "WOVR")) {
        apta_waveform_overview_view_t overview;
        apta_status_t status;
        uint64_t columns = 0u;
        uint32_t index;
        apta_waveform_overview_view_init(&overview);
        status = apta_result_get_waveform_overview(result, 0u, &overview);
        printf("%s\"WOVR\":", first_section ? "" : ",");
        if (status == APTA_STATUS_OK) {
            for (index = 0u; index < overview.span_count; ++index) {
                columns += overview.spans[index].column_count;
            }
            printf("{\"state\":");
            apta_tool_json_string(stdout,
                apta_tool_feature_state_name(overview.state),
                strlen(apta_tool_feature_state_name(overview.state)));
            printf(",\"confidence\":%u,\"span_count\":%u,"
                   "\"column_count\":%llu,\"frames_per_column\":%u}",
                   (unsigned)overview.confidence,
                   overview.span_count,
                   (unsigned long long)columns,
                   overview.level.frames_per_column);
        } else {
            fputs("null", stdout);
        }
        first_section = 0;
    }
    if (include_section(section, "WDTL")) {
        printf("%s\"WDTL\":%s",
               first_section ? "" : ",",
               (features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u ? "true" : "false");
        first_section = 0;
    }
    if (include_section(section, "META")) {
        apta_metadata_view_t metadata;
        apta_status_t status;
        int first = 1;
        apta_metadata_view_init(&metadata);
        status = apta_result_get_metadata(result, &metadata);
        printf("%s\"META\":", first_section ? "" : ",");
        if (status == APTA_STATUS_OK) {
            fputc('{', stdout);
            if ((metadata.flags & APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT) != 0u) {
                print_json_text_field("producer_name", &metadata.producer_name, &first);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT) != 0u) {
                print_json_text_field("producer_version", &metadata.producer_version_string, &first);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_BACKEND_NAME_PRESENT) != 0u) {
                print_json_text_field("backend_name", &metadata.backend_name, &first);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT) != 0u) {
                print_json_text_field("backend_version", &metadata.backend_version, &first);
            }
            if ((metadata.flags & APTA_METADATA_FLAG_CREATION_TIME_PRESENT) != 0u) {
                printf("%s\"creation_unix_time\":%llu",
                       first ? "" : ",",
                       (unsigned long long)metadata.creation_unix_time);
            }
            fputc('}', stdout);
        } else {
            fputs("null", stdout);
        }
        first_section = 0;
    }
    if (include_section(section, "TEMP")) {
        apta_tempo_view_t tempo;
        apta_status_t status;
        apta_tempo_view_init(&tempo);
        status = apta_result_get_tempo(result, NULL, &tempo);
        printf("%s\"TEMP\":", first_section ? "" : ",");
        if (status == APTA_STATUS_OK) {
            printf("{\"tempo_millibpm\":%u,\"state\":",
                   tempo.selected.tempo_millibpm);
            apta_tool_json_string(stdout,
                apta_tool_feature_state_name(tempo.selected.state),
                strlen(apta_tool_feature_state_name(tempo.selected.state)));
            printf(",\"confidence\":%u,\"candidate_count\":%u}",
                   (unsigned)tempo.selected.confidence,
                   tempo.candidate_count);
        } else {
            fputs("null", stdout);
        }
        first_section = 0;
    }
    if (include_section(section, "LGRD")) {
        apta_grid_view_t grid;
        apta_status_t status;
        apta_grid_view_init(&grid);
        status = apta_result_get_beatgrid(
            result, APTA_FEATURE_LOCAL_BEATGRID, NULL, &grid);
        printf("%s\"LGRD\":", first_section ? "" : ",");
        if (status == APTA_STATUS_OK) {
            printf("{\"state\":");
            apta_tool_json_string(stdout,
                apta_tool_feature_state_name(grid.state),
                strlen(apta_tool_feature_state_name(grid.state)));
            printf(",\"confidence\":%u,\"segment_count\":%u,"
                   "\"coverage_range_count\":%u",
                   (unsigned)grid.confidence,
                   grid.segment_count,
                   grid.coverage_range_count);
            if (grid.segment_count != 0u) {
                printf(",\"anchor_frame\":%llu,\"period_whole_frames\":%llu,"
                       "\"period_fraction_q32\":%u,\"beat_count\":%u",
                       (unsigned long long)grid.segments[0].anchor_position.whole_frame,
                       (unsigned long long)grid.segments[0].frames_per_beat.whole_frames,
                       grid.segments[0].frames_per_beat.fraction_q32,
                       grid.segments[0].beat_count);
            }
            fputc('}', stdout);
        } else {
            fputs("null", stdout);
        }
        first_section = 0;
    }
    if (include_section(section, "GGRD")) {
        apta_grid_view_t grid;
        apta_status_t status;
        apta_grid_view_init(&grid);
        status = apta_result_get_beatgrid(
            result, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &grid);
        printf("%s\"GGRD\":", first_section ? "" : ",");
        if (status == APTA_STATUS_OK) {
            printf("{\"state\":");
            apta_tool_json_string(stdout,
                apta_tool_feature_state_name(grid.state),
                strlen(apta_tool_feature_state_name(grid.state)));
            printf(",\"confidence\":%u,\"segment_count\":%u,"
                   "\"coverage_range_count\":%u",
                   (unsigned)grid.confidence,
                   grid.segment_count,
                   grid.coverage_range_count);
            if (grid.segment_count != 0u) {
                printf(",\"nominal_tempo_millibpm\":%u,\"anchor_frame\":%llu,"
                       "\"period_whole_frames\":%llu,"
                       "\"period_fraction_q32\":%u,\"beat_count\":%u",
                       grid.segments[0].nominal_tempo_millibpm,
                       (unsigned long long)grid.segments[0].anchor_position.whole_frame,
                       (unsigned long long)grid.segments[0].frames_per_beat.whole_frames,
                       grid.segments[0].frames_per_beat.fraction_q32,
                       grid.segments[0].beat_count);
            }
            fputc('}', stdout);
        } else {
            fputs("null", stdout);
        }
        first_section = 0;
    }
    if (include_section(section, "REVN")) {
        apta_grid_revision_view_t revision;
        apta_grid_revision_view_init(&revision);
        printf("%s\"REVN\":", first_section ? "" : ",");
        if (apta_result_get_grid_revision(result, &revision) ==
            APTA_STATUS_OK) {
            printf("{\"revision_id\":%u,\"previous_revision_id\":%u,"
                   "\"state\":",
                   revision.revision_id, revision.previous_revision_id);
            apta_tool_json_string(stdout,
                apta_tool_grid_revision_state_name(revision.state),
                strlen(apta_tool_grid_revision_state_name(revision.state)));
            printf(",\"confidence\":%u,\"proposed_segment_count\":%u,"
                   "\"proposed_beat_count\":%u,\"flags\":%u}",
                   (unsigned)revision.confidence,
                   revision.proposed_segment_count,
                   revision.proposed_beat_count,
                   revision.flags);
        } else {
            fputs("null", stdout);
        }
    }
    fputs("}\n", stdout);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *section = NULL;
    int json = 0;
    int index;
    apta_tool_buffer_t file = {NULL, 0u};
    apta_context_config_t context_config;
    apta_parse_options_t parse_options;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_status_t status;
    int exit_code = 1;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("apta-inspect %u.%u.%u\n",
               APTA_VERSION_MAJOR, APTA_VERSION_MINOR, APTA_VERSION_PATCH);
        return 0;
    }
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }
    path = argv[1];
    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[index], "--section") == 0) {
            if (++index >= argc || section != NULL) {
                print_usage(stderr);
                return 2;
            }
            section = argv[index];
        } else {
            fprintf(stderr, "apta-inspect: unknown option: %s\n", argv[index]);
            return 2;
        }
    }
    if (!section_is_valid(section)) {
        fprintf(stderr, "apta-inspect: invalid section: %s\n", section);
        return 2;
    }

    status = apta_tool_read_file(
        path, APTA_TOOL_DEFAULT_MAX_FILE_BYTES, &file);
    if (status < 0) {
        fprintf(stderr, "apta-inspect: cannot read %s: %s\n",
                path, apta_tool_status_name(status));
        goto cleanup;
    }
    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING;
    status = apta_context_create(&context_config, &context);
    if (status < 0) {
        fprintf(stderr, "apta-inspect: cannot create context: %s\n",
                apta_tool_status_name(status));
        goto cleanup;
    }
    apta_parse_options_init(&parse_options);
    parse_options.maximum_file_bytes = APTA_TOOL_DEFAULT_MAX_FILE_BYTES;
    status = apta_result_parse(
        context, &parse_options, file.data, file.size, &result);
    if (status < 0) {
        fprintf(stderr, "apta-inspect: invalid container: %s\n",
                apta_tool_status_name(status));
        goto cleanup;
    }
    if (file.size < 96u) {
        fprintf(stderr, "apta-inspect: invalid fixed header\n");
        goto cleanup;
    }

    if (json) {
        print_json(&file, result, section);
    } else {
        print_human(&file, result, section);
    }
    exit_code = 0;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (context != NULL) {
        (void)apta_context_destroy(context);
    }
    apta_tool_buffer_release(&file);
    return exit_code;
}
