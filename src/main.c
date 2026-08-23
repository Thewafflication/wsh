/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file main.c
 * @brief WSH command-line and native-console front end.
 */

#include "frontend.h"
#include "wsh/core.h"
#include "wsh/evaluator.h"
#include "wsh/windows_runtime.h"
#include "wsh/wsh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define WSH_EXIT_USAGE 2
#define WSH_EXIT_IO 5
#define WSH_INPUT_LIMIT (16U * 1024U * 1024U)

/** Dynamically retained bytes for a redirected logical input line. */
typedef struct wsh_file_reader {
    /** Borrowed redirected standard-input handle. */
    HANDLE handle;
    /** Owned line storage. */
    unsigned char *bytes;
    /** Allocated storage size. */
    size_t capacity;
    /** Buffered bytes from the last ReadFile call. */
    unsigned char chunk[4096];
    /** Number of valid bytes in chunk. */
    size_t chunk_length;
    /** Next unread byte in chunk. */
    size_t chunk_offset;
    /** Nonzero after the underlying stream reaches EOF. */
    int reached_eof;
} wsh_file_reader;

/** Native wide-console input state. */
typedef struct wsh_console_reader {
    /** Borrowed standard-input console handle. */
    HANDLE handle;
    /** Owned UTF-16 line storage. */
    uint16_t *units;
    /** Number of allocated UTF-16 units. */
    size_t capacity;
    /** Owned strict UTF-8 line returned to the session. */
    char *bytes;
    /** Default allocator used for bytes. */
    wsh_allocator allocator;
    /** Nonzero after console EOF is observed. */
    int reached_eof;
} wsh_console_reader;

/** UTF-8 writer backed by either a wide console or a byte stream. */
typedef struct wsh_stream_writer {
    /** Borrowed C stream used when the handle is redirected. */
    FILE *stream;
    /** Borrowed matching Windows standard handle. */
    HANDLE handle;
    /** Nonzero when handle accepts WriteConsoleW. */
    int is_console;
} wsh_stream_writer;

/** Borrowed command text exposed as one batch input line. */
typedef struct wsh_memory_reader {
    /** Borrowed exact command bytes. */
    const unsigned char *bytes;
    /** Number of command bytes. */
    size_t length;
    /** Nonzero after the command has been returned once. */
    int consumed;
} wsh_memory_reader;

/** Runtime adapter and diagnostic cursor for one front-end evaluator. */
typedef struct wsh_main_evaluation {
    /** Evaluator retained across interactive commands. */
    wsh_evaluator *evaluator;
    /** Context that owns variables and diagnostics. */
    wsh_context *context;
    /** Owned concrete Windows effect boundary. */
    wsh_windows_runtime *windows_runtime;
    /** Concrete callback table used behind the console adapter. */
    wsh_runtime concrete_runtime;
    /** Borrowed normal-output writer. */
    wsh_stream_writer *output;
    /** Borrowed diagnostic-output writer. */
    wsh_stream_writer *error;
    /** First diagnostic not yet displayed. */
    size_t next_diagnostic;
} wsh_main_evaluation;

/** Print concise command usage to the selected stream. */
static void usage(FILE *stream, const char *program_name)
{
    fprintf(
        stream,
        "Usage: %s [--interactive|-i|--non-interactive|-I]\n"
        "       %s -c command\n"
        "       %s script.wsh [argument ...]\n"
        "       %s [--help|-h|--version|-V|--print-abi]\n"
        "\n"
        "With no source operand, read standard input. A console input uses\n"
        "prompts and multiline recovery; redirected input is batch input.\n"
        "  -i, --interactive      require a console on standard input\n"
        "  -I, --non-interactive  disable prompts and interactive recovery\n",
        program_name,
        program_name,
        program_name,
        program_name);
}

/** Return one borrowed in-memory command and then EOF. */
static wsh_frontend_read_result read_memory_line(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    wsh_memory_reader *reader;

    reader = (wsh_memory_reader *)user_data;
    *out_bytes = NULL;
    *out_length = 0U;
    if (reader == NULL || reader->consumed) {
        return WSH_FRONTEND_READ_EOF;
    }
    reader->consumed = 1;
    *out_bytes = reader->bytes;
    *out_length = reader->length;
    return WSH_FRONTEND_READ_LINE;
}

/** Return whether a standard handle is a usable Windows console. */
static int handle_is_console(HANDLE handle)
{
    DWORD mode;

    return handle != NULL && handle != INVALID_HANDLE_VALUE &&
        GetConsoleMode(handle, &mode) != 0;
}

