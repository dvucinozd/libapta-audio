# SPDX-License-Identifier: Apache-2.0
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES espidf esp_timer heap log)
