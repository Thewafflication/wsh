/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file frontend.h
 * @brief Input-session boundary shared by batch and interactive front ends.
 */

#ifndef WSH_FRONTEND_H
#define WSH_FRONTEND_H

#include <stddef.h>

#include "wsh/parser.h"

/** Result of requesting the next logical input line. */
typedef enum wsh_frontend_read_result {
    /** A borrowed line was returned. */
    WSH_FRONTEND_READ_LINE = 0,
    /** The input source reached an orderly end. */
    WSH_FRONTEND_READ_EOF = 1,
    /** The input source could not be read. */
    WSH_FRONTEND_READ_ERROR = 2,
    /** Input could not be represented as strict UTF-8. */
    WSH_FRONTEND_READ_ENCODING = 3,
    /** The input source exceeded its resource ceiling. */
    WSH_FRONTEND_READ_RESOURCE = 4,
    /** Interactive pending input was cancelled without submission. */
    WSH_FRONTEND_READ_CANCELLED = 5
} wsh_frontend_read_result;

/** Read one logical line, retaining returned bytes until the next read. */
typedef wsh_frontend_read_result (*wsh_frontend_read_fn)(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length);

/** Write an exact UTF-8 byte sequence to one front-end stream. */
typedef int (*wsh_frontend_write_fn)(
    void *user_data,
    const char *bytes,
    size_t length);

/** Evaluate one complete immutable parse tree. */
typedef int (*wsh_frontend_evaluate_fn)(
    void *user_data,
    const wsh_parse_tree *tree);

/** Publish one interactive pending-input cancellation. */
typedef int (*wsh_frontend_cancel_fn)(void *user_data);

/** Observe one completely parsed and evaluated source submission. */
typedef int (*wsh_frontend_submitted_fn)(
    void *user_data,
    const unsigned char *bytes,
    size_t length,
    int status);

/** Return nonzero when the session should stop after a submission. */
typedef int (*wsh_frontend_stop_fn)(void *user_data);

/** Injected input, output, and evaluation operations for one session. */
typedef struct wsh_frontend_io {
    /** Opaque state supplied to read_line. */
    void *input_data;
    /** Required logical-line reader. */
    wsh_frontend_read_fn read_line;
    /** Opaque state supplied to write_output. */
    void *output_data;
    /** Required normal-output writer. */
    wsh_frontend_write_fn write_output;
    /** Opaque state supplied to write_error. */
    void *error_data;
    /** Required diagnostic writer. */
    wsh_frontend_write_fn write_error;
    /** Opaque state supplied to evaluate. */
    void *evaluation_data;
    /** Optional evaluator; null accepts complete input without effects. */
    wsh_frontend_evaluate_fn evaluate;
    /** Optional interactive cancellation observer. */
    wsh_frontend_cancel_fn cancel;
    /** Optional successful-parse submission observer. */
    wsh_frontend_submitted_fn submitted;
    /** Optional post-submission session stop predicate. */
    wsh_frontend_stop_fn should_stop;
} wsh_frontend_io;

/** Session behavior selected after command-line mode detection. */
typedef struct wsh_frontend_options {
    /** Nonzero enables prompts and recoverable syntax errors. */
    int interactive;
    /** Literal UTF-8 primary prompt. */
    const char *primary_prompt;
    /** Literal UTF-8 continuation prompt. */
    const char *continuation_prompt;
    /** Maximum accumulated command bytes. */
    size_t max_command_bytes;
} wsh_frontend_options;

/** Initialize the specified 1.0 front-end defaults. */
void wsh_frontend_options_init(wsh_frontend_options *out_options);

/**
 * Run one input session until EOF or a fatal failure.
 * @return A stable WSH process exit code.
 */
int wsh_frontend_run(
    const wsh_frontend_options *options,
    const wsh_frontend_io *io);

#endif
