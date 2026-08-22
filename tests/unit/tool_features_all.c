// SPDX-License-Identifier: Apache-2.0
/*
 * `--features all` must mean every native feature the build can analyze.
 *
 * The authority is a context created with requested_capabilities == 0, because
 * that asks the library to expose its complete available capability set. The
 * test must not seed the context with apta_tool_all_features(): doing so makes
 * the comparison circular and can hide a newly implemented analyzer from the
 * shipped CLI.
 */
#include <stdio.h>

#include <apta/apta.h>

#include "../../tools/apta_tool_common.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_feature_mask_t parsed = 0u;
    apta_feature_mask_t supported;

    /* Zero requested capabilities means the full native build capability set. */
    apta_context_config_init(&config);
    config.requested_capabilities = 0u;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    supported = apta_context_get_capabilities(context);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    CHECK(apta_tool_parse_feature_list("all", &parsed) == APTA_STATUS_OK);

    if (parsed != supported) {
        const apta_feature_mask_t missing = supported & ~parsed;
        const apta_feature_mask_t extra = parsed & ~supported;

        fprintf(stderr,
                "--features all does not match what the build supports.\n"
                "  parsed    0x%08lx\n"
                "  supported 0x%08lx\n"
                "  missing from all  0x%08lx\n"
                "  requested but unsupported 0x%08lx\n",
                (unsigned long)parsed, (unsigned long)supported,
                (unsigned long)missing, (unsigned long)extra);
        return 1;
    }

    CHECK((parsed & APTA_FEATURE_MUSICAL_KEY) != 0u);
    CHECK((parsed & APTA_FEATURE_METER_DOWNBEAT) != 0u);

    /* Each named token must be accepted and expand its dependencies into a
     * session-valid feature set. */
    {
        static const char *const tokens[] = {
            "waveform", "detail", "3band", "bpm", "beatgrid",
            "global", "dynamic", "locking", "key", "meter", "all"
        };
        size_t i;

        for (i = 0u; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
            apta_feature_mask_t one = 0u;
            apta_session_config_t session_config;

            if (apta_tool_parse_feature_list(tokens[i], &one) !=
                APTA_STATUS_OK) {
                fprintf(stderr, "token \"%s\" rejected\n", tokens[i]);
                return 1;
            }
            CHECK(one != 0u);

            apta_session_config_init(&session_config);
            session_config.source_sample_rate = 44100u;
            session_config.channel_count = 2u;
            session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
            session_config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
            session_config.total_frames = 44100u * 4u;
            session_config.requested_features = one;

            apta_context_config_init(&config);
            config.requested_capabilities = supported;
            CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
            {
                apta_session_t *session = NULL;
                const apta_status_t status =
                    apta_session_create(context, &session_config, &session);

                if (status != APTA_STATUS_OK) {
                    fprintf(stderr,
                            "token \"%s\" produced a feature set a session "
                            "rejects (status %d)\n",
                            tokens[i], (int)status);
                    (void)apta_context_destroy(context);
                    return 1;
                }
                CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
            }
            CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
        }
    }

    /* Every native supported feature must have a printable name. */
    {
        apta_feature_mask_t bit;

        for (bit = 1u; bit != 0u; bit <<= 1) {
            char buffer[256];
            FILE *sink;

            if ((supported & bit) == 0u) {
                continue;
            }
            sink = tmpfile();
            CHECK(sink != NULL);
            apta_tool_print_feature_list(sink, bit);
            rewind(sink);
            if (fgets(buffer, sizeof(buffer), sink) == NULL ||
                buffer[0] == '\0' || buffer[0] == '\n') {
                fprintf(stderr,
                        "feature bit 0x%08lx has no printed name\n",
                        (unsigned long)bit);
                fclose(sink);
                return 1;
            }
            fclose(sink);
        }
    }

    return 0;
}
