/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file windows_runtime.c
 * @brief Bounded Win32 process, environment, pipe, and job orchestration.
 */

#include "wsh/windows_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/** Maximum command line accepted by CreateProcessW, including its NUL. */
#define WSH_WINDOWS_COMMAND_UNITS 32767U
/** Guaranteed logical descriptor range. */
#define WSH_WINDOWS_DESCRIPTORS 10U
/** Private environment-envelope variable name. */
#define WSH_WINDOWS_ENVELOPE "_WSH_ENV_V1"
/** Parent nonce variable paired with the private envelope. */
#define WSH_WINDOWS_PARENT_NONCE "_WSH_PARENT_NONCE"
/** Private environment variable carrying inherited logical handles. */
#define WSH_WINDOWS_DESCRIPTOR_MAP "_WSH_FD_MAP_V1"
/** Shell-generated process error status. */
#define WSH_WINDOWS_LAUNCH_STATUS 8U
/** Shell-generated timeout status. */
#define WSH_WINDOWS_TIMEOUT_STATUS 9U
/** Shell-generated interruption status. */
#define WSH_WINDOWS_CANCEL_STATUS 130U

#ifndef FILE_SHARE_DELETE
/** Old-SDK value for delete-sharing access. */
#define FILE_SHARE_DELETE 0x00000004U
#endif

#ifndef PIPE_REJECT_REMOTE_CLIENTS
/** Old-SDK value that limits a named pipe to local peers. */
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008U
#endif

#ifndef FILE_FLAG_FIRST_PIPE_INSTANCE
/** Old-SDK value that rejects a preexisting named-pipe instance. */
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000U
#endif

#ifndef EXTENDED_STARTUPINFO_PRESENT
/** Old-SDK process-creation flag for STARTUPINFOEXW. */
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000U
#endif

#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
/** Old-SDK attribute identifier for an explicit handle list. */
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002U
#endif

/** Opaque process-thread attribute list used by optional modern APIs. */
typedef void *wsh_proc_thread_attribute_list;

/** Locally declared STARTUPINFOEXW for old SDK header compatibility. */
typedef struct wsh_startup_info_ex_w {
    /** Required ordinary startup information prefix. */
    STARTUPINFOW startup;
    /** Optional process-thread attribute list. */
    wsh_proc_thread_attribute_list attributes;
} wsh_startup_info_ex_w;

/** Optional InitializeProcThreadAttributeList signature. */
typedef WINBOOL (WINAPI *wsh_initialize_attribute_list_fn)(
    wsh_proc_thread_attribute_list,
    DWORD,
    DWORD,
    SIZE_T *);

/** Optional UpdateProcThreadAttribute signature. */
typedef WINBOOL (WINAPI *wsh_update_attribute_fn)(
    wsh_proc_thread_attribute_list,
    DWORD,
    DWORD_PTR,
    void *,
    SIZE_T,
    void *,
    SIZE_T *);

/** Optional DeleteProcThreadAttributeList signature. */
typedef void (WINAPI *wsh_delete_attribute_list_fn)(
    wsh_proc_thread_attribute_list);

/** Optional CompareStringOrdinal signature. */
typedef int (WINAPI *wsh_compare_ordinal_fn)(
    LPCWSTR,
    int,
    LPCWSTR,
    int,
    WINBOOL);

/** Optional CreateJobObjectW signature for compact import libraries. */
typedef HANDLE (WINAPI *wsh_create_job_fn)(
    LPSECURITY_ATTRIBUTES,
    LPCWSTR);

/** Optional SetInformationJobObject signature. */
typedef WINBOOL (WINAPI *wsh_set_job_information_fn)(
    HANDLE,
    JOBOBJECTINFOCLASS,
    LPVOID,
    DWORD);

/** Optional AssignProcessToJobObject signature. */
typedef WINBOOL (WINAPI *wsh_assign_process_job_fn)(HANDLE, HANDLE);

/** Optional TerminateJobObject signature. */
typedef WINBOOL (WINAPI *wsh_terminate_job_fn)(HANDLE, UINT);

/** Growable allocator-backed UTF-16 buffer. */
typedef struct wsh_wide_buffer {
    /** Runtime allocator. */
    wsh_allocator allocator;
    /** Owned units. */
    uint16_t *units;
    /** Initialized unit count. */
    size_t length;
    /** Allocated unit count. */
    size_t capacity;
    /** Hard unit ceiling. */
    size_t maximum;
} wsh_wide_buffer;

/** One converted exported environment entry. */
typedef struct wsh_environment_entry {
    /** Owned UTF-16 name. */
    uint16_t *name;
    /** Name units excluding NUL. */
    size_t name_length;
    /** Owned UTF-16 value. */
    uint16_t *value;
    /** Value units excluding NUL. */
    size_t value_length;
} wsh_environment_entry;

/** One launched process group retained for foreground or background work. */
typedef struct wsh_windows_group {
    /** Owned process handles in source order. */
    HANDLE *processes;
    /** Owned process identifiers in source order. */
    DWORD *identifiers;
    /** Number of initialized process entries. */
    size_t process_count;
    /** Allocated process slots. */
    size_t process_capacity;
    /** Optional owned kill-on-close job. */
    HANDLE job;
    /** Root identifier exposed through `$apid`. */
    DWORD root_identifier;
    /** Nonzero after cancellation determines synthetic status. */
    int cancelled;
    /** Synthetic status after timeout or explicit cancellation. */
    uint32_t cancellation_status;
} wsh_windows_group;

/** State passed to the concurrent capture reader. */
typedef struct wsh_capture_state {
    /** Borrowed runtime until reader thread joins. */
    struct wsh_windows_runtime *runtime;
    /** Owned read pipe handle. */
    HANDLE handle;
    /** Owned captured byte buffer. */
    char *bytes;
    /** Captured byte count. */
    size_t length;
    /** Allocated byte count. */
    size_t capacity;
    /** Nonzero after a limit, allocation, or read failure. */
    int failed;
} wsh_capture_state;

/** One suspended named-pipe provider and its connection worker. */
typedef struct wsh_windows_substitution {
    /** Borrowed owner until the worker is joined. */
    struct wsh_windows_runtime *runtime;
    /** Owned local named-pipe server endpoint. */
    HANDLE pipe;
    /** Owned connector/collector thread. */
    HANDLE worker;
    /** Owned suspended primary process thread. */
    HANDLE process_thread;
    /** Owned provider process group. */
    wsh_windows_group *group;
    /** Owned zero-terminated pipe name. */
    uint16_t *path;
    /** Client access used to unblock an unopened provider during cleanup. */
    DWORD client_access;
} wsh_windows_substitution;

/** One fully prepared stage and its logical descriptor table. */
typedef struct wsh_windows_stage {
    /** Owned UTF-8 resolved executable path. */
    wsh_string *resolved;
    /** Owned separately resolved UTF-16 application name. */
    uint16_t *application;
    /** Application-name units excluding NUL. */
    size_t application_length;
    /** Owned mutable structured or raw command line. */
    uint16_t *command_line;
    /** Command-line units excluding NUL. */
    size_t command_line_length;
    /** Owned explicit double-NUL environment block. */
    uint16_t *environment;
    /** Environment units including the final double NUL. */
    size_t environment_length;
    /** Current borrowed or owned logical descriptor handles. */
    HANDLE descriptors[WSH_WINDOWS_DESCRIPTORS];
    /** Nonzero for descriptor handles owned by this stage. */
    int descriptor_owned[WSH_WINDOWS_DESCRIPTORS];
    /** Process information after successful suspended creation. */
    PROCESS_INFORMATION process;
} wsh_windows_stage;

/** Process-wide lock protecting temporary inheritable handle copies. */
static volatile LONG wsh_windows_inheritance_lock = 0;

/** Concrete isolated Windows runtime. */
struct wsh_windows_runtime {
    /** Copied finite options. */
    wsh_windows_runtime_options options;
    /** Owned logical UTF-16 working directory. */
    uint16_t *working_directory;
    /** Working-directory units excluding NUL. */
    size_t working_directory_length;
    /** Owned captured per-drive logical current directories. */
    uint16_t *drive_directories[26];
    /** Unit counts for captured per-drive directories. */
    size_t drive_directory_lengths[26];
    /** Owned absolute current WSH executable path. */
    uint16_t *executable_path;
    /** Executable-path units excluding NUL. */
    size_t executable_path_length;
    /** Borrowed inherited logical descriptors for nested WSH launches. */
    HANDLE base_descriptors[WSH_WINDOWS_DESCRIPTORS];
    /** Process-wide serialized legacy inheritance lock. */
    CRITICAL_SECTION launch_lock;
    /** Nonzero after launch_lock initialization. */
    int launch_lock_initialized;
    /** Optional modern attribute initializer. */
    wsh_initialize_attribute_list_fn initialize_attributes;
    /** Optional modern attribute updater. */
    wsh_update_attribute_fn update_attribute;
    /** Optional modern attribute destructor. */
    wsh_delete_attribute_list_fn delete_attributes;
    /** Optional locale-independent Windows ordinal comparator. */
    wsh_compare_ordinal_fn compare_ordinal;
    /** Optional job-object constructor. */
    wsh_create_job_fn create_job;
    /** Optional job-object limit setter. */
    wsh_set_job_information_fn set_job_information;
    /** Optional job-object process assignment. */
    wsh_assign_process_job_fn assign_process_job;
    /** Optional job-object termination operation. */
    wsh_terminate_job_fn terminate_job;
    /** Observable capabilities and selected fallback. */
    wsh_windows_runtime_capabilities capabilities;
    /** Owned outstanding background groups in launch order. */
    wsh_windows_group **groups;
    /** Number of retained background groups. */
    size_t group_count;
    /** Allocated group slots. */
    size_t group_capacity;
    /** Instance nonce used only to correlate a nested envelope pair. */
    uint64_t nonce;
    /** Monotonic local named-pipe sequence. */
    LONG pipe_sequence;
    /** Owned pending named-pipe provider records. */
    wsh_windows_substitution **substitutions;
    /** Number of pending provider records. */
    size_t substitution_count;
    /** Allocated provider record slots. */
    size_t substitution_capacity;
};

/** Cancel or collect every registered named-pipe provider. */
static void wsh_windows_collect_substitutions(
    wsh_windows_runtime *runtime,
    int cancel);

/** Append one shell-generated process status. */
static wsh_result wsh_windows_append_status(
    wsh_status_builder *status,
    uint32_t code);

/** Return whether multiplication fits in size_t. */
static int wsh_windows_multiply(
    size_t left,
    size_t right,
    size_t *out_value)
{
    if (out_value == NULL ||
        (left != 0U && right > (size_t)-1 / left)) {
        return 0;
    }
    *out_value = left * right;
    return 1;
}

/** Increment one shared 32-bit sequence without a kernel32 import. */
static LONG wsh_windows_increment(volatile LONG *value)
{
#if defined(__aarch64__)
    LONG observed;
    LONG desired;
    LONG actual;

    observed = *value;
    do {
        desired = (LONG)((DWORD)observed + 1UL);
        actual = InterlockedCompareExchange(value, desired, observed);
        if (actual == observed) {
            return desired;
        }
        observed = actual;
    } while (1);
#else
    return InterlockedIncrement(value);
#endif
}

/** Allocate and zero runtime-owned memory. */
static void *wsh_windows_allocate(
    const wsh_windows_runtime *runtime,
    size_t size)
{
    void *result;

    result = runtime->options.allocator.allocate(
        runtime->options.allocator.user_data,
        size == 0U ? 1U : size);
    if (result != NULL) {
        memset(result, 0, size);
    }
    return result;
}

/** Release runtime-owned memory. */
static void wsh_windows_release(
    const wsh_windows_runtime *runtime,
    void *pointer)
{
    if (pointer != NULL) {
        runtime->options.allocator.deallocate(
            runtime->options.allocator.user_data,
            pointer);
    }
}

/** Copy one zero-terminated UTF-16 path into runtime ownership. */
static wsh_result wsh_windows_copy_wide_path(
    const wsh_windows_runtime *runtime,
    const uint16_t *path,
    size_t length,
    uint16_t **out_path)
{
    size_t bytes;

    *out_path = NULL;
    if (!wsh_windows_multiply(
            length + 1U, sizeof(**out_path), &bytes)) {
        return WSH_ERR_RESOURCE;
    }
    *out_path = (uint16_t *)wsh_windows_allocate(runtime, bytes);
    if (*out_path == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memcpy(*out_path, path, length * sizeof(**out_path));
    (*out_path)[length] = 0U;
    return WSH_OK;
}

/** Grow an allocator-backed array without publishing partial state. */
static wsh_result wsh_windows_grow(
    const wsh_windows_runtime *runtime,
    void **array,
    size_t item_size,
    size_t count,
    size_t *capacity,
    size_t required,
    size_t maximum)
{
    size_t next;
    size_t old_bytes;
    size_t new_bytes;
    void *replacement;

    if (runtime == NULL || array == NULL || capacity == NULL ||
        item_size == 0U || required > maximum) {
        return required > maximum ? WSH_ERR_RESOURCE : WSH_ERR_INVALID;
    }
    if (required <= *capacity) {
        return WSH_OK;
    }
    next = *capacity == 0U ? 4U : *capacity;
    if (next > maximum) {
        next = maximum;
    }
    while (next < required) {
        next = next > maximum / 2U ? maximum : next * 2U;
    }
    if (!wsh_windows_multiply(count, item_size, &old_bytes) ||
        !wsh_windows_multiply(next, item_size, &new_bytes)) {
        return WSH_ERR_RESOURCE;
    }
    replacement = wsh_windows_allocate(runtime, new_bytes);
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (*array != NULL && old_bytes != 0U) {
        memcpy(replacement, *array, old_bytes);
    }
    wsh_windows_release(runtime, *array);
    *array = replacement;
    *capacity = next;
    return WSH_OK;
}

/** Initialize one growable wide buffer. */
static void wsh_wide_buffer_init(
    const wsh_windows_runtime *runtime,
    wsh_wide_buffer *buffer,
    size_t maximum)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->allocator = runtime->options.allocator;
    buffer->maximum = maximum;
}

/** Destroy one growable wide buffer. */
static void wsh_wide_buffer_destroy(wsh_wide_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }
    buffer->allocator.deallocate(
        buffer->allocator.user_data,
        buffer->units);
    memset(buffer, 0, sizeof(*buffer));
}

/** Ensure one wide buffer has space for required initialized units. */
static wsh_result wsh_wide_buffer_reserve(
    wsh_wide_buffer *buffer,
    size_t required)
{
    size_t next;
    size_t bytes;
    uint16_t *replacement;

    if (buffer == NULL || required > buffer->maximum) {
        return required > buffer->maximum ?
            WSH_ERR_RESOURCE : WSH_ERR_INVALID;
    }
    if (required <= buffer->capacity) {
        return WSH_OK;
    }
    next = buffer->capacity == 0U ? 32U : buffer->capacity;
    if (next > buffer->maximum) {
        next = buffer->maximum;
    }
    while (next < required) {
        next = next > buffer->maximum / 2U ?
            buffer->maximum : next * 2U;
    }
    if (!wsh_windows_multiply(next, sizeof(*replacement), &bytes)) {
        return WSH_ERR_RESOURCE;
    }
    replacement = (uint16_t *)buffer->allocator.allocate(
        buffer->allocator.user_data,
        bytes == 0U ? 1U : bytes);
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (buffer->length != 0U) {
        memcpy(
            replacement,
            buffer->units,
            buffer->length * sizeof(*replacement));
    }
    buffer->allocator.deallocate(
        buffer->allocator.user_data,
        buffer->units);
    buffer->units = replacement;
    buffer->capacity = next;
    return WSH_OK;
}

/** Append repeated UTF-16 units to one wide buffer. */
static wsh_result wsh_wide_buffer_repeat(
    wsh_wide_buffer *buffer,
    uint16_t unit,
    size_t count)
{
    size_t required;
    wsh_result result;

    if (buffer == NULL || count > (size_t)-1 - buffer->length) {
        return WSH_ERR_RESOURCE;
    }
    required = buffer->length + count;
    result = wsh_wide_buffer_reserve(buffer, required);
    while (result == WSH_OK && count != 0U) {
        buffer->units[buffer->length++] = unit;
        count -= 1U;
    }
    return result;
}

/** Append a UTF-16 slice to one wide buffer. */
static wsh_result wsh_wide_buffer_append(
    wsh_wide_buffer *buffer,
    const uint16_t *units,
    size_t length)
{
    size_t required;
    wsh_result result;

    if (buffer == NULL || (units == NULL && length != 0U) ||
        length > (size_t)-1 - buffer->length) {
        return WSH_ERR_INVALID;
    }
    required = buffer->length + length;
    result = wsh_wide_buffer_reserve(buffer, required);
    if (result == WSH_OK && length != 0U) {
        memcpy(
            buffer->units + buffer->length,
            units,
            length * sizeof(*units));
        buffer->length = required;
    }
    return result;
}

/** Publish one buffer and reset its mutable owner. */
static wsh_result wsh_wide_buffer_finish(
    wsh_wide_buffer *buffer,
    uint16_t **out_units,
    size_t *out_length)
{
    wsh_result result;

    if (buffer == NULL || out_units == NULL || out_length == NULL) {
        return WSH_ERR_INVALID;
    }
    result = wsh_wide_buffer_reserve(buffer, buffer->length + 1U);
    if (result != WSH_OK) {
        return result;
    }
    buffer->units[buffer->length] = 0U;
    *out_units = buffer->units;
    *out_length = buffer->length;
    buffer->units = NULL;
    buffer->length = 0U;
    buffer->capacity = 0U;
    return WSH_OK;
}

