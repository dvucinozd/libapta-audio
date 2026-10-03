// SPDX-License-Identifier: Apache-2.0
// Public unchanged-C checkpoint rehydration and compatibility traces.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "failed line %d: %s\n", __LINE__, #expression); \
            exit(2); \
        } \
    } while (0)

static void snapshot(apta_session_t *session, unsigned event, int status, unsigned accepted) {
    const apta_result_t *result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_t info;
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_init(&overview);
    int found = apta_result_get_waveform_overview(result, 0, &overview);
    CHECK(found == APTA_STATUS_OK || found == APTA_STATUS_NOT_AVAILABLE);
    printf("H %u %d %u %u %" PRIu64 " %u %" PRIu64 " %" PRIu64 " %u %u %u\n",
           event, status, accepted, apta_session_get_state(session), info.generation,
           info.session_state, info.available_features, info.changed_features,
           found == APTA_STATUS_OK ? overview.state : 0,
           found == APTA_STATUS_OK ? overview.confidence : 0,
           found == APTA_STATUS_OK ? overview.span_count : 0);
    if (found == APTA_STATUS_OK) {
        for (uint32_t i = 0; i < overview.span_count; ++i) {
            const apta_waveform_span_t *span = &overview.spans[i];
            printf("S %" PRIu64 " %" PRIu64 " %u\n", span->source_range.first_frame,
                   span->source_range.end_frame, span->column_count);
            for (uint32_t j = 0; j < span->column_count; ++j) {
                const apta_waveform_column_t *column = &span->columns[j];
                printf("C %d %d %u %u %u %u %u\n", column->minimum, column->maximum,
                       column->rms, column->low, column->mid, column->high, column->flags);
            }
        }
    }
    apta_result_release(result);
}

