// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>

#include <apta/apta.h>

_Static_assert(sizeof(apta_status_t) == 4u, "apta_status_t must be 32-bit");
_Static_assert(sizeof(apta_source_frame_t) == 8u, "source frames must be 64-bit");
_Static_assert(sizeof(apta_generation_t) == 8u, "generation must be 64-bit");
_Static_assert(sizeof(apta_feature_mask_t) == 8u, "feature mask must be 64-bit");
_Static_assert(sizeof(apta_confidence_value_t) == 1u, "confidence must be one octet");

_Static_assert(sizeof(apta_fractional_frame_t) == 16u, "fractional frame layout changed");
_Static_assert(sizeof(apta_frame_period_t) == 16u, "frame period layout changed");
_Static_assert(sizeof(apta_waveform_column_t) == 10u, "waveform column layout changed");
_Static_assert(sizeof(apta_tempo_candidate_t) == 16u, "tempo candidate layout changed");

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
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_serialize_options_t);
APTA_ASSERT_EXTENSIBLE_PREFIX(apta_parse_options_t);

int apta_abi_layout_compile_probe(void)
{
    return 0;
}
