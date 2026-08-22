/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file frontend.c
 * @brief Prompt, multiline parsing, diagnostics, and recovery session logic.
 */

#include "frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WSH_EXIT_SUCCESS 0
#define WSH_EXIT_GENERAL 1
#define WSH_EXIT_SYNTAX 3
#define WSH_EXIT_IO 5
#define WSH_EXIT_ENCODING 6
#define WSH_EXIT_RESOURCE 9

/** Mutable source bytes accumulated until the parser accepts one command. */
typedef struct wsh_pending_input {
    /** Owned bytes followed by a convenience NUL. */
    unsigned char *bytes;
    /** Number of source bytes excluding the NUL. */
    size_t length;
    /** Number of allocated bytes including space for the NUL. */
    size_t capacity;
} wsh_pending_input;

/** Write one NUL-terminated message through an injected stream. */
static int write_text(
    wsh_frontend_write_fn writer,
    void *user_data,
    const char *text)
{
    return writer(user_data, text, strlen(text));
}

/** Grow and append exact source bytes without exceeding the session limit. */
static int append_pending(
    wsh_pending_input *pending,
    const unsigned char *bytes,
    size_t length,
    size_t limit)
{
    size_t needed;
    size_t capacity;
    unsigned char *replacement;

    if (length > limit - pending->length) {
        return 0;
    }
    needed = pending->length + length + 1U;
    if (needed <= pending->capacity) {
        if (length > 0U) {
            memcpy(pending->bytes + pending->length, bytes, length);
        }
        pending->length += length;
        pending->bytes[pending->length] = 0;
        return 1;
    }

    capacity = pending->capacity == 0U ? 256U : pending->capacity;
    while (capacity < needed) {
        size_t grown;

        grown = capacity > limit / 2U ? limit + 1U : capacity * 2U;
        if (grown <= capacity) {
            return 0;
        }
        capacity = grown;
    }
    replacement = (unsigned char *)realloc(pending->bytes, capacity);
    if (replacement == NULL) {
        return 0;
    }
    pending->bytes = replacement;
    pending->capacity = capacity;
    if (length > 0U) {
        memcpy(pending->bytes + pending->length, bytes, length);
    }
    pending->length += length;
    pending->bytes[pending->length] = 0;
    return 1;
}

/** Emit the first retained syntax diagnostic with its source location. */
static int write_syntax_diagnostic(
    const wsh_frontend_io *io,
    const wsh_parse_tree *tree)
{
    wsh_syntax_diagnostic_view diagnostic;
    char prefix[96];
    int length;

    if (wsh_parse_tree_diagnostic_count(tree) == 0U ||
        wsh_parse_tree_diagnostic_at(tree, 0U, &diagnostic) != WSH_OK) {
        return write_text(
            io->write_error,
            io->error_data,
            "wsh: syntax error\r\n");
    }

    length = snprintf(
        prefix,
        sizeof(prefix),
        "wsh: %lu:%lu: ",
        (unsigned long)diagnostic.span.start.line,
        (unsigned long)diagnostic.span.start.scalar_column);
    if (length < 0 || (size_t)length >= sizeof(prefix) ||
        io->write_error(io->error_data, prefix, (size_t)length) != 0 ||
        io->write_error(
            io->error_data,
            diagnostic.message.data,
            diagnostic.message.length) != 0) {
        return 1;
    }
    return write_text(io->write_error, io->error_data, "\r\n");
}

