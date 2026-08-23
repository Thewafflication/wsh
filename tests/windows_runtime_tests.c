/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file windows_runtime_tests.c
 * @brief Controlled native tests for the M5 Windows execution boundary.
 */

#include "wsh/core.h"
#include "wsh/windows_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/** Active test case name used by failure diagnostics. */
static const char *test_case;

/** Report one failed controlled check and return false. */
static int check_result(int condition, const char *expression, int line)
{
    if (condition) {
        return 1;
    }
    fprintf(
        stderr, "%s:%d: check failed: %s\n", test_case, line, expression);
    return 0;
}

/** Require one expression inside a test function. */
#define CHECK(expression) \
    do { \
        if (!check_result((expression), #expression, __LINE__)) { \
            return 0; \
        } \
    } while (0)

/** One owned concrete runtime and its isolated context. */
typedef struct test_runtime {
    /** Owned Windows runtime. */
    wsh_windows_runtime *windows;
    /** Owned portable context. */
    wsh_context *context;
    /** Copied abstract interface. */
    wsh_runtime interface;
} test_runtime;

/** Create an immutable value from borrowed C strings. */
static wsh_result make_value(
    const char *const *items,
    size_t count,
    wsh_value **out_value)
{
    wsh_allocator allocator;
    wsh_limits limits;
    wsh_value_builder *builder;
    size_t index;
    wsh_result result;

    allocator = wsh_allocator_default();
    limits = wsh_limits_default();
    builder = NULL;
    *out_value = NULL;
    result = wsh_value_builder_create(&allocator, &limits, &builder);
    for (index = 0U; result == WSH_OK && index < count; ++index) {
        result = wsh_value_builder_append(
            builder, wsh_string_view_from_cstr(items[index]));
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_value);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Create one test runtime and import its process environment. */
static wsh_result create_runtime(
    const wsh_windows_runtime_options *options,
    test_runtime *out_runtime)
{
    wsh_context_options context_options;
    wsh_result result;

    memset(out_runtime, 0, sizeof(*out_runtime));
    result = wsh_windows_runtime_create(options, &out_runtime->windows);
    if (result != WSH_OK) {
        return result;
    }
    out_runtime->interface = wsh_windows_runtime_interface(
        out_runtime->windows);
    wsh_context_options_init(&context_options);
    context_options.runtime = out_runtime->interface;
    result = wsh_context_create(&context_options, &out_runtime->context);
    if (result == WSH_OK) {
        result = wsh_windows_runtime_import_environment(
            out_runtime->windows, out_runtime->context);
    }
    return result;
}

/** Destroy one test runtime after all registered work is collected. */
static void destroy_runtime(test_runtime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    wsh_context_destroy(runtime->context);
    wsh_windows_runtime_destroy(runtime->windows);
    memset(runtime, 0, sizeof(*runtime));
}

/** Invoke one request through the public portable boundary. */
static wsh_result invoke_request(
    test_runtime *runtime,
    wsh_runtime_operation operation,
    const char *subject,
    const wsh_value *arguments,
    const wsh_runtime_launch_plan *plan,
    wsh_value **out_output,
    wsh_status_list **out_status)
{
    wsh_runtime_request request;

    memset(&request, 0, sizeof(request));
    request.operation = operation;
    request.subject = wsh_string_view_from_cstr(subject);
    request.arguments = arguments;
    request.context = runtime->context;
    request.launch_plan = plan;
    return wsh_context_runtime_invoke(
        runtime->context, &request, out_output, out_status);
}

/** Return whether one value item equals an exact C string. */
static int value_equals(
    const wsh_value *value,
    size_t index,
    const char *expected)
{
    wsh_string_view item;

    return wsh_value_at(value, index, &item) == WSH_OK &&
        wsh_string_view_equal(item, wsh_string_view_from_cstr(expected));
}

/** Return whether one status item equals an exact code. */
static int status_equals(
    const wsh_status_list *status,
    size_t index,
    uint32_t expected)
{
    uint32_t actual;

    return wsh_status_list_at(status, index, &actual) == WSH_OK &&
        actual == expected;
}

/** Launch one command, optionally capturing its standard output. */
static wsh_result launch_one(
    test_runtime *runtime,
    const char *subject,
    const char *const *arguments,
    size_t argument_count,
    const wsh_runtime_redirection *redirections,
    size_t redirection_count,
    unsigned flags,
    uint32_t timeout,
    wsh_value **out_output,
    wsh_status_list **out_status)
{
    wsh_value *argument_value;
    wsh_runtime_command command;
    wsh_runtime_launch_plan plan;
    wsh_result result;

    argument_value = NULL;
    result = make_value(arguments, argument_count, &argument_value);
    memset(&command, 0, sizeof(command));
    memset(&plan, 0, sizeof(plan));
    command.subject = wsh_string_view_from_cstr(subject);
    command.arguments = argument_value;
    command.redirections = redirections;
    command.redirection_count = redirection_count;
    plan.commands = &command;
    plan.command_count = 1U;
    plan.flags = flags;
    plan.timeout_milliseconds = timeout;
    if (result == WSH_OK) {
        result = invoke_request(
            runtime,
            WSH_RUNTIME_LAUNCH,
            subject,
            argument_value,
            &plan,
            out_output,
            out_status);
    }
    wsh_value_destroy(argument_value);
    return result;
}

/** Verify direct launch and exact structured argument round trips. */
static int test_launch_arguments(test_runtime *runtime, const char *probe)
{
    const char *arguments[] = {
        "args", "", " ", "a\"b", "trail\\"
    };
    const char *expected =
        "00000000:\n"
        "00000001: \n"
        "00000003:a\"b\n"
        "00000006:trail\\\n";
    wsh_value *output;
    wsh_status_list *status;

    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime,
        probe,
        arguments,
        sizeof(arguments) / sizeof(arguments[0]),
        NULL,
        0U,
        WSH_RUNTIME_LAUNCH_CAPTURE,
        0U,
        &output,
        &status) == WSH_OK);
    CHECK(wsh_status_list_count(status) == 1U);
    CHECK(status_equals(status, 0U, 0U));
    CHECK(wsh_value_count(output) == 1U);
    CHECK(value_equals(output, 0U, expected));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    return 1;
}

