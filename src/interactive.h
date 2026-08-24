/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file interactive.h
 * @brief Executable-owned native console, completion, and history session.
 */

#ifndef WSH_INTERACTIVE_H
#define WSH_INTERACTIVE_H

#include <windows.h>

#include "frontend.h"
#include "wsh/core.h"
#include "wsh/evaluator.h"
#include "wsh/windows_runtime.h"

/** Opaque owner of one interactive console and history session. */
typedef struct wsh_interactive_session wsh_interactive_session;

/** Immutable dependencies for one interactive session. */
typedef struct wsh_interactive_options {
    /** Borrowed console input handle. */
    HANDLE input;
    /** Borrowed normal-output handle. */
    HANDLE output;
    /** Borrowed diagnostic-output handle. */
    HANDLE error;
    /** Borrowed isolated shell context. */
    wsh_context *context;
    /** Borrowed persistent evaluator. */
    wsh_evaluator *evaluator;
    /** Borrowed concrete child/path runtime. */
    wsh_windows_runtime *runtime;
    /** Maximum submitted UTF-8 bytes. */
    size_t max_command_bytes;
    /** Maximum retained history entries. */
    size_t max_history_entries;
    /** Maximum history file bytes. */
    size_t max_history_bytes;
    /** Nonzero enables history load and persistence. */
    int history_enabled;
    /** Nonzero forces the reported basic input fallback. */
    int force_basic_input;
} wsh_interactive_options;

/** Initialize accepted finite interactive defaults. */
void wsh_interactive_options_init(
    wsh_interactive_options *out_options);

/** Create an idle session without loading persistent history. */
wsh_result wsh_interactive_create(
    const wsh_interactive_options *options,
    wsh_interactive_session **out_session);

/** Load bounded history after profile evaluation. */
wsh_result wsh_interactive_load_history(
    wsh_interactive_session *session);

/** Persist history, restore console state, and destroy the session. */
void wsh_interactive_destroy(wsh_interactive_session *session);

/** Read one complete edited command, cancellation, or EOF. */
wsh_frontend_read_result wsh_interactive_read(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length);

/** Record one evaluated complete submission unless suppressed. */
int wsh_interactive_submitted(
    wsh_interactive_session *session,
    const unsigned char *bytes,
    size_t length,
    int status);

/** Publish status 130 and run `sigint` after pending-input cancellation. */
int wsh_interactive_cancelled(
    wsh_interactive_session *session);

/** Execute one accepted `history::` request. */
wsh_result wsh_interactive_history_invoke(
    wsh_interactive_session *session,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status);

/** Resolve an accepted evaluator exit against live background jobs. */
int wsh_interactive_resolve_exit(
    wsh_interactive_session *session);

/** Return nonzero after interactive exit has been accepted. */
int wsh_interactive_should_stop(
    const wsh_interactive_session *session);

/** Invoke `sigexit` once with the supplied orderly status. */
void wsh_interactive_signal_exit(
    wsh_interactive_session *session,
    uint32_t status);

#endif
