// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_RESULT_BUILDER_H
#define APTA_RESULT_BUILDER_H

#include <stdint.h>

#include <apta/apta_metadata.h>
#include <apta/apta_result.h>
#include <apta/apta_s6.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct apta_result_builder apta_result_builder_t;
typedef uint32_t apta_result_provenance_origin_t;

#define APTA_RESULT_PROVENANCE_UNSPECIFIED     0u
#define APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT 1u
#define APTA_RESULT_PROVENANCE_NATIVE_ANALYSIS 2u

#define APTA_RESULT_PROVENANCE_MAX_SOURCE_NAME_BYTES 255u
#define APTA_RESULT_PROVENANCE_MAX_VERSION_BYTES     127u

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t flags;
    uint32_t maximum_overview_spans;
    uint32_t maximum_waveform_columns;
    uint32_t maximum_detail_tiles;
    uint32_t maximum_tempo_candidates;
    uint32_t maximum_grid_coverage_ranges;
    uint32_t maximum_grid_segments;
    uint32_t maximum_grid_beats;
    uint32_t maximum_key_candidates;
    uint32_t maximum_meter_segments;
    uint32_t maximum_quality_records;
    uint32_t reserved32[4];

    uint64_t maximum_allocation_bytes;
    uint64_t reserved64[3];
} apta_result_builder_options_t;

/*
 * maximum_allocation_bytes bounds the complete graph owned by each finalized
 * result: the result object, all copied payloads, and internal representation
 * wrappers. Builder-retained setter copies are not part of that result graph.
 */

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_generation_t generation;
    uint32_t container_version;
    apta_session_state_t session_state;
    uint32_t flags;
    uint32_t reserved32[3];
    uint64_t lineage_id_high;
    uint64_t lineage_id_low;
    uint64_t reserved64[2];
} apta_result_builder_info_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_result_provenance_origin_t origin;
    uint32_t flags;
    apta_utf8_view_t source_name;
    apta_utf8_view_t source_version;
    uint32_t reserved32[4];
    uint64_t reserved64[2];
} apta_result_provenance_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t tile_count;
    const apta_waveform_tile_view_t *tiles;
    uint32_t flags;
    uint32_t reserved32[3];
} apta_waveform_detail_input_t;

APTA_API void APTA_CALL
apta_result_builder_options_init(apta_result_builder_options_t *options);

APTA_API void APTA_CALL
apta_result_builder_info_init(apta_result_builder_info_t *info);

APTA_API void APTA_CALL
apta_result_provenance_init(apta_result_provenance_t *provenance);

APTA_API void APTA_CALL
apta_waveform_detail_input_init(apta_waveform_detail_input_t *input);

/*
 * Setters copy all pointer-backed input before returning. Finalize is
 * non-consuming: it creates a second deep copy in a normal immutable result,
 * and the builder may then be changed, reset, finalized again, or destroyed.
 * Tempo and key candidate arrays may be empty (count zero and pointer NULL);
 * in that selected-only form the selected value remains authoritative.
 */
APTA_API apta_status_t APTA_CALL
apta_result_builder_create(
    apta_context_t *context,
    const apta_result_builder_options_t *options,
    apta_result_builder_t **builder_out);

APTA_API void APTA_CALL
apta_result_builder_destroy(apta_result_builder_t *builder);

APTA_API void APTA_CALL
apta_result_builder_reset(apta_result_builder_t *builder);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_info(
    apta_result_builder_t *builder,
    const apta_result_builder_info_t *info);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_source_info(
    apta_result_builder_t *builder,
    const apta_source_info_t *source_info);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_metadata(
    apta_result_builder_t *builder,
    const apta_metadata_t *metadata);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_provenance(
    apta_result_builder_t *builder,
    const apta_result_provenance_t *provenance);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_waveform_overview(
    apta_result_builder_t *builder,
    const apta_waveform_overview_view_t *overview);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_waveform_detail(
    apta_result_builder_t *builder,
    const apta_waveform_detail_input_t *detail);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_tempo(
    apta_result_builder_t *builder,
    const apta_tempo_view_t *tempo);

/* EXPLICIT grids with two or more beats are checked against the selected BPM
 * using every adjacent beat interval. A single beat is accepted as phase-only
 * evidence: it preserves the imported position but cannot verify a period. */
APTA_API apta_status_t APTA_CALL
apta_result_builder_set_beatgrid(
    apta_result_builder_t *builder,
    apta_feature_mask_t grid_feature,
    const apta_grid_view_t *grid);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_grid_revision(
    apta_result_builder_t *builder,
    const apta_grid_revision_view_t *revision);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_key(
    apta_result_builder_t *builder,
    const apta_key_view_t *key);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_meter(
    apta_result_builder_t *builder,
    const apta_meter_view_t *meter);

APTA_API apta_status_t APTA_CALL
apta_result_builder_set_quality(
    apta_result_builder_t *builder,
    const apta_quality_view_t *quality);

APTA_API apta_status_t APTA_CALL
apta_result_builder_finalize(
    const apta_result_builder_t *builder,
    const apta_result_t **result_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_provenance(
    const apta_result_t *result,
    apta_result_provenance_t *provenance_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_RESULT_BUILDER_H */
