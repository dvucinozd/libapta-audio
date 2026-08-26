# SPDX-License-Identifier: Apache-2.0

set(APTA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(APTA_CORE_SRCS
    "${APTA_ROOT}/src/beatgrid/apta_s6.c"
    "${APTA_ROOT}/src/beatgrid/apta_s6_accessors.c"
    "${APTA_ROOT}/src/beatgrid/apta_s6_cleanup.c"
    "${APTA_ROOT}/src/beatgrid/apta_s6_pool_snapshot.c"
    "${APTA_ROOT}/src/beatgrid/apta_s6_process.c"
    "${APTA_ROOT}/src/beatgrid/apta_s6_session_guard.c"
    "${APTA_ROOT}/src/beatgrid/apta_meter.c"
    "${APTA_ROOT}/src/beatgrid/apta_meter_process.c"
    "${APTA_ROOT}/src/beatgrid/apta_meter_snapshot.c"
    "${APTA_ROOT}/src/beatgrid/apta_meter_pool_snapshot.c"
    "${APTA_ROOT}/src/key/apta_key.c"
    "${APTA_ROOT}/src/key/apta_key_process.c"
    "${APTA_ROOT}/src/key/apta_key_snapshot.c"
    "${APTA_ROOT}/src/key/apta_key_pool_snapshot.c"
    "${APTA_ROOT}/src/confidence/apta_quality_model.c"
    "${APTA_ROOT}/src/core/apta_config.c"
    "${APTA_ROOT}/src/core/apta_context.c"
    "${APTA_ROOT}/src/core/apta_initializers.c"
    "${APTA_ROOT}/src/core/apta_memory.c"
    "${APTA_ROOT}/src/core/apta_metadata.c"
    "${APTA_ROOT}/src/core/apta_result_allocation.c"
    "${APTA_ROOT}/src/core/apta_result_accessors.c"
    "${APTA_ROOT}/src/core/apta_result_detail_accessors.c"
    "${APTA_ROOT}/src/core/apta_result_lifetime.c"
    "${APTA_ROOT}/src/core/apta_result_pool.c"
    "${APTA_ROOT}/src/core/apta_result_pool_layout.c"
    "${APTA_ROOT}/src/core/apta_result_pool_publication.c"
    "${APTA_ROOT}/src/core/apta_result_pool_snapshot.c"
    "${APTA_ROOT}/src/core/apta_scheduler_policy.c"
    "${APTA_ROOT}/src/core/apta_session_detail_contract.c"
    "${APTA_ROOT}/src/core/apta_session_lifecycle.c"
    "${APTA_ROOT}/src/core/apta_session_metadata_bounded_guard.c"
    "${APTA_ROOT}/src/core/apta_session_metadata_workspace.c"
    "${APTA_ROOT}/src/core/apta_session_pull.c"
    "${APTA_ROOT}/src/core/apta_session_pull_activation.c"
    "${APTA_ROOT}/src/core/apta_session_result_pool_activation.c"
    "${APTA_ROOT}/src/core/apta_session_scheduler.c"
    "${APTA_ROOT}/src/core/apta_session_source.c"
    "${APTA_ROOT}/src/core/apta_session_workspace.c"
    "${APTA_ROOT}/src/core/apta_session_workspace_lifecycle.c"
    "${APTA_ROOT}/src/serialization/apta_crc32c.c"
    "${APTA_ROOT}/src/serialization/apta_meta_reader.c"
    "${APTA_ROOT}/src/serialization/apta_meta_reader_hardening.c"
    "${APTA_ROOT}/src/serialization/apta_meta_writer.c"
    "${APTA_ROOT}/src/serialization/apta_s4_reader.c"
    "${APTA_ROOT}/src/serialization/apta_s4_writer.c"
    "${APTA_ROOT}/src/serialization/apta_s6_reader.c"
    "${APTA_ROOT}/src/serialization/apta_s6_writer.c"
    "${APTA_ROOT}/src/serialization/apta_wdtl_reader.c"
    "${APTA_ROOT}/src/serialization/apta_wdtl_writer.c"
    "${APTA_ROOT}/src/serialization/apta_wovr_reader.c"
    "${APTA_ROOT}/src/serialization/apta_wovr_reader_hardening.c"
    "${APTA_ROOT}/src/serialization/apta_wovr_writer.c"
    "${APTA_ROOT}/src/tempo/apta_s4.c"
    "${APTA_ROOT}/src/tempo/apta_s4_accessors.c"
    "${APTA_ROOT}/src/tempo/apta_s4_cleanup.c"
    "${APTA_ROOT}/src/tempo/apta_s4_pool_snapshot.c"
    "${APTA_ROOT}/src/tempo/apta_s4_process.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_detail_mvp.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_detail_replay.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_detail_replay_hardening.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_detail_requests.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_input.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_input_detail.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_process.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_process_detail.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_scheduler.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_snapshot.c"
    "${APTA_ROOT}/src/waveform/apta_waveform_snapshot_combined.c"
    "${CMAKE_CURRENT_LIST_DIR}/apta_espidf.c")

idf_component_register(
    SRCS ${APTA_CORE_SRCS}
    INCLUDE_DIRS "${APTA_ROOT}/include" "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIV_INCLUDE_DIRS "${APTA_ROOT}/src/core"
    REQUIRES esp_timer heap log
    PRIV_REQUIRES esp-dsp)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_wovr_reader.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_wovr_reader_hardening.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_wovr_hardened")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_wdtl_reader.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_waveform")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_meta_reader.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_meta_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_meta_reader_hardening.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_meta_hardened")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_s4_reader.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_s4")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_wovr_writer.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_wovr;apta_result_serialize=apta_result_serialize_wovr")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_wdtl_writer.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_waveform;apta_result_serialize=apta_result_serialize_waveform")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_meta_writer.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_meta;apta_result_serialize=apta_result_serialize_meta")
    set_source_files_properties(
        "${APTA_ROOT}/src/serialization/apta_s4_writer.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_s4;apta_result_serialize=apta_result_serialize_s4")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_metadata.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_set_metadata=apta_session_set_metadata_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_metadata_workspace.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_set_metadata=apta_session_set_metadata_workspace_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_result_lifetime.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_publish_result=apta_internal_publish_result_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_lifecycle.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_create=apta_session_create_base;apta_session_destroy=apta_session_destroy_heap_base;apta_session_process=apta_session_process_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_detail_contract.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_create=apta_session_create_contract_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_workspace_lifecycle.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_create=apta_session_create_workspace_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_result_pool_activation.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_create=apta_session_create_activation_base;apta_session_process=apta_session_process_activation_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_pull_activation.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_create=apta_session_create_s6_guard_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_source.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_set_source=apta_session_set_source_base;apta_session_push_pcm=apta_session_push_pcm_base;apta_session_signal_end_of_input=apta_session_signal_end_of_input_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_session_scheduler.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_session_set_focus=apta_session_set_focus_base;apta_session_request_region=apta_session_request_region_base;apta_session_next_pcm_request=apta_session_next_pcm_request_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_result_accessors.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_base;apta_result_get_waveform_tile=apta_result_get_waveform_tile_base;apta_result_get_tempo=apta_result_get_tempo_base;apta_result_get_beatgrid=apta_result_get_beatgrid_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_result_detail_accessors.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_s4_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/tempo/apta_s4_accessors.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_s6_base;apta_result_get_beatgrid=apta_result_get_beatgrid_s6_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/core/apta_result_pool_snapshot.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/tempo/apta_s4_pool_snapshot.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_s6_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/waveform/apta_waveform_input.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_accept_pcm=apta_internal_waveform_accept_pcm_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/waveform/apta_waveform_process.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_base;apta_internal_waveform_next_pcm_request=apta_internal_waveform_next_pcm_request_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/waveform/apta_waveform_process_detail.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_s4_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/tempo/apta_s4_process.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_s6_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/beatgrid/apta_s6_process.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_meter_base;apta_internal_analysis_pending=apta_internal_analysis_pending_meter_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/waveform/apta_waveform_snapshot_combined.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_build_snapshot=apta_internal_waveform_build_snapshot_meter_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/beatgrid/apta_s6_pool_snapshot.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_meter_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/beatgrid/apta_meter_process.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_key_base;apta_internal_analysis_pending=apta_internal_analysis_pending_key_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/beatgrid/apta_meter_snapshot.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_build_snapshot=apta_internal_waveform_build_snapshot_key_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/beatgrid/apta_meter_pool_snapshot.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_key_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/waveform/apta_waveform_snapshot.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_build_snapshot=apta_internal_waveform_build_overview_snapshot;apta_internal_waveform_cleanup_session=apta_internal_waveform_cleanup_session_base;apta_internal_waveform_cleanup_result=apta_internal_waveform_cleanup_result_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/tempo/apta_s4_cleanup.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_waveform_cleanup_session=apta_internal_waveform_cleanup_session_s6_base")
    set_source_files_properties(
        "${APTA_ROOT}/src/waveform/apta_waveform_detail_replay.c"
        PROPERTIES COMPILE_DEFINITIONS "apta_internal_detail_next_pcm_request=apta_internal_detail_next_pcm_request_base;apta_internal_detail_accept_replay=apta_internal_detail_accept_replay_base")

    set_target_properties(${COMPONENT_LIB} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APTA_BUILDING_LIBRARY=1)

    if(CONFIG_APTA_EXPERIMENTAL_MULTIBAND_ONSET)
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE APTA_INTERNAL_MULTIBAND_ONSET=1)
    endif()

    # C3: wire the Kconfig capacity options to the library's #ifndef-guarded
    # constants. Each is only defined when the host set it, so an unconfigured
    # build keeps the library defaults.
    if(CONFIG_APTA_OVERVIEW_FRAMES_PER_COLUMN)
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE
                APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN=${CONFIG_APTA_OVERVIEW_FRAMES_PER_COLUMN}u)
    endif()
    if(CONFIG_APTA_MAX_DETAIL_TILES)
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE
                APTA_INTERNAL_MAX_DETAIL_TILES=${CONFIG_APTA_MAX_DETAIL_TILES}u)
    endif()
    if(CONFIG_APTA_ONSET_BIN_CAPACITY)
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE
                APTA_INTERNAL_ONSET_BIN_CAPACITY=${CONFIG_APTA_ONSET_BIN_CAPACITY}u)
    endif()
    if(CONFIG_APTA_GLOBAL_BIN_CAPACITY)
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE
                APTA_INTERNAL_GLOBAL_BIN_CAPACITY=${CONFIG_APTA_GLOBAL_BIN_CAPACITY}u)
    endif()
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-error=type-limits)
    target_link_libraries(${COMPONENT_LIB} PRIVATE m)
endif()