/** Return whether an argument needs Microsoft C-runtime quotation. */
static int wsh_argument_needs_quotes(
    const uint16_t *units,
    size_t length)
{
    size_t index;

    if (length == 0U) {
        return 1;
    }
    for (index = 0U; index < length; ++index) {
        if (units[index] == (uint16_t)' ' ||
            units[index] == (uint16_t)'\t' ||
            units[index] == (uint16_t)'"') {
            return 1;
        }
    }
    return 0;
}

/** Append one Microsoft C-runtime compatible serialized argument. */
static wsh_result wsh_serialize_argument(
    wsh_wide_buffer *buffer,
    const uint16_t *units,
    size_t length)
{
    size_t index;
    size_t slashes;
    wsh_result result;

    if (!wsh_argument_needs_quotes(units, length)) {
        return wsh_wide_buffer_append(buffer, units, length);
    }
    result = wsh_wide_buffer_repeat(buffer, (uint16_t)'"', 1U);
    index = 0U;
    while (result == WSH_OK && index < length) {
        slashes = 0U;
        while (index < length && units[index] == (uint16_t)'\\') {
            slashes += 1U;
            index += 1U;
        }
        if (index == length) {
            result = wsh_wide_buffer_repeat(
                buffer, (uint16_t)'\\', slashes * 2U);
            break;
        }
        if (units[index] == (uint16_t)'"') {
            result = wsh_wide_buffer_repeat(
                buffer, (uint16_t)'\\', slashes * 2U + 1U);
        } else {
            result = wsh_wide_buffer_repeat(
                buffer, (uint16_t)'\\', slashes);
        }
        if (result == WSH_OK) {
            result = wsh_wide_buffer_repeat(buffer, units[index], 1U);
        }
        index += 1U;
    }
    if (result == WSH_OK) {
        result = wsh_wide_buffer_repeat(buffer, (uint16_t)'"', 1U);
    }
    return result;
}

/** Convert one strict UTF-8 view through runtime allocation and limits. */
static wsh_result wsh_windows_to_wide(
    const wsh_windows_runtime *runtime,
    wsh_string_view text,
    uint16_t **out_units,
    size_t *out_length)
{
    return wsh_utf8_to_utf16(
        &runtime->options.allocator,
        &runtime->options.limits,
        text,
        out_units,
        out_length);
}

/** Convert one validated wide slice through runtime allocation and limits. */
static wsh_result wsh_windows_from_wide(
    const wsh_windows_runtime *runtime,
    const uint16_t *units,
    size_t length,
    char **out_bytes,
    size_t *out_length)
{
    return wsh_utf16_to_utf8(
        &runtime->options.allocator,
        &runtime->options.limits,
        units,
        length,
        out_bytes,
        out_length);
}

/** @brief Implements wsh_windows_runtime_serialize. */
wsh_result wsh_windows_runtime_serialize(
    const wsh_windows_runtime *runtime,
    wsh_string_view executable,
    const wsh_value *arguments,
    uint16_t **out_units,
    size_t *out_length)
{
    wsh_wide_buffer buffer;
    uint16_t *wide;
    size_t wide_length;
    size_t index;
    wsh_string_view argument;
    wsh_result result;

    if (runtime == NULL || out_units == NULL || out_length == NULL ||
        executable.data == NULL || executable.length == 0U) {
        return WSH_ERR_INVALID;
    }
    *out_units = NULL;
    *out_length = 0U;
    wsh_wide_buffer_init(
        runtime, &buffer, WSH_WINDOWS_COMMAND_UNITS - 1U);
    wide = NULL;
    result = wsh_windows_to_wide(
        runtime, executable, &wide, &wide_length);
    if (result == WSH_OK) {
        result = wsh_serialize_argument(&buffer, wide, wide_length);
    }
    wsh_windows_release(runtime, wide);
    for (index = 0U; result == WSH_OK &&
         index < wsh_value_count(arguments); ++index) {
        result = wsh_wide_buffer_repeat(&buffer, (uint16_t)' ', 1U);
        if (result == WSH_OK) {
            result = wsh_value_at(arguments, index, &argument);
        }
        wide = NULL;
        if (result == WSH_OK) {
            result = wsh_windows_to_wide(
                runtime, argument, &wide, &wide_length);
        }
        if (result == WSH_OK) {
            result = wsh_serialize_argument(&buffer, wide, wide_length);
        }
        wsh_windows_release(runtime, wide);
    }
    if (result == WSH_OK) {
        result = wsh_wide_buffer_finish(
            &buffer, out_units, out_length);
    }
    wsh_wide_buffer_destroy(&buffer);
    return result;
}

/** Copy one dynamically sized Win32 path-producing call result. */
static wsh_result wsh_windows_capture_directory(
    const wsh_windows_runtime *runtime,
    uint16_t **out_units,
    size_t *out_length)
{
    DWORD required;
    DWORD received;
    uint16_t *units;
    size_t bytes;

    required = GetCurrentDirectoryW(0U, NULL);
    if (required == 0U ||
        !wsh_windows_multiply(required, sizeof(*units), &bytes)) {
        return WSH_ERR_INTERNAL;
    }
    units = (uint16_t *)wsh_windows_allocate(runtime, bytes);
    if (units == NULL) {
        return WSH_ERR_RESOURCE;
    }
    received = GetCurrentDirectoryW(required, (LPWSTR)units);
    if (received == 0U || received >= required) {
        wsh_windows_release(runtime, units);
        return WSH_ERR_INTERNAL;
    }
    *out_units = units;
    *out_length = received;
    return WSH_OK;
}

/** Copy the current executable path with a dynamically grown buffer. */
static wsh_result wsh_windows_capture_executable(
    const wsh_windows_runtime *runtime,
    uint16_t **out_units,
    size_t *out_length)
{
    size_t capacity;
    size_t bytes;
    uint16_t *units;
    DWORD received;

    capacity = 260U;
    while (capacity <= runtime->options.limits.max_string_bytes) {
        if (!wsh_windows_multiply(capacity, sizeof(*units), &bytes)) {
            return WSH_ERR_RESOURCE;
        }
        units = (uint16_t *)wsh_windows_allocate(runtime, bytes);
        if (units == NULL) {
            return WSH_ERR_RESOURCE;
        }
        received = GetModuleFileNameW(NULL, (LPWSTR)units, (DWORD)capacity);
        if (received != 0U && received < capacity - 1U) {
            *out_units = units;
            *out_length = received;
            return WSH_OK;
        }
        wsh_windows_release(runtime, units);
        if (capacity > runtime->options.limits.max_string_bytes / 2U) {
            break;
        }
        capacity *= 2U;
    }
    return WSH_ERR_RESOURCE;
}

/** Destroy one retained process group and every handle it owns. */
static void wsh_windows_group_destroy(
    wsh_windows_runtime *runtime,
    wsh_windows_group *group)
{
    size_t index;

    if (runtime == NULL || group == NULL) {
        return;
    }
    for (index = 0U; index < group->process_count; ++index) {
        if (group->processes[index] != NULL &&
            group->processes[index] != INVALID_HANDLE_VALUE) {
            CloseHandle(group->processes[index]);
        }
    }
    if (group->job != NULL) {
        CloseHandle(group->job);
    }
    wsh_windows_release(runtime, group->processes);
    wsh_windows_release(runtime, group->identifiers);
    wsh_windows_release(runtime, group);
}

/** Terminate and collect all processes still retained by one group. */
static void wsh_windows_group_force(
    wsh_windows_runtime *runtime,
    wsh_windows_group *group,
    uint32_t status)
{
    size_t index;

    if (runtime == NULL || group == NULL) {
        return;
    }
    for (index = 0U; index < group->process_count; ++index) {
        (void)GenerateConsoleCtrlEvent(
            CTRL_BREAK_EVENT,
            group->identifiers[index]);
    }
    if (runtime->options.cancellation_grace_milliseconds != 0U) {
        for (index = 0U; index < group->process_count; ++index) {
            (void)WaitForSingleObject(
                group->processes[index],
                runtime->options.cancellation_grace_milliseconds);
        }
    }
    if (group->job != NULL && runtime->terminate_job != NULL) {
        (void)runtime->terminate_job(group->job, status);
    } else {
        for (index = 0U; index < group->process_count; ++index) {
            (void)TerminateProcess(group->processes[index], status);
        }
    }
    for (index = 0U; index < group->process_count; ++index) {
        (void)WaitForSingleObject(group->processes[index], INFINITE);
    }
    group->cancelled = 1;
    group->cancellation_status = status;
}

/** @brief Implements wsh_windows_runtime_options_init. */
void wsh_windows_runtime_options_init(
    wsh_windows_runtime_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    memset(out_options, 0, sizeof(*out_options));
    out_options->allocator = wsh_allocator_default();
    out_options->limits = wsh_limits_default();
    out_options->max_children = 64U;
    out_options->max_capture_bytes = 16U * 1024U * 1024U;
    out_options->cancellation_grace_milliseconds = 250U;
    out_options->allow_raw_launch = 1;
}

/** Resolve optional modern process and ordinal-comparison APIs. */
static void wsh_windows_load_capabilities(wsh_windows_runtime *runtime)
{
    HMODULE kernel;

    kernel = GetModuleHandleW(L"kernel32.dll");
    if (kernel == NULL) {
        return;
    }
    runtime->initialize_attributes =
        (wsh_initialize_attribute_list_fn)GetProcAddress(
            kernel, "InitializeProcThreadAttributeList");
    runtime->update_attribute =
        (wsh_update_attribute_fn)GetProcAddress(
            kernel, "UpdateProcThreadAttribute");
    runtime->delete_attributes =
        (wsh_delete_attribute_list_fn)GetProcAddress(
            kernel, "DeleteProcThreadAttributeList");
    runtime->compare_ordinal =
        (wsh_compare_ordinal_fn)GetProcAddress(
            kernel, "CompareStringOrdinal");
    runtime->create_job = (wsh_create_job_fn)GetProcAddress(
        kernel, "CreateJobObjectW");
    runtime->set_job_information =
        (wsh_set_job_information_fn)GetProcAddress(
            kernel, "SetInformationJobObject");
    runtime->assign_process_job =
        (wsh_assign_process_job_fn)GetProcAddress(
            kernel, "AssignProcessToJobObject");
    runtime->terminate_job = (wsh_terminate_job_fn)GetProcAddress(
        kernel, "TerminateJobObject");
    runtime->capabilities.explicit_handle_list =
        !runtime->options.force_legacy_inheritance &&
        runtime->initialize_attributes != NULL &&
        runtime->update_attribute != NULL &&
        runtime->delete_attributes != NULL;
}

/** Import a validated inherited logical-descriptor map when present. */
static void wsh_windows_load_descriptor_map(
    wsh_windows_runtime *runtime)
{
    WCHAR map[190];
    DWORD length;
    size_t index;
    size_t offset;
    size_t digit_index;
    unsigned digit;
    uintptr_t value;
    HANDLE handle;
    DWORD flags;

    for (index = 0U; index < WSH_WINDOWS_DESCRIPTORS; ++index) {
        runtime->base_descriptors[index] = INVALID_HANDLE_VALUE;
    }
    runtime->base_descriptors[0] = GetStdHandle(STD_INPUT_HANDLE);
    runtime->base_descriptors[1] = GetStdHandle(STD_OUTPUT_HANDLE);
    runtime->base_descriptors[2] = GetStdHandle(STD_ERROR_HANDLE);
    length = GetEnvironmentVariableW(
        L"_WSH_FD_MAP_V1", map, sizeof(map) / sizeof(map[0]));
    if (length != 189U) {
        return;
    }
    offset = 0U;
    for (index = 0U; index < WSH_WINDOWS_DESCRIPTORS; ++index) {
        if (map[offset++] != (WCHAR)('0' + index) ||
            map[offset++] != L':') {
            return;
        }
        value = 0U;
        for (digit_index = 0U; digit_index < 16U; ++digit_index) {
            if (map[offset] >= L'0' && map[offset] <= L'9') {
                digit = (unsigned)(map[offset] - L'0');
            } else if (map[offset] >= L'a' && map[offset] <= L'f') {
                digit = (unsigned)(map[offset] - L'a') + 10U;
            } else {
                return;
            }
            if (value > (UINTPTR_MAX - digit) / 16U) {
                return;
            }
            value = value * 16U + digit;
            offset += 1U;
        }
        if (index + 1U < WSH_WINDOWS_DESCRIPTORS &&
            map[offset++] != L',') {
            return;
        }
        handle = (HANDLE)value;
        if (handle != NULL && handle != INVALID_HANDLE_VALUE &&
            GetHandleInformation(handle, &flags)) {
            runtime->base_descriptors[index] = handle;
        }
    }
}

/** Capture Windows hidden `=X:` per-drive directory entries. */
static wsh_result wsh_windows_capture_drive_directories(
    wsh_windows_runtime *runtime)
{
    LPWCH block;
    const uint16_t *entry;
    size_t length;
    size_t drive;
    size_t current_drive;
    wsh_result result;

    result = WSH_OK;
    if (runtime->working_directory_length >= 3U &&
        runtime->working_directory[1] == (uint16_t)':') {
        current_drive = (size_t)(
            (runtime->working_directory[0] >= (uint16_t)'a' ?
             runtime->working_directory[0] - (uint16_t)'a' :
             runtime->working_directory[0] - (uint16_t)'A'));
        if (current_drive < 26U) {
            result = wsh_windows_copy_wide_path(
                runtime,
                runtime->working_directory,
                runtime->working_directory_length,
                &runtime->drive_directories[current_drive]);
            runtime->drive_directory_lengths[current_drive] =
                runtime->working_directory_length;
        }
    }
    block = result == WSH_OK ? GetEnvironmentStringsW() : NULL;
    if (result == WSH_OK && block == NULL) {
        return WSH_ERR_INTERNAL;
    }
    entry = (const uint16_t *)block;
    while (result == WSH_OK && entry[0] != 0U) {
        length = 0U;
        while (entry[length] != 0U) {
            length += 1U;
        }
        if (length >= 7U && entry[0] == (uint16_t)'=' &&
            entry[2] == (uint16_t)':' && entry[3] == (uint16_t)'=' &&
            ((entry[1] >= (uint16_t)'A' &&
              entry[1] <= (uint16_t)'Z') ||
             (entry[1] >= (uint16_t)'a' &&
              entry[1] <= (uint16_t)'z'))) {
            drive = (size_t)(entry[1] >= (uint16_t)'a' ?
                entry[1] - (uint16_t)'a' :
                entry[1] - (uint16_t)'A');
            wsh_windows_release(
                runtime, runtime->drive_directories[drive]);
            runtime->drive_directories[drive] = NULL;
            result = wsh_windows_copy_wide_path(
                runtime,
                entry + 4U,
                length - 4U,
                &runtime->drive_directories[drive]);
            runtime->drive_directory_lengths[drive] = length - 4U;
        }
        entry += length + 1U;
    }
    if (block != NULL) {
        FreeEnvironmentStringsW(block);
    }
    return result;
}

