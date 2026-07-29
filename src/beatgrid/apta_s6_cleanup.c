// SPDX-License-Identifier: Apache-2.0
#include "apta_s6_internal.h"

void apta_internal_waveform_cleanup_session_s6_base(apta_session_t *session);

void apta_internal_waveform_cleanup_session(apta_session_t *session)
{
    apta_internal_s6_cleanup_session(session);
    apta_internal_waveform_cleanup_session_s6_base(session);
}
