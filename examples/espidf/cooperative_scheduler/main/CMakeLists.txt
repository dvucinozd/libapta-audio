# SPDX-License-Identifier: Apache-2.0
set(APTA_COOPERATIVE_SRCS
    "app_main.c"
    "interchange_probe.c")
set(APTA_COOPERATIVE_REQUIRES
    espidf
    esp_timer
    heap
    log)

if(CONFIG_APTA_P4_HARDWARE_EVIDENCE)
    list(APPEND APTA_COOPERATIVE_SRCS "hardware_evidence.c")
    list(APPEND APTA_COOPERATIVE_REQUIRES espressif__usb_device_uac)
endif()

idf_component_register(
    SRCS ${APTA_COOPERATIVE_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${APTA_COOPERATIVE_REQUIRES}
    WHOLE_ARCHIVE)