/** @brief Implements wsh_windows_runtime_create. */
wsh_result wsh_windows_runtime_create(
    const wsh_windows_runtime_options *options,
    wsh_windows_runtime **out_runtime)
{
    wsh_windows_runtime_options applied;
    wsh_windows_runtime *runtime;
    LARGE_INTEGER counter;
    wsh_result result;

    if (out_runtime == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_runtime = NULL;
    wsh_windows_runtime_options_init(&applied);
    if (options != NULL) {
        applied = *options;
    }
    if (applied.allocator.allocate == NULL ||
        applied.allocator.deallocate == NULL ||
        applied.max_children == 0U ||
        applied.max_capture_bytes == 0U ||
        applied.limits.max_string_bytes == 0U ||
        applied.limits.max_list_items == 0U) {
        return WSH_ERR_INVALID;
    }
    runtime = (wsh_windows_runtime *)applied.allocator.allocate(
        applied.allocator.user_data, sizeof(*runtime));
    if (runtime == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->options = applied;
    InitializeCriticalSection(&runtime->launch_lock);
    runtime->launch_lock_initialized = 1;
    result = wsh_windows_capture_directory(
        runtime,
        &runtime->working_directory,
        &runtime->working_directory_length);
    if (result == WSH_OK) {
        result = wsh_windows_capture_drive_directories(runtime);
    }
    if (result == WSH_OK) {
        result = wsh_windows_capture_executable(
            runtime,
            &runtime->executable_path,
            &runtime->executable_path_length);
    }
    if (result != WSH_OK) {
        wsh_windows_runtime_destroy(runtime);
        return result;
    }
    wsh_windows_load_capabilities(runtime);
    wsh_windows_load_descriptor_map(runtime);
    runtime->capabilities.job_object =
        !runtime->options.force_tracked_fallback &&
        runtime->create_job != NULL &&
        runtime->set_job_information != NULL &&
        runtime->assign_process_job != NULL &&
        runtime->terminate_job != NULL;
    counter.QuadPart = 0;
    (void)QueryPerformanceCounter(&counter);
    runtime->nonce = (uint64_t)(uint32_t)counter.LowPart;
    runtime->nonce <<= 32U;
    runtime->nonce ^= (uint64_t)GetCurrentProcessId() << 16U;
    runtime->nonce ^= (uint64_t)GetTickCount();
    *out_runtime = runtime;
    return WSH_OK;
}

/** @brief Implements wsh_windows_runtime_destroy. */
void wsh_windows_runtime_destroy(wsh_windows_runtime *runtime)
{
    size_t index;
    wsh_allocator allocator;

    if (runtime == NULL) {
        return;
    }
    wsh_windows_collect_substitutions(runtime, 1);
    for (index = 0U; index < runtime->group_count; ++index) {
        wsh_windows_group_force(
            runtime,
            runtime->groups[index],
            WSH_WINDOWS_CANCEL_STATUS);
        wsh_windows_group_destroy(runtime, runtime->groups[index]);
    }
    wsh_windows_release(runtime, runtime->groups);
    wsh_windows_release(runtime, runtime->working_directory);
    for (index = 0U; index < 26U; ++index) {
        wsh_windows_release(runtime, runtime->drive_directories[index]);
    }
    wsh_windows_release(runtime, runtime->executable_path);
    if (runtime->launch_lock_initialized) {
        DeleteCriticalSection(&runtime->launch_lock);
    }
    allocator = runtime->options.allocator;
    memset(runtime, 0, sizeof(*runtime));
    allocator.deallocate(allocator.user_data, runtime);
}

/** @brief Implements wsh_windows_runtime_get_capabilities. */
wsh_result wsh_windows_runtime_get_capabilities(
    const wsh_windows_runtime *runtime,
    wsh_windows_runtime_capabilities *out_capabilities)
{
    if (runtime == NULL || out_capabilities == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_capabilities = runtime->capabilities;
    return WSH_OK;
}

/** Compare two UTF-16 names with Windows ordinal ignore-case semantics. */
static int wsh_windows_compare_names(
    const wsh_windows_runtime *runtime,
    const uint16_t *left,
    size_t left_length,
    const uint16_t *right,
    size_t right_length)
{
    int result;

    if (runtime->compare_ordinal != NULL &&
        left_length <= (size_t)INT_MAX &&
        right_length <= (size_t)INT_MAX) {
        result = runtime->compare_ordinal(
            (LPCWSTR)left,
            (int)left_length,
            (LPCWSTR)right,
            (int)right_length,
            TRUE);
        if (result == CSTR_LESS_THAN) {
            return -1;
        }
        if (result == CSTR_GREATER_THAN) {
            return 1;
        }
        if (result == CSTR_EQUAL) {
            return 0;
        }
    }
    result = CompareStringW(
        LOCALE_INVARIANT,
        NORM_IGNORECASE,
        (LPCWSTR)left,
        (int)left_length,
        (LPCWSTR)right,
        (int)right_length);
    if (result == CSTR_LESS_THAN) {
        return -1;
    }
    if (result == CSTR_GREATER_THAN) {
        return 1;
    }
    if (result == CSTR_EQUAL) {
        return 0;
    }
    if (left_length != right_length) {
        return left_length < right_length ? -1 : 1;
    }
    result = memcmp(left, right, left_length * sizeof(*left));
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

/** Compare two UTF-8 exported names through the Windows boundary. */
static int wsh_windows_names_equal(
    void *user_data,
    wsh_string_view left,
    wsh_string_view right)
{
    wsh_windows_runtime *runtime;
    uint16_t *left_wide;
    uint16_t *right_wide;
    size_t left_length;
    size_t right_length;
    int equal;

    runtime = (wsh_windows_runtime *)user_data;
    left_wide = NULL;
    right_wide = NULL;
    if (runtime == NULL ||
        wsh_windows_to_wide(
            runtime, left, &left_wide, &left_length) != WSH_OK ||
        wsh_windows_to_wide(
            runtime, right, &right_wide, &right_length) != WSH_OK) {
        wsh_windows_release(runtime, left_wide);
        wsh_windows_release(runtime, right_wide);
        return 0;
    }
    equal = wsh_windows_compare_names(
        runtime,
        left_wide,
        left_length,
        right_wide,
        right_length) == 0;
    wsh_windows_release(runtime, left_wide);
    wsh_windows_release(runtime, right_wide);
    return equal;
}

/** Return whether one UTF-16 unit is a Windows path separator. */
static int wsh_windows_is_separator(uint16_t unit)
{
    return unit == (uint16_t)'\\' || unit == (uint16_t)'/';
}

/** Return whether a UTF-16 subject is explicit rather than a bare name. */
static int wsh_windows_is_explicit(
    const uint16_t *units,
    size_t length)
{
    size_t index;

    if (length >= 2U && units[1] == (uint16_t)':') {
        return 1;
    }
    for (index = 0U; index < length; ++index) {
        if (wsh_windows_is_separator(units[index])) {
            return 1;
        }
    }
    return 0;
}

/** Return whether a path's last component has an explicit extension. */
static int wsh_windows_has_extension(
    const uint16_t *units,
    size_t length)
{
    size_t index;

    index = length;
    while (index != 0U) {
        index -= 1U;
        if (wsh_windows_is_separator(units[index]) ||
            units[index] == (uint16_t)':') {
            return 0;
        }
        if (units[index] == (uint16_t)'.') {
            return 1;
        }
    }
    return 0;
}

/** Return whether a candidate names an existing non-directory file. */
static int wsh_windows_candidate_exists(const uint16_t *candidate)
{
    DWORD attributes;

    attributes = GetFileAttributesW((LPCWSTR)candidate);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

/** Join a base directory and relative UTF-16 path. */
static wsh_result wsh_windows_join_path(
    const wsh_windows_runtime *runtime,
    const uint16_t *base,
    size_t base_length,
    const uint16_t *path,
    size_t path_length,
    uint16_t **out_path,
    size_t *out_length)
{
    wsh_wide_buffer buffer;
    wsh_result result;
    int need_separator;

    wsh_wide_buffer_init(
        runtime, &buffer, runtime->options.limits.max_string_bytes);
    result = wsh_wide_buffer_append(&buffer, base, base_length);
    need_separator = base_length != 0U && path_length != 0U &&
        !wsh_windows_is_separator(base[base_length - 1U]) &&
        !wsh_windows_is_separator(path[0]);
    if (result == WSH_OK && need_separator) {
        result = wsh_wide_buffer_repeat(&buffer, (uint16_t)'\\', 1U);
    }
    if (result == WSH_OK) {
        result = wsh_wide_buffer_append(&buffer, path, path_length);
    }
    if (result == WSH_OK) {
        result = wsh_wide_buffer_finish(&buffer, out_path, out_length);
    }
    wsh_wide_buffer_destroy(&buffer);
    return result;
}

/** Build an absolute candidate against the runtime logical directory. */
static wsh_result wsh_windows_absolute_candidate(
    const wsh_windows_runtime *runtime,
    const uint16_t *path,
    size_t path_length,
    uint16_t **out_path,
    size_t *out_length)
{
    size_t drive;
    uint16_t root[4];

    if (path_length > 2U && path[1] == (uint16_t)':' &&
        wsh_windows_is_separator(path[2])) {
        return wsh_windows_join_path(
            runtime, NULL, 0U, path, path_length, out_path, out_length);
    }
    if (path_length >= 2U && path[1] == (uint16_t)':') {
        drive = path[0] >= (uint16_t)'a' ?
            (size_t)(path[0] - (uint16_t)'a') :
            (size_t)(path[0] - (uint16_t)'A');
        if (drive < 26U && runtime->drive_directories[drive] != NULL) {
            return wsh_windows_join_path(
                runtime,
                runtime->drive_directories[drive],
                runtime->drive_directory_lengths[drive],
                path + 2U,
                path_length - 2U,
                out_path,
                out_length);
        }
        root[0] = path[0];
        root[1] = (uint16_t)':';
        root[2] = (uint16_t)'\\';
        root[3] = 0U;
        return wsh_windows_join_path(
            runtime,
            root,
            3U,
            path + 2U,
            path_length - 2U,
            out_path,
            out_length);
    }
    if (path_length >= 2U && wsh_windows_is_separator(path[0]) &&
        wsh_windows_is_separator(path[1])) {
        return wsh_windows_join_path(
            runtime, NULL, 0U, path, path_length, out_path, out_length);
    }
    if (path_length != 0U && wsh_windows_is_separator(path[0]) &&
        runtime->working_directory_length >= 2U &&
        runtime->working_directory[1] == (uint16_t)':') {
        return wsh_windows_join_path(
            runtime,
            runtime->working_directory,
            2U,
            path,
            path_length,
            out_path,
            out_length);
    }
    return wsh_windows_join_path(
        runtime,
        runtime->working_directory,
        runtime->working_directory_length,
        path,
        path_length,
        out_path,
        out_length);
}

/** Try exact/exe/com variants of one already joined base candidate. */
static wsh_result wsh_windows_try_extensions(
    const wsh_windows_runtime *runtime,
    const uint16_t *candidate,
    size_t candidate_length,
    int has_extension,
    uint16_t **out_path,
    size_t *out_length)
{
    static const uint16_t exe[] = {'.', 'e', 'x', 'e'};
    static const uint16_t com[] = {'.', 'c', 'o', 'm'};
    const uint16_t *suffixes[3];
    size_t suffix_lengths[3];
    size_t count;
    size_t index;
    uint16_t *trial;
    size_t trial_length;
    wsh_result result;

    suffixes[0] = NULL;
    suffix_lengths[0] = 0U;
    count = 1U;
    if (!has_extension) {
        suffixes[1] = exe;
        suffix_lengths[1] = sizeof(exe) / sizeof(exe[0]);
        suffixes[2] = com;
        suffix_lengths[2] = sizeof(com) / sizeof(com[0]);
        count = 3U;
    }
    for (index = 0U; index < count; ++index) {
        wsh_wide_buffer buffer;

        wsh_wide_buffer_init(
            runtime, &buffer, runtime->options.limits.max_string_bytes);
        result = wsh_wide_buffer_append(
            &buffer, candidate, candidate_length);
        if (result == WSH_OK) {
            result = wsh_wide_buffer_append(
                &buffer, suffixes[index], suffix_lengths[index]);
        }
        if (result == WSH_OK) {
            result = wsh_wide_buffer_finish(
                &buffer, &trial, &trial_length);
        }
        wsh_wide_buffer_destroy(&buffer);
        if (result != WSH_OK) {
            return result;
        }
        if (wsh_windows_candidate_exists(trial)) {
            *out_path = trial;
            *out_length = trial_length;
            return WSH_OK;
        }
        wsh_windows_release(runtime, trial);
    }
    return WSH_ERR_MISMATCH;
}

/** Resolve one explicit UTF-16 subject against the logical directory. */
static wsh_result wsh_windows_resolve_explicit(
    const wsh_windows_runtime *runtime,
    const uint16_t *subject,
    size_t subject_length,
    uint16_t **out_path,
    size_t *out_length)
{
    uint16_t *candidate;
    size_t candidate_length;
    wsh_result result;

    candidate = NULL;
    result = wsh_windows_absolute_candidate(
        runtime,
        subject,
        subject_length,
        &candidate,
        &candidate_length);
    if (result == WSH_OK) {
        result = wsh_windows_try_extensions(
            runtime,
            candidate,
            candidate_length,
            wsh_windows_has_extension(subject, subject_length),
            out_path,
            out_length);
    }
    wsh_windows_release(runtime, candidate);
    return result;
}

/** Resolve one bare name under one UTF-8 search directory. */
static wsh_result wsh_windows_resolve_in_directory(
    const wsh_windows_runtime *runtime,
    wsh_string_view directory,
    const uint16_t *subject,
    size_t subject_length,
    uint16_t **out_path,
    size_t *out_length)
{
    uint16_t *wide_directory;
    size_t directory_length;
    uint16_t *absolute_directory;
    size_t absolute_length;
    uint16_t *candidate;
    size_t candidate_length;
    wsh_result result;

    wide_directory = NULL;
    absolute_directory = NULL;
    candidate = NULL;
    result = wsh_windows_to_wide(
        runtime, directory, &wide_directory, &directory_length);
    if (result == WSH_OK && directory_length == 0U) {
        wsh_windows_release(runtime, wide_directory);
        wide_directory = NULL;
        directory_length = 1U;
        wide_directory = (uint16_t *)wsh_windows_allocate(
            runtime, 2U * sizeof(*wide_directory));
        if (wide_directory == NULL) {
            return WSH_ERR_RESOURCE;
        }
        wide_directory[0] = (uint16_t)'.';
        wide_directory[1] = 0U;
    }
    if (result == WSH_OK) {
        result = wsh_windows_absolute_candidate(
            runtime,
            wide_directory,
            directory_length,
            &absolute_directory,
            &absolute_length);
    }
    if (result == WSH_OK) {
        result = wsh_windows_join_path(
            runtime,
            absolute_directory,
            absolute_length,
            subject,
            subject_length,
            &candidate,
            &candidate_length);
    }
    if (result == WSH_OK) {
        result = wsh_windows_try_extensions(
            runtime,
            candidate,
            candidate_length,
            wsh_windows_has_extension(subject, subject_length),
            out_path,
            out_length);
    }
    wsh_windows_release(runtime, wide_directory);
    wsh_windows_release(runtime, absolute_directory);
    wsh_windows_release(runtime, candidate);
    return result;
}

/** @brief Implements wsh_windows_runtime_resolve. */
wsh_result wsh_windows_runtime_resolve(
    wsh_windows_runtime *runtime,
    const wsh_context *context,
    wsh_string_view subject,
    wsh_string **out_path)
{
    uint16_t *wide_subject;
    size_t subject_length;
    uint16_t *resolved;
    size_t resolved_length;
    char *utf8;
    size_t utf8_length;
    const wsh_value *path;
    wsh_string_view directory;
    size_t index;
    wsh_result result;

    if (runtime == NULL || context == NULL || out_path == NULL ||
        subject.data == NULL || subject.length == 0U) {
        return WSH_ERR_INVALID;
    }
    *out_path = NULL;
    wide_subject = NULL;
    resolved = NULL;
    utf8 = NULL;
    result = wsh_windows_to_wide(
        runtime, subject, &wide_subject, &subject_length);
    if (result == WSH_OK &&
        wsh_windows_is_explicit(wide_subject, subject_length)) {
        result = wsh_windows_resolve_explicit(
            runtime,
            wide_subject,
            subject_length,
            &resolved,
            &resolved_length);
    } else if (result == WSH_OK) {
        result = WSH_ERR_MISMATCH;
        if (!runtime->options.safe_path) {
            directory.data = ".";
            directory.length = 1U;
            result = wsh_windows_resolve_in_directory(
                runtime,
                directory,
                wide_subject,
                subject_length,
                &resolved,
                &resolved_length);
        }
        path = NULL;
        if (result == WSH_ERR_MISMATCH &&
            wsh_context_get_variable(
                context,
                wsh_string_view_from_cstr("path"),
                &path) == WSH_OK) {
            for (index = 0U; result == WSH_ERR_MISMATCH &&
                 index < wsh_value_count(path); ++index) {
                if (wsh_value_at(path, index, &directory) != WSH_OK) {
                    result = WSH_ERR_INTERNAL;
                } else {
                    result = wsh_windows_resolve_in_directory(
                        runtime,
                        directory,
                        wide_subject,
                        subject_length,
                        &resolved,
                        &resolved_length);
                }
            }
        }
    }
    if (result == WSH_OK) {
        result = wsh_windows_from_wide(
            runtime,
            resolved,
            resolved_length,
            &utf8,
            &utf8_length);
    }
    if (result == WSH_OK) {
        wsh_string_view view;

        view.data = utf8;
        view.length = utf8_length;
        result = wsh_string_create(
            &runtime->options.allocator,
            &runtime->options.limits,
            view,
            out_path);
    }
    wsh_windows_release(runtime, wide_subject);
    wsh_windows_release(runtime, resolved);
    wsh_windows_release(runtime, utf8);
    return result;
}

/** Destroy one converted environment entry. */
static void wsh_environment_entry_destroy(
    const wsh_windows_runtime *runtime,
    wsh_environment_entry *entry)
{
    if (entry == NULL) {
        return;
    }
    wsh_windows_release(runtime, entry->name);
    wsh_windows_release(runtime, entry->value);
    memset(entry, 0, sizeof(*entry));
}

/** Return whether a UTF-16 environment name is syntactically valid. */
static int wsh_environment_name_valid(
    const uint16_t *name,
    size_t length)
{
    size_t index;

    if (name == NULL || length == 0U) {
        return 0;
    }
    for (index = 0U; index < length; ++index) {
        if (name[index] == 0U || name[index] == (uint16_t)'=') {
            return 0;
        }
    }
    return 1;
}

/** Compare environment entries by insensitive name then exact units. */
static int wsh_environment_entry_compare(
    const wsh_windows_runtime *runtime,
    const wsh_environment_entry *left,
    const wsh_environment_entry *right)
{
    int result;
    size_t common;

    result = wsh_windows_compare_names(
        runtime,
        left->name,
        left->name_length,
        right->name,
        right->name_length);
    if (result != 0) {
        return result;
    }
    common = left->name_length < right->name_length ?
        left->name_length : right->name_length;
    result = memcmp(left->name, right->name, common * sizeof(*left->name));
    if (result != 0) {
        return result < 0 ? -1 : 1;
    }
    if (left->name_length == right->name_length) {
        return 0;
    }
    return left->name_length < right->name_length ? -1 : 1;
}

/** Sort environment entries with deterministic insertion sort. */
static void wsh_environment_entries_sort(
    const wsh_windows_runtime *runtime,
    wsh_environment_entry *entries,
    size_t count)
{
    size_t index;

    for (index = 1U; index < count; ++index) {
        wsh_environment_entry current;
        size_t position;

        current = entries[index];
        position = index;
        while (position != 0U &&
            wsh_environment_entry_compare(
                runtime,
                &current,
                &entries[position - 1U]) < 0) {
            entries[position] = entries[position - 1U];
            position -= 1U;
        }
        entries[position] = current;
    }
}

/** Append one byte as two lowercase hexadecimal characters. */
static wsh_result wsh_envelope_append_hex_byte(
    wsh_string_builder *builder,
    unsigned char byte)
{
    static const char digits[] = "0123456789abcdef";
    char text[2];
    wsh_string_view view;

    text[0] = digits[(byte >> 4U) & 15U];
    text[1] = digits[byte & 15U];
    view.data = text;
    view.length = sizeof(text);
    return wsh_string_builder_append(builder, view);
}

/** Append one fixed-width hexadecimal size to an envelope. */
static wsh_result wsh_envelope_append_size(
    wsh_string_builder *builder,
    size_t value)
{
    char text[17];
    int length;
    wsh_string_view view;

    length = snprintf(text, sizeof(text), "%016llx",
        (unsigned long long)value);
    if (length != 16) {
        return WSH_ERR_INTERNAL;
    }
    view.data = text;
    view.length = 16U;
    return wsh_string_builder_append(builder, view);
}

/** Append one length-prefixed hexadecimal UTF-8 field. */
static wsh_result wsh_envelope_append_field(
    wsh_string_builder *builder,
    wsh_string_view field)
{
    size_t index;
    wsh_result result;

    result = wsh_envelope_append_size(builder, field.length);
    if (result == WSH_OK) {
        result = wsh_string_builder_append(
            builder, wsh_string_view_from_cstr(":"));
    }
    for (index = 0U; result == WSH_OK && index < field.length; ++index) {
        result = wsh_envelope_append_hex_byte(
            builder, (unsigned char)field.data[index]);
    }
    if (result == WSH_OK) {
        result = wsh_string_builder_append(
            builder, wsh_string_view_from_cstr(":"));
    }
    return result;
}

/** Build the bounded private environment-list envelope. */
static wsh_result wsh_windows_build_envelope(
    const wsh_windows_runtime *runtime,
    const wsh_context *context,
    wsh_string **out_envelope)
{
    wsh_string_builder *builder;
    wsh_string_view name;
    const wsh_value *value;
    int exported;
    size_t variable_index;
    size_t item_index;
    wsh_string_view item;
    char header[32];
    int header_length;
    wsh_string_view header_view;
    wsh_result result;

    *out_envelope = NULL;
    result = wsh_string_builder_create(
        &runtime->options.allocator,
        &runtime->options.limits,
        &builder);
    header_length = snprintf(
        header,
        sizeof(header),
        "1:%016llx:",
        (unsigned long long)runtime->nonce);
    header_view.data = header;
    header_view.length = header_length > 0 ? (size_t)header_length : 0U;
    if (result == WSH_OK && header_length != 19) {
        result = WSH_ERR_INTERNAL;
    }
    if (result == WSH_OK) {
        result = wsh_string_builder_append(builder, header_view);
    }
    for (variable_index = 0U; result == WSH_OK &&
         variable_index < wsh_context_variable_count(context);
         ++variable_index) {
        result = wsh_context_variable_at(
            context,
            variable_index,
            &name,
            &value,
            &exported);
        if (result != WSH_OK || !exported) {
            continue;
        }
        /* Scalars are already preserved exactly by the ordinary environment
         * entry.  The private envelope is needed only for list shapes that a
         * Windows scalar cannot represent without loss. */
        if (wsh_value_count(value) == 1U) {
            continue;
        }
        result = wsh_envelope_append_field(builder, name);
        if (result == WSH_OK) {
            result = wsh_envelope_append_size(
                builder, wsh_value_count(value));
        }
        if (result == WSH_OK) {
            result = wsh_string_builder_append(
                builder, wsh_string_view_from_cstr(":"));
        }
        for (item_index = 0U; result == WSH_OK &&
             item_index < wsh_value_count(value); ++item_index) {
            result = wsh_value_at(value, item_index, &item);
            if (result == WSH_OK) {
                result = wsh_envelope_append_field(builder, item);
            }
        }
        if (result == WSH_OK) {
            result = wsh_string_builder_append(
                builder, wsh_string_view_from_cstr(";"));
        }
    }
    if (result == WSH_OK) {
        result = wsh_string_builder_finish(builder, out_envelope);
    }
    wsh_string_builder_destroy(builder);
    return result;
}

/** Flatten the WSH path list to a Windows semicolon-separated scalar. */
static wsh_result wsh_windows_flatten_path(
    const wsh_windows_runtime *runtime,
    const wsh_value *value,
    wsh_string **out_value)
{
    wsh_string_builder *builder;
    size_t index;
    wsh_string_view item;
    wsh_result result;

    *out_value = NULL;
    result = wsh_string_builder_create(
        &runtime->options.allocator,
        &runtime->options.limits,
        &builder);
    for (index = 0U; result == WSH_OK &&
         index < wsh_value_count(value); ++index) {
        result = wsh_value_at(value, index, &item);
        if (result == WSH_OK && memchr(item.data, ';', item.length) != NULL) {
            result = WSH_ERR_MISMATCH;
        }
        if (result == WSH_OK && index != 0U) {
            result = wsh_string_builder_append(
                builder, wsh_string_view_from_cstr(";"));
        }
        if (result == WSH_OK) {
            result = wsh_string_builder_append(builder, item);
        }
    }
    if (result == WSH_OK) {
        result = wsh_string_builder_finish(builder, out_value);
    }
    wsh_string_builder_destroy(builder);
    return result;
}

/** Add one converted environment entry to a growable array. */
static wsh_result wsh_environment_entry_add(
    const wsh_windows_runtime *runtime,
    wsh_environment_entry **entries,
    size_t *count,
    size_t *capacity,
    wsh_string_view name,
    wsh_string_view value)
{
    wsh_environment_entry entry;
    wsh_result result;

    memset(&entry, 0, sizeof(entry));
    result = wsh_windows_to_wide(
        runtime, name, &entry.name, &entry.name_length);
    if (result == WSH_OK &&
        !wsh_environment_name_valid(entry.name, entry.name_length)) {
        result = WSH_ERR_MISMATCH;
    }
    if (result == WSH_OK) {
        result = wsh_windows_to_wide(
            runtime, value, &entry.value, &entry.value_length);
    }
    if (result == WSH_OK) {
        result = wsh_windows_grow(
            runtime,
            (void **)entries,
            sizeof(**entries),
            *count,
            capacity,
            *count + 1U,
            runtime->options.limits.max_variables + 3U);
    }
    if (result == WSH_OK) {
        (*entries)[*count] = entry;
        *count += 1U;
    } else {
        wsh_environment_entry_destroy(runtime, &entry);
    }
    return result;
}

/** @brief Implements wsh_windows_runtime_environment_block. */
wsh_result wsh_windows_runtime_environment_block(
    const wsh_windows_runtime *runtime,
    const wsh_context *context,
    int nested_wsh,
    uint16_t **out_units,
    size_t *out_length)
{
    wsh_environment_entry *entries;
    size_t count;
    size_t capacity;
    size_t variable_index;
    size_t index;
    wsh_string_view name;
    const wsh_value *value;
    int exported;
    wsh_string_view scalar;
    wsh_string *flattened;
    wsh_string *envelope;
    char nonce[17];
    int nonce_length;
    wsh_string_view nonce_view;
    char descriptor_map[190];
    size_t descriptor_offset;
    size_t descriptor_index;
    size_t digit_index;
    wsh_string_view descriptor_view;
    wsh_wide_buffer block;
    wsh_result result;

    if (runtime == NULL || context == NULL || out_units == NULL ||
        out_length == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_units = NULL;
    *out_length = 0U;
    entries = NULL;
    count = 0U;
    capacity = 0U;
    envelope = NULL;
    result = WSH_OK;
    for (variable_index = 0U; result == WSH_OK &&
         variable_index < wsh_context_variable_count(context);
         ++variable_index) {
        flattened = NULL;
        result = wsh_context_variable_at(
            context,
            variable_index,
            &name,
            &value,
            &exported);
        if (result != WSH_OK || !exported) {
            continue;
        }
        if (wsh_windows_names_equal(
                (void *)runtime,
                name,
                wsh_string_view_from_cstr("path"))) {
            result = wsh_windows_flatten_path(runtime, value, &flattened);
            name = wsh_string_view_from_cstr("PATH");
            scalar = wsh_string_bytes(flattened);
        } else if (wsh_value_count(value) == 1U) {
            result = wsh_value_at(value, 0U, &scalar);
        } else if (nested_wsh) {
            continue;
        } else {
            result = WSH_ERR_MISMATCH;
        }
        if (result == WSH_OK) {
            result = wsh_environment_entry_add(
                runtime,
                &entries,
                &count,
                &capacity,
                name,
                scalar);
        }
        wsh_string_destroy(flattened);
    }
    if (result == WSH_OK && nested_wsh) {
        result = wsh_windows_build_envelope(runtime, context, &envelope);
    }
    nonce_length = snprintf(
        nonce,
        sizeof(nonce),
        "%016llx",
        (unsigned long long)runtime->nonce);
    nonce_view.data = nonce;
    nonce_view.length = nonce_length > 0 ? (size_t)nonce_length : 0U;
    if (result == WSH_OK && nested_wsh && nonce_length != 16) {
        result = WSH_ERR_INTERNAL;
    }
    if (result == WSH_OK && nested_wsh) {
        result = wsh_environment_entry_add(
            runtime,
            &entries,
            &count,
            &capacity,
            wsh_string_view_from_cstr(WSH_WINDOWS_PARENT_NONCE),
            nonce_view);
    }
    if (result == WSH_OK && nested_wsh) {
        result = wsh_environment_entry_add(
            runtime,
            &entries,
            &count,
            &capacity,
            wsh_string_view_from_cstr(WSH_WINDOWS_ENVELOPE),
            wsh_string_bytes(envelope));
    }
    descriptor_offset = 0U;
    for (descriptor_index = 0U;
         descriptor_index < WSH_WINDOWS_DESCRIPTORS;
         ++descriptor_index) {
        descriptor_map[descriptor_offset++] =
            (char)('0' + descriptor_index);
        descriptor_map[descriptor_offset++] = ':';
        for (digit_index = 0U; digit_index < 16U; ++digit_index) {
            descriptor_map[descriptor_offset++] = '0';
        }
        if (descriptor_index + 1U < WSH_WINDOWS_DESCRIPTORS) {
            descriptor_map[descriptor_offset++] = ',';
        }
    }
    descriptor_map[descriptor_offset] = '\0';
    descriptor_view.data = descriptor_map;
    descriptor_view.length = descriptor_offset;
    if (result == WSH_OK && nested_wsh) {
        result = wsh_environment_entry_add(
            runtime,
            &entries,
            &count,
            &capacity,
            wsh_string_view_from_cstr(WSH_WINDOWS_DESCRIPTOR_MAP),
            descriptor_view);
    }
    if (result == WSH_OK) {
        wsh_environment_entries_sort(runtime, entries, count);
        for (index = 1U; index < count; ++index) {
            if (wsh_windows_compare_names(
                    runtime,
                    entries[index - 1U].name,
                    entries[index - 1U].name_length,
                    entries[index].name,
                    entries[index].name_length) == 0) {
                result = WSH_ERR_MISMATCH;
                break;
            }
        }
    }
    wsh_wide_buffer_init(runtime, &block, WSH_WINDOWS_COMMAND_UNITS);
    for (index = 0U; result == WSH_OK && index < count; ++index) {
        result = wsh_wide_buffer_append(
            &block, entries[index].name, entries[index].name_length);
        if (result == WSH_OK) {
            result = wsh_wide_buffer_repeat(&block, (uint16_t)'=', 1U);
        }
        if (result == WSH_OK) {
            result = wsh_wide_buffer_append(
                &block, entries[index].value, entries[index].value_length);
        }
        if (result == WSH_OK) {
            result = wsh_wide_buffer_repeat(&block, 0U, 1U);
        }
    }
    if (result == WSH_OK) {
        result = wsh_wide_buffer_repeat(&block, 0U, 1U);
    }
    if (result == WSH_OK && count == 0U) {
        result = wsh_wide_buffer_repeat(&block, 0U, 1U);
    }
    if (result == WSH_OK) {
        result = wsh_wide_buffer_finish(&block, out_units, out_length);
    }
    wsh_wide_buffer_destroy(&block);
    for (index = 0U; index < count; ++index) {
        wsh_environment_entry_destroy(runtime, &entries[index]);
    }
    wsh_windows_release(runtime, entries);
    wsh_string_destroy(envelope);
    return result;
}

/** Import one scalar UTF-16 environment entry into a context. */
static wsh_result wsh_windows_import_scalar(
    wsh_windows_runtime *runtime,
    wsh_context *context,
    const uint16_t *name,
    size_t name_length,
    const uint16_t *value,
    size_t value_length)
{
    char *name_utf8;
    size_t name_bytes;
    char *value_utf8;
    size_t value_bytes;
    wsh_value_builder *builder;
    wsh_value *list;
    wsh_string_view name_view;
    wsh_string_view value_view;
    wsh_result result;

    builder = NULL;
    name_utf8 = NULL;
    value_utf8 = NULL;
    list = NULL;
    result = wsh_windows_from_wide(
        runtime, name, name_length, &name_utf8, &name_bytes);
    if (result == WSH_OK) {
        result = wsh_windows_from_wide(
            runtime, value, value_length, &value_utf8, &value_bytes);
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_create(
            &runtime->options.allocator,
            &runtime->options.limits,
            &builder);
    }
    value_view.data = value_utf8;
    value_view.length = value_bytes;
    if (result == WSH_OK) {
        result = wsh_value_builder_append(builder, value_view);
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, &list);
    }
    wsh_value_builder_destroy(builder);
    name_view.data = name_utf8;
    name_view.length = name_bytes;
    if (result == WSH_OK) {
        result = wsh_context_import_variable(context, name_view, list);
    }
    wsh_value_destroy(list);
    wsh_windows_release(runtime, name_utf8);
    wsh_windows_release(runtime, value_utf8);
    return result;
}

/** Import PATH as the WSH path list adapter. */
static wsh_result wsh_windows_import_path(
    wsh_windows_runtime *runtime,
    wsh_context *context,
    const uint16_t *value,
    size_t value_length)
{
    wsh_value_builder *builder;
    wsh_value *list;
    size_t start;
    size_t index;
    char *utf8;
    size_t utf8_length;
    wsh_string_view item;
    wsh_result result;

    builder = NULL;
    list = NULL;
    result = wsh_value_builder_create(
        &runtime->options.allocator,
        &runtime->options.limits,
        &builder);
    start = 0U;
    for (index = 0U; result == WSH_OK && index <= value_length; ++index) {
        if (index != value_length && value[index] != (uint16_t)';') {
            continue;
        }
        utf8 = NULL;
        result = wsh_windows_from_wide(
            runtime,
            value + start,
            index - start,
            &utf8,
            &utf8_length);
        item.data = utf8;
        item.length = utf8_length;
        if (result == WSH_OK) {
            result = wsh_value_builder_append(builder, item);
        }
        wsh_windows_release(runtime, utf8);
        start = index + 1U;
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, &list);
    }
    wsh_value_builder_destroy(builder);
    if (result == WSH_OK) {
        result = wsh_context_import_variable(
            context, wsh_string_view_from_cstr("path"), list);
    }
    wsh_value_destroy(list);
    return result;
}

/** One fully decoded private envelope variable. */
typedef struct wsh_envelope_variable {
    /** Owned exact variable name. */
    wsh_string *name;
    /** Owned exact list value. */
    wsh_value *value;
} wsh_envelope_variable;

/** Decode one lowercase or uppercase hexadecimal digit. */
static int wsh_envelope_hex_digit(char character, unsigned *out_digit)
{
    if (character >= '0' && character <= '9') {
        *out_digit = (unsigned)(character - '0');
        return 1;
    }
    if (character >= 'a' && character <= 'f') {
        *out_digit = (unsigned)(character - 'a') + 10U;
        return 1;
    }
    if (character >= 'A' && character <= 'F') {
        *out_digit = (unsigned)(character - 'A') + 10U;
        return 1;
    }
    return 0;
}

/** Decode one fixed-width hexadecimal size followed by a colon. */
static int wsh_envelope_parse_size(
    const char *bytes,
    size_t length,
    size_t *offset,
    size_t *out_value)
{
    size_t index;
    size_t value;
    unsigned digit;

    if (*offset > length || length - *offset < 17U) {
        return 0;
    }
    value = 0U;
    for (index = 0U; index < 16U; ++index) {
        if (!wsh_envelope_hex_digit(
                bytes[*offset + index], &digit) ||
            value > ((size_t)-1 - digit) / 16U) {
            return 0;
        }
        value = value * 16U + digit;
    }
    if (bytes[*offset + 16U] != ':') {
        return 0;
    }
    *offset += 17U;
    *out_value = value;
    return 1;
}

/** Decode one length-prefixed hexadecimal UTF-8 envelope field. */
static wsh_result wsh_envelope_parse_field(
    wsh_windows_runtime *runtime,
    const char *bytes,
    size_t length,
    size_t *offset,
    wsh_string **out_field)
{
    size_t field_length;
    size_t encoded_length;
    size_t index;
    unsigned high;
    unsigned low;
    char *field;
    wsh_string_view view;
    wsh_result result;

    *out_field = NULL;
    if (!wsh_envelope_parse_size(
            bytes, length, offset, &field_length) ||
        !wsh_windows_multiply(field_length, 2U, &encoded_length) ||
        *offset > length || encoded_length >= length - *offset) {
        return WSH_ERR_INVALID;
    }
    field = (char *)wsh_windows_allocate(runtime, field_length + 1U);
    if (field == NULL) {
        return WSH_ERR_RESOURCE;
    }
    for (index = 0U; index < field_length; ++index) {
        if (!wsh_envelope_hex_digit(
                bytes[*offset + index * 2U], &high) ||
            !wsh_envelope_hex_digit(
                bytes[*offset + index * 2U + 1U], &low)) {
            wsh_windows_release(runtime, field);
            return WSH_ERR_INVALID;
        }
        field[index] = (char)((high << 4U) | low);
    }
    *offset += encoded_length;
    if (bytes[*offset] != ':') {
        wsh_windows_release(runtime, field);
        return WSH_ERR_INVALID;
    }
    *offset += 1U;
    view.data = field;
    view.length = field_length;
    result = wsh_string_create(
        &runtime->options.allocator,
        &runtime->options.limits,
        view,
        out_field);
    wsh_windows_release(runtime, field);
    return result;
}

/** Destroy a decoded private-envelope variable array. */
static void wsh_envelope_variables_destroy(
    wsh_windows_runtime *runtime,
    wsh_envelope_variable *variables,
    size_t count)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        wsh_string_destroy(variables[index].name);
        wsh_value_destroy(variables[index].value);
    }
    wsh_windows_release(runtime, variables);
}

/** Validate and import one paired versioned nested-WSH envelope. */
static wsh_result wsh_windows_import_envelope(
    wsh_windows_runtime *runtime,
    wsh_context *context,
    const uint16_t *envelope,
    size_t envelope_length,
    const uint16_t *nonce,
    size_t nonce_length)
{
    char *bytes;
    size_t byte_length;
    char *nonce_bytes;
    size_t nonce_byte_length;
    size_t offset;
    size_t item_count;
    size_t item_index;
    size_t count;
    size_t capacity;
    size_t index;
    size_t other;
    wsh_envelope_variable *variables;
    wsh_envelope_variable variable;
    wsh_value_builder *builder;
    wsh_string *item;
    wsh_result result;

    bytes = NULL;
    nonce_bytes = NULL;
    variables = NULL;
    count = 0U;
    capacity = 0U;
    result = wsh_windows_from_wide(
        runtime, envelope, envelope_length, &bytes, &byte_length);
    if (result == WSH_OK) {
        result = wsh_windows_from_wide(
            runtime, nonce, nonce_length, &nonce_bytes,
            &nonce_byte_length);
    }
    if (result == WSH_OK &&
        (byte_length < 19U || memcmp(bytes, "1:", 2U) != 0 ||
         nonce_byte_length != 16U ||
         memcmp(bytes + 2U, nonce_bytes, 16U) != 0 ||
         bytes[18] != ':')) {
        result = WSH_ERR_INVALID;
    }
    offset = 19U;
    while (result == WSH_OK && offset < byte_length) {
        memset(&variable, 0, sizeof(variable));
        builder = NULL;
        result = wsh_envelope_parse_field(
            runtime, bytes, byte_length, &offset, &variable.name);
        if (result == WSH_OK &&
            !wsh_envelope_parse_size(
                bytes, byte_length, &offset, &item_count)) {
            result = WSH_ERR_INVALID;
        }
        if (result == WSH_OK &&
            item_count > runtime->options.limits.max_list_items) {
            result = WSH_ERR_RESOURCE;
        }
        if (result == WSH_OK) {
            result = wsh_value_builder_create(
                &runtime->options.allocator,
                &runtime->options.limits,
                &builder);
        }
        for (item_index = 0U; result == WSH_OK &&
             item_index < item_count; ++item_index) {
            item = NULL;
            result = wsh_envelope_parse_field(
                runtime, bytes, byte_length, &offset, &item);
            if (result == WSH_OK) {
                result = wsh_value_builder_append(
                    builder, wsh_string_bytes(item));
            }
            wsh_string_destroy(item);
        }
        if (result == WSH_OK) {
            result = wsh_value_builder_finish(builder, &variable.value);
        }
        wsh_value_builder_destroy(builder);
        if (result == WSH_OK &&
            (offset >= byte_length || bytes[offset] != ';')) {
            result = WSH_ERR_INVALID;
        }
        if (result == WSH_OK) {
            offset += 1U;
            result = wsh_windows_grow(
                runtime,
                (void **)&variables,
                sizeof(*variables),
                count,
                &capacity,
                count + 1U,
                runtime->options.limits.max_variables);
        }
        if (result == WSH_OK) {
            variables[count++] = variable;
        } else {
            wsh_string_destroy(variable.name);
            wsh_value_destroy(variable.value);
        }
    }
    for (index = 0U; result == WSH_OK && index < count; ++index) {
        for (other = index + 1U; other < count; ++other) {
            if (wsh_windows_names_equal(
                    runtime,
                    wsh_string_bytes(variables[index].name),
                    wsh_string_bytes(variables[other].name))) {
                result = WSH_ERR_MISMATCH;
                break;
            }
        }
    }
    for (index = 0U; result == WSH_OK && index < count; ++index) {
        result = wsh_context_import_variable(
            context,
            wsh_string_bytes(variables[index].name),
            variables[index].value);
    }
    wsh_envelope_variables_destroy(runtime, variables, count);
    wsh_windows_release(runtime, bytes);
    wsh_windows_release(runtime, nonce_bytes);
    return result;
}

/** @brief Implements wsh_windows_runtime_import_environment. */
wsh_result wsh_windows_runtime_import_environment(
    wsh_windows_runtime *runtime,
    wsh_context *context)
{
    LPWCH block;
    const uint16_t *entry;
    size_t length;
    size_t equals;
    const uint16_t *envelope;
    size_t envelope_length;
    const uint16_t *nonce;
    size_t nonce_length;
    wsh_result result;

    if (runtime == NULL || context == NULL) {
        return WSH_ERR_INVALID;
    }
    block = GetEnvironmentStringsW();
    if (block == NULL) {
        return WSH_ERR_INTERNAL;
    }
    result = WSH_OK;
    envelope = NULL;
    envelope_length = 0U;
    nonce = NULL;
    nonce_length = 0U;
    entry = (const uint16_t *)block;
    while (result == WSH_OK && entry[0] != 0U) {
        length = 0U;
        while (entry[length] != 0U) {
            length += 1U;
        }
        equals = 0U;
        while (equals < length && entry[equals] != (uint16_t)'=') {
            equals += 1U;
        }
        if (equals != 0U && equals < length) {
            if (wsh_windows_compare_names(
                    runtime,
                    entry,
                    equals,
                    (const uint16_t *)L"PATH",
                    4U) == 0) {
                result = wsh_windows_import_path(
                    runtime,
                    context,
                    entry + equals + 1U,
                    length - equals - 1U);
            } else if (wsh_windows_compare_names(
                    runtime,
                    entry,
                    equals,
                    (const uint16_t *)L"_WSH_ENV_V1",
                    11U) == 0) {
                envelope = entry + equals + 1U;
                envelope_length = length - equals - 1U;
            } else if (wsh_windows_compare_names(
                    runtime,
                    entry,
                    equals,
                    (const uint16_t *)L"_WSH_PARENT_NONCE",
                    17U) == 0) {
                nonce = entry + equals + 1U;
                nonce_length = length - equals - 1U;
            } else if (wsh_windows_compare_names(
                    runtime,
                    entry,
                    equals,
                    (const uint16_t *)L"_WSH_FD_MAP_V1",
                    14U) == 0) {
                /* Runtime creation already consumed this private map. */
            } else {
                result = wsh_windows_import_scalar(
                    runtime,
                    context,
                    entry,
                    equals,
                    entry + equals + 1U,
                    length - equals - 1U);
            }
        }
        entry += length + 1U;
    }
    if (result == WSH_OK && envelope != NULL && nonce != NULL) {
        result = wsh_windows_import_envelope(
            runtime,
            context,
            envelope,
            envelope_length,
            nonce,
            nonce_length);
    }
    FreeEnvironmentStringsW(block);
    return result;
}

/** Return whether one UTF-8 path ends in `.wsh` ignoring ASCII case. */
static int wsh_windows_is_wsh_path(wsh_string_view path)
{
    const char *tail;

    if (path.length < 4U) {
        return 0;
    }
    tail = path.data + path.length - 4U;
    return tail[0] == '.' &&
        (tail[1] == 'w' || tail[1] == 'W') &&
        (tail[2] == 's' || tail[2] == 'S') &&
        (tail[3] == 'h' || tail[3] == 'H');
}

/** Build structured nested-WSH arguments with script path first. */
static wsh_result wsh_windows_nested_arguments(
    const wsh_windows_runtime *runtime,
    wsh_string_view script,
    const wsh_value *arguments,
    wsh_value **out_arguments)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_string_view item;
    wsh_result result;

    builder = NULL;
    *out_arguments = NULL;
    result = wsh_value_builder_create(
        &runtime->options.allocator,
        &runtime->options.limits,
        &builder);
    if (result == WSH_OK) {
        result = wsh_value_builder_append(builder, script);
    }
    for (index = 0U; result == WSH_OK &&
         index < wsh_value_count(arguments); ++index) {
        result = wsh_value_at(arguments, index, &item);
        if (result == WSH_OK) {
            result = wsh_value_builder_append(builder, item);
        }
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_arguments);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Build structured arguments for one internal pipeline echo stage. */
static wsh_result wsh_windows_echo_arguments(
    const wsh_windows_runtime *runtime,
    const wsh_value *arguments,
    wsh_value **out_arguments)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_string_view item;
    wsh_result result;

    builder = NULL;
    *out_arguments = NULL;
    result = wsh_value_builder_create(
        &runtime->options.allocator,
        &runtime->options.limits,
        &builder);
    if (result == WSH_OK) {
        result = wsh_value_builder_append(
            builder, wsh_string_view_from_cstr("--runtime-echo"));
    }
    for (index = 0U; result == WSH_OK &&
         index < wsh_value_count(arguments); ++index) {
        result = wsh_value_at(arguments, index, &item);
        if (result == WSH_OK) {
            result = wsh_value_builder_append(builder, item);
        }
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_arguments);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Initialize a stage from this runtime's inherited logical descriptors. */
static void wsh_windows_stage_descriptors_init(
    const wsh_windows_runtime *runtime,
    wsh_windows_stage *stage)
{
    size_t index;

    for (index = 0U; index < WSH_WINDOWS_DESCRIPTORS; ++index) {
        stage->descriptors[index] = runtime->base_descriptors[index];
    }
}

/** Close one owned descriptor mapping before replacing it. */
static void wsh_windows_stage_close_descriptor(
    wsh_windows_stage *stage,
    size_t descriptor)
{
    if (stage->descriptor_owned[descriptor] &&
        stage->descriptors[descriptor] != NULL &&
        stage->descriptors[descriptor] != INVALID_HANDLE_VALUE) {
        CloseHandle(stage->descriptors[descriptor]);
    }
    stage->descriptors[descriptor] = INVALID_HANDLE_VALUE;
    stage->descriptor_owned[descriptor] = 0;
}

/** Destroy one prepared stage and all resources not transferred to a group. */
static void wsh_windows_stage_destroy(
    wsh_windows_runtime *runtime,
    wsh_windows_stage *stage)
{
    size_t index;

    if (runtime == NULL || stage == NULL) {
        return;
    }
    for (index = 0U; index < WSH_WINDOWS_DESCRIPTORS; ++index) {
        wsh_windows_stage_close_descriptor(stage, index);
    }
    if (stage->process.hThread != NULL) {
        CloseHandle(stage->process.hThread);
    }
    if (stage->process.hProcess != NULL) {
        CloseHandle(stage->process.hProcess);
    }
    wsh_string_destroy(stage->resolved);
    wsh_windows_release(runtime, stage->application);
    wsh_windows_release(runtime, stage->command_line);
    wsh_windows_release(runtime, stage->environment);
    memset(stage, 0, sizeof(*stage));
}

/** Copy the runtime executable path as strict UTF-8. */
static wsh_result wsh_windows_executable_utf8(
    const wsh_windows_runtime *runtime,
    wsh_string **out_path)
{
    char *bytes;
    size_t length;
    wsh_string_view view;
    wsh_result result;

    bytes = NULL;
    *out_path = NULL;
    result = wsh_windows_from_wide(
        runtime,
        runtime->executable_path,
        runtime->executable_path_length,
        &bytes,
        &length);
    view.data = bytes;
    view.length = length;
    if (result == WSH_OK) {
        result = wsh_string_create(
            &runtime->options.allocator,
            &runtime->options.limits,
            view,
            out_path);
    }
    wsh_windows_release(runtime, bytes);
    return result;
}

/** Prepare resolution, command line, and environment for one stage. */
static wsh_result wsh_windows_stage_prepare(
    wsh_windows_runtime *runtime,
    const wsh_context *context,
    const wsh_runtime_command *command,
    wsh_windows_stage *stage)
{
    wsh_string_view resolved_view;
    wsh_string *application_utf8;
    wsh_value *nested_arguments;
    const wsh_value *serialized_arguments;
    int nested_wsh;
    wsh_result result;

    memset(stage, 0, sizeof(*stage));
    wsh_windows_stage_descriptors_init(runtime, stage);
    application_utf8 = NULL;
    nested_arguments = NULL;
    if (command->shell_echo) {
        result = wsh_windows_executable_utf8(
            runtime, &application_utf8);
        if (result == WSH_OK) {
            result = wsh_string_create(
                &runtime->options.allocator,
                &runtime->options.limits,
                wsh_string_bytes(application_utf8),
                &stage->resolved);
        }
    } else {
        result = wsh_windows_runtime_resolve(
            runtime, context, command->subject, &stage->resolved);
    }
    if (result != WSH_OK) {
        wsh_string_destroy(application_utf8);
        return result;
    }
    resolved_view = wsh_string_bytes(stage->resolved);
    nested_wsh = command->shell_echo ||
        (!command->raw && wsh_windows_is_wsh_path(resolved_view));
    serialized_arguments = command->arguments;
    if (command->shell_echo) {
        result = wsh_windows_echo_arguments(
            runtime, command->arguments, &nested_arguments);
        serialized_arguments = nested_arguments;
        if (result == WSH_OK) {
            result = wsh_windows_to_wide(
                runtime,
                wsh_string_bytes(application_utf8),
                &stage->application,
                &stage->application_length);
        }
    } else if (nested_wsh) {
        result = wsh_windows_executable_utf8(runtime, &application_utf8);
        if (result == WSH_OK) {
            result = wsh_windows_nested_arguments(
                runtime,
                resolved_view,
                command->arguments,
                &nested_arguments);
        }
        serialized_arguments = nested_arguments;
        if (result == WSH_OK) {
            result = wsh_windows_to_wide(
                runtime,
                wsh_string_bytes(application_utf8),
                &stage->application,
                &stage->application_length);
        }
    } else {
        result = wsh_windows_to_wide(
            runtime,
            resolved_view,
            &stage->application,
            &stage->application_length);
    }
    if (result == WSH_OK && command->raw) {
        if (!runtime->options.allow_raw_launch) {
            result = WSH_ERR_INVALID;
        } else {
            result = wsh_windows_to_wide(
                runtime,
                command->raw_command_line,
                &stage->command_line,
                &stage->command_line_length);
            if (result == WSH_OK &&
                stage->command_line_length >= WSH_WINDOWS_COMMAND_UNITS) {
                result = WSH_ERR_RESOURCE;
            }
        }
    } else if (result == WSH_OK) {
        result = wsh_windows_runtime_serialize(
            runtime,
            nested_wsh ? wsh_string_bytes(application_utf8) :
                resolved_view,
            serialized_arguments,
            &stage->command_line,
            &stage->command_line_length);
    }
    if (result == WSH_OK) {
        result = wsh_windows_runtime_environment_block(
            runtime,
            context,
            nested_wsh,
            &stage->environment,
            &stage->environment_length);
    }
    wsh_string_destroy(application_utf8);
    wsh_value_destroy(nested_arguments);
    if (result != WSH_OK) {
        wsh_windows_stage_destroy(runtime, stage);
    }
    return result;
}

/** Duplicate a handle inside the current process with selected inheritance. */
static wsh_result wsh_windows_duplicate_handle(
    HANDLE source,
    int inheritable,
    HANDLE *out_handle)
{
    if (source == NULL || source == INVALID_HANDLE_VALUE ||
        out_handle == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_handle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            source,
            GetCurrentProcess(),
            out_handle,
            0U,
            inheritable ? TRUE : FALSE,
            DUPLICATE_SAME_ACCESS)) {
        return WSH_ERR_INTERNAL;
    }
    return WSH_OK;
}

/** Resolve and open one redirection target with explicit Win32 flags. */
static wsh_result wsh_windows_open_redirection(
    wsh_windows_runtime *runtime,
    wsh_string_view operand,
    DWORD access,
    DWORD disposition,
    HANDLE *out_handle)
{
    uint16_t *wide;
    size_t wide_length;
    uint16_t *absolute;
    size_t absolute_length;
    HANDLE handle;
    wsh_result result;

    wide = NULL;
    absolute = NULL;
    *out_handle = INVALID_HANDLE_VALUE;
    result = wsh_windows_to_wide(
        runtime, operand, &wide, &wide_length);
    if (result == WSH_OK) {
        result = wsh_windows_absolute_candidate(
            runtime,
            wide,
            wide_length,
            &absolute,
            &absolute_length);
    }
    if (result == WSH_OK) {
        handle = CreateFileW(
            (LPCWSTR)absolute,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            disposition,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            result = WSH_ERR_MISMATCH;
        } else {
            *out_handle = handle;
        }
    }
    wsh_windows_release(runtime, wide);
    wsh_windows_release(runtime, absolute);
    return result;
}

/** Write normalized here text to a delete-on-close temporary file. */
static wsh_result wsh_windows_open_here(
    wsh_windows_runtime *runtime,
    wsh_string_view operand,
    HANDLE *out_handle)
{
    char name[96];
    int name_length;
    wsh_string_view name_view;
    uint16_t *wide_name;
    size_t wide_length;
    uint16_t *absolute;
    size_t absolute_length;
    HANDLE handle;
    char *normalized;
    size_t normalized_length;
    size_t index;
    DWORD written;
    wsh_result result;

    *out_handle = INVALID_HANDLE_VALUE;
    normalized = NULL;
    if (operand.length > ((size_t)-1) / 2U ||
        operand.length * 2U > runtime->options.limits.max_string_bytes) {
        return WSH_ERR_RESOURCE;
    }
    normalized = (char *)wsh_windows_allocate(
        runtime, operand.length * 2U + 1U);
    if (normalized == NULL) {
        return WSH_ERR_RESOURCE;
    }
    normalized_length = 0U;
    for (index = 0U; index < operand.length; ++index) {
        if (operand.data[index] == '\n') {
            normalized[normalized_length++] = '\r';
        }
        normalized[normalized_length++] = operand.data[index];
    }
    result = WSH_ERR_MISMATCH;
    handle = INVALID_HANDLE_VALUE;
    for (index = 0U; index < 32U && result == WSH_ERR_MISMATCH; ++index) {
        name_length = snprintf(
            name,
            sizeof(name),
            ".wsh-here-%lu-%lu.tmp",
            (unsigned long)GetCurrentProcessId(),
            (unsigned long)wsh_windows_increment(&runtime->pipe_sequence));
        if (name_length <= 0 || (size_t)name_length >= sizeof(name)) {
            result = WSH_ERR_INTERNAL;
            break;
        }
        name_view.data = name;
        name_view.length = (size_t)name_length;
        wide_name = NULL;
        absolute = NULL;
        result = wsh_windows_to_wide(
            runtime, name_view, &wide_name, &wide_length);
        if (result == WSH_OK) {
            result = wsh_windows_absolute_candidate(
                runtime,
                wide_name,
                wide_length,
                &absolute,
                &absolute_length);
        }
        if (result == WSH_OK) {
            handle = CreateFileW(
                (LPCWSTR)absolute,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                NULL);
            result = handle == INVALID_HANDLE_VALUE ?
                WSH_ERR_MISMATCH : WSH_OK;
        }
        wsh_windows_release(runtime, wide_name);
        wsh_windows_release(runtime, absolute);
    }
    if (result == WSH_OK && normalized_length != 0U) {
        written = 0U;
        if (normalized_length > (size_t)0xffffffffUL ||
            !WriteFile(
                handle,
                normalized,
                (DWORD)normalized_length,
                &written,
                NULL) ||
            written != normalized_length) {
            result = WSH_ERR_INTERNAL;
        }
    }
    if (result == WSH_OK &&
        SetFilePointer(handle, 0L, NULL, FILE_BEGIN) ==
            INVALID_SET_FILE_POINTER &&
        GetLastError() != NO_ERROR) {
        result = WSH_ERR_INTERNAL;
    }
    wsh_windows_release(runtime, normalized);
    if (result == WSH_OK) {
        *out_handle = handle;
    } else if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    return result;
}

/** Apply one stage's descriptor actions in exact source order. */
static wsh_result wsh_windows_stage_apply_redirections(
    wsh_windows_runtime *runtime,
    const wsh_runtime_command *command,
    wsh_windows_stage *stage)
{
    size_t index;
    const wsh_runtime_redirection *action;
    HANDLE handle;
    DWORD access;
    DWORD disposition;
    wsh_result result;

    result = WSH_OK;
    for (index = 0U; result == WSH_OK &&
         index < command->redirection_count; ++index) {
        action = &command->redirections[index];
        if (action->target_descriptor >= WSH_WINDOWS_DESCRIPTORS ||
            (action->kind == WSH_RUNTIME_REDIRECT_DUPLICATE &&
             action->source_descriptor >= WSH_WINDOWS_DESCRIPTORS)) {
            return WSH_ERR_INVALID;
        }
        handle = INVALID_HANDLE_VALUE;
        if (action->kind == WSH_RUNTIME_REDIRECT_INPUT ||
            action->kind == WSH_RUNTIME_REDIRECT_OUTPUT ||
            action->kind == WSH_RUNTIME_REDIRECT_APPEND) {
            access = action->kind == WSH_RUNTIME_REDIRECT_INPUT ?
                GENERIC_READ : GENERIC_WRITE;
            disposition = action->kind == WSH_RUNTIME_REDIRECT_INPUT ?
                OPEN_EXISTING :
                action->kind == WSH_RUNTIME_REDIRECT_OUTPUT ?
                    CREATE_ALWAYS : OPEN_ALWAYS;
            result = wsh_windows_open_redirection(
                runtime,
                action->operand,
                access,
                disposition,
                &handle);
            if (result == WSH_OK &&
                action->kind == WSH_RUNTIME_REDIRECT_APPEND &&
                SetFilePointer(
                    handle, 0L, NULL, FILE_END) == INVALID_SET_FILE_POINTER &&
                GetLastError() != NO_ERROR) {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
                result = WSH_ERR_INTERNAL;
            }
        } else if (action->kind == WSH_RUNTIME_REDIRECT_HERE) {
            result = wsh_windows_open_here(
                runtime, action->operand, &handle);
        } else if (action->kind == WSH_RUNTIME_REDIRECT_DUPLICATE) {
            result = wsh_windows_duplicate_handle(
                stage->descriptors[action->source_descriptor],
                0,
                &handle);
        } else if (action->kind == WSH_RUNTIME_REDIRECT_CLOSE) {
            result = WSH_OK;
        } else {
            result = WSH_ERR_INVALID;
        }
        if (result == WSH_OK) {
            wsh_windows_stage_close_descriptor(
                stage, action->target_descriptor);
            if (action->kind != WSH_RUNTIME_REDIRECT_CLOSE) {
                stage->descriptors[action->target_descriptor] = handle;
                stage->descriptor_owned[action->target_descriptor] = 1;
            }
        } else if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
    return result;
}

/** Create one kill-on-close job unless tracked fallback is forced. */
static HANDLE wsh_windows_create_job(wsh_windows_runtime *runtime)
{
    HANDLE job;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;

    if (!runtime->capabilities.job_object) {
        runtime->capabilities.tracked_fallback = 1;
        return NULL;
    }
    job = runtime->create_job(NULL, NULL);
    if (job == NULL) {
        runtime->capabilities.tracked_fallback = 1;
        return NULL;
    }
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!runtime->set_job_information(
            job,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        CloseHandle(job);
        runtime->capabilities.tracked_fallback = 1;
        return NULL;
    }
    return job;
}

/** Acquire the process-wide temporary inheritance spin lock. */
static void wsh_windows_inheritance_acquire(void)
{
    while (InterlockedCompareExchange(
            &wsh_windows_inheritance_lock, 1L, 0L) != 0L) {
        Sleep(0U);
    }
}

/** Release the process-wide temporary inheritance spin lock. */
static void wsh_windows_inheritance_release(void)
{
    (void)InterlockedExchange(&wsh_windows_inheritance_lock, 0L);
}

/** Allocate one process group with a fixed bounded process capacity. */
static wsh_windows_group *wsh_windows_group_create(
    wsh_windows_runtime *runtime,
    size_t capacity)
{
    wsh_windows_group *group;
    size_t bytes;

    if (capacity == 0U || capacity > runtime->options.max_children ||
        !wsh_windows_multiply(capacity, sizeof(*group->processes), &bytes)) {
        return NULL;
    }
    group = (wsh_windows_group *)wsh_windows_allocate(
        runtime, sizeof(*group));
    if (group == NULL) {
        return NULL;
    }
    group->processes = (HANDLE *)wsh_windows_allocate(runtime, bytes);
    if (!wsh_windows_multiply(
            capacity, sizeof(*group->identifiers), &bytes)) {
        wsh_windows_group_destroy(runtime, group);
        return NULL;
    }
    group->identifiers = (DWORD *)wsh_windows_allocate(runtime, bytes);
    if (group->processes == NULL || group->identifiers == NULL) {
        wsh_windows_group_destroy(runtime, group);
        return NULL;
    }
    group->process_capacity = capacity;
    group->job = wsh_windows_create_job(runtime);
    return group;
}

/** Append a newly created process to its fixed-capacity group. */
static wsh_result wsh_windows_group_append(
    wsh_windows_group *group,
    HANDLE process,
    DWORD identifier)
{
    if (group == NULL || process == NULL ||
        process == INVALID_HANDLE_VALUE ||
        group->process_count >= group->process_capacity) {
        return WSH_ERR_INVALID;
    }
    group->processes[group->process_count] = process;
    group->identifiers[group->process_count] = identifier;
    group->process_count += 1U;
    if (group->root_identifier == 0U) {
        group->root_identifier = identifier;
    }
    return WSH_OK;
}

/** Fill a nested launch's private descriptor-map placeholders. */
static void wsh_windows_publish_descriptor_map(
    uint16_t *environment,
    HANDLE inherited[WSH_WINDOWS_DESCRIPTORS])
{
    static const WCHAR name[] = L"_WSH_FD_MAP_V1=";
    static const WCHAR digits[] = L"0123456789abcdef";
    size_t entry_length;
    size_t name_length;
    size_t index;
    size_t digit_index;
    uint64_t value;
    uint16_t *field;

    if (environment == NULL) {
        return;
    }
    name_length = sizeof(name) / sizeof(name[0]) - 1U;
    while (environment[0] != 0U) {
        entry_length = 0U;
        while (environment[entry_length] != 0U) {
            entry_length += 1U;
        }
        if (entry_length == name_length + 189U &&
            memcmp(environment, name, name_length * sizeof(name[0])) == 0) {
            field = environment + name_length;
            for (index = 0U;
                 index < WSH_WINDOWS_DESCRIPTORS;
                 ++index) {
                value = (uint64_t)(uintptr_t)inherited[index];
                for (digit_index = 0U; digit_index < 16U;
                     ++digit_index) {
                    field[2U + digit_index] = digits[
                        (value >> ((15U - digit_index) * 4U)) & 15U];
                }
                field += index + 1U < WSH_WINDOWS_DESCRIPTORS ?
                    19U : 18U;
            }
            return;
        }
        environment += entry_length + 1U;
    }
}

/** Create one suspended child with an exact temporary handle set. */
static wsh_result wsh_windows_create_suspended(
    wsh_windows_runtime *runtime,
    wsh_windows_stage *stage)
{
    HANDLE inherited[WSH_WINDOWS_DESCRIPTORS];
    HANDLE handle_list[WSH_WINDOWS_DESCRIPTORS];
    size_t handle_count;
    size_t index;
    STARTUPINFOW ordinary;
    wsh_startup_info_ex_w extended;
    STARTUPINFOW *startup;
    SIZE_T attribute_bytes;
    void *attribute_storage;
    DWORD creation_flags;
    WINBOOL created;
    wsh_result result;

    memset(inherited, 0, sizeof(inherited));
    memset(handle_list, 0, sizeof(handle_list));
    memset(&ordinary, 0, sizeof(ordinary));
    memset(&extended, 0, sizeof(extended));
    memset(&stage->process, 0, sizeof(stage->process));
    handle_count = 0U;
    attribute_storage = NULL;
    result = WSH_OK;
    wsh_windows_inheritance_acquire();
    for (index = 0U; result == WSH_OK &&
         index < WSH_WINDOWS_DESCRIPTORS; ++index) {
        if (stage->descriptors[index] == NULL ||
            stage->descriptors[index] == INVALID_HANDLE_VALUE) {
            inherited[index] = NULL;
            continue;
        }
        result = wsh_windows_duplicate_handle(
            stage->descriptors[index], 1, &inherited[index]);
        if (result == WSH_OK) {
            handle_list[handle_count++] = inherited[index];
        }
    }
    ordinary.cb = sizeof(ordinary);
    ordinary.dwFlags = STARTF_USESTDHANDLES;
    ordinary.hStdInput = inherited[0];
    ordinary.hStdOutput = inherited[1];
    ordinary.hStdError = inherited[2];
    startup = &ordinary;
    creation_flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED |
        CREATE_NEW_PROCESS_GROUP;
    if (result == WSH_OK &&
        runtime->capabilities.explicit_handle_list && handle_count != 0U) {
        attribute_bytes = 0U;
        (void)runtime->initialize_attributes(
            NULL, 1U, 0U, &attribute_bytes);
        attribute_storage = wsh_windows_allocate(
            runtime, (size_t)attribute_bytes);
        if (attribute_storage == NULL) {
            result = WSH_ERR_RESOURCE;
        } else {
            extended.startup = ordinary;
            extended.startup.cb = sizeof(extended);
            extended.attributes = attribute_storage;
            if (!runtime->initialize_attributes(
                    extended.attributes,
                    1U,
                    0U,
                    &attribute_bytes) ||
                !runtime->update_attribute(
                    extended.attributes,
                    0U,
                    (DWORD_PTR)PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    handle_list,
                    handle_count * sizeof(handle_list[0]),
                    NULL,
                    NULL)) {
                result = WSH_ERR_INTERNAL;
            } else {
                startup = &extended.startup;
                creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
            }
        }
    }
    created = FALSE;
    if (result == WSH_OK) {
        wsh_windows_publish_descriptor_map(
            stage->environment, inherited);
        created = CreateProcessW(
            (LPCWSTR)stage->application,
            (LPWSTR)stage->command_line,
            NULL,
            NULL,
            handle_count != 0U ? TRUE : FALSE,
            creation_flags,
            stage->environment,
            (LPCWSTR)runtime->working_directory,
            startup,
            &stage->process);
        if (!created) {
            result = WSH_ERR_MISMATCH;
        }
    }
    if (extended.attributes != NULL &&
        runtime->delete_attributes != NULL) {
        runtime->delete_attributes(extended.attributes);
    }
    wsh_windows_release(runtime, attribute_storage);
    for (index = 0U; index < WSH_WINDOWS_DESCRIPTORS; ++index) {
        if (inherited[index] != NULL &&
            inherited[index] != INVALID_HANDLE_VALUE) {
            CloseHandle(inherited[index]);
        }
    }
    wsh_windows_inheritance_release();
    return result;
}

/** Assign one suspended child to its job or select tracked fallback. */
static wsh_result wsh_windows_assign_group(
    wsh_windows_runtime *runtime,
    wsh_windows_group *group,
    wsh_windows_stage *stage)
{
    if (group->job != NULL &&
        !runtime->assign_process_job(
            group->job, stage->process.hProcess)) {
        if (group->process_count != 0U) {
            return WSH_ERR_INTERNAL;
        }
        CloseHandle(group->job);
        group->job = NULL;
        runtime->capabilities.tracked_fallback = 1;
    }
    return WSH_OK;
}

/** Connect, start, and collect one named-pipe provider. */
static DWORD WINAPI wsh_windows_substitution_thread(void *user_data)
{
    wsh_windows_substitution *substitution;
    DWORD error;
    size_t index;

    substitution = (wsh_windows_substitution *)user_data;
    if (!ConnectNamedPipe(substitution->pipe, NULL)) {
        error = GetLastError();
        if (error != ERROR_PIPE_CONNECTED && error != ERROR_NO_DATA) {
            (void)TerminateProcess(
                substitution->group->processes[0],
                WSH_WINDOWS_LAUNCH_STATUS);
        }
    }
    (void)ResumeThread(substitution->process_thread);
    CloseHandle(substitution->process_thread);
    substitution->process_thread = NULL;
    for (index = 0U;
         index < substitution->group->process_count;
         ++index) {
        (void)WaitForSingleObject(
            substitution->group->processes[index], INFINITE);
    }
    (void)FlushFileBuffers(substitution->pipe);
    (void)DisconnectNamedPipe(substitution->pipe);
    CloseHandle(substitution->pipe);
    substitution->pipe = INVALID_HANDLE_VALUE;
    return 0U;
}

/** Destroy one already joined named-pipe provider record. */
static void wsh_windows_substitution_destroy(
    wsh_windows_runtime *runtime,
    wsh_windows_substitution *substitution)
{
    if (substitution == NULL) {
        return;
    }
    if (substitution->process_thread != NULL) {
        CloseHandle(substitution->process_thread);
    }
    if (substitution->worker != NULL) {
        CloseHandle(substitution->worker);
    }
    if (substitution->pipe != NULL &&
        substitution->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(substitution->pipe);
    }
    wsh_windows_group_destroy(runtime, substitution->group);
    wsh_windows_release(runtime, substitution->path);
    wsh_windows_release(runtime, substitution);
}

/** Cancel or collect every registered named-pipe provider. */
static void wsh_windows_collect_substitutions(
    wsh_windows_runtime *runtime,
    int cancel)
{
    size_t index;
    wsh_windows_substitution *substitution;
    HANDLE peer;
    DWORD wait_result;

    if (runtime == NULL) {
        return;
    }
    for (index = 0U; index < runtime->substitution_count; ++index) {
        substitution = runtime->substitutions[index];
        if (cancel && substitution->pipe != INVALID_HANDLE_VALUE) {
            peer = CreateFileW(
                (LPCWSTR)substitution->path,
                substitution->client_access,
                0U,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
            if (peer != INVALID_HANDLE_VALUE) {
                CloseHandle(peer);
            }
        }
        wait_result = WaitForSingleObject(
            substitution->worker, cancel ? 25U : INFINITE);
        if (wait_result == WAIT_TIMEOUT) {
            wsh_windows_group_force(
                runtime,
                substitution->group,
                WSH_WINDOWS_CANCEL_STATUS);
            (void)WaitForSingleObject(substitution->worker, INFINITE);
        }
        wsh_windows_substitution_destroy(runtime, substitution);
    }
    runtime->substitution_count = 0U;
    wsh_windows_release(runtime, runtime->substitutions);
    runtime->substitutions = NULL;
    runtime->substitution_capacity = 0U;
}

/** Prepare and register one simple named-pipe provider. */
static wsh_result wsh_windows_process_substitution(
    wsh_windows_runtime *runtime,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    char path[128];
    int path_length;
    uint16_t *wide_path;
    size_t wide_length;
    DWORD pipe_access;
    DWORD client_access;
    unsigned descriptor;
    HANDLE pipe;
    wsh_windows_stage stage;
    wsh_windows_group *group;
    wsh_windows_substitution *substitution;
    wsh_string_view path_view;
    wsh_result result;

    if (request->launch_plan == NULL ||
        request->launch_plan->command_count != 1U ||
        request->launch_plan->edge_count != 0U) {
        return WSH_ERR_INVALID;
    }
    if (wsh_string_view_equal(
            request->subject, wsh_string_view_from_cstr("read"))) {
        pipe_access = PIPE_ACCESS_OUTBOUND;
        client_access = GENERIC_READ;
        descriptor = 1U;
    } else if (wsh_string_view_equal(
            request->subject, wsh_string_view_from_cstr("write"))) {
        pipe_access = PIPE_ACCESS_INBOUND;
        client_access = GENERIC_WRITE;
        descriptor = 0U;
    } else if (wsh_string_view_equal(
            request->subject, wsh_string_view_from_cstr("duplex"))) {
        pipe_access = PIPE_ACCESS_DUPLEX;
        client_access = GENERIC_READ | GENERIC_WRITE;
        descriptor = 0U;
    } else {
        return WSH_ERR_INVALID;
    }
    path_length = snprintf(
        path,
        sizeof(path),
        "\\\\.\\pipe\\wsh-%lu-%08lx-%ld",
        (unsigned long)GetCurrentProcessId(),
        (unsigned long)(runtime->nonce & 0xffffffffUL),
        (long)wsh_windows_increment(&runtime->pipe_sequence));
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
        return WSH_ERR_INTERNAL;
    }
    path_view.data = path;
    path_view.length = (size_t)path_length;
    wide_path = NULL;
    result = wsh_windows_to_wide(
        runtime, path_view, &wide_path, &wide_length);
    pipe = INVALID_HANDLE_VALUE;
    if (result == WSH_OK) {
        pipe = CreateNamedPipeW(
            (LPCWSTR)wide_path,
            pipe_access | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1U,
            65536U,
            65536U,
            0U,
            NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            result = WSH_ERR_INTERNAL;
        }
    }
    memset(&stage, 0, sizeof(stage));
    group = NULL;
    substitution = NULL;
    if (result == WSH_OK) {
        result = wsh_windows_stage_prepare(
            runtime,
            request->context,
            &request->launch_plan->commands[0],
            &stage);
    }
    if (result == WSH_OK) {
        result = wsh_windows_stage_apply_redirections(
            runtime,
            &request->launch_plan->commands[0],
            &stage);
    }
    if (result == WSH_OK) {
        wsh_windows_stage_close_descriptor(&stage, descriptor);
        stage.descriptors[descriptor] = pipe;
        if (pipe_access == PIPE_ACCESS_DUPLEX) {
            wsh_windows_stage_close_descriptor(&stage, 1U);
            stage.descriptors[1U] = pipe;
        }
        group = wsh_windows_group_create(runtime, 1U);
        result = group == NULL ? WSH_ERR_RESOURCE : WSH_OK;
    }
    if (result == WSH_OK) {
        result = wsh_windows_create_suspended(runtime, &stage);
    }
    if (result == WSH_OK) {
        result = wsh_windows_assign_group(runtime, group, &stage);
    }
    if (result == WSH_OK) {
        result = wsh_windows_group_append(
            group,
            stage.process.hProcess,
            stage.process.dwProcessId);
        if (result == WSH_OK) {
            stage.process.hProcess = NULL;
        }
    }
    if (result == WSH_OK) {
        substitution = (wsh_windows_substitution *)
            wsh_windows_allocate(runtime, sizeof(*substitution));
        if (substitution == NULL) {
            result = WSH_ERR_RESOURCE;
        }
    }
    if (result == WSH_OK) {
        substitution->runtime = runtime;
        substitution->pipe = pipe;
        substitution->process_thread = stage.process.hThread;
        substitution->group = group;
        substitution->path = wide_path;
        substitution->client_access = client_access;
        pipe = INVALID_HANDLE_VALUE;
        stage.process.hThread = NULL;
        group = NULL;
        wide_path = NULL;
        substitution->worker = CreateThread(
            NULL,
            0U,
            wsh_windows_substitution_thread,
            substitution,
            0U,
            NULL);
        if (substitution->worker == NULL) {
            result = WSH_ERR_RESOURCE;
        }
    }
    if (result == WSH_OK) {
        result = wsh_windows_grow(
            runtime,
            (void **)&runtime->substitutions,
            sizeof(*runtime->substitutions),
            runtime->substitution_count,
            &runtime->substitution_capacity,
            runtime->substitution_count + 1U,
            runtime->options.max_children);
    }
    if (result == WSH_OK) {
        runtime->substitutions[runtime->substitution_count++] =
            substitution;
        substitution = NULL;
        result = wsh_value_builder_append(output, path_view);
    }
    if (result == WSH_OK) {
        result = wsh_windows_append_status(status, 0U);
    }
    if (result != WSH_OK && substitution != NULL) {
        if (substitution->worker != NULL) {
            HANDLE peer;

            peer = CreateFileW(
                (LPCWSTR)substitution->path,
                substitution->client_access,
                0U,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
            if (peer != INVALID_HANDLE_VALUE) {
                CloseHandle(peer);
            }
            (void)WaitForSingleObject(substitution->worker, 25U);
        }
        wsh_windows_group_force(
            runtime,
            substitution->group,
            WSH_WINDOWS_LAUNCH_STATUS);
        if (substitution->worker != NULL) {
            (void)WaitForSingleObject(substitution->worker, INFINITE);
        }
        wsh_windows_substitution_destroy(runtime, substitution);
    }
    if (result != WSH_OK && group != NULL) {
        wsh_windows_group_force(
            runtime, group, WSH_WINDOWS_LAUNCH_STATUS);
    }
    wsh_windows_group_destroy(runtime, group);
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
    }
    wsh_windows_release(runtime, wide_path);
    wsh_windows_stage_destroy(runtime, &stage);
    return result;
}

/** Read capture bytes concurrently until every inherited writer closes. */
static DWORD WINAPI wsh_windows_capture_thread(void *user_data)
{
    wsh_capture_state *state;
    char chunk[4096];
    DWORD received;
    DWORD error;
    size_t required;
    size_t next;
    char *replacement;

    state = (wsh_capture_state *)user_data;
    for (;;) {
        received = 0U;
        if (!ReadFile(
                state->handle,
                chunk,
                sizeof(chunk),
                &received,
                NULL)) {
            error = GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_HANDLE_EOF) {
                state->failed = 1;
            }
            break;
        }
        if (received == 0U) {
            break;
        }
        if (received > state->runtime->options.max_capture_bytes -
                state->length) {
            state->failed = 1;
            break;
        }
        required = state->length + received;
        if (required > state->capacity) {
            next = state->capacity == 0U ? 4096U : state->capacity;
            while (next < required) {
                next = next >
                    state->runtime->options.max_capture_bytes / 2U ?
                    state->runtime->options.max_capture_bytes : next * 2U;
            }
            replacement = (char *)wsh_windows_allocate(
                state->runtime, next);
            if (replacement == NULL) {
                state->failed = 1;
                break;
            }
            if (state->length != 0U) {
                memcpy(replacement, state->bytes, state->length);
            }
            wsh_windows_release(state->runtime, state->bytes);
            state->bytes = replacement;
            state->capacity = next;
        }
        memcpy(state->bytes + state->length, chunk, received);
        state->length = required;
    }
    CloseHandle(state->handle);
    state->handle = INVALID_HANDLE_VALUE;
    return 0U;
}

/** Wait one group and append statuses in source order. */
static wsh_result wsh_windows_wait_group(
    wsh_windows_runtime *runtime,
    wsh_windows_group *group,
    uint32_t timeout,
    wsh_status_builder *status)
{
    DWORD start;
    DWORD now;
    DWORD elapsed;
    DWORD remaining;
    DWORD wait_result;
    DWORD code;
    size_t index;
    wsh_result result;

    start = GetTickCount();
    result = WSH_OK;
    for (index = 0U; index < group->process_count; ++index) {
        remaining = INFINITE;
        if (timeout != 0U) {
            now = GetTickCount();
            elapsed = now - start;
            remaining = elapsed >= timeout ? 0U : timeout - elapsed;
        }
        wait_result = WaitForSingleObject(
            group->processes[index], remaining);
        if (wait_result == WAIT_TIMEOUT) {
            wsh_windows_group_force(
                runtime, group, WSH_WINDOWS_TIMEOUT_STATUS);
            break;
        }
        if (wait_result != WAIT_OBJECT_0) {
            result = WSH_ERR_INTERNAL;
            break;
        }
    }
    for (index = 0U; result == WSH_OK &&
         index < group->process_count; ++index) {
        code = group->cancellation_status;
        if (!group->cancelled &&
            !GetExitCodeProcess(group->processes[index], &code)) {
            result = WSH_ERR_INTERNAL;
        }
        if (result == WSH_OK) {
            result = wsh_status_builder_append(status, (uint32_t)code);
        }
    }
    return result;
}

/** Retain one completed launch group as outstanding background work. */
static wsh_result wsh_windows_register_group(
    wsh_windows_runtime *runtime,
    wsh_windows_group *group)
{
    wsh_result result;

    result = wsh_windows_grow(
        runtime,
        (void **)&runtime->groups,
        sizeof(*runtime->groups),
        runtime->group_count,
        &runtime->group_capacity,
        runtime->group_count + 1U,
        runtime->options.max_children);
    if (result == WSH_OK) {
        runtime->groups[runtime->group_count++] = group;
    }
    return result;
}

/** Launch one validated command or pipeline plan. */
static wsh_result wsh_windows_launch_plan(
    wsh_windows_runtime *runtime,
    const wsh_context *context,
    const wsh_runtime_launch_plan *plan,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    wsh_windows_stage *stages;
    size_t stage_bytes;
    size_t index;
    HANDLE pipe_read;
    HANDLE pipe_write;
    wsh_windows_group *group;
    wsh_capture_state capture;
    HANDLE capture_thread;
    uint32_t timeout;
    wsh_result result;

    if (plan == NULL || plan->commands == NULL ||
        plan->command_count == 0U ||
        plan->command_count > runtime->options.max_children ||
        plan->edge_count + 1U != plan->command_count ||
        (plan->edge_count != 0U && plan->edges == NULL) ||
        ((plan->flags & WSH_RUNTIME_LAUNCH_BACKGROUND) != 0U &&
         (plan->flags & WSH_RUNTIME_LAUNCH_CAPTURE) != 0U)) {
        return WSH_ERR_INVALID;
    }
    if (!wsh_windows_multiply(
            plan->command_count, sizeof(*stages), &stage_bytes)) {
        return WSH_ERR_RESOURCE;
    }
    stages = (wsh_windows_stage *)wsh_windows_allocate(
        runtime, stage_bytes);
    if (stages == NULL) {
        return WSH_ERR_RESOURCE;
    }
    group = NULL;
    memset(&capture, 0, sizeof(capture));
    capture.runtime = runtime;
    capture.handle = INVALID_HANDLE_VALUE;
    capture_thread = NULL;
    result = WSH_OK;
    for (index = 0U; result == WSH_OK &&
         index < plan->command_count; ++index) {
        result = wsh_windows_stage_prepare(
            runtime, context, &plan->commands[index], &stages[index]);
    }
    for (index = 0U; result == WSH_OK && index < plan->edge_count;
         ++index) {
        if (plan->edges[index].output_descriptor >=
                WSH_WINDOWS_DESCRIPTORS ||
            plan->edges[index].input_descriptor >=
                WSH_WINDOWS_DESCRIPTORS ||
            !CreatePipe(&pipe_read, &pipe_write, NULL, 0U)) {
            result = WSH_ERR_INTERNAL;
            break;
        }
        wsh_windows_stage_close_descriptor(
            &stages[index], plan->edges[index].output_descriptor);
        stages[index].descriptors[
            plan->edges[index].output_descriptor] = pipe_write;
        stages[index].descriptor_owned[
            plan->edges[index].output_descriptor] = 1;
        wsh_windows_stage_close_descriptor(
            &stages[index + 1U], plan->edges[index].input_descriptor);
        stages[index + 1U].descriptors[
            plan->edges[index].input_descriptor] = pipe_read;
        stages[index + 1U].descriptor_owned[
            plan->edges[index].input_descriptor] = 1;
    }
    if (result == WSH_OK &&
        (plan->flags & WSH_RUNTIME_LAUNCH_CAPTURE) != 0U) {
        if (!CreatePipe(&pipe_read, &pipe_write, NULL, 0U)) {
            result = WSH_ERR_INTERNAL;
        } else {
            capture.handle = pipe_read;
            wsh_windows_stage_close_descriptor(
                &stages[plan->command_count - 1U], 1U);
            stages[plan->command_count - 1U].descriptors[1] = pipe_write;
            stages[plan->command_count - 1U].descriptor_owned[1] = 1;
        }
    }
    for (index = 0U; result == WSH_OK &&
         index < plan->command_count; ++index) {
        result = wsh_windows_stage_apply_redirections(
            runtime, &plan->commands[index], &stages[index]);
    }
    if (result == WSH_OK) {
        group = wsh_windows_group_create(runtime, plan->command_count);
        if (group == NULL) {
            result = WSH_ERR_RESOURCE;
        }
    }
    for (index = 0U; result == WSH_OK &&
         index < plan->command_count; ++index) {
        result = wsh_windows_create_suspended(runtime, &stages[index]);
        if (result == WSH_OK) {
            result = wsh_windows_assign_group(
                runtime, group, &stages[index]);
        }
        if (result == WSH_OK) {
            result = wsh_windows_group_append(
                group,
                stages[index].process.hProcess,
                stages[index].process.dwProcessId);
            if (result == WSH_OK) {
                stages[index].process.hProcess = NULL;
            }
        }
    }
    if (result == WSH_OK && capture.handle != INVALID_HANDLE_VALUE) {
        capture_thread = CreateThread(
            NULL, 0U, wsh_windows_capture_thread, &capture, 0U, NULL);
        if (capture_thread == NULL) {
            result = WSH_ERR_RESOURCE;
        }
    }
    for (index = 0U; result == WSH_OK &&
         index < plan->command_count; ++index) {
        if (ResumeThread(stages[index].process.hThread) == (DWORD)-1) {
            result = WSH_ERR_INTERNAL;
        }
        CloseHandle(stages[index].process.hThread);
        stages[index].process.hThread = NULL;
    }
    for (index = 0U; index < plan->command_count; ++index) {
        size_t descriptor;

        for (descriptor = 0U; descriptor < WSH_WINDOWS_DESCRIPTORS;
             ++descriptor) {
            wsh_windows_stage_close_descriptor(
                &stages[index], descriptor);
        }
    }
    if (result != WSH_OK && group != NULL &&
        group->process_count != 0U) {
        wsh_windows_group_force(
            runtime, group, WSH_WINDOWS_LAUNCH_STATUS);
    }
    if (result != WSH_OK) {
        for (index = 0U; index < plan->command_count; ++index) {
            if (stages[index].process.hProcess != NULL) {
                (void)TerminateProcess(
                    stages[index].process.hProcess,
                    WSH_WINDOWS_LAUNCH_STATUS);
                (void)WaitForSingleObject(
                    stages[index].process.hProcess, INFINITE);
            }
        }
    }
    if (capture.handle != INVALID_HANDLE_VALUE && capture_thread == NULL) {
        CloseHandle(capture.handle);
        capture.handle = INVALID_HANDLE_VALUE;
    }
    if (result == WSH_OK &&
        (plan->flags & WSH_RUNTIME_LAUNCH_BACKGROUND) != 0U) {
        char identifier[16];
        int length;
        wsh_string_view identifier_view;

        result = wsh_windows_register_group(runtime, group);
        if (result == WSH_OK) {
            length = snprintf(
                identifier,
                sizeof(identifier),
                "%lu",
                (unsigned long)group->root_identifier);
            identifier_view.data = identifier;
            identifier_view.length = length > 0 ? (size_t)length : 0U;
            result = length > 0 && (size_t)length < sizeof(identifier) ?
                wsh_value_builder_append(output, identifier_view) :
                WSH_ERR_INTERNAL;
        }
        if (result == WSH_OK) {
            result = wsh_status_builder_append(status, 0U);
            group = NULL;
        }
    } else if (result == WSH_OK) {
        timeout = plan->timeout_milliseconds != 0U ?
            plan->timeout_milliseconds :
            runtime->options.default_timeout_milliseconds;
        result = wsh_windows_wait_group(
            runtime, group, timeout, status);
    }
    if (capture_thread != NULL) {
        (void)WaitForSingleObject(capture_thread, INFINITE);
        CloseHandle(capture_thread);
        capture_thread = NULL;
        if (result == WSH_OK && capture.failed) {
            result = WSH_ERR_RESOURCE;
        }
        if (result == WSH_OK && capture.length != 0U) {
            wsh_string_view captured;

            captured.data = capture.bytes;
            captured.length = capture.length;
            result = wsh_value_builder_append(output, captured);
        }
    }
    if (result != WSH_OK && group != NULL &&
        group->process_count != 0U) {
        wsh_windows_group_force(
            runtime, group, WSH_WINDOWS_LAUNCH_STATUS);
    }
    wsh_windows_release(runtime, capture.bytes);
    wsh_windows_group_destroy(runtime, group);
    for (index = 0U; index < plan->command_count; ++index) {
        wsh_windows_stage_destroy(runtime, &stages[index]);
    }
    wsh_windows_release(runtime, stages);
    if ((plan->flags & WSH_RUNTIME_LAUNCH_BACKGROUND) == 0U) {
        wsh_windows_collect_substitutions(
            runtime, result == WSH_OK ? 0 : 1);
    } else if (result != WSH_OK) {
        wsh_windows_collect_substitutions(runtime, 1);
    }
    return result;
}

/** Append one shell-generated status while preserving a successful callback. */
static wsh_result wsh_windows_append_status(
    wsh_status_builder *status,
    uint32_t code)
{
    return wsh_status_builder_append(status, code);
}

/** Read and decode one source file beneath the logical directory. */
static wsh_result wsh_windows_read_source(
    wsh_windows_runtime *runtime,
    wsh_string_view path,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    HANDLE handle;
    DWORD high;
    DWORD low;
    size_t length;
    unsigned char *bytes;
    size_t offset;
    DWORD received;
    wsh_source *source;
    wsh_string_view text;
    wsh_result result;

    handle = INVALID_HANDLE_VALUE;
    bytes = NULL;
    source = NULL;
    result = wsh_windows_open_redirection(
        runtime, path, GENERIC_READ, OPEN_EXISTING, &handle);
    if (result != WSH_OK) {
        return wsh_windows_append_status(status, 5U);
    }
    high = 0U;
    SetLastError(NO_ERROR);
    low = GetFileSize(handle, &high);
    if ((low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) ||
        high != 0U || low > runtime->options.limits.max_source_bytes) {
        result = WSH_ERR_RESOURCE;
    }
    length = low;
    if (result == WSH_OK) {
        bytes = (unsigned char *)wsh_windows_allocate(
            runtime, length == 0U ? 1U : length);
        if (bytes == NULL) {
            result = WSH_ERR_RESOURCE;
        }
    }
    offset = 0U;
    while (result == WSH_OK && offset < length) {
        received = 0U;
        if (!ReadFile(
                handle,
                bytes + offset,
                (DWORD)(length - offset),
                &received,
                NULL) || received == 0U) {
            result = WSH_ERR_INTERNAL;
        } else {
            offset += received;
        }
    }
    CloseHandle(handle);
    if (result == WSH_OK) {
        result = wsh_source_create(
            &runtime->options.allocator,
            &runtime->options.limits,
            bytes,
            length,
            &source);
    }
    if (result == WSH_OK) {
        text = wsh_source_text(source);
        result = wsh_value_builder_append(output, text);
    }
    if (result == WSH_OK) {
        result = wsh_windows_append_status(status, 0U);
    }
    wsh_source_destroy(source);
    wsh_windows_release(runtime, bytes);
    return result;
}

/** Enumerate filesystem candidates for one unquoted path pattern. */
static wsh_result wsh_windows_match_paths(
    wsh_windows_runtime *runtime,
    wsh_string_view pattern,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    uint16_t *wide;
    size_t wide_length;
    uint16_t *absolute;
    size_t absolute_length;
    size_t prefix_length;
    size_t index;
    HANDLE find;
    WIN32_FIND_DATAW data;
    wsh_wide_buffer candidate;
    uint16_t *candidate_units;
    size_t candidate_length;
    char *candidate_utf8;
    size_t candidate_bytes;
    wsh_string_view candidate_view;
    wsh_result result;

    wide = NULL;
    absolute = NULL;
    find = INVALID_HANDLE_VALUE;
    result = wsh_windows_to_wide(
        runtime, pattern, &wide, &wide_length);
    if (result == WSH_OK) {
        result = wsh_windows_absolute_candidate(
            runtime,
            wide,
            wide_length,
            &absolute,
            &absolute_length);
    }
    prefix_length = 0U;
    for (index = 0U; index < wide_length; ++index) {
        if (wsh_windows_is_separator(wide[index])) {
            prefix_length = index + 1U;
        }
    }
    if (result == WSH_OK) {
        find = FindFirstFileW((LPCWSTR)absolute, &data);
        if (find == INVALID_HANDLE_VALUE &&
            GetLastError() != ERROR_FILE_NOT_FOUND &&
            GetLastError() != ERROR_PATH_NOT_FOUND) {
            result = WSH_ERR_MISMATCH;
        }
    }
    while (result == WSH_OK && find != INVALID_HANDLE_VALUE) {
        size_t file_length;

        file_length = 0U;
        while (data.cFileName[file_length] != 0U) {
            file_length += 1U;
        }
        if (!((file_length == 1U && data.cFileName[0] == L'.') ||
              (file_length == 2U && data.cFileName[0] == L'.' &&
               data.cFileName[1] == L'.'))) {
            wsh_wide_buffer_init(
                runtime,
                &candidate,
                runtime->options.limits.max_string_bytes);
            candidate_units = NULL;
            candidate_utf8 = NULL;
            result = wsh_wide_buffer_append(
                &candidate, wide, prefix_length);
            if (result == WSH_OK) {
                result = wsh_wide_buffer_append(
                    &candidate,
                    (const uint16_t *)data.cFileName,
                    file_length);
            }
            if (result == WSH_OK) {
                result = wsh_wide_buffer_finish(
                    &candidate,
                    &candidate_units,
                    &candidate_length);
            }
            wsh_wide_buffer_destroy(&candidate);
            if (result == WSH_OK) {
                result = wsh_windows_from_wide(
                    runtime,
                    candidate_units,
                    candidate_length,
                    &candidate_utf8,
                    &candidate_bytes);
            }
            candidate_view.data = candidate_utf8;
            candidate_view.length = candidate_bytes;
            if (result == WSH_OK) {
                result = wsh_value_builder_append(output, candidate_view);
            }
            wsh_windows_release(runtime, candidate_units);
            wsh_windows_release(runtime, candidate_utf8);
        }
        if (result == WSH_OK && !FindNextFileW(find, &data)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) {
                result = WSH_ERR_INTERNAL;
            }
            break;
        }
    }
    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
    }
    if (result == WSH_OK) {
        result = wsh_windows_append_status(status, 0U);
    }
    wsh_windows_release(runtime, wide);
    wsh_windows_release(runtime, absolute);
    return result;
}

