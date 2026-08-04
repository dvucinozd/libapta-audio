# SPDX-License-Identifier: Apache-2.0
idf_component_register(
    SRCS
        "app_main.c"
        "interchange_probe.c"
    INCLUDE_DIRS "."
    REQUIRES espidf esp_timer heap log
    WHOLE_ARCHIVE)
