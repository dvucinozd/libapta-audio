// SPDX-License-Identifier: Apache-2.0
#include <apta/apta.h>

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_focus_t focus;
    apta_work_budget_t budget;
    apta_result_info_t result_info;

    apta_context_config_init(&context_config);
    apta_session_config_init(&session_config);
    apta_focus_init(&focus);
    apta_work_budget_init(&budget);

    result_info.struct_size = (uint32_t)sizeof(result_info);
    result_info.api_version = APTA_API_VERSION;

    return (APTA_API_VERSION == 0u) ? 1 : 0;
}
