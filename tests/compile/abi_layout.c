// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>

#include <apta/apta.h>

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(_Alignof(uint64_t) == 4u || _Alignof(uint64_t) == 8u,
               "unsupported pointer-32 uint64_t alignment");
_Static_assert(
    sizeof(apta_key_view_t) == (_Alignof(uint64_t) == 8u ? 72u : 68u),
    "pointer-32 apta_key_view_t size");
_Static_assert(_Alignof(apta_key_view_t) == _Alignof(uint64_t),
               "pointer-32 apta_key_view_t alignment");
_Static_assert(offsetof(apta_key_view_t, flags) == 52u,
               "pointer-32 apta_key_view_t flags offset");
_Static_assert(
    sizeof(apta_meter_segment_t) == (_Alignof(uint64_t) == 8u ? 80u : 76u),
    "pointer-32 apta_meter_segment_t size");
_Static_assert(_Alignof(apta_meter_segment_t) == _Alignof(uint64_t),
               "pointer-32 apta_meter_segment_t alignment");
_Static_assert(
    sizeof(apta_meter_view_t) == (_Alignof(uint64_t) == 8u ? 64u : 60u),
    "pointer-32 apta_meter_view_t size");
_Static_assert(_Alignof(apta_meter_view_t) == _Alignof(uint64_t),
               "pointer-32 apta_meter_view_t alignment");
_Static_assert(offsetof(apta_meter_view_t, flags) == 44u,
               "pointer-32 apta_meter_view_t flags offset");
_Static_assert(
    sizeof(apta_quality_view_t) == (_Alignof(uint64_t) == 8u ? 48u : 44u),
    "pointer-32 apta_quality_view_t size");
_Static_assert(_Alignof(apta_quality_view_t) == _Alignof(uint64_t),
               "pointer-32 apta_quality_view_t alignment");
_Static_assert(sizeof(apta_output_stream_t) == 56u,
               "pointer-32 apta_output_stream_t size");
_Static_assert(offsetof(apta_output_stream_t, reserved64) == 24u,
               "pointer-32 apta_output_stream_t reserved offset");
_Static_assert(
    sizeof(apta_input_stream_t) ==
        (_Alignof(uint64_t) == 8u ? 56u : 52u),
    "pointer-32 apta_input_stream_t size");
_Static_assert(
    offsetof(apta_input_stream_t, reserved64) ==
        (_Alignof(uint64_t) == 8u ? 24u : 20u),
    "pointer-32 apta_input_stream_t reserved offset");
_Static_assert(
    sizeof(apta_stream_parse_options_t) ==
        (_Alignof(uint64_t) == 8u ? 104u : 100u),
    "pointer-32 apta_stream_parse_options_t size");
_Static_assert(
    offsetof(apta_stream_parse_options_t, scratch_buffer_size) ==
        (_Alignof(uint64_t) == 8u ? 64u : 60u),
    "pointer-32 stream scratch-size offset");
#else
_Static_assert(sizeof(apta_output_stream_t) == 72u,
               "pointer-64 apta_output_stream_t size");
_Static_assert(sizeof(apta_input_stream_t) == 64u,
               "pointer-64 apta_input_stream_t size");
_Static_assert(sizeof(apta_stream_parse_options_t) == 104u,
               "pointer-64 apta_stream_parse_options_t size");
#endif

_Static_assert(sizeof(apta_status_t) == 4u, "apta_status_t must be 32-bit");
_Static_assert(sizeof(apta_source_frame_t) == 8u, "source frames must be 64-bit");
_Static_assert(sizeof(apta_generation_t) == 8u, "generation must be 64-bit");
_Static_assert(sizeof(apta_feature_mask_t) == 8u, "feature mask must be 64-bit");
_Static_assert(sizeof(apta_confidence_value_t) == 1u, "confidence must be one octet");

_Static_assert(sizeof(apta_fractional_frame_t) == 16u, "fractional frame layout changed");
_Static_assert(sizeof(apta_frame_period_t) == 16u, "frame period layout changed");
_Static_assert(sizeof(apta_waveform_column_t) == 10u, "waveform column layout changed");
_Static_assert(sizeof(apta_tempo_candidate_t) == 16u, "tempo candidate layout changed");
_Static_assert(sizeof(apta_key_candidate_t) == 16u, "key candidate layout changed");
_Static_assert(
    sizeof(apta_metadata_t) == sizeof(apta_metadata_view_t),
    "metadata input/view layouts diverged");

_Static_assert(offsetof(apta_frame_range_t, struct_size) == 0u, "range prefix changed");
_Static_assert(offsetof(apta_frame_range_t, api_version) == 4u, "range version offset changed");
_Static_assert(offsetof(apta_frame_range_t, first_frame) == 8u, "range first-frame offset changed");
_Static_assert(offsetof(apta_frame_range_t, end_frame) == 16u, "range end-frame offset changed");
_Static_assert(sizeof(apta_frame_range_t) == 24u, "range size changed");

#define APTA_ASSERT_EXTENSIBLE_PREFIX(type_name) \
    _Static_assert(offsetof(type_name, struct_size) == 0u, #type_name " struct_size offset changed"); \
    _Static_assert(offsetof(type_name, api_version) == 4u, #type_name " api_version offset changed")

APTA_ASSERT_EXTENSIBLE_PREFIX(apta_context_config_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_session_config_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_pcm_block_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_pcm_source_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_focus_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_region_request_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_work_budget_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_result_info_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_waveform_overview_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_waveform_tile_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_tempo_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_grid_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_key_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_meter_segment_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_meter_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_quality_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_result_builder_options_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_result_builder_info_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_result_provenance_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_waveform_detail_input_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_metadata_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_metadata_view_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_serialize_options_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_parse_options_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_output_stream_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_input_stream_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_stream_parse_options_t);

int apta_abi_layout_compile_probe(void)
{
    return 0;
}
