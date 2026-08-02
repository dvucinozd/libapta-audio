// SPDX-License-Identifier: Apache-2.0
#include <apta/apta.h>
#include <apta/desktop/apta_decoder.h>
#include <apta/desktop/apta_file.h>
#include <apta/desktop/apta_posix_file.h>

int main()
{
    apta_context_config_t context_config{};
    apta_session_config_t session_config{};
    apta_focus_t focus{};
    apta_work_budget_t budget{};
    apta_result_info_t result_info{};

    context_config.struct_size = static_cast<uint32_t>(sizeof(context_config));
    context_config.api_version = APTA_API_VERSION;
    session_config.struct_size = static_cast<uint32_t>(sizeof(session_config));
    session_config.api_version = APTA_API_VERSION;
    focus.struct_size = static_cast<uint32_t>(sizeof(focus));
    focus.api_version = APTA_API_VERSION;
    budget.struct_size = static_cast<uint32_t>(sizeof(budget));
    budget.api_version = APTA_API_VERSION;
    result_info.struct_size = static_cast<uint32_t>(sizeof(result_info));
    result_info.api_version = APTA_API_VERSION;

    return APTA_API_VERSION == 0u ? 1 : 0;
}
