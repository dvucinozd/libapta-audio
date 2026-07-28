// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_work_budget_t budget;
    apta_result_info_t info;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 44100u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;

    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_is_cancel_requested(session) == 0u);

    apta_session_request_cancel(session);
    CHECK(apta_session_is_cancel_requested(session) == 1u);

    apta_work_budget_init(&budget);
    budget.maximum_steps = 1u;

    CHECK(apta_session_process(session, &budget, NULL) == APTA_ERROR_CANCELLED);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CANCELLED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.api_version = APTA_API_VERSION;

    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.generation == 2u);
    CHECK(info.session_state == APTA_SESSION_CANCELLED);

    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;

    return 0;
}
