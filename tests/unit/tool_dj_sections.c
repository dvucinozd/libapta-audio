// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "../../tools/apta_tool_common.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    FILE *sink;
    char text[128];
    size_t count;
    CHECK(apta_tool_section_is_known("MKEY"));
    CHECK(apta_tool_section_is_known("MTRD"));
    CHECK(apta_tool_section_is_known("CONF"));
    sink = tmpfile();
    CHECK(sink != NULL);
    apta_tool_print_feature_list(
        sink, APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT |
                  APTA_FEATURE_CALIBRATED_QUALITY);
    rewind(sink);
    count = fread(text, 1u, sizeof(text) - 1u, sink);
    fclose(sink);
    text[count] = '\0';
    CHECK(strstr(text, "musical-key") != NULL);
    CHECK(strstr(text, "meter-downbeat") != NULL);
    CHECK(strstr(text, "calibrated-quality") != NULL);
    return 0;
}