/** Write evaluator text through one selected runtime descriptor mapping. */
static wsh_result wsh_windows_write(
    wsh_windows_runtime *runtime,
    const wsh_runtime_request *request,
    wsh_status_builder *status)
{
    wsh_windows_stage stage;
    const wsh_runtime_command *command;
    unsigned descriptor;
    wsh_string_view text;
    DWORD written;
    wsh_result result;

    if (request->arguments == NULL ||
        wsh_value_count(request->arguments) != 1U ||
        wsh_value_at(request->arguments, 0U, &text) != WSH_OK) {
        return WSH_ERR_INVALID;
    }
    memset(&stage, 0, sizeof(stage));
    wsh_windows_stage_descriptors_init(runtime, &stage);
    command = request->launch_plan != NULL &&
        request->launch_plan->command_count == 1U ?
        &request->launch_plan->commands[0] : NULL;
    result = command == NULL ? WSH_OK :
        wsh_windows_stage_apply_redirections(runtime, command, &stage);
    descriptor = wsh_string_view_equal(
        request->subject, wsh_string_view_from_cstr("stderr")) ? 2U : 1U;
    if (result == WSH_OK &&
        (stage.descriptors[descriptor] == NULL ||
         stage.descriptors[descriptor] == INVALID_HANDLE_VALUE)) {
        result = WSH_ERR_MISMATCH;
    }
    if (result == WSH_OK && text.length != 0U) {
        written = 0U;
        if (text.length > 0xffffffffUL ||
            !WriteFile(
                stage.descriptors[descriptor],
                text.data,
                (DWORD)text.length,
                &written,
                NULL) || written != text.length) {
            result = WSH_ERR_INTERNAL;
        }
    }
    wsh_windows_stage_destroy(runtime, &stage);
    if (result == WSH_OK) {
        result = wsh_windows_append_status(status, 0U);
    }
    return result;
}