/** Verify Unicode boundary acceptance and strict rejection. */
static int test_unicode(test_runtime *runtime, const char *probe)
{
    const char *arguments[] = {"raw", "snowman-\xe2\x98\x83"};
    const char invalid[] = {(char)0xc0, (char)0xaf, '\0'};
    wsh_string_view executable;
    uint16_t *units;
    size_t length;
    wsh_value *output;
    wsh_status_list *status;
    wsh_string_view captured;

    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, arguments, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    CHECK(wsh_value_at(output, 0U, &captured) == WSH_OK);
    CHECK(captured.length >= 11U);
    CHECK(memcmp(
        captured.data + captured.length - 11U,
        "snowman-\xe2\x98\x83",
        11U) == 0);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    executable.data = invalid;
    executable.length = 2U;
    units = NULL;
    CHECK(wsh_windows_runtime_serialize(
        runtime->windows, executable, NULL, &units, &length) ==
        WSH_ERR_ENCODING);
    return 1;
}

/** Verify ordered file/here redirections with exact bytes. */
static int test_redirection(test_runtime *runtime, const char *probe)
{
    char directory[MAX_PATH];
    char path[MAX_PATH];
    const char *emit_arguments[] = {"emit", "redirected"};
    const char *copy_arguments[] = {"copy"};
    wsh_runtime_redirection redirection;
    HANDLE file;
    char bytes[32];
    DWORD received;
    wsh_value *output;
    wsh_status_list *status;

    CHECK(GetTempPathA(sizeof(directory), directory) != 0U);
    CHECK(GetTempFileNameA(directory, "wsh", 0U, path) != 0U);
    memset(&redirection, 0, sizeof(redirection));
    redirection.kind = WSH_RUNTIME_REDIRECT_OUTPUT;
    redirection.target_descriptor = 1U;
    redirection.operand = wsh_string_view_from_cstr(path);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, emit_arguments, 2U, &redirection, 1U,
        0U, 0U, &output, &status) == WSH_OK);
    CHECK(status_equals(status, 0U, 0U));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    file = CreateFileA(
        path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    received = 0U;
    CHECK(ReadFile(file, bytes, sizeof(bytes), &received, NULL));
    CloseHandle(file);
    DeleteFileA(path);
    CHECK(received == 10U && memcmp(bytes, "redirected", 10U) == 0);

    redirection.kind = WSH_RUNTIME_REDIRECT_HERE;
    redirection.target_descriptor = 0U;
    redirection.operand = wsh_string_view_from_cstr("here\n");
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, copy_arguments, 1U, &redirection, 1U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    CHECK(value_equals(output, 0U, "here\r\n"));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    return 1;
}

