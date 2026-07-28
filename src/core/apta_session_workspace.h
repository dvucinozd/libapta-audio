// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_SESSION_WORKSPACE_H
#define APTA_SESSION_WORKSPACE_H

#include "apta_internal.h"

size_t apta_internal_session_workspace_minimum_size(void);

int apta_internal_session_uses_workspace(
    const apta_session_t *session);

apta_status_t apta_internal_session_workspace_initialize(
    apta_session_t *session);

apta_status_t apta_internal_workspace_session_prepare(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

void apta_internal_workspace_session_commit(
    apta_session_t *session);

void apta_internal_workspace_session_abandon(
    apta_session_t *session);

void *apta_internal_session_allocate(
    apta_session_t *session,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags);

void apta_internal_session_deallocate(
    apta_session_t *session,
    void *memory);

#endif /* APTA_SESSION_WORKSPACE_H */