/** Ensure a byte or UTF-16 buffer has at least needed elements. */
static void *reserve_buffer(
    void *buffer,
    size_t *capacity,
    size_t needed,
    size_t element_size)
{
    size_t grown;
    void *replacement;

    if (needed <= *capacity) {
        return buffer;
    }
    grown = *capacity == 0U ? 256U : *capacity;
    while (grown < needed) {
        if (grown > WSH_INPUT_LIMIT / 2U) {
            grown = WSH_INPUT_LIMIT;
        } else {
            grown *= 2U;
        }
        if (grown < needed || grown > (size_t)-1 / element_size) {
            return 0;
        }
    }
    replacement = realloc(buffer, grown * element_size);
    if (replacement == NULL) {
        return NULL;
    }
    *capacity = grown;
    return replacement;
}

/** Read one exact logical line from a redirected byte stream. */
static wsh_frontend_read_result read_file_line(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    wsh_file_reader *reader;
    size_t length;
    unsigned char character;
    DWORD read_count;
    DWORD error;
    void *replacement;

    reader = (wsh_file_reader *)user_data;
    *out_bytes = NULL;
    *out_length = 0U;
    if (reader->reached_eof) {
        return WSH_FRONTEND_READ_EOF;
    }

    length = 0U;
    for (;;) {
        if (reader->chunk_offset == reader->chunk_length) {
            read_count = 0U;
            if (!ReadFile(
                    reader->handle,
                    reader->chunk,
                    sizeof(reader->chunk),
                    &read_count,
                    NULL)) {
                error = GetLastError();
                if (error != ERROR_BROKEN_PIPE &&
                    error != ERROR_HANDLE_EOF) {
                    return WSH_FRONTEND_READ_ERROR;
                }
                read_count = 0U;
            }
            reader->chunk_length = read_count;
            reader->chunk_offset = 0U;
            if (read_count == 0U) {
                reader->reached_eof = 1;
                if (length == 0U) {
                    return WSH_FRONTEND_READ_EOF;
                }
                break;
            }
        }
        character = reader->chunk[reader->chunk_offset++];
        if (length == WSH_INPUT_LIMIT) {
            return WSH_FRONTEND_READ_RESOURCE;
        }
        replacement = reserve_buffer(
            reader->bytes,
            &reader->capacity,
            length + 1U,
            sizeof(*reader->bytes));
        if (replacement == NULL) {
            return WSH_FRONTEND_READ_RESOURCE;
        }
        reader->bytes = (unsigned char *)replacement;
        reader->bytes[length++] = character;
        if (character == '\n') {
            break;
        }
    }

    *out_bytes = reader->bytes;
    *out_length = length;
    return WSH_FRONTEND_READ_LINE;
}

/** Read one edited line from the native console and convert it to UTF-8. */
static wsh_frontend_read_result read_console_line(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    wsh_console_reader *reader;
    size_t length;
    WCHAR chunk[256];
    DWORD read_count;
    DWORD index;
    wsh_result result;
    void *replacement;
    int end_line;

    reader = (wsh_console_reader *)user_data;
    *out_bytes = NULL;
    *out_length = 0U;
    if (reader->reached_eof) {
        return WSH_FRONTEND_READ_EOF;
    }
    wsh_allocator_release(&reader->allocator, reader->bytes);
    reader->bytes = NULL;

    length = 0U;
    end_line = 0;
    for (;;) {
        read_count = 0U;
        if (!ReadConsoleW(
                reader->handle,
                chunk,
                sizeof(chunk) / sizeof(chunk[0]),
                &read_count,
                NULL)) {
            return WSH_FRONTEND_READ_ERROR;
        }
        if (read_count == 0U) {
            reader->reached_eof = 1;
            if (length == 0U) {
                return WSH_FRONTEND_READ_EOF;
            }
            break;
        }
        for (index = 0U; index < read_count; index++) {
            if (chunk[index] == 0x001aU) {
                reader->reached_eof = 1;
                end_line = 1;
                break;
            }
            if (chunk[index] == L'\r') {
                continue;
            }
            if (length == WSH_INPUT_LIMIT) {
                return WSH_FRONTEND_READ_RESOURCE;
            }
            replacement = reserve_buffer(
                reader->units,
                &reader->capacity,
                length + 1U,
                sizeof(*reader->units));
            if (replacement == NULL) {
                return WSH_FRONTEND_READ_RESOURCE;
            }
            reader->units = (uint16_t *)replacement;
            reader->units[length++] = (uint16_t)chunk[index];
            if (chunk[index] == L'\n') {
                end_line = 1;
                break;
            }
        }
        if (end_line) {
            break;
        }
    }
    if (reader->reached_eof && length == 0U) {
        return WSH_FRONTEND_READ_EOF;
    }

    result = wsh_utf16_to_utf8(
        &reader->allocator,
        NULL,
        reader->units,
        length,
        &reader->bytes,
        out_length);
    if (result != WSH_OK) {
        return result == WSH_ERR_ENCODING ? WSH_FRONTEND_READ_ENCODING :
            WSH_FRONTEND_READ_RESOURCE;
    }
    *out_bytes = (const unsigned char *)reader->bytes;
    return WSH_FRONTEND_READ_LINE;
}