/** Set the isolated logical working directory after type validation. */
static wsh_result wsh_windows_set_directory(
    wsh_windows_runtime *runtime,
    wsh_string_view path,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    uint16_t *wide;
    size_t wide_length;
    uint16_t *absolute;
    size_t absolute_length;
    DWORD attributes;
    char *utf8;
    size_t utf8_length;
    uint16_t *drive_copy;
    size_t drive;
    wsh_string_view view;
    wsh_result result;

    wide = NULL;
    absolute = NULL;
    utf8 = NULL;
    drive_copy = NULL;
    result = wsh_windows_to_wide(
        runtime, path, &wide, &wide_length);
    if (result == WSH_OK) {
        result = wsh_windows_absolute_candidate(
            runtime,
            wide,
            wide_length,
            &absolute,
            &absolute_length);
    }
    if (result == WSH_OK) {
        attributes = GetFileAttributesW((LPCWSTR)absolute);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            result = WSH_ERR_MISMATCH;
        }
    }
    if (result == WSH_OK) {
        result = wsh_windows_from_wide(
            runtime,
            absolute,
            absolute_length,
            &utf8,
            &utf8_length);
    }
    if (result == WSH_OK) {
        drive = 26U;
        if (absolute_length >= 2U &&
            absolute[1] == (uint16_t)':') {
            drive = absolute[0] >= (uint16_t)'a' ?
                (size_t)(absolute[0] - (uint16_t)'a') :
                (size_t)(absolute[0] - (uint16_t)'A');
        }
        if (drive < 26U) {
            result = wsh_windows_copy_wide_path(
                runtime, absolute, absolute_length, &drive_copy);
        }
    }
    if (result == WSH_OK) {
        wsh_windows_release(runtime, runtime->working_directory);
        runtime->working_directory = absolute;
        runtime->working_directory_length = absolute_length;
        absolute = NULL;
        if (drive < 26U) {
            wsh_windows_release(
                runtime, runtime->drive_directories[drive]);
            runtime->drive_directories[drive] = drive_copy;
            runtime->drive_directory_lengths[drive] =
                runtime->working_directory_length;
            drive_copy = NULL;
        }
        view.data = utf8;
        view.length = utf8_length;
        result = wsh_value_builder_append(output, view);
    }
    if (result == WSH_OK) {
        result = wsh_windows_append_status(status, 0U);
    }
    wsh_windows_release(runtime, wide);
    wsh_windows_release(runtime, absolute);
    wsh_windows_release(runtime, drive_copy);
    wsh_windows_release(runtime, utf8);
    return result;
}