/** Verify concurrent pipeline transfer and source-ordered statuses. */
static int test_pipeline(test_runtime *runtime, const char *probe)
{
    const char *left_items[] = {"emit", "pipeline", "7"};
    const char *right_items[] = {"copy", "3"};
    wsh_value *left_arguments;
    wsh_value *right_arguments;
    wsh_runtime_command commands[2];
    wsh_runtime_pipeline_edge edge;
    wsh_runtime_launch_plan plan;
    wsh_value *output;
    wsh_status_list *status;

    left_arguments = NULL;
    right_arguments = NULL;
    CHECK(make_value(left_items, 3U, &left_arguments) == WSH_OK);
    CHECK(make_value(right_items, 2U, &right_arguments) == WSH_OK);
    memset(commands, 0, sizeof(commands));
    commands[0].subject = wsh_string_view_from_cstr(probe);
    commands[0].arguments = left_arguments;
    commands[1].subject = wsh_string_view_from_cstr(probe);
    commands[1].arguments = right_arguments;
    edge.output_descriptor = 1U;
    edge.input_descriptor = 0U;
    memset(&plan, 0, sizeof(plan));
    plan.commands = commands;
    plan.command_count = 2U;
    plan.edges = &edge;
    plan.edge_count = 1U;
    plan.flags = WSH_RUNTIME_LAUNCH_CAPTURE;
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime, WSH_RUNTIME_PIPELINE, "pipeline", NULL, &plan,
        &output, &status) == WSH_OK);
    CHECK(value_equals(output, 0U, "pipeline"));
    CHECK(wsh_status_list_count(status) == 2U);
    CHECK(status_equals(status, 0U, 7U));
    CHECK(status_equals(status, 1U, 3U));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_value_destroy(left_arguments);
    wsh_value_destroy(right_arguments);
    return 1;
}

/** Verify exported scalar blocks and nested WSH envelope production. */
static int test_environment(test_runtime *runtime, const char *probe)
{
    const char *scalar[] = {"value-\xe2\x98\x83"};
    const char *stress_items[1];
    const char *arguments[] = {"env", "WSH_M5_VALUE"};
    wsh_value *value;
    wsh_value *stress_value;
    wsh_value *output;
    wsh_status_list *status;
    uint16_t *block;
    size_t block_length;
    size_t index;
    int found_envelope;
    wsh_allocator allocator;
    char stress_text[12001];

    value = NULL;
    stress_value = NULL;
    CHECK(make_value(scalar, 1U, &value) == WSH_OK);
    CHECK(wsh_context_set_variable(
        runtime->context,
        wsh_string_view_from_cstr("WSH_M5_VALUE"), value) == WSH_OK);
    CHECK(wsh_context_set_exported(
        runtime->context,
        wsh_string_view_from_cstr("WSH_M5_VALUE"), 1) == WSH_OK);
    CHECK(wsh_context_set_variable(
        runtime->context,
        wsh_string_view_from_cstr("wsh_m5_value"), value) == WSH_OK);
    CHECK(wsh_context_set_exported(
        runtime->context,
        wsh_string_view_from_cstr("wsh_m5_value"), 1) ==
        WSH_ERR_MISMATCH);
    CHECK(wsh_context_unset_variable(
        runtime->context,
        wsh_string_view_from_cstr("wsh_m5_value")) == WSH_OK);
    memset(stress_text, 'x', sizeof(stress_text) - 1U);
    stress_text[sizeof(stress_text) - 1U] = '\0';
    stress_items[0] = stress_text;
    CHECK(make_value(stress_items, 1U, &stress_value) == WSH_OK);
    CHECK(wsh_context_set_variable(
        runtime->context,
        wsh_string_view_from_cstr("WSH_CI_STRESS"),
        stress_value) == WSH_OK);
    CHECK(wsh_context_set_exported(
        runtime->context,
        wsh_string_view_from_cstr("WSH_CI_STRESS"),
        1) == WSH_OK);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, arguments, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    CHECK(value_equals(output, 0U, scalar[0]));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    block = NULL;
    CHECK(wsh_windows_runtime_environment_block(
        runtime->windows,
        runtime->context,
        1,
        &block,
        &block_length) == WSH_OK);
    found_envelope = 0;
    for (index = 0U; index + 11U < block_length; ++index) {
        if (memcmp(
                block + index,
                L"_WSH_ENV_V1",
                11U * sizeof(*block)) == 0) {
            found_envelope = 1;
            break;
        }
    }
    CHECK(found_envelope);
    allocator = wsh_allocator_default();
    wsh_allocator_release(&allocator, block);
    wsh_value_destroy(stress_value);
    wsh_value_destroy(value);
    return 1;
}

