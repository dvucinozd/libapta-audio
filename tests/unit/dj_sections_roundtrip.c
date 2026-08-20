// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4u) << 32u);
}

static int load_golden(uint8_t *bytes, size_t capacity, size_t *size_out)
{
    FILE *file = fopen(APTA_DJ_GOLDEN_HEX_PATH, "rb");
    int high = -1;
    size_t size = 0u;
    int character;
    if (file == NULL || bytes == NULL || size_out == NULL) return 0;
    while ((character = fgetc(file)) != EOF) {
        int value;
        if (character >= '0' && character <= '9') value = character - '0';
        else if (character >= 'a' && character <= 'f') {
            value = character - 'a' + 10;
        } else if (character >= 'A' && character <= 'F') {
            value = character - 'A' + 10;
        } else {
            continue;
        }
        if (high < 0) high = value;
        else {
            if (size == capacity) {
                fclose(file);
                return 0;
            }
            bytes[size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    if (high >= 0) return 0;
    *size_out = size;
    return 1;
}

static void set_range(
    apta_frame_range_t *range,
    apta_source_frame_t first,
    apta_source_frame_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static int build_result(
    apta_context_t *context,
    const apta_result_t **result_out)
{
    apta_result_builder_t *builder = NULL;
    apta_result_builder_info_t info;
    apta_result_provenance_t provenance;
    apta_source_info_t source;
    apta_waveform_column_t column = {
        -10, 20, 8u, 1u, 2u, 3u,
        APTA_WAVEFORM_COLUMN_VALID | APTA_WAVEFORM_COLUMN_HAS_3BAND};
    apta_waveform_span_t span;
    apta_waveform_overview_view_t overview;
    apta_key_candidate_t candidates[2];
    apta_key_view_t key;
    apta_meter_segment_t segments[2];
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    apta_status_t status;

    *result_out = NULL;
    status = apta_result_builder_create(context, NULL, &builder);
    if (status < 0) return 0;

    apta_result_builder_info_init(&info);
    info.generation = 9u;
    info.container_version = 1u;
    if (apta_result_builder_set_info(builder, &info) < 0) goto failure;

    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "golden";
    provenance.source_name.size = 6u;
    if (apta_result_builder_set_provenance(builder, &provenance) < 0) {
        goto failure;
    }

    apta_source_info_init(&source);
    source.total_frames = 96000u;
    source.sample_rate = 48000u;
    source.channel_count = 2u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    if (apta_result_builder_set_source_info(builder, &source) < 0) goto failure;

    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 0u, 96000u);
    span.column_count = 1u;
    span.columns = &column;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 96000u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    overview.span_count = 1u;
    overview.spans = &span;
    if (apta_result_builder_set_waveform_overview(builder, &overview) < 0) {
        goto failure;
    }

    memset(candidates, 0, sizeof(candidates));
    candidates[0].tonic = 9u;
    candidates[0].mode = APTA_KEY_MODE_MINOR;
    candidates[0].tuning_offset_cents = -7;
    candidates[0].score = 62000u;
    candidates[0].confidence = 88u;
    candidates[1].tonic = 0u;
    candidates[1].mode = APTA_KEY_MODE_MAJOR;
    candidates[1].tuning_offset_cents = 3;
    candidates[1].score = 12000u;
    candidates[1].confidence = 40u;
    apta_key_view_init(&key);
    set_range(&key.applicability_range, 120u, 95000u);
    key.tonic = 9u;
    key.mode = APTA_KEY_MODE_MINOR;
    key.tuning_offset_cents = -7;
    key.confidence = 88u;
    key.state = APTA_FEATURE_FINAL;
    key.candidate_count = 2u;
    key.candidates = candidates;
    if (apta_result_builder_set_key(builder, &key) < 0) goto failure;

    memset(segments, 0, sizeof(segments));
    segments[0].struct_size = (uint32_t)sizeof(segments[0]);
    segments[0].api_version = APTA_API_VERSION;
    set_range(&segments[0].applicability_range, 0u, 48000u);
    segments[0].downbeat_frame = 0u;
    segments[0].downbeat_ordinal = -4;
    segments[0].numerator = 4u;
    segments[0].denominator = 4u;
    segments[0].state = APTA_FEATURE_FINAL;
    segments[0].confidence = 86u;
    segments[0].segment_id = 4u;
    segments[1] = segments[0];
    set_range(&segments[1].applicability_range, 48000u, 96000u);
    segments[1].downbeat_frame = 48000u;
    segments[1].downbeat_ordinal = 12;
    segments[1].numerator = 3u;
    segments[1].segment_id = 7u;
    apta_meter_view_init(&meter);
    meter.downbeat_frame = segments[0].downbeat_frame;
    meter.downbeat_ordinal = segments[0].downbeat_ordinal;
    meter.numerator = segments[0].numerator;
    meter.denominator = segments[0].denominator;
    meter.state = APTA_FEATURE_FINAL;
    meter.confidence = 86u;
    meter.segment_count = 2u;
    meter.segments = segments;
    if (apta_result_builder_set_meter(builder, &meter) < 0) goto failure;

    apta_quality_view_init(&quality);
    quality.feature = APTA_FEATURE_MUSICAL_KEY;
    quality.calibration_model_id = UINT32_C(0x10203040);
    quality.evidence_coverage_permille = 930u;
    quality.confidence = 84u;
    quality.state = APTA_FEATURE_FINAL;
    quality.flags = APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT;
    if (apta_result_builder_set_quality(builder, &quality) < 0) goto failure;
    quality.feature = APTA_FEATURE_METER_DOWNBEAT;
    quality.calibration_model_id = UINT32_C(0x50607080);
    quality.evidence_coverage_permille = APTA_EVIDENCE_COVERAGE_UNKNOWN;
    quality.confidence = APTA_CONFIDENCE_UNKNOWN;
    quality.flags = APTA_QUALITY_FLAG_OUT_OF_DOMAIN;
    if (apta_result_builder_set_quality(builder, &quality) < 0) goto failure;

    status = apta_result_builder_finalize(builder, result_out);
    apta_result_builder_destroy(builder);
    return status == APTA_STATUS_OK;

failure:
    apta_result_builder_destroy(builder);
    return 0;
}

int main(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    uint64_t size = 0u;
    size_t written = 0u;
    uint8_t *bytes = NULL;
    uint8_t *again = NULL;
    uint8_t golden[664];
    size_t golden_size = 0u;
    const uint8_t *directory;
    uint64_t key_offset;
    uint64_t meter_offset;
    uint64_t quality_offset;

    apta_context_config_init(&config);
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(build_result(context, &result));
    CHECK(apta_result_query_serialized_size(result, NULL, &size) ==
          APTA_STATUS_OK);
    CHECK(size <= SIZE_MAX);
    bytes = (uint8_t *)malloc((size_t)size);
    again = (uint8_t *)malloc((size_t)size);
    CHECK(bytes != NULL && again != NULL);
    CHECK(apta_result_serialize(
              result, NULL, bytes, (size_t)size, &written) == APTA_STATUS_OK);
    CHECK(written == (size_t)size);
    CHECK(load_golden(golden, sizeof(golden), &golden_size));
    CHECK(golden_size == written);
    CHECK(memcmp(bytes, golden, written) == 0);

    CHECK(get_u32(bytes + 20u) == 4u);
    directory = bytes + get_u64(bytes + 24u);
    CHECK(memcmp(directory + 0u, "WOVR", 4u) == 0);
    CHECK(memcmp(directory + 40u, "MKEY", 4u) == 0);
    CHECK(memcmp(directory + 80u, "MTRD", 4u) == 0);
    CHECK(memcmp(directory + 120u, "CONF", 4u) == 0);

    key_offset = get_u64(directory + 40u + 8u);
    CHECK(get_u64(directory + 40u + 16u) == 72u);
    CHECK(get_u16(bytes + key_offset) == 1u);
    CHECK(bytes[key_offset + 2u] == APTA_FEATURE_FINAL);
    CHECK(bytes[key_offset + 3u] == 88u);
    CHECK(bytes[key_offset + 4u] == 9u);
    CHECK(bytes[key_offset + 5u] == APTA_KEY_MODE_MINOR);
    CHECK(get_u16(bytes + key_offset + 6u) == UINT16_C(0xfff9));
    CHECK(get_u32(bytes + key_offset + 12u) == 2u);
    CHECK(get_u64(bytes + key_offset + 16u) == 120u);
    CHECK(get_u64(bytes + key_offset + 24u) == 95000u);
    CHECK(get_u32(bytes + key_offset + 32u) == 40u);
    CHECK(bytes[key_offset + 40u] == 9u);
    CHECK(bytes[key_offset + 56u] == 0u);

    meter_offset = get_u64(directory + 80u + 8u);
    CHECK(get_u64(directory + 80u + 16u) == 160u);
    CHECK(get_u16(bytes + meter_offset) == 1u);
    CHECK(bytes[meter_offset + 2u] == APTA_FEATURE_FINAL);
    CHECK(get_u16(bytes + meter_offset + 4u) == 4u);
    CHECK(get_u32(bytes + meter_offset + 12u) == 2u);
    CHECK(get_u64(bytes + meter_offset + 16u) == 0u);
    CHECK(get_u64(bytes + meter_offset + 24u) == UINT64_MAX - 3u);
    CHECK(get_u32(bytes + meter_offset + 32u) == 48u);
    CHECK(get_u64(bytes + meter_offset + 48u) == 0u);
    CHECK(get_u64(bytes + meter_offset + 104u) == 48000u);

    quality_offset = get_u64(directory + 120u + 8u);
    CHECK(get_u64(directory + 120u + 16u) == 80u);
    CHECK(get_u16(bytes + quality_offset) == 1u);
    CHECK(get_u16(bytes + quality_offset + 2u) == 32u);
    CHECK(get_u32(bytes + quality_offset + 4u) == 2u);
    CHECK(get_u32(bytes + quality_offset + 8u) == 16u);
    CHECK(get_u64(bytes + quality_offset + 16u) ==
          APTA_FEATURE_MUSICAL_KEY);
    CHECK(get_u32(bytes + quality_offset + 24u) == UINT32_C(0x10203040));
    CHECK(get_u64(bytes + quality_offset + 48u) ==
          APTA_FEATURE_METER_DOWNBEAT);

    CHECK(apta_result_parse(context, NULL, bytes, written, &parsed) ==
          APTA_STATUS_OK);
    apta_key_view_init(&key);
    CHECK(apta_result_get_key(parsed, NULL, &key) == APTA_STATUS_OK);
    CHECK(key.applicability_range.first_frame == 120u);
    CHECK(key.applicability_range.end_frame == 95000u);
    CHECK(key.tonic == 9u && key.mode == APTA_KEY_MODE_MINOR);
    CHECK(key.tuning_offset_cents == -7 && key.confidence == 88u);
    CHECK(key.state == APTA_FEATURE_FINAL && key.flags == 0u);
    CHECK(key.candidate_count == 2u && key.candidates != NULL);
    CHECK(key.candidates[0].tonic == 9u);
    CHECK(key.candidates[0].mode == APTA_KEY_MODE_MINOR);
    CHECK(key.candidates[0].tuning_offset_cents == -7);
    CHECK(key.candidates[0].score == 62000u);
    CHECK(key.candidates[0].confidence == 88u);
    CHECK(key.candidates[0].flags == 0u);
    CHECK(key.candidates[1].tonic == 0u);
    CHECK(key.candidates[1].mode == APTA_KEY_MODE_MAJOR);
    CHECK(key.candidates[1].tuning_offset_cents == 3);
    CHECK(key.candidates[1].score == 12000u);
    CHECK(key.candidates[1].confidence == 40u);
    CHECK(key.candidates[1].flags == 0u);
    apta_meter_view_init(&meter);
    CHECK(apta_result_get_meter(parsed, NULL, &meter) == APTA_STATUS_OK);
    CHECK(meter.downbeat_frame == 0u && meter.downbeat_ordinal == -4);
    CHECK(meter.numerator == 4u && meter.denominator == 4u);
    CHECK(meter.state == APTA_FEATURE_FINAL && meter.confidence == 86u);
    CHECK(meter.flags == 0u && meter.segment_count == 2u);
    CHECK(meter.segments[0].applicability_range.first_frame == 0u);
    CHECK(meter.segments[0].applicability_range.end_frame == 48000u);
    CHECK(meter.segments[0].downbeat_frame == 0u);
    CHECK(meter.segments[0].downbeat_ordinal == -4);
    CHECK(meter.segments[0].numerator == 4u);
    CHECK(meter.segments[0].denominator == 4u);
    CHECK(meter.segments[0].state == APTA_FEATURE_FINAL);
    CHECK(meter.segments[0].confidence == 86u);
    CHECK(meter.segments[0].flags == 0u);
    CHECK(meter.segments[0].segment_id == 4u);
    CHECK(meter.segments[1].applicability_range.first_frame == 48000u);
    CHECK(meter.segments[1].applicability_range.end_frame == 96000u);
    CHECK(meter.segments[1].downbeat_frame == 48000u);
    CHECK(meter.segments[1].downbeat_ordinal == 12);
    CHECK(meter.segments[1].numerator == 3u);
    CHECK(meter.segments[1].denominator == 4u);
    CHECK(meter.segments[1].state == APTA_FEATURE_FINAL);
    CHECK(meter.segments[1].confidence == 86u);
    CHECK(meter.segments[1].flags == 0u);
    CHECK(meter.segments[1].segment_id == 7u);
    apta_quality_view_init(&quality);
    CHECK(apta_result_get_quality(
              parsed, APTA_FEATURE_MUSICAL_KEY, &quality) ==
          APTA_STATUS_OK);
    CHECK(quality.feature == APTA_FEATURE_MUSICAL_KEY);
    CHECK(quality.calibration_model_id == UINT32_C(0x10203040));
    CHECK(quality.evidence_coverage_permille == 930u);
    CHECK(quality.confidence == 84u);
    CHECK(quality.state == APTA_FEATURE_FINAL);
    CHECK(quality.flags == APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT);
    apta_quality_view_init(&quality);
    CHECK(apta_result_get_quality(
              parsed, APTA_FEATURE_METER_DOWNBEAT, &quality) ==
          APTA_STATUS_OK);
    CHECK(quality.feature == APTA_FEATURE_METER_DOWNBEAT);
    CHECK(quality.calibration_model_id == UINT32_C(0x50607080));
    CHECK(quality.evidence_coverage_permille ==
          APTA_EVIDENCE_COVERAGE_UNKNOWN);
    CHECK(quality.confidence == APTA_CONFIDENCE_UNKNOWN);
    CHECK(quality.state == APTA_FEATURE_FINAL);
    CHECK(quality.flags == APTA_QUALITY_FLAG_OUT_OF_DOMAIN);
    CHECK(apta_result_serialize(
              parsed, NULL, again, (size_t)size, &written) == APTA_STATUS_OK);
    CHECK(memcmp(bytes, again, (size_t)size) == 0);

    apta_result_release(parsed);
    apta_result_release(result);
    free(again);
    free(bytes);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