/** Parse one nonzero decimal background process identifier. */
static int wsh_windows_parse_identifier(
    wsh_string_view text,
    DWORD *out_identifier)
{
    uint32_t value;
    size_t index;
    unsigned digit;

    if (text.length == 0U || out_identifier == NULL) {
        return 0;
    }
    value = 0U;
    for (index = 0U; index < text.length; ++index) {
        if (text.data[index] < '0' || text.data[index] > '9') {
            return 0;
        }
        digit = (unsigned)(text.data[index] - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return 0;
        }
        value = value * 10U + digit;
    }
    if (value == 0U) {
        return 0;
    }
    *out_identifier = (DWORD)value;
    return 1;
}

/** Find one background group by its exposed root identifier. */
static size_t wsh_windows_find_group(
    const wsh_windows_runtime *runtime,
    DWORD identifier)
{
    size_t index;

    for (index = 0U; index < runtime->group_count; ++index) {
        if (runtime->groups[index]->root_identifier == identifier) {
            return index;
        }
    }
    return runtime->group_count;
}

/** Remove and return one background group without destroying it. */
static wsh_windows_group *wsh_windows_take_group(
    wsh_windows_runtime *runtime,
    size_t index)
{
    wsh_windows_group *group;

    if (index >= runtime->group_count) {
        return NULL;
    }
    group = runtime->groups[index];
    if (index + 1U < runtime->group_count) {
        memmove(
            &runtime->groups[index],
            &runtime->groups[index + 1U],
            (runtime->group_count - index - 1U) *
                sizeof(*runtime->groups));
    }
    runtime->group_count -= 1U;
    return group;
}