/** Verify raw policy, logical directory, and exact resolution. */
static int test_resolution_raw(test_runtime *runtime, const char *probe)
{
    wsh_string *resolved;
    wsh_value *output;
    wsh_status_list *status;
    wsh_runtime_command command;
    wsh_runtime_launch_plan plan;
    char raw[32768];
    wsh_windows_runtime_options options;
    test_runtime denied;
    char temporary[MAX_PATH];
    char associated[MAX_PATH];
    wsh_runtime_command associated_command;
    wsh_runtime_launch_plan associated_plan;

    resolved = NULL;
    CHECK(wsh_windows_runtime_resolve(
        runtime->windows,
        runtime->context,
        wsh_string_view_from_cstr(probe),
        &resolved) == WSH_OK);
    CHECK(wsh_string_bytes(resolved).length != 0U);
    wsh_string_destroy(resolved);
    CHECK(snprintf(raw, sizeof(raw), "\"%s\" raw marker", probe) > 0);
    memset(&command, 0, sizeof(command));
    command.subject = wsh_string_view_from_cstr(probe);
    command.raw = 1;
    command.raw_command_line = wsh_string_view_from_cstr(raw);
    memset(&plan, 0, sizeof(plan));
    plan.commands = &command;
    plan.command_count = 1U;
    plan.flags = WSH_RUNTIME_LAUNCH_CAPTURE;
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime, WSH_RUNTIME_LAUNCH, probe, NULL, &plan,
        &output, &status) == WSH_OK);
    CHECK(wsh_status_list_is_success(status));
    CHECK(wsh_value_count(output) == 1U);
    CHECK(value_equals(output, 0U, raw));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    CHECK(GetTempPathA(sizeof(temporary), temporary) != 0U);
    CHECK(GetTempFileNameA(temporary, "wsh", 0U, associated) != 0U);
    memset(&associated_command, 0, sizeof(associated_command));
    associated_command.subject = wsh_string_view_from_cstr(associated);
    memset(&associated_plan, 0, sizeof(associated_plan));
    associated_plan.commands = &associated_command;
    associated_plan.command_count = 1U;
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime,
        WSH_RUNTIME_LAUNCH,
        associated,
        NULL,
        &associated_plan,
        &output,
        &status) == WSH_OK);
    CHECK(!wsh_status_list_is_success(status));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    CHECK(DeleteFileA(associated));
    wsh_windows_runtime_options_init(&options);
    options.allow_raw_launch = 0;
    CHECK(create_runtime(&options, &denied) == WSH_OK);
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        &denied, WSH_RUNTIME_LAUNCH, probe, NULL, &plan,
        &output, &status) == WSH_OK);
    CHECK(!wsh_status_list_is_success(status));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    destroy_runtime(&denied);
    return 1;
}

