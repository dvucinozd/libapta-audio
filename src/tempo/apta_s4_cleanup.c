// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

void apta_internal_waveform_cleanup_session_base(apta_session_t *session);

void apta_internal_waveform_cleanup_session(apta_session_t *session)
{
    apta_internal_s4_cleanup_session(session);
    apta_internal_waveform_cleanup_session_base(session);
}