/** Write strict UTF-8 using wide console output when available. */
static int write_stream(void *user_data, const char *bytes, size_t length)
{
    wsh_stream_writer *writer;
    wsh_allocator allocator;
    wsh_string_view text;
    uint16_t *units;
    size_t unit_count;
    size_t offset;
    DWORD written;
    wsh_result result;

    writer = (wsh_stream_writer *)user_data;
    if (!writer->is_console) {
        if (length > 0U &&
            fwrite(bytes, 1U, length, writer->stream) != length) {
            return 1;
        }
        return fflush(writer->stream) == 0 ? 0 : 1;
    }

    allocator = wsh_allocator_default();
    text.data = bytes;
    text.length = length;
    result = wsh_utf8_to_utf16(
        &allocator,
        NULL,
        text,
        &units,
        &unit_count);
    if (result != WSH_OK) {
        return 1;
    }
    offset = 0U;
    while (offset < unit_count) {
        DWORD chunk;

        chunk = unit_count - offset > 32768U ? 32768U :
            (DWORD)(unit_count - offset);
        written = 0U;
        if (!WriteConsoleW(
                writer->handle,
                units + offset,
                chunk,
                &written,
                NULL) || written != chunk) {
            wsh_allocator_release(&allocator, units);
            return 1;
        }
        offset += written;
    }
    wsh_allocator_release(&allocator, units);
    return 0;
}

/** Adapt evaluator write requests to the selected front-end stream. */
static wsh_result invoke_frontend_runtime(
    void *user_data,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    wsh_main_evaluation *evaluation;
    wsh_string_view text;
    const wsh_runtime_command *command;
    int redirected;

    evaluation = (wsh_main_evaluation *)user_data;
    if (evaluation == NULL || request == NULL || status == NULL) {
        return WSH_ERR_INVALID;
    }
    if (request->operation != WSH_RUNTIME_WRITE) {
        return evaluation->concrete_runtime.invoke(
            evaluation->concrete_runtime.user_data,
            request,
            output,
            status);
    }
    if (request->arguments == NULL ||
        wsh_value_count(request->arguments) != 1U ||
        wsh_value_at(request->arguments, 0U, &text) != WSH_OK) {
        return WSH_ERR_INVALID;
    }
    command = request->launch_plan != NULL &&
        request->launch_plan->command_count == 1U ?
        &request->launch_plan->commands[0] : NULL;
    redirected = command != NULL && command->redirection_count != 0U;
    if (redirected) {
        return evaluation->concrete_runtime.invoke(
            evaluation->concrete_runtime.user_data,
            request,
            output,
            status);
    }
    if (request->launch_plan != NULL &&
        (request->launch_plan->flags &
         WSH_RUNTIME_LAUNCH_CAPTURE) != 0U) {
        return wsh_status_builder_append(status, 0U);
    }
    if (write_stream(
            wsh_string_view_equal(
                request->subject,
                wsh_string_view_from_cstr("stderr")) ?
                evaluation->error : evaluation->output,
            text.data,
            text.length) != 0) {
        return WSH_ERR_INTERNAL;
    }
    return wsh_status_builder_append(status, 0U);
}

/** Delegate Windows ordinal environment-name comparison. */
static int compare_frontend_names(
    void *user_data,
    wsh_string_view left,
    wsh_string_view right)
{
    wsh_main_evaluation *evaluation;

    evaluation = (wsh_main_evaluation *)user_data;
    return evaluation->concrete_runtime.names_equal(
        evaluation->concrete_runtime.user_data, left, right);
}