/** Verify isolated logical cwd use by child launch and path resolution. */
static int test_working_directory(
    test_runtime *runtime,
    const char *probe)
{
    char temporary[MAX_PATH];
    char directory[MAX_PATH];
    char copied_probe[MAX_PATH];
    char exact_probe[MAX_PATH];
    char com_probe[MAX_PATH];
    char drive_relative[MAX_PATH];
    const char *directory_item[1];
    const char *arguments[] = {"cwd"};
    wsh_value *directory_value;
    wsh_value *output;
    wsh_status_list *status;
    wsh_string_view expected;
    char expected_bytes[MAX_PATH * 4];
    wsh_string *resolved;

    CHECK(GetTempPathA(sizeof(temporary), temporary) != 0U);
    CHECK(snprintf(
        directory,
        sizeof(directory),
        "%swsh-m5-cwd-%lu",
        temporary,
        (unsigned long)GetCurrentProcessId()) > 0);
    (void)RemoveDirectoryA(directory);
    CHECK(CreateDirectoryA(directory, NULL));
    directory_item[0] = directory;
    directory_value = NULL;
    CHECK(make_value(directory_item, 1U, &directory_value) == WSH_OK);
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime,
        WSH_RUNTIME_WORKING_DIRECTORY,
        "cd",
        directory_value,
        NULL,
        &output,
        &status) == WSH_OK);
    CHECK(wsh_value_at(output, 0U, &expected) == WSH_OK);
    CHECK(expected.length < sizeof(expected_bytes));
    memcpy(expected_bytes, expected.data, expected.length);
    expected_bytes[expected.length] = '\0';
    wsh_value_destroy(directory_value);
    wsh_value_destroy(output);
    wsh_status_list_destroy(status);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, arguments, 1U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    CHECK(value_equals(output, 0U, expected_bytes));
    wsh_value_destroy(output);
    wsh_status_list_destroy(status);
    CHECK(snprintf(
        copied_probe,
        sizeof(copied_probe),
        "%s\\runtime-probe.exe",
        directory) > 0);
    CHECK(CopyFileA(probe, copied_probe, FALSE));
    CHECK(snprintf(
        exact_probe,
        sizeof(exact_probe),
        "%s\\runtime-probe",
        directory) > 0);
    CHECK(snprintf(
        com_probe,
        sizeof(com_probe),
        "%s\\runtime-probe.com",
        directory) > 0);
    CHECK(snprintf(
        drive_relative,
        sizeof(drive_relative),
        "%c:runtime-probe",
        directory[0]) > 0);
    resolved = NULL;
    CHECK(CopyFileA(probe, exact_probe, FALSE));
    CHECK(wsh_windows_runtime_resolve(
        runtime->windows,
        runtime->context,
        wsh_string_view_from_cstr(drive_relative),
        &resolved) == WSH_OK);
    CHECK(wsh_string_view_equal(
        wsh_string_bytes(resolved),
        wsh_string_view_from_cstr(exact_probe)));
    wsh_string_destroy(resolved);
    CHECK(DeleteFileA(exact_probe));
    resolved = NULL;
    CHECK(wsh_windows_runtime_resolve(
        runtime->windows,
        runtime->context,
        wsh_string_view_from_cstr(drive_relative),
        &resolved) == WSH_OK);
    CHECK(wsh_string_view_equal(
        wsh_string_bytes(resolved),
        wsh_string_view_from_cstr(copied_probe)));
    wsh_string_destroy(resolved);
    CHECK(DeleteFileA(copied_probe));
    CHECK(CopyFileA(probe, com_probe, FALSE));
    resolved = NULL;
    CHECK(wsh_windows_runtime_resolve(
        runtime->windows,
        runtime->context,
        wsh_string_view_from_cstr(drive_relative),
        &resolved) == WSH_OK);
    CHECK(wsh_string_view_equal(
        wsh_string_bytes(resolved),
        wsh_string_view_from_cstr(com_probe)));
    wsh_string_destroy(resolved);
    CHECK(DeleteFileA(com_probe));
    CHECK(RemoveDirectoryA(directory));
    return 1;
}

/** Verify safe-path omission of the logical current directory. */
static int test_safe_path(const char *probe)
{
    wsh_windows_runtime_options options;
    test_runtime runtime;
    const char *basename;
    const char *separator;
    wsh_string *resolved;
    wsh_result result;

    separator = strrchr(probe, '\\');
    basename = separator == NULL ? probe : separator + 1;
    wsh_windows_runtime_options_init(&options);
    options.safe_path = 1;
    CHECK(create_runtime(&options, &runtime) == WSH_OK);
    resolved = NULL;
    result = wsh_windows_runtime_resolve(
        runtime.windows,
        runtime.context,
        wsh_string_view_from_cstr(basename),
        &resolved);
    wsh_string_destroy(resolved);
    destroy_runtime(&runtime);
    CHECK(result == WSH_ERR_MISMATCH);
    return 1;
}

