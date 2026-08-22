/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file main.c
 * @brief WSH command-line and native-console front end.
 */

#include "frontend.h"
#include "wsh/core.h"
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

/** Print concise command usage to the selected stream. */
static void usage(FILE *stream, const char *program_name)
{
    fprintf(
        stream,
        "Usage: %s [--interactive|-i|--non-interactive|-I]\n"
        "       %s [--help|-h|--version|-V|--print-abi]\n"
        "\n"
        "With no source operand, read standard input. A console input uses\n"
        "prompts and multiline recovery; redirected input is batch input.\n"
        "  -i, --interactive      require a console on standard input\n"
        "  -I, --non-interactive  disable prompts and interactive recovery\n",
        program_name,
        program_name);
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

/** Run standard input in the selected batch or interactive mode. */
static int run_standard_input(int interactive)
{
    HANDLE input_handle;
    HANDLE output_handle;
    HANDLE error_handle;
    wsh_file_reader file_reader;
    wsh_console_reader console_reader;
    wsh_stream_writer output_writer;
    wsh_stream_writer error_writer;
    wsh_frontend_options options;
    wsh_frontend_io io;
    DWORD original_input_mode;
    DWORD input_mode;
    int restore_input_mode;
    int result;

    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    error_handle = GetStdHandle(STD_ERROR_HANDLE);
    memset(&file_reader, 0, sizeof(file_reader));
    memset(&console_reader, 0, sizeof(console_reader));
    memset(&io, 0, sizeof(io));
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

    file_reader.handle = input_handle;
    console_reader.handle = input_handle;
    console_reader.allocator = wsh_allocator_default();
    output_writer.stream = stdout;
    output_writer.handle = output_handle;
    output_writer.is_console = handle_is_console(output_handle);
    error_writer.stream = stderr;
    error_writer.handle = error_handle;
    error_writer.is_console = handle_is_console(error_handle);

    io.input_data = interactive ? (void *)&console_reader :
        (void *)&file_reader;
    io.read_line = interactive ? read_console_line : read_file_line;
    io.output_data = &output_writer;
    io.write_output = write_stream;
    io.error_data = &error_writer;
    io.write_error = write_stream;
    wsh_frontend_options_init(&options);
    options.interactive = interactive;
    result = wsh_frontend_run(&options, &io);

    free(file_reader.bytes);
    free(console_reader.units);
    wsh_allocator_release(&console_reader.allocator, console_reader.bytes);
    if (restore_input_mode &&
        !SetConsoleMode(input_handle, original_input_mode) && result == 0) {
        result = WSH_EXIT_IO;
    }
    return result;
}

/** Process information options and select the standard-input mode. */
int main(int argc, char **argv)
{
    int require_interactive;
    int disable_interactive;
    int end_options;
    int i;
    size_t short_index;
    HANDLE input_handle;
    int input_is_console;

    require_interactive = 0;
    disable_interactive = 0;
    end_options = 0;
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
            usage(stderr, argv[0]);
            return WSH_EXIT_USAGE;
        }
    }

    if (require_interactive && disable_interactive) {
        fprintf(stderr, "wsh: interactive modes are mutually exclusive\n");
        return WSH_EXIT_USAGE;
    }
    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    input_is_console = handle_is_console(input_handle);
    if (require_interactive && !input_is_console) {
        fprintf(stderr, "wsh: interactive mode requires a console on stdin\n");
        return WSH_EXIT_IO;
    }
    return run_standard_input(input_is_console && !disable_interactive);
}