/** Display newly retained evaluator diagnostics. */
static void write_evaluation_diagnostics(wsh_main_evaluation *evaluation)
{
    wsh_diagnostic_view diagnostic;
    char prefix[192];
    int length;

    while (evaluation->next_diagnostic <
        wsh_context_diagnostic_count(evaluation->context)) {
        if (wsh_context_diagnostic_at(
                evaluation->context,
                evaluation->next_diagnostic,
                &diagnostic) != WSH_OK) {
            return;
        }
        evaluation->next_diagnostic += 1U;
        if (diagnostic.has_span) {
            length = snprintf(
                prefix,
                sizeof(prefix),
                "wsh: %lu:%lu: ",
                (unsigned long)diagnostic.span.start.line,
                (unsigned long)diagnostic.span.start.scalar_column);
        } else {
            length = snprintf(prefix, sizeof(prefix), "wsh: ");
        }
        if (length > 0 && (size_t)length < sizeof(prefix)) {
            (void)write_stream(evaluation->error, prefix, (size_t)length);
        }
        (void)write_stream(
            evaluation->error,
            diagnostic.message.data,
            diagnostic.message.length);
        (void)write_stream(evaluation->error, "\n", 1U);
    }
}

/** Evaluate one complete front-end tree and return its process-style status. */
static int evaluate_frontend_tree(
    void *user_data,
    const wsh_parse_tree *tree)
{
    wsh_main_evaluation *evaluation;
    wsh_status_list *status;
    wsh_result result;
    size_t index;
    uint32_t code;
    uint32_t exit_code;

    evaluation = (wsh_main_evaluation *)user_data;
    status = NULL;
    result = wsh_evaluate(evaluation->evaluator, tree, &status);
    write_evaluation_diagnostics(evaluation);
    if (result != WSH_OK) {
        wsh_status_list_destroy(status);
        return result == WSH_ERR_RESOURCE ? 4 : 1;
    }
    exit_code = 0U;
    for (index = 0U; index < wsh_status_list_count(status); ++index) {
        if (wsh_status_list_at(status, index, &code) == WSH_OK &&
            exit_code == 0U && code != 0U) {
            exit_code = code;
        }
    }
    wsh_status_list_destroy(status);
    return (int)exit_code;
}

/** Run standard input in the selected batch or interactive mode. */
static int run_input_session(
    int interactive,
    const char *source_name,
    wsh_frontend_read_fn read_line,
    void *input_data,
    HANDLE input_handle,
    int argument_count,
    char **arguments)
{
    HANDLE output_handle;
    HANDLE error_handle;
    wsh_stream_writer output_writer;
    wsh_stream_writer error_writer;
    wsh_frontend_options options;
    wsh_frontend_io io;
    wsh_main_evaluation evaluation;
    wsh_context_options context_options;
    wsh_evaluator_options evaluator_options;
    wsh_runtime runtime;
    wsh_value_builder *argument_builder;
    wsh_value *argument_value;
    wsh_allocator allocator;
    wsh_limits limits;
    wsh_result core_result;
    int argument_index;
    DWORD original_input_mode;
    DWORD input_mode;
    int restore_input_mode;
    int result;

    output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    error_handle = GetStdHandle(STD_ERROR_HANDLE);
    memset(&io, 0, sizeof(io));
    memset(&evaluation, 0, sizeof(evaluation));
    argument_builder = NULL;
    argument_value = NULL;
    restore_input_mode = 0;

    if (interactive) {
        if (!GetConsoleMode(input_handle, &original_input_mode)) {
            return WSH_EXIT_IO;
        }
        input_mode = original_input_mode | ENABLE_ECHO_INPUT |
            ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
        if (input_mode != original_input_mode) {
            if (!SetConsoleMode(input_handle, input_mode)) {
                return WSH_EXIT_IO;
            }
            restore_input_mode = 1;
        }
    }

    output_writer.stream = stdout;
    output_writer.handle = output_handle;
    output_writer.is_console = handle_is_console(output_handle);
    error_writer.stream = stderr;
    error_writer.handle = error_handle;
    error_writer.is_console = handle_is_console(error_handle);

    io.input_data = input_data;
    io.read_line = read_line;
    io.output_data = &output_writer;
    io.write_output = write_stream;
    io.error_data = &error_writer;
    io.write_error = write_stream;
    evaluation.output = &output_writer;
    evaluation.error = &error_writer;
    if (wsh_windows_runtime_create(
            NULL, &evaluation.windows_runtime) != WSH_OK) {
        result = 4;
        goto cleanup;
    }
    evaluation.concrete_runtime = wsh_windows_runtime_interface(
        evaluation.windows_runtime);
    memset(&runtime, 0, sizeof(runtime));
    runtime.user_data = &evaluation;
    runtime.invoke = invoke_frontend_runtime;
    runtime.names_equal = compare_frontend_names;
    wsh_context_options_init(&context_options);
    context_options.runtime = runtime;
    if (wsh_context_create(
            &context_options, &evaluation.context) != WSH_OK) {
        result = 4;
        goto cleanup;
    }
    if (wsh_windows_runtime_import_environment(
            evaluation.windows_runtime,
            evaluation.context) != WSH_OK) {
        result = 4;
        goto cleanup;
    }
    allocator = wsh_allocator_default();
    limits = wsh_limits_default();
    core_result = wsh_value_builder_create(
        &allocator, &limits, &argument_builder);
    for (argument_index = 0;
         core_result == WSH_OK && argument_index < argument_count;
         ++argument_index) {
        core_result = wsh_value_builder_append(
            argument_builder,
            wsh_string_view_from_cstr(arguments[argument_index]));
    }
    if (core_result == WSH_OK) {
        core_result = wsh_value_builder_finish(
            argument_builder, &argument_value);
    }
    if (core_result == WSH_OK) {
        core_result = wsh_context_set_variable(
            evaluation.context,
            wsh_string_view_from_cstr("*"),
            argument_value);
    }
    if (core_result != WSH_OK) {
        result = 4;
        goto cleanup;
    }
    wsh_evaluator_options_init(&evaluator_options);
    evaluator_options.source_name = wsh_string_view_from_cstr(
        source_name);
    if (wsh_evaluator_create(
            evaluation.context,
            &evaluator_options,
            &evaluation.evaluator) != WSH_OK) {
        result = 4;
        goto cleanup;
    }
    io.evaluation_data = &evaluation;
    io.evaluate = evaluate_frontend_tree;
    wsh_frontend_options_init(&options);
    options.interactive = interactive;
    result = wsh_frontend_run(&options, &io);

cleanup:
    wsh_evaluator_destroy(evaluation.evaluator);
    wsh_context_destroy(evaluation.context);
    wsh_windows_runtime_destroy(evaluation.windows_runtime);
    wsh_value_builder_destroy(argument_builder);
    wsh_value_destroy(argument_value);
    if (restore_input_mode &&
        !SetConsoleMode(input_handle, original_input_mode) && result == 0) {
        result = WSH_EXIT_IO;
    }
    return result;
}