/** Verify background wait, timeout, cancellation, and fallback metadata. */
static int test_jobs(test_runtime *runtime, const char *probe)
{
    const char *quick[] = {"sleep", "10", "6"};
    const char *slow[] = {"sleep", "500"};
    wsh_value *output;
    wsh_status_list *status;
    wsh_value *empty;

    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, quick, 3U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_BACKGROUND, 0U, &output, &status) == WSH_OK);
    CHECK(wsh_value_count(output) == 1U);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    empty = NULL;
    CHECK(make_value(NULL, 0U, &empty) == WSH_OK);
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime, WSH_RUNTIME_WAIT, "wait", empty, NULL,
        &output, &status) == WSH_OK);
    CHECK(status_equals(status, 0U, 6U));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_value_destroy(empty);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, slow, 2U, NULL, 0U, 0U, 10U,
        &output, &status) == WSH_OK);
    CHECK(status_equals(status, 0U, 9U));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, slow, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_BACKGROUND, 0U, &output, &status) == WSH_OK);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    empty = NULL;
    CHECK(make_value(NULL, 0U, &empty) == WSH_OK);
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime, WSH_RUNTIME_CANCEL, "cancel", empty, NULL,
        &output, &status) == WSH_OK);
    CHECK(status_equals(status, 0U, 130U));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_value_destroy(empty);
    return 1;
}