/** Parse and optionally evaluate the currently accumulated source. */
static int process_pending(
    const wsh_frontend_options *options,
    const wsh_frontend_io *io,
    wsh_pending_input *pending,
    int at_eof,
    int *out_incomplete,
    int *out_status)
{
    wsh_source *source;
    wsh_parse_tree *tree;
    wsh_result result;
    wsh_syntax_status syntax_status;
    int status;

    *out_incomplete = 0;
    result = wsh_source_create(
        NULL,
        NULL,
        pending->bytes,
        pending->length,
        &source);
    if (result != WSH_OK) {
        return result == WSH_ERR_ENCODING ? WSH_EXIT_ENCODING :
            WSH_EXIT_RESOURCE;
    }
    result = wsh_parse(NULL, source, &tree);
    wsh_source_destroy(source);
    if (result != WSH_OK) {
        return result == WSH_ERR_RESOURCE ? WSH_EXIT_RESOURCE :
            WSH_EXIT_GENERAL;
    }

    syntax_status = wsh_parse_tree_status(tree);
    if (syntax_status == WSH_SYNTAX_INCOMPLETE && !at_eof) {
        *out_incomplete = 1;
        wsh_parse_tree_destroy(tree);
        return WSH_EXIT_SUCCESS;
    }
    if (syntax_status != WSH_SYNTAX_COMPLETE) {
        if (write_syntax_diagnostic(io, tree) != 0) {
            wsh_parse_tree_destroy(tree);
            return WSH_EXIT_IO;
        }
        wsh_parse_tree_destroy(tree);
        pending->length = 0U;
        if (pending->bytes != NULL) {
            pending->bytes[0] = 0;
        }
        *out_status = WSH_EXIT_SYNTAX;
        return options->interactive ? WSH_EXIT_SUCCESS : WSH_EXIT_SYNTAX;
    }

    status = io->evaluate == NULL ? WSH_EXIT_SUCCESS :
        io->evaluate(io->evaluation_data, tree);
    wsh_parse_tree_destroy(tree);
    pending->length = 0U;
    if (pending->bytes != NULL) {
        pending->bytes[0] = 0;
    }
    *out_status = status;
    return WSH_EXIT_SUCCESS;
}

/** Implements wsh_frontend_options_init. */
void wsh_frontend_options_init(wsh_frontend_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    out_options->interactive = 0;
    out_options->primary_prompt = "% ";
    out_options->continuation_prompt = "; ";
    out_options->max_command_bytes = 16U * 1024U * 1024U;
}

/** Implements wsh_frontend_run. */
int wsh_frontend_run(
    const wsh_frontend_options *options,
    const wsh_frontend_io *io)
{
    wsh_pending_input pending;
    const unsigned char *line;
    size_t length;
    wsh_frontend_read_result read_result;
    int incomplete;
    int status;
    int result;

    if (options == NULL || io == NULL || io->read_line == NULL ||
        io->write_output == NULL || io->write_error == NULL ||
        options->primary_prompt == NULL ||
        options->continuation_prompt == NULL ||
        options->max_command_bytes == 0U ||
        options->max_command_bytes == (size_t)-1) {
        return WSH_EXIT_GENERAL;
    }

    memset(&pending, 0, sizeof(pending));
    incomplete = 0;
    status = WSH_EXIT_SUCCESS;
    for (;;) {
        if (options->interactive &&
            write_text(
                io->write_output,
                io->output_data,
                incomplete ? options->continuation_prompt :
                    options->primary_prompt) != 0) {
            free(pending.bytes);
            return WSH_EXIT_IO;
        }

        line = NULL;
        length = 0U;
        read_result = io->read_line(io->input_data, &line, &length);
        if (read_result == WSH_FRONTEND_READ_ERROR) {
            free(pending.bytes);
            return WSH_EXIT_IO;
        }
        if (read_result == WSH_FRONTEND_READ_ENCODING) {
            free(pending.bytes);
            return WSH_EXIT_ENCODING;
        }
        if (read_result == WSH_FRONTEND_READ_RESOURCE) {
            free(pending.bytes);
            return WSH_EXIT_RESOURCE;
        }
        if (read_result == WSH_FRONTEND_READ_EOF) {
            if (pending.length == 0U) {
                free(pending.bytes);
                return status;
            }
            result = process_pending(
                options,
                io,
                &pending,
                1,
                &incomplete,
                &status);
            free(pending.bytes);
            return result == WSH_EXIT_SUCCESS ? status : result;
        }
        if (line == NULL && length != 0U) {
            free(pending.bytes);
            return WSH_EXIT_GENERAL;
        }
        if (!append_pending(
                &pending,
                line,
                length,
                options->max_command_bytes)) {
            free(pending.bytes);
            return WSH_EXIT_RESOURCE;
        }

        result = process_pending(
            options,
            io,
            &pending,
            0,
            &incomplete,
            &status);
        if (result != WSH_EXIT_SUCCESS) {
            free(pending.bytes);
            return result;
        }
    }
}