/** Run standard input in the selected batch or interactive mode. */
static int run_standard_input(int interactive)
{
    HANDLE input_handle;
    wsh_file_reader file_reader;
    wsh_console_reader console_reader;
    int result;

    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    memset(&file_reader, 0, sizeof(file_reader));
    memset(&console_reader, 0, sizeof(console_reader));
    file_reader.handle = input_handle;
    console_reader.handle = input_handle;
    console_reader.allocator = wsh_allocator_default();
    result = run_input_session(
        interactive,
        interactive ? "wsh" : "stdin",
        interactive ? read_console_line : read_file_line,
        interactive ? (void *)&console_reader : (void *)&file_reader,
        input_handle,
        0,
        NULL);
    free(file_reader.bytes);
    free(console_reader.units);
    wsh_allocator_release(&console_reader.allocator, console_reader.bytes);
    return result;
}

/** Run a literal command as one non-interactive input. */
static int run_command(const char *command)
{
    wsh_memory_reader reader;

    memset(&reader, 0, sizeof(reader));
    reader.bytes = (const unsigned char *)command;
    reader.length = strlen(command);
    return run_input_session(
        0,
        "-c",
        read_memory_line,
        &reader,
        GetStdHandle(STD_INPUT_HANDLE),
        0,
        NULL);
}

/** Open a strict UTF-8 path with the native wide Windows boundary. */
static HANDLE open_script(const char *path)
{
    wsh_allocator allocator;
    wsh_string_view view;
    uint16_t *wide;
    size_t length;
    HANDLE handle;

    allocator = wsh_allocator_default();
    view = wsh_string_view_from_cstr(path);
    wide = NULL;
    if (wsh_utf8_to_utf16(
            &allocator, NULL, view, &wide, &length) != WSH_OK) {
        return INVALID_HANDLE_VALUE;
    }
    handle = CreateFileW(
        (LPCWSTR)wide,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    wsh_allocator_release(&allocator, wide);
    return handle;
}

/** Run one UTF-8-named script with remaining arguments in `$*`. */
static int run_script(
    const char *path,
    int argument_count,
    char **arguments)
{
    HANDLE handle;
    wsh_file_reader reader;
    int result;

    handle = open_script(path);
    if (handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "wsh: cannot open script: %s\n", path);
        return WSH_EXIT_IO;
    }
    memset(&reader, 0, sizeof(reader));
    reader.handle = handle;
    result = run_input_session(
        0,
        path,
        read_file_line,
        &reader,
        handle,
        argument_count,
        arguments);
    free(reader.bytes);
    CloseHandle(handle);
    return result;
}