/** Verify one read-direction named-pipe provider exchange. */
static int test_process_substitution(
    test_runtime *runtime,
    const char *probe)
{
    const char *items[] = {"emit", "substitution"};
    const char *noop[] = {"emit", ""};
    wsh_value *arguments;
    wsh_runtime_command command;
    wsh_runtime_launch_plan plan;
    wsh_value *output;
    wsh_status_list *status;
    wsh_string_view path;
    wsh_allocator allocator;
    uint16_t *wide;
    size_t wide_length;
    HANDLE peer;
    char bytes[32];
    DWORD received;
    DWORD written;
    const char *copy_items[] = {"copy"};
    wsh_runtime_redirection redirection;
    char temporary[MAX_PATH];
    char output_path[MAX_PATH];
    HANDLE file;

    arguments = NULL;
    CHECK(make_value(items, 2U, &arguments) == WSH_OK);
    memset(&command, 0, sizeof(command));
    command.subject = wsh_string_view_from_cstr(probe);
    command.arguments = arguments;
    memset(&plan, 0, sizeof(plan));
    plan.commands = &command;
    plan.command_count = 1U;
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime,
        WSH_RUNTIME_PROCESS_SUBSTITUTION,
        "read",
        NULL,
        &plan,
        &output,
        &status) == WSH_OK);
    CHECK(wsh_value_at(output, 0U, &path) == WSH_OK);
    allocator = wsh_allocator_default();
    wide = NULL;
    CHECK(wsh_utf8_to_utf16(
        &allocator, NULL, path, &wide, &wide_length) == WSH_OK);
    peer = CreateFileW(
        (LPCWSTR)wide,
        GENERIC_READ,
        0U,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    CHECK(peer != INVALID_HANDLE_VALUE);
    received = 0U;
    CHECK(ReadFile(peer, bytes, sizeof(bytes), &received, NULL));
    CHECK(received == 12U && memcmp(bytes, "substitution", 12U) == 0);
    CloseHandle(peer);
    wsh_allocator_release(&allocator, wide);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_value_destroy(arguments);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, noop, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);

    CHECK(GetTempPathA(sizeof(temporary), temporary) != 0U);
    CHECK(GetTempFileNameA(temporary, "wsh", 0U, output_path) != 0U);
    arguments = NULL;
    CHECK(make_value(copy_items, 1U, &arguments) == WSH_OK);
    memset(&redirection, 0, sizeof(redirection));
    redirection.kind = WSH_RUNTIME_REDIRECT_OUTPUT;
    redirection.target_descriptor = 1U;
    redirection.operand = wsh_string_view_from_cstr(output_path);
    command.arguments = arguments;
    command.redirections = &redirection;
    command.redirection_count = 1U;
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime,
        WSH_RUNTIME_PROCESS_SUBSTITUTION,
        "write",
        NULL,
        &plan,
        &output,
        &status) == WSH_OK);
    CHECK(wsh_value_at(output, 0U, &path) == WSH_OK);
    wide = NULL;
    CHECK(wsh_utf8_to_utf16(
        &allocator, NULL, path, &wide, &wide_length) == WSH_OK);
    peer = CreateFileW(
        (LPCWSTR)wide,
        GENERIC_WRITE,
        0U,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    CHECK(peer != INVALID_HANDLE_VALUE);
    written = 0U;
    CHECK(WriteFile(peer, "write-path", 10U, &written, NULL));
    CHECK(written == 10U);
    CloseHandle(peer);
    wsh_allocator_release(&allocator, wide);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_value_destroy(arguments);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, noop, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    file = CreateFileA(
        output_path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    received = 0U;
    CHECK(ReadFile(file, bytes, sizeof(bytes), &received, NULL));
    CloseHandle(file);
    CHECK(DeleteFileA(output_path));
    CHECK(received == 10U && memcmp(bytes, "write-path", 10U) == 0);

    arguments = NULL;
    CHECK(make_value(copy_items, 1U, &arguments) == WSH_OK);
    command.arguments = arguments;
    command.redirections = NULL;
    command.redirection_count = 0U;
    output = NULL;
    status = NULL;
    CHECK(invoke_request(
        runtime,
        WSH_RUNTIME_PROCESS_SUBSTITUTION,
        "duplex",
        NULL,
        &plan,
        &output,
        &status) == WSH_OK);
    CHECK(wsh_value_at(output, 0U, &path) == WSH_OK);
    wide = NULL;
    CHECK(wsh_utf8_to_utf16(
        &allocator, NULL, path, &wide, &wide_length) == WSH_OK);
    peer = CreateFileW(
        (LPCWSTR)wide,
        GENERIC_READ | GENERIC_WRITE,
        0U,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    CHECK(peer != INVALID_HANDLE_VALUE);
    written = 0U;
    CHECK(WriteFile(peer, "duplex", 6U, &written, NULL));
    CHECK(written == 6U);
    CHECK(FlushFileBuffers(peer));
    received = 0U;
    CHECK(ReadFile(peer, bytes, 6U, &received, NULL));
    CHECK(received == 6U && memcmp(bytes, "duplex", 6U) == 0);
    CloseHandle(peer);
    wsh_allocator_release(&allocator, wide);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_value_destroy(arguments);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, noop, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    return 1;
}

/** Verify failure cleanup and bounded capture behavior. */
static int test_failure_limits(test_runtime *runtime, const char *probe)
{
    typedef WINBOOL (WINAPI *get_handle_count_fn)(HANDLE, PDWORD);
    const char *items[] = {"emit", "captured"};
    const char *invalid_items[] = {"invalid"};
    size_t index;
    wsh_value *output;
    wsh_status_list *status;
    wsh_windows_runtime_options options;
    test_runtime limited;
    char *oversized;
    wsh_string_view oversized_view;
    uint16_t *units;
    size_t unit_count;
    get_handle_count_fn get_handle_count;
    DWORD handles_before;
    DWORD handles_after;

    get_handle_count = (get_handle_count_fn)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "GetProcessHandleCount");
    handles_before = 0U;
    if (get_handle_count != NULL) {
        CHECK(get_handle_count(GetCurrentProcess(), &handles_before));
    }

    for (index = 0U; index < 20U; ++index) {
        output = NULL;
        status = NULL;
        CHECK(launch_one(
            runtime,
            "wsh-m5-certainly-absent-command",
            NULL,
            0U,
            NULL,
            0U,
            0U,
            0U,
            &output,
            &status) == WSH_OK);
        CHECK(!wsh_status_list_is_success(status));
        wsh_status_list_destroy(status);
        wsh_value_destroy(output);
    }
    if (get_handle_count != NULL) {
        CHECK(get_handle_count(GetCurrentProcess(), &handles_after));
        CHECK(handles_after <= handles_before + 1U);
    }
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, items, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) == WSH_OK);
    CHECK(value_equals(output, 0U, "captured"));
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    oversized = (char *)malloc(32768U);
    CHECK(oversized != NULL);
    memset(oversized, 'a', 32767U);
    oversized[32767] = '\0';
    oversized_view.data = oversized;
    oversized_view.length = 32767U;
    units = NULL;
    CHECK(wsh_windows_runtime_serialize(
        runtime->windows,
        oversized_view,
        NULL,
        &units,
        &unit_count) == WSH_ERR_RESOURCE);
    free(oversized);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        runtime, probe, invalid_items, 1U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) ==
        WSH_ERR_ENCODING);
    CHECK(output == NULL && status == NULL);
    wsh_windows_runtime_options_init(&options);
    options.max_capture_bytes = 2U;
    CHECK(create_runtime(&options, &limited) == WSH_OK);
    output = NULL;
    status = NULL;
    CHECK(launch_one(
        &limited, probe, items, 2U, NULL, 0U,
        WSH_RUNTIME_LAUNCH_CAPTURE, 0U, &output, &status) ==
        WSH_ERR_RESOURCE);
    CHECK(output == NULL && status == NULL);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    destroy_runtime(&limited);
    return 1;
}