static const apta_result_t *checkpoint(apta_context_t *context, unsigned scenario, int second) {
    apta_result_builder_t *builder = NULL;
    apta_result_builder_options_t options;
    apta_result_builder_options_init(&options);
    CHECK(apta_result_builder_create(context, &options, &builder) == APTA_STATUS_OK);
    apta_source_info_t source;
    apta_source_info_init(&source);
    source.sample_rate = 48000;
    source.channel_count = 1;
    source.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    source.total_frames = scenario == 10 ? APTA_TOTAL_FRAMES_UNKNOWN
                          : scenario == 11 ? 2500 : scenario == 13 ? 8192 : 4096;
    source.fingerprint_kind = APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
    source.fingerprint[0] = 0x41;
    CHECK(apta_result_builder_set_source_info(builder, &source) == APTA_STATUS_OK);
    apta_result_builder_info_t info;
    apta_result_builder_info_init(&info);
    info.generation = 77;
    info.session_state = APTA_SESSION_ACTIVE;
    info.lineage_id_high = 123;
    info.lineage_id_low = 456;
    CHECK(apta_result_builder_set_info(builder, &info) == APTA_STATUS_OK);
    apta_result_provenance_t provenance;
    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "seed";
    provenance.source_name.size = 4;
    CHECK(apta_result_builder_set_provenance(builder, &provenance) == APTA_STATUS_OK);
    apta_waveform_column_t columns[8];
    const uint16_t energy[8] = {0, 1, 32767, 32768, 65534, 65535, 32000, 17};
    for (unsigned i = 0; i < 8; ++i) {
        columns[i] = (apta_waveform_column_t){0};
        columns[i].minimum = i == 5 ? INT16_MIN : (int16_t)(-12000 - i * 1000);
        columns[i].maximum = i == 5 ? INT16_MAX : (int16_t)(19000 + i * 1000);
        columns[i].rms = scenario == 13 ? energy[i] : 32000;
        columns[i].flags = i == 5 ? 5 : 9;
        if (i != 5) { columns[i].low = 12; columns[i].mid = 23; columns[i].high = 34; }
    }
    apta_waveform_span_t span = {0};
    span.first_column_index = scenario == 11 ? 2 : scenario == 2 && second ? 1 : 0;
    span.column_count = scenario == 13 ? 8 : scenario == 2 ? 2 : 1;
    span.source_range.first_frame = (uint64_t)span.first_column_index * 1024;
    span.source_range.end_frame = scenario == 11 ? 2500
                                  : span.source_range.first_frame + span.column_count * 1024;
    span.columns = columns;
    span.source_range.struct_size = sizeof(span.source_range);
    span.source_range.api_version = APTA_API_VERSION;
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 1024;
    overview.state = APTA_FEATURE_PARTIAL;
    overview.confidence = 42;
    overview.span_count = 1;
    overview.spans = &span;
    CHECK(apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    const apta_result_t *result = NULL;
    CHECK(apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK);
    apta_result_builder_destroy(builder);
    return result;
}

static int push(apta_session_t *session, unsigned event, uint64_t first, uint32_t count) {
    int16_t pcm[4096];
    CHECK(count <= 4096);
    for (unsigned i = 0; i < count; ++i) pcm[i] = 1000;
    apta_pcm_block_t block;
    apta_pcm_block_init(&block);
    block.first_frame = first;
    block.frame_count = count;
    block.data = pcm;
    uint32_t accepted = 0;
    int status = apta_session_push_pcm(session, &block, &accepted);
    snapshot(session, event, status, accepted);
    return status;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    unsigned scenario = (unsigned)strtoul(argv[1], NULL, 10);
    CHECK(scenario <= 13);
    apta_context_config_t cc;
    apta_context_config_init(&cc);
    apta_context_t *context = NULL;
    CHECK(apta_context_create(&cc, &context) == APTA_STATUS_OK);
    apta_session_config_t config;
    apta_session_config_init(&config);
    config.source_sample_rate = scenario == 4 ? 44100 : 48000;
    config.channel_count = scenario == 5 ? 2 : 1;
    config.channel_layout = scenario == 5 ? APTA_CHANNEL_LAYOUT_STEREO : APTA_CHANNEL_LAYOUT_MONO;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.total_frames = scenario == 6 || scenario == 13 ? 8192 : scenario == 11 ? 2500 : 4096;
    config.overview_frames_per_column = scenario == 3 ? 2048 : 1024;
    config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    if (scenario != 8 && scenario != 9) {
        config.source_fingerprint_kind = APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
        config.source_fingerprint[0] = scenario == 7 ? 0x42 : 0x41;
    }
    // C bounded-slot configuration rejects every additional flag. Exercise
    // strict identity through its supported ordinary publication path.
    if (scenario == 9) config.flags = APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING;
    void *workspace = aligned_alloc(64, 262144);
    CHECK(workspace != NULL);
    config.static_workspace = workspace;
    config.static_workspace_size = 262144;
    apta_session_t *session = NULL;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    snapshot(session, 0, 0, 0);
    if (scenario == 12) push(session, 1, 1024, 1024);
    const apta_result_t *seed = checkpoint(context, scenario, 0);
    int status = apta_session_seed_from_result(session, seed);
    apta_result_release(seed);
    snapshot(session, 2, status, 0);
    if (status != APTA_STATUS_OK) goto cleanup;
    if (scenario == 1 || scenario == 2) {
        seed = checkpoint(context, scenario, 1);
        status = apta_session_seed_from_result(session, seed);
        apta_result_release(seed);
        snapshot(session, 3, status, 0);
        CHECK(status == APTA_STATUS_OK);
    }
    apta_work_budget_t budget;
    apta_work_budget_init(&budget);
    snapshot(session, 4, apta_session_process(session, &budget, NULL), 0);
    if (scenario != 13) {
        uint64_t first = scenario == 11 ? 0 : scenario == 2 ? 3072 : 1024;
        uint32_t count = scenario == 11 ? 2048 : (uint32_t)(4096 - first);
        CHECK(push(session, 5, first, count) == APTA_STATUS_OK);
    }
    status = apta_session_signal_end_of_input(session, config.total_frames);
    snapshot(session, 6, status, 0);
    CHECK(status == APTA_STATUS_OK);
    snapshot(session, 7, apta_session_process(session, &budget, NULL), 0);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    {
        const apta_result_t *result = apta_session_acquire_result(session);
        apta_result_info_t info;
        apta_result_info_init(&info);
        CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
        CHECK(info.lineage_id_high != 123 || info.lineage_id_low != 456);
        apta_result_release(result);
    }
cleanup:
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    free(workspace);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