/** Write exact internal-stage bytes to the inherited standard output. */
static int write_internal_bytes(const char *bytes, size_t length)
{
    HANDLE output;
    size_t offset;
    DWORD chunk;
    DWORD written;

    output = GetStdHandle(STD_OUTPUT_HANDLE);
    offset = 0U;
    while (offset < length) {
        chunk = length - offset > 32768U ?
            32768U : (DWORD)(length - offset);
        written = 0U;
        if (!WriteFile(
                output,
                bytes + offset,
                chunk,
                &written,
                NULL) || written != chunk) {
            return 0;
        }
        offset += written;
    }
    return 1;
}

/** Execute the private process form used for an `echo` pipeline stage. */
static int run_internal_echo(int argc, char **argv)
{
    int first;
    int index;
    int newline;

    first = 2;
    newline = 1;
    if (first < argc && strcmp(argv[first], "-n") == 0) {
        first += 1;
        newline = 0;
    }
    for (index = first; index < argc; ++index) {
        if (index != first && !write_internal_bytes(" ", 1U)) {
            return WSH_EXIT_IO;
        }
        if (!write_internal_bytes(argv[index], strlen(argv[index]))) {
            return WSH_EXIT_IO;
        }
    }
    if (newline && !write_internal_bytes("\r\n", 2U)) {
        return WSH_EXIT_IO;
    }
    return 0;
}

/** Process information options and select the standard-input mode. */
static int wsh_main(int argc, char **argv)
{
    int require_interactive;
    int disable_interactive;
    int end_options;
    int i;
    size_t short_index;
    HANDLE input_handle;
    int input_is_console;
    const char *command;
    int script_index;

    if (argc >= 2 && strcmp(argv[1], "--runtime-echo") == 0) {
        return run_internal_echo(argc, argv);
    }

    require_interactive = 0;
    disable_interactive = 0;
    end_options = 0;
    command = NULL;
    script_index = 0;
    for (i = 1; i < argc; ++i) {
        if (!end_options && strcmp(argv[i], "--") == 0) {
            end_options = 1;
        } else if (!end_options &&
            (strcmp(argv[i], "--interactive") == 0 ||
             strcmp(argv[i], "-i") == 0)) {
            require_interactive = 1;
        } else if (!end_options &&
            (strcmp(argv[i], "--non-interactive") == 0 ||
             strcmp(argv[i], "-I") == 0)) {
            disable_interactive = 1;
        } else if (!end_options && strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc || i + 2 != argc) {
                usage(stderr, argv[0]);
                return WSH_EXIT_USAGE;
            }
            command = argv[i + 1];
            break;
        } else if (!end_options &&
            (strcmp(argv[i], "--help") == 0 ||
             strcmp(argv[i], "-h") == 0)) {
            if (argc != 2) {
                usage(stderr, argv[0]);
                return WSH_EXIT_USAGE;
            }
            usage(stdout, argv[0]);
            return 0;
        } else if (!end_options &&
            (strcmp(argv[i], "--version") == 0 ||
             strcmp(argv[i], "-V") == 0)) {
            if (argc != 2) {
                usage(stderr, argv[0]);
                return WSH_EXIT_USAGE;
            }
            return wsh_print_version(stdout);
        } else if (!end_options && strcmp(argv[i], "--print-abi") == 0) {
            if (argc != 2) {
                usage(stderr, argv[0]);
                return WSH_EXIT_USAGE;
            }
            printf("wsh embedding ABI %u\n", WSH_EMBEDDING_ABI);
            return 0;
        } else if (!end_options && argv[i][0] == '-' &&
            argv[i][1] != '-' && argv[i][1] != '\0' &&
            argv[i][2] != '\0') {
            for (short_index = 1U; argv[i][short_index] != '\0';
                 short_index++) {
                if (argv[i][short_index] == 'i') {
                    require_interactive = 1;
                } else if (argv[i][short_index] == 'I') {
                    disable_interactive = 1;
                } else {
                    usage(stderr, argv[0]);
                    return WSH_EXIT_USAGE;
                }
            }
        } else {
            script_index = i;
            break;
        }
    }

    if (require_interactive && disable_interactive) {
        fprintf(stderr, "wsh: interactive modes are mutually exclusive\n");
        return WSH_EXIT_USAGE;
    }
    if (command != NULL || script_index != 0) {
        if (require_interactive) {
            fprintf(stderr, "wsh: -i cannot be used with command input\n");
            return WSH_EXIT_USAGE;
        }
        if (command != NULL) {
            return run_command(command);
        }
        return run_script(
            argv[script_index],
            argc - script_index - 1,
            argv + script_index + 1);
    }
    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    input_is_console = handle_is_console(input_handle);
    if (require_interactive && !input_is_console) {
        fprintf(stderr, "wsh: interactive mode requires a console on stdin\n");
        return WSH_EXIT_IO;
    }
    return run_standard_input(input_is_console && !disable_interactive);
}