/** Verify the forced reviewed legacy containment and inheritance paths. */
static int test_fallback(const char *probe)
{
    wsh_windows_runtime_options options;
    wsh_windows_runtime_capabilities capabilities;
    test_runtime runtime;
    int result;

    wsh_windows_runtime_options_init(&options);
    options.force_legacy_inheritance = 1;
    options.force_tracked_fallback = 1;
    CHECK(create_runtime(&options, &runtime) == WSH_OK);
    CHECK(wsh_windows_runtime_get_capabilities(
        runtime.windows, &capabilities) == WSH_OK);
    CHECK(!capabilities.explicit_handle_list);
    CHECK(!capabilities.job_object);
    result = test_launch_arguments(&runtime, probe) &&
        test_jobs(&runtime, probe);
    destroy_runtime(&runtime);
    return result;
}

/** Dispatch one controlled M5 test ID to its native checks. */
static int dispatch_test(
    test_runtime *runtime,
    const char *probe,
    const char *identifier)
{
    if (strcmp(identifier, "TC-0011") == 0 ||
        strcmp(identifier, "TC-0013") == 0) {
        return test_launch_arguments(runtime, probe);
    }
    if (strcmp(identifier, "TC-0015") == 0) {
        return test_redirection(runtime, probe);
    }
    if (strcmp(identifier, "TC-0016") == 0) {
        return test_pipeline(runtime, probe);
    }
    if (strcmp(identifier, "TC-0018") == 0) {
        return test_unicode(runtime, probe);
    }
    if (strcmp(identifier, "TC-0039") == 0) {
        return test_working_directory(runtime, probe);
    }
    if (strcmp(identifier, "TC-0042") == 0 ||
        strcmp(identifier, "TC-0045") == 0) {
        return test_resolution_raw(runtime, probe);
    }
    if (strcmp(identifier, "TC-0043") == 0 ||
        strcmp(identifier, "TC-0047") == 0) {
        return test_environment(runtime, probe);
    }
    if (strcmp(identifier, "TC-0050") == 0) {
        return test_jobs(runtime, probe);
    }
    if (strcmp(identifier, "TC-0051") == 0) {
        return test_process_substitution(runtime, probe);
    }
    if (strcmp(identifier, "TC-0024") == 0 ||
        strcmp(identifier, "TC-0052") == 0 ||
        strcmp(identifier, "TC-0074") == 0) {
        return test_failure_limits(runtime, probe);
    }
    if (strcmp(identifier, "TC-0070") == 0) {
        return test_launch_arguments(runtime, probe) &&
            test_environment(runtime, probe);
    }
    if (strcmp(identifier, "TC-0012") == 0) {
        return test_safe_path(probe) &&
            test_resolution_raw(runtime, probe);
    }
    if (strcmp(identifier, "TC-0075") == 0) {
        return test_fallback(probe);
    }
    fprintf(stderr, "unknown test case: %s\n", identifier);
    return 0;
}

/** Run one selected controlled native test. */
int main(int argc, char **argv)
{
    test_runtime runtime;
    int passed;

    if (argc != 3) {
        fprintf(stderr, "usage: windows-runtime-tests TC-NNNN probe.exe\n");
        return 2;
    }
    test_case = argv[1];
    if (create_runtime(NULL, &runtime) != WSH_OK) {
        fprintf(stderr, "%s: runtime creation failed\n", test_case);
        return 1;
    }
    passed = dispatch_test(&runtime, argv[2], argv[1]);
    destroy_runtime(&runtime);
    if (!passed) {
        return 1;
    }
    printf("%s passed\n", test_case);
    return 0;
}