/** Wait selected background groups or every group in launch order. */
static wsh_result wsh_windows_wait_background(
    wsh_windows_runtime *runtime,
    const wsh_value *arguments,
    wsh_status_builder *status)
{
    size_t argument_index;
    size_t group_index;
    wsh_string_view item;
    DWORD identifier;
    wsh_windows_group *group;
    wsh_result result;
    int had_groups;

    result = WSH_OK;
    if (wsh_value_count(arguments) == 0U) {
        had_groups = runtime->group_count != 0U;
        while (result == WSH_OK && runtime->group_count != 0U) {
            group = wsh_windows_take_group(runtime, 0U);
            result = wsh_windows_wait_group(runtime, group, 0U, status);
            wsh_windows_group_destroy(runtime, group);
        }
        if (result == WSH_OK && !had_groups) {
            /* An empty wait still produces one successful simple status. */
            result = wsh_windows_append_status(status, 0U);
        }
        if (result == WSH_OK) {
            wsh_windows_collect_substitutions(runtime, 0);
        }
        return result;
    }
    for (argument_index = 0U; result == WSH_OK &&
         argument_index < wsh_value_count(arguments); ++argument_index) {
        result = wsh_value_at(arguments, argument_index, &item);
        if (result == WSH_OK &&
            !wsh_windows_parse_identifier(item, &identifier)) {
            result = WSH_ERR_INVALID;
        }
        group_index = result == WSH_OK ?
            wsh_windows_find_group(runtime, identifier) :
            runtime->group_count;
        if (result == WSH_OK && group_index == runtime->group_count) {
            result = WSH_ERR_MISMATCH;
        }
        if (result == WSH_OK) {
            group = wsh_windows_take_group(runtime, group_index);
            result = wsh_windows_wait_group(runtime, group, 0U, status);
            wsh_windows_group_destroy(runtime, group);
        }
    }
    if (result == WSH_OK) {
        wsh_windows_collect_substitutions(runtime, 0);
    }
    return result;
}