#if defined(__aarch64__)
/** Emit temporary native ARM64 startup diagnostics without using stdio. */
static void trace_arm64_startup(const char *message, size_t length)
{
    HANDLE error_handle;
    DWORD written;

    error_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (error_handle != NULL && error_handle != INVALID_HANDLE_VALUE) {
        WriteFile(error_handle, message, (DWORD)length, &written, NULL);
    }
}

#define WSH_TRACE_ARM64(message) \
    trace_arm64_startup((message), sizeof(message) - 1U)
#else
#define WSH_TRACE_ARM64(message) ((void)0)
#endif

/** Grow one temporary UTF-16 command-line argument. */
static int append_argument_unit(
    uint16_t **units,
    size_t *length,
    size_t *capacity,
    uint16_t unit)
{
    size_t next;
    uint16_t *replacement;

    if (*length == *capacity) {
        next = *capacity == 0U ? 32U : *capacity * 2U;
        if (next < *capacity ||
            next > (size_t)-1 / sizeof(**units)) {
            return 0;
        }
        replacement = (uint16_t *)realloc(
            *units, next * sizeof(**units));
        if (replacement == NULL) {
            return 0;
        }
        *units = replacement;
        *capacity = next;
    }
    (*units)[(*length)++] = unit;
    return 1;
}

/** Convert one strict UTF-16 command-line argument to owned UTF-8. */
static int command_line_argument_to_utf8(
    const uint16_t *units,
    size_t length,
    char **out_argument)
{
    size_t capacity;
    size_t input_index;
    size_t output_index;
    uint32_t scalar;
    uint16_t unit;
    uint16_t low;
    char *bytes;

    *out_argument = NULL;
    if (length > ((size_t)-1 - 1U) / 3U) {
        return 0;
    }
    capacity = length * 3U + 1U;
    bytes = (char *)malloc(capacity);
    if (bytes == NULL) {
        return 0;
    }
    input_index = 0U;
    output_index = 0U;
    while (input_index < length) {
        unit = units[input_index++];
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            if (input_index == length) {
                free(bytes);
                return 0;
            }
            low = units[input_index++];
            if (low < 0xDC00U || low > 0xDFFFU) {
                free(bytes);
                return 0;
            }
            scalar = 0x10000U +
                (((uint32_t)unit - 0xD800U) << 10U) +
                ((uint32_t)low - 0xDC00U);
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            free(bytes);
            return 0;
        } else {
            scalar = unit;
        }
        if (scalar <= 0x7FU) {
            bytes[output_index++] = (char)scalar;
        } else if (scalar <= 0x7FFU) {
            bytes[output_index++] = (char)(0xC0U | (scalar >> 6U));
            bytes[output_index++] = (char)(0x80U | (scalar & 0x3FU));
        } else if (scalar <= 0xFFFFU) {
            bytes[output_index++] = (char)(0xE0U | (scalar >> 12U));
            bytes[output_index++] =
                (char)(0x80U | ((scalar >> 6U) & 0x3FU));
            bytes[output_index++] = (char)(0x80U | (scalar & 0x3FU));
        } else {
            bytes[output_index++] = (char)(0xF0U | (scalar >> 18U));
            bytes[output_index++] =
                (char)(0x80U | ((scalar >> 12U) & 0x3FU));
            bytes[output_index++] =
                (char)(0x80U | ((scalar >> 6U) & 0x3FU));
            bytes[output_index++] = (char)(0x80U | (scalar & 0x3FU));
        }
    }
    bytes[output_index] = '\0';
    *out_argument = bytes;
    return 1;
}

