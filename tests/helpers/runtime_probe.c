/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file runtime_probe.c
 * @brief Native child used by controlled Windows runtime integration tests.
 */

#include "wsh/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/** Write exact bytes to one Windows standard handle. */
static int probe_write(DWORD identifier, const char *bytes, size_t length)
{
    HANDLE handle;
    size_t offset;
    DWORD chunk;
    DWORD written;

    handle = GetStdHandle(identifier);
    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    offset = 0U;
    while (offset < length) {
        chunk = length - offset > 32768U ?
            32768U : (DWORD)(length - offset);
        written = 0U;
        if (!WriteFile(
                handle,
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

/** Copy standard input bytes to standard output until EOF. */
static int probe_copy(void)
{
    HANDLE input;
    char bytes[4096];
    DWORD received;

    input = GetStdHandle(STD_INPUT_HANDLE);
    for (;;) {
        received = 0U;
        if (!ReadFile(input, bytes, sizeof(bytes), &received, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE ||
                GetLastError() == ERROR_PIPE_NOT_CONNECTED) {
                return 1;
            }
            return 0;
        }
        if (received == 0U) {
            return 1;
        }
        if (!probe_write(STD_OUTPUT_HANDLE, bytes, received)) {
            return 0;
        }
    }
}

/** Emit every user argument with an exact hexadecimal byte length. */
static int probe_arguments(int argc, char **argv)
{
    int index;
    char prefix[32];
    int length;
    size_t argument_length;

    for (index = 2; index < argc; ++index) {
        argument_length = strlen(argv[index]);
        length = snprintf(
            prefix, sizeof(prefix), "%08lx:",
            (unsigned long)argument_length);
        if (length != 9 ||
            !probe_write(STD_OUTPUT_HANDLE, prefix, (size_t)length) ||
            !probe_write(
                STD_OUTPUT_HANDLE, argv[index], argument_length) ||
            !probe_write(STD_OUTPUT_HANDLE, "\n", 1U)) {
            return 0;
        }
    }
    return 1;
}

/** Write the exact wide command line after strict UTF-8 conversion. */
static int probe_raw_line(void)
{
    LPCWSTR command_line;
    size_t units;
    wsh_allocator allocator;
    char *bytes;
    size_t length;
    wsh_result result;
    int written;

    command_line = GetCommandLineW();
    units = 0U;
    while (command_line[units] != 0U) {
        units += 1U;
    }
    allocator = wsh_allocator_default();
    bytes = NULL;
    result = wsh_utf16_to_utf8(
        &allocator,
        NULL,
        (const uint16_t *)command_line,
        units,
        &bytes,
        &length);
    written = result == WSH_OK &&
        probe_write(STD_OUTPUT_HANDLE, bytes, length);
    wsh_allocator_release(&allocator, bytes);
    return written;
}

/** Write the exact current directory as UTF-8. */
static int probe_directory(void)
{
    DWORD required;
    uint16_t *units;
    DWORD received;
    wsh_allocator allocator;
    char *bytes;
    size_t length;
    int result;

    required = GetCurrentDirectoryW(0U, NULL);
    units = (uint16_t *)malloc(required * sizeof(*units));
    if (units == NULL) {
        return 0;
    }
    received = GetCurrentDirectoryW(required, (LPWSTR)units);
    allocator = wsh_allocator_default();
    bytes = NULL;
    result = received != 0U && received < required &&
        wsh_utf16_to_utf8(
            &allocator,
            NULL,
            units,
            received,
            &bytes,
            &length) == WSH_OK &&
        probe_write(STD_OUTPUT_HANDLE, bytes, length);
    wsh_allocator_release(&allocator, bytes);
    free(units);
    return result;
}

/** Write one named environment value as UTF-8. */
static int probe_environment(const char *name)
{
    wsh_allocator allocator;
    uint16_t *wide_name;
    size_t name_units;
    DWORD required;
    uint16_t *wide_value;
    DWORD received;
    char *bytes;
    size_t length;
    int result;

    allocator = wsh_allocator_default();
    wide_name = NULL;
    if (wsh_utf8_to_utf16(
            &allocator,
            NULL,
            wsh_string_view_from_cstr(name),
            &wide_name,
            &name_units) != WSH_OK) {
        return 0;
    }
    required = GetEnvironmentVariableW((LPCWSTR)wide_name, NULL, 0U);
    if (required == 0U) {
        wsh_allocator_release(&allocator, wide_name);
        return probe_write(STD_OUTPUT_HANDLE, "<absent>", 8U);
    }
    wide_value = (uint16_t *)malloc(required * sizeof(*wide_value));
    if (wide_value == NULL) {
        wsh_allocator_release(&allocator, wide_name);
        return 0;
    }
    received = GetEnvironmentVariableW(
        (LPCWSTR)wide_name, (LPWSTR)wide_value, required);
    bytes = NULL;
    result = received < required &&
        wsh_utf16_to_utf8(
            &allocator,
            NULL,
            wide_value,
            received,
            &bytes,
            &length) == WSH_OK &&
        probe_write(STD_OUTPUT_HANDLE, bytes, length);
    wsh_allocator_release(&allocator, bytes);
    wsh_allocator_release(&allocator, wide_name);
    free(wide_value);
    return result;
}

/** Parse one unsigned decimal probe operand. */
static int probe_unsigned(const char *text, uint32_t *out_value)
{
    uint32_t value;
    size_t index;
    unsigned digit;

    value = 0U;
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    for (index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }
        digit = (unsigned)(text[index] - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return 0;
        }
        value = value * 10U + digit;
    }
    *out_value = value;
    return 1;
}

/** Dispatch one stable native child probe operation. */
int main(int argc, char **argv)
{
    uint32_t code;
    uint32_t milliseconds;

    if (argc < 2) {
        return 2;
    }
    if (strcmp(argv[1], "args") == 0) {
        return probe_arguments(argc, argv) ? 0 : 3;
    }
    if (strcmp(argv[1], "raw") == 0) {
        return probe_raw_line() ? 0 : 3;
    }
    if (strcmp(argv[1], "cwd") == 0) {
        return probe_directory() ? 0 : 3;
    }
    if (strcmp(argv[1], "env") == 0 && argc == 3) {
        return probe_environment(argv[2]) ? 0 : 3;
    }
    if (strcmp(argv[1], "copy") == 0) {
        code = 0U;
        if (argc == 3 && !probe_unsigned(argv[2], &code)) {
            return 2;
        }
        return probe_copy() ? (int)code : 3;
    }
    if (strcmp(argv[1], "emit") == 0 && argc >= 3) {
        code = 0U;
        if (argc >= 4 && !probe_unsigned(argv[3], &code)) {
            return 2;
        }
        return probe_write(
            STD_OUTPUT_HANDLE, argv[2], strlen(argv[2])) ?
            (int)code : 3;
    }
    if (strcmp(argv[1], "stderr") == 0 && argc == 3) {
        return probe_write(
            STD_ERROR_HANDLE, argv[2], strlen(argv[2])) ? 0 : 3;
    }
    if (strcmp(argv[1], "invalid") == 0) {
        static const char invalid[] = {(char)0xc0, (char)0xaf};

        return probe_write(
            STD_OUTPUT_HANDLE, invalid, sizeof(invalid)) ? 0 : 3;
    }
    if (strcmp(argv[1], "sleep") == 0 && argc >= 3 &&
        probe_unsigned(argv[2], &milliseconds)) {
        code = 0U;
        if (argc >= 4 && !probe_unsigned(argv[3], &code)) {
            return 2;
        }
        Sleep(milliseconds);
        return (int)code;
    }
    return 2;
}