/** Cancel selected background groups or every retained group. */
static wsh_result wsh_windows_cancel_background(
    wsh_windows_runtime *runtime,
    const wsh_value *arguments,
    wsh_status_builder *status)
{
    size_t index;
    size_t group_index;
    wsh_string_view item;
    DWORD identifier;
    wsh_windows_group *group;
    wsh_result result;

    result = WSH_OK;
    if (wsh_value_count(arguments) == 0U) {
        while (runtime->group_count != 0U) {
            group = wsh_windows_take_group(runtime, 0U);
            wsh_windows_group_force(
                runtime, group, WSH_WINDOWS_CANCEL_STATUS);
            wsh_windows_group_destroy(runtime, group);
        }
        wsh_windows_collect_substitutions(runtime, 1);
        return wsh_windows_append_status(status, WSH_WINDOWS_CANCEL_STATUS);
    }
    for (index = 0U; result == WSH_OK &&
         index < wsh_value_count(arguments); ++index) {
        result = wsh_value_at(arguments, index, &item);
        if (result == WSH_OK &&
            !wsh_windows_parse_identifier(item, &identifier)) {
            result = WSH_ERR_INVALID;
        }
        group_index = result == WSH_OK ?
            wsh_windows_find_group(runtime, identifier) :
            runtime->group_count;
        if (result == WSH_OK && group_index == runtime->group_count) {
            result = WSH_ERR_MISMATCH;
        }
        if (result == WSH_OK) {
            group = wsh_windows_take_group(runtime, group_index);
            wsh_windows_group_force(
                runtime, group, WSH_WINDOWS_CANCEL_STATUS);
            wsh_windows_group_destroy(runtime, group);
        }
    }
    if (result == WSH_OK) {
        wsh_windows_collect_substitutions(runtime, 1);
        result = wsh_windows_append_status(status, WSH_WINDOWS_CANCEL_STATUS);
    }
    return result;
}

/** Dispatch one abstract operation to the concrete Windows runtime. */
static wsh_result wsh_windows_invoke(
    void *user_data,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    wsh_windows_runtime *runtime;
    wsh_result result;
    uint32_t code;
    wsh_string_view path;

    runtime = (wsh_windows_runtime *)user_data;
    if (runtime == NULL || request == NULL || output == NULL ||
        status == NULL) {
        return WSH_ERR_INVALID;
    }
    if (request->operation == WSH_RUNTIME_LAUNCH ||
        request->operation == WSH_RUNTIME_PIPELINE) {
        result = wsh_windows_launch_plan(
            runtime,
            request->context,
            request->launch_plan,
            output,
            status);
        if (result == WSH_OK) {
            return WSH_OK;
        }
        if (result == WSH_ERR_ENCODING ||
            (result == WSH_ERR_RESOURCE && request->launch_plan != NULL &&
             (request->launch_plan->flags &
              WSH_RUNTIME_LAUNCH_CAPTURE) != 0U)) {
            return result;
        }
        code = result == WSH_ERR_MISMATCH ? 7U :
            result == WSH_ERR_INVALID ? 4U :
            result == WSH_ERR_RESOURCE ? 9U : 8U;
        return wsh_windows_append_status(status, code);
    }
    if (request->operation == WSH_RUNTIME_READ_SOURCE) {
        return wsh_windows_read_source(
            runtime, request->subject, output, status);
    }
    if (request->operation == WSH_RUNTIME_MATCH_PATHS) {
        return wsh_windows_match_paths(
            runtime, request->subject, output, status);
    }
    if (request->operation == WSH_RUNTIME_WRITE) {
        return wsh_windows_write(runtime, request, status);
    }
    if (request->operation == WSH_RUNTIME_WORKING_DIRECTORY) {
        if (request->arguments == NULL ||
            wsh_value_count(request->arguments) != 1U ||
            wsh_value_at(request->arguments, 0U, &path) != WSH_OK) {
            return WSH_ERR_INVALID;
        }
        return wsh_windows_set_directory(runtime, path, output, status);
    }
    if (request->operation == WSH_RUNTIME_WAIT) {
        return wsh_windows_wait_background(
            runtime, request->arguments, status);
    }
    if (request->operation == WSH_RUNTIME_CANCEL) {
        return wsh_windows_cancel_background(
            runtime, request->arguments, status);
    }
    if (request->operation == WSH_RUNTIME_PROCESS_SUBSTITUTION) {
        return wsh_windows_process_substitution(
            runtime, request, output, status);
    }
    return WSH_ERR_INVALID;
}

/** @brief Implements wsh_windows_runtime_interface. */
wsh_runtime wsh_windows_runtime_interface(wsh_windows_runtime *runtime)
{
    wsh_runtime interface;

    memset(&interface, 0, sizeof(interface));
    interface.user_data = runtime;
    interface.invoke = wsh_windows_invoke;
    interface.names_equal = wsh_windows_names_equal;
    return interface;
}