/** Parse GetCommandLineW with the Microsoft C-runtime quoting algorithm. */
static int parse_wide_arguments(int *out_count, char ***out_arguments)
{
    const uint16_t *command_line;
    size_t offset;
    size_t slash_count;
    int quoted;
    uint16_t *wide_argument;
    size_t wide_length;
    size_t wide_capacity;
    char *argument;
    char **arguments;
    size_t count;
    size_t capacity;
    char **replacement;

    WSH_TRACE_ARM64("wsh-startup: parse-enter\n");
    *out_count = 0;
    *out_arguments = NULL;
    command_line = (const uint16_t *)GetCommandLineW();
    WSH_TRACE_ARM64("wsh-startup: command-line\n");
    offset = 0U;
    arguments = NULL;
    count = 0U;
    capacity = 0U;
    while (command_line[offset] != 0U) {
        while (command_line[offset] == L' ' ||
               command_line[offset] == L'\t') {
            offset += 1U;
        }
        if (command_line[offset] == 0U) {
            break;
        }
        WSH_TRACE_ARM64("wsh-startup: argument-enter\n");
        wide_argument = NULL;
        wide_length = 0U;
        wide_capacity = 0U;
        quoted = 0;
        while (command_line[offset] != 0U &&
            (quoted || (command_line[offset] != L' ' &&
                        command_line[offset] != L'\t'))) {
            slash_count = 0U;
            while (command_line[offset] == L'\\') {
                slash_count += 1U;
                offset += 1U;
            }
            if (command_line[offset] == L'\"') {
                while (slash_count >= 2U) {
                    if (!append_argument_unit(
                            &wide_argument,
                            &wide_length,
                            &wide_capacity,
                            L'\\')) {
                        free(wide_argument);
                        goto failure;
                    }
                    slash_count -= 2U;
                }
                if (slash_count == 1U) {
                    if (!append_argument_unit(
                            &wide_argument,
                            &wide_length,
                            &wide_capacity,
                            L'\"')) {
                        free(wide_argument);
                        goto failure;
                    }
                    offset += 1U;
                } else if (quoted && command_line[offset + 1U] == L'\"') {
                    if (!append_argument_unit(
                            &wide_argument,
                            &wide_length,
                            &wide_capacity,
                            L'\"')) {
                        free(wide_argument);
                        goto failure;
                    }
                    offset += 2U;
                } else {
                    quoted = !quoted;
                    offset += 1U;
                }
            } else {
                while (slash_count != 0U) {
                    if (!append_argument_unit(
                            &wide_argument,
                            &wide_length,
                            &wide_capacity,
                            L'\\')) {
                        free(wide_argument);
                        goto failure;
                    }
                    slash_count -= 1U;
                }
                if (command_line[offset] != 0U) {
                    if (!append_argument_unit(
                            &wide_argument,
                            &wide_length,
                            &wide_capacity,
                            command_line[offset++])) {
                        free(wide_argument);
                        goto failure;
                    }
                }
            }
        }
        argument = NULL;
        WSH_TRACE_ARM64("wsh-startup: argument-parsed\n");
        if (!command_line_argument_to_utf8(
                wide_argument, wide_length, &argument)) {
            free(wide_argument);
            goto failure;
        }
        WSH_TRACE_ARM64("wsh-startup: argument-converted\n");
        free(wide_argument);
        if (count == capacity) {
            size_t next;

            next = capacity == 0U ? 8U : capacity * 2U;
            replacement = (char **)realloc(
                arguments, next * sizeof(*arguments));
            if (replacement == NULL) {
                free(argument);
                goto failure;
            }
            arguments = replacement;
            capacity = next;
        }
        arguments[count++] = argument;
        WSH_TRACE_ARM64("wsh-startup: argument-stored\n");
    }
    WSH_TRACE_ARM64("wsh-startup: parse-return\n");
    *out_count = (int)count;
    *out_arguments = arguments;
    return 1;

failure:
    while (count != 0U) {
        count -= 1U;
        free(arguments[count]);
    }
    free(arguments);
    return 0;
}

/** Release strict UTF-8 arguments created from GetCommandLineW. */
static void destroy_arguments(int count, char **arguments)
{
    while (count > 0) {
        count -= 1;
        free(arguments[count]);
    }
    free(arguments);
}

/** Convert the native command line before dispatching the CLI. */
int main(int ignored_count, char **ignored_arguments)
{
    int count;
    char **arguments;
    int result;

#if defined(__aarch64__)
    return 42;
#endif
    (void)ignored_count;
    (void)ignored_arguments;
    WSH_TRACE_ARM64("wsh-startup: main\n");
    if (!parse_wide_arguments(&count, &arguments) || count == 0) {
        WSH_TRACE_ARM64("wsh-startup: parse-failed\n");
        return WSH_EXIT_USAGE;
    }
    WSH_TRACE_ARM64("wsh-startup: dispatch\n");
    result = wsh_main(count, arguments);
    WSH_TRACE_ARM64("wsh-startup: destroy\n");
    destroy_arguments(count, arguments);
    WSH_TRACE_ARM64("wsh-startup: return\n");
    return result;
}
