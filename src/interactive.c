/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file interactive.c
 * @brief Native Windows console editor, completion, history, and signals.
 */

#include "interactive.h"

#include "standard_library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Maximum complete pending UTF-8 source size. */
#define WSH_INTERACTIVE_INPUT_LIMIT (16U * 1024U * 1024U)
/** Maximum retained interactive history entries. */
#define WSH_INTERACTIVE_HISTORY_ENTRIES 5000U
/** Maximum retained history file and command-byte total. */
#define WSH_INTERACTIVE_HISTORY_BYTES (4U * 1024U * 1024U)
/** Maximum deterministic completion candidates. */
#define WSH_INTERACTIVE_CANDIDATES 1024U

/** Return the length of one zero-terminated UTF-16 sequence. */
static size_t interactive_wide_length(const uint16_t *units)
{
    size_t length;

    length = 0U;
    while (units[length] != 0U) {
        length += 1U;
    }
    return length;
}

/** Append one ASCII string to a bounded UTF-16 buffer. */
static size_t interactive_wide_ascii(
    uint16_t *destination,
    size_t capacity,
    size_t offset,
    const char *text)
{
    while (*text != '\0' && offset + 1U < capacity) {
        destination[offset++] = (uint16_t)(unsigned char)*text++;
    }
    destination[offset] = 0U;
    return offset;
}

/** Append one fixed eight-digit hexadecimal value as UTF-16. */
static size_t interactive_wide_hex(
    uint16_t *destination,
    size_t capacity,
    size_t offset,
    uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift;

    for (shift = 28U; shift <= 28U && offset + 1U < capacity;
         shift -= 4U) {
        destination[offset++] =
            (uint16_t)digits[(value >> shift) & 0x0fU];
        if (shift == 0U) {
            break;
        }
    }
    destination[offset] = 0U;
    return offset;
}

/** One inert retained history record. */
typedef struct wsh_history_entry {
    /** Owned strict UTF-8 command bytes followed by NUL. */
    char *command;
    /** Command bytes excluding NUL. */
    size_t length;
    /** Serialized JSONL record bytes including CRLF. */
    size_t serialized_length;
    /** RFC 3339 UTC timestamp. */
    char time[21];
} wsh_history_entry;

/** One owned completion spelling. */
typedef struct wsh_completion_candidate {
    /** Owned strict UTF-8 candidate. */
    char *text;
    /** Candidate bytes excluding NUL. */
    size_t length;
} wsh_completion_candidate;

/** Optional current-Windows ordinal comparison resolved at runtime. */
typedef int (WINAPI *wsh_compare_string_ordinal_fn)(
    LPCWSTR,
    int,
    LPCWSTR,
    int,
    BOOL);

/** Executable-owned mutable interactive state. */
struct wsh_interactive_session {
    /** Copied immutable session dependencies and bounds. */
    wsh_interactive_options options;
    /** Saved caller console input mode. */
    DWORD original_input_mode;
    /** Nonzero when original_input_mode is valid. */
    int mode_saved;
    /** Nonzero when advanced input-record editing is available. */
    int advanced;
    /** Nonzero after the fallback diagnostic was written. */
    int fallback_reported;
    /** Owned pending UTF-16 command. */
    uint16_t *units;
    /** Pending UTF-16 units. */
    size_t length;
    /** Allocated pending units. */
    size_t capacity;
    /** Cursor UTF-16 unit offset. */
    size_t cursor;
    /** Nonzero selects overwrite insertion. */
    int overwrite;
    /** Owned strict UTF-8 command returned to the front end. */
    char *bytes;
    /** Returned UTF-8 byte count. */
    size_t byte_length;
    /** Default allocator used for core conversions. */
    wsh_allocator allocator;
    /** Current primary prompt as owned UTF-16. */
    uint16_t *primary_prompt;
    /** Current primary prompt units. */
    size_t primary_length;
    /** Current continuation prompt as owned UTF-16. */
    uint16_t *continuation_prompt;
    /** Current continuation prompt units. */
    size_t continuation_length;
    /** Editor origin immediately following the primary prompt. */
    COORD origin;
    /** Number of screen cells cleared by the next redraw. */
    DWORD rendered_cells;
    /** Owned retained history records. */
    wsh_history_entry *history;
    /** Number of retained history records. */
    size_t history_count;
    /** Sum of serialized record bytes, excluding the file header. */
    size_t history_bytes;
    /** Allocated history slots. */
    size_t history_capacity;
    /** Current navigation index, count means the unpublished command. */
    size_t history_index;
    /** Nonzero suppresses persistence of the active submission. */
    int suppress_current;
    /** Nonzero after post-profile history initialization completed. */
    int history_ready;
    /** Nonzero after a malformed-history warning was written. */
    int history_warned;
    /** Owned absolute UTF-16 history path. */
    uint16_t *history_path;
    /** History path units excluding NUL. */
    size_t history_path_length;
    /** Owned path-derived cross-process mutex. */
    HANDLE history_mutex;
    /** Nonzero when this session may replace history. */
    int history_writable;
    /** Monotonic suffix component for same-tick temporary files. */
    uint32_t history_serial;
    /** Owned current completion set. */
    wsh_completion_candidate *candidates;
    /** Number of completion candidates. */
    size_t candidate_count;
    /** Allocated completion slots. */
    size_t candidate_capacity;
    /** Pending-buffer start of the active completion token. */
    size_t completion_start;
    /** Index used by repeated forward/reverse completion. */
    size_t completion_index;
    /** Nonzero while repeated Tab may cycle the retained set. */
    int completion_active;
    /** Nonzero preserves an existing apostrophe-quoted token. */
    int completion_quoted;
    /** Nonzero after one EOF refusal with live background work. */
    int eof_refused;
    /** Nonzero after an evaluator exit was accepted. */
    int stop_requested;
    /** Nonzero after sigexit was invoked. */
    int sigexit_invoked;
    /** Nonzero after excess prompt elements were diagnosed. */
    int prompt_warned;
};

/** Return whether a handle currently names a console device. */
static int interactive_is_console(HANDLE handle)
{
    DWORD mode;

    return handle != NULL && handle != INVALID_HANDLE_VALUE &&
        GetConsoleMode(handle, &mode) != 0;
}

/** Write exact bytes to a non-console handle. */
static int interactive_write_bytes(
    HANDLE handle,
    const char *bytes,
    size_t length)
{
    DWORD chunk;
    DWORD written;
    size_t offset;

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

/** Write exact UTF-16 units to a console handle. */
static int interactive_write_wide(
    HANDLE handle,
    const uint16_t *units,
    size_t length)
{
    size_t offset;
    DWORD chunk;
    DWORD written;

    offset = 0U;
    while (offset < length) {
        chunk = length - offset > 32768U ?
            32768U : (DWORD)(length - offset);
        written = 0U;
        if (!WriteConsoleW(
                handle,
                units + offset,
                chunk,
                &written,
                NULL) || written != chunk) {
            return 0;
        }
        offset += written;
    }
    return 1;
}

/** Write strict UTF-8 through a console-wide or raw byte boundary. */
static int interactive_write_utf8(
    wsh_interactive_session *session,
    HANDLE handle,
    const char *bytes,
    size_t length)
{
    uint16_t *wide;
    size_t wide_length;
    wsh_string_view text;
    wsh_result result;
    int written;

    if (!interactive_is_console(handle)) {
        return interactive_write_bytes(handle, bytes, length);
    }
    text.data = bytes;
    text.length = length;
    wide = NULL;
    result = wsh_utf8_to_utf16(
        &session->allocator,
        NULL,
        text,
        &wide,
        &wide_length);
    if (result != WSH_OK) {
        return 0;
    }
    written = interactive_write_wide(handle, wide, wide_length);
    wsh_allocator_release(&session->allocator, wide);
    return written;
}

/** Write one NUL-terminated diagnostic. */
static int interactive_warning(
    wsh_interactive_session *session,
    const char *message)
{
    return interactive_write_utf8(
        session,
        session->options.error,
        message,
        strlen(message));
}

/** Grow a bounded heap array without exposing partial state. */
static void *interactive_reserve(
    void *buffer,
    size_t *capacity,
    size_t required,
    size_t element_size,
    size_t maximum)
{
    size_t next;
    void *replacement;

    if (required <= *capacity) {
        return buffer;
    }
    if (required > maximum || element_size == 0U ||
        required > (size_t)-1 / element_size) {
        return NULL;
    }
    next = *capacity == 0U ? 32U : *capacity;
    while (next < required) {
        next = next > maximum / 2U ? maximum : next * 2U;
    }
    replacement = realloc(buffer, next * element_size);
    if (replacement == NULL) {
        return NULL;
    }
    *capacity = next;
    return replacement;
}

/** Copy one bounded strict UTF-8 sequence with a final NUL. */
static char *interactive_copy_bytes(const char *bytes, size_t length)
{
    char *copy;

    if (length == (size_t)-1) {
        return NULL;
    }
    copy = (char *)malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0U) {
        memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    return copy;
}

/** Destroy one retained history record. */
static void interactive_history_entry_destroy(wsh_history_entry *entry)
{
    if (entry != NULL) {
        free(entry->command);
        memset(entry, 0, sizeof(*entry));
    }
}

/** Remove all retained history records. */
static void interactive_history_clear(
    wsh_interactive_session *session)
{
    size_t index;

    for (index = 0U; index < session->history_count; ++index) {
        interactive_history_entry_destroy(&session->history[index]);
    }
    session->history_count = 0U;
    session->history_bytes = 0U;
    session->history_index = 0U;
}

/** Format the current UTC time as the accepted JSONL timestamp. */
static void interactive_history_time(char out_time[21])
{
    SYSTEMTIME time;

    GetSystemTime(&time);
    (void)snprintf(
        out_time,
        21U,
        "%04u-%02u-%02uT%02u:%02u:%02uZ",
        (unsigned)time.wYear,
        (unsigned)time.wMonth,
        (unsigned)time.wDay,
        (unsigned)time.wHour,
        (unsigned)time.wMinute,
        (unsigned)time.wSecond);
}

/** Return whether text has the fixed RFC 3339 UTC shape. */
static int interactive_history_time_valid(
    const char *text,
    size_t length)
{
    size_t index;
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    unsigned days_in_month;
    static const unsigned month_days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if (length != 20U || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        text[19] != 'Z') {
        return 0;
    }
    for (index = 0U; index < 19U; ++index) {
        if (index == 4U || index == 7U || index == 10U ||
            index == 13U || index == 16U) {
            continue;
        }
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }
    }
    year = (unsigned)(text[0] - '0') * 1000U +
        (unsigned)(text[1] - '0') * 100U +
        (unsigned)(text[2] - '0') * 10U +
        (unsigned)(text[3] - '0');
    month = (unsigned)(text[5] - '0') * 10U +
        (unsigned)(text[6] - '0');
    day = (unsigned)(text[8] - '0') * 10U +
        (unsigned)(text[9] - '0');
    hour = (unsigned)(text[11] - '0') * 10U +
        (unsigned)(text[12] - '0');
    minute = (unsigned)(text[14] - '0') * 10U +
        (unsigned)(text[15] - '0');
    second = (unsigned)(text[17] - '0') * 10U +
        (unsigned)(text[18] - '0');
    if (month == 0U || month > 12U || day == 0U ||
        hour > 23U || minute > 59U || second > 60U) {
        return 0;
    }
    days_in_month = month_days[month - 1U];
    if (month == 2U && (year % 4U == 0U) &&
        (year % 100U != 0U || year % 400U == 0U)) {
        days_in_month = 29U;
    }
    return day <= days_in_month;
}

/** Return the exact serialized JSONL bytes for one command record. */
static int interactive_history_serialized_length(
    const char *bytes,
    size_t length,
    size_t *out_length)
{
    size_t index;
    size_t escaped;
    size_t added;
    unsigned char character;

    escaped = 0U;
    for (index = 0U; index < length; ++index) {
        character = (unsigned char)bytes[index];
        if (character < 0x20U && character != '\n' &&
            character != '\r' && character != '\t') {
            added = 6U;
        } else if (character == '"' || character == '\\' ||
            character == '\n' || character == '\r' ||
            character == '\t') {
            added = 2U;
        } else {
            added = 1U;
        }
        if (escaped > (size_t)-1 - added) {
            return 0;
        }
        escaped += added;
    }
    if (escaped > (size_t)-1 - 46U) {
        return 0;
    }
    *out_length = escaped + 46U;
    return 1;
}

/** Append one validated history command with optional supplied time. */
static int interactive_history_append(
    wsh_interactive_session *session,
    const char *bytes,
    size_t length,
    const char *time)
{
    wsh_history_entry *replacement;
    wsh_history_entry *entry;
    size_t removed_length;
    size_t serialized_length;
    size_t record_budget;

    if (length > session->options.max_history_bytes ||
        wsh_utf8_validate(
            (wsh_string_view){bytes, length}, NULL) != WSH_OK) {
        return 0;
    }
    if (session->options.max_history_bytes <= 38U ||
        !interactive_history_serialized_length(
            bytes, length, &serialized_length)) {
        return 0;
    }
    record_budget = session->options.max_history_bytes - 38U;
    if (serialized_length > record_budget) {
        return 0;
    }
    if (session->history_count != 0U) {
        entry = &session->history[session->history_count - 1U];
        if (entry->length == length &&
            memcmp(entry->command, bytes, length) == 0) {
            return 1;
        }
    }
    if (session->options.max_history_entries == 0U) {
        return 1;
    }
    while (session->history_count != 0U &&
        (session->history_count ==
             session->options.max_history_entries ||
         session->history_bytes > record_budget - serialized_length)) {
        removed_length = session->history[0].serialized_length;
        interactive_history_entry_destroy(&session->history[0]);
        memmove(
            session->history,
            session->history + 1U,
            (session->history_count - 1U) *
                sizeof(*session->history));
        session->history_count -= 1U;
        session->history_bytes -= removed_length;
    }
    replacement = (wsh_history_entry *)interactive_reserve(
        session->history,
        &session->history_capacity,
        session->history_count + 1U,
        sizeof(*session->history),
        session->options.max_history_entries);
    if (replacement == NULL) {
        return 0;
    }
    session->history = replacement;
    entry = &session->history[session->history_count];
    memset(entry, 0, sizeof(*entry));
    entry->command = interactive_copy_bytes(bytes, length);
    if (entry->command == NULL) {
        return 0;
    }
    entry->length = length;
    entry->serialized_length = serialized_length;
    if (time == NULL) {
        interactive_history_time(entry->time);
    } else {
        memcpy(entry->time, time, 21U);
    }
    session->history_count += 1U;
    session->history_bytes += serialized_length;
    session->history_index = session->history_count;
    return 1;
}

/** Resolve the default per-user history path from APPDATA. */
static int interactive_history_path(
    wsh_interactive_session *session)
{
    static const uint16_t suffix[] =
        L"\\Waughtal\\WSH\\history.txt";
    DWORD required;
    DWORD received;
    size_t suffix_length;
    uint16_t *path;

    required = GetEnvironmentVariableW(L"APPDATA", NULL, 0U);
    if (required == 0U) {
        return 0;
    }
    suffix_length = sizeof(suffix) / sizeof(suffix[0]) - 1U;
    if ((size_t)required > (size_t)-1 - suffix_length) {
        return 0;
    }
    path = (uint16_t *)malloc(
        ((size_t)required + suffix_length) * sizeof(*path));
    if (path == NULL) {
        return 0;
    }
    received = GetEnvironmentVariableW(
        L"APPDATA", (LPWSTR)path, required);
    if (received == 0U || received >= required) {
        free(path);
        return 0;
    }
    memcpy(
        path + received,
        suffix,
        (suffix_length + 1U) * sizeof(*path));
    session->history_path = path;
    session->history_path_length = received + suffix_length;
    return 1;
}

/** Acquire the path-derived process mutex used for history replacement. */
static int interactive_history_lock(
    wsh_interactive_session *session)
{
    uint32_t hash;
    size_t index;
    WCHAR name[64];
    DWORD wait_result;
    size_t name_length;

    hash = 2166136261UL;
    for (index = 0U; index < session->history_path_length; ++index) {
        uint16_t unit;

        unit = session->history_path[index];
        if (unit >= (uint16_t)'A' && unit <= (uint16_t)'Z') {
            unit = (uint16_t)(unit + ((uint16_t)'a' - (uint16_t)'A'));
        }
        hash ^= unit;
        hash *= 16777619UL;
    }
    name_length = interactive_wide_ascii(
        (uint16_t *)name,
        sizeof(name) / sizeof(name[0]),
        0U,
        "Local\\WSH-History-");
    (void)interactive_wide_hex(
        (uint16_t *)name,
        sizeof(name) / sizeof(name[0]),
        name_length,
        hash);
    session->history_mutex = CreateMutexW(NULL, FALSE, name);
    if (session->history_mutex == NULL) {
        return 0;
    }
    wait_result = WaitForSingleObject(session->history_mutex, 0U);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
        CloseHandle(session->history_mutex);
        session->history_mutex = NULL;
        return 0;
    }
    session->history_writable = 1;
    return 1;
}

/** Append one UTF-8 scalar to a growable byte string. */
static int interactive_json_scalar(
    char **bytes,
    size_t *length,
    size_t *capacity,
    uint32_t scalar)
{
    char encoded[4];
    size_t count;
    char *replacement;

    if (scalar <= 0x7fU) {
        encoded[0] = (char)scalar;
        count = 1U;
    } else if (scalar <= 0x7ffU) {
        encoded[0] = (char)(0xc0U | (scalar >> 6U));
        encoded[1] = (char)(0x80U | (scalar & 0x3fU));
        count = 2U;
    } else if (scalar <= 0xffffU) {
        encoded[0] = (char)(0xe0U | (scalar >> 12U));
        encoded[1] = (char)(0x80U | ((scalar >> 6U) & 0x3fU));
        encoded[2] = (char)(0x80U | (scalar & 0x3fU));
        count = 3U;
    } else {
        encoded[0] = (char)(0xf0U | (scalar >> 18U));
        encoded[1] = (char)(0x80U | ((scalar >> 12U) & 0x3fU));
        encoded[2] = (char)(0x80U | ((scalar >> 6U) & 0x3fU));
        encoded[3] = (char)(0x80U | (scalar & 0x3fU));
        count = 4U;
    }
    replacement = (char *)interactive_reserve(
        *bytes,
        capacity,
        *length + count + 1U,
        sizeof(**bytes),
        WSH_INTERACTIVE_HISTORY_BYTES + 1U);
    if (replacement == NULL) {
        return 0;
    }
    *bytes = replacement;
    memcpy(*bytes + *length, encoded, count);
    *length += count;
    (*bytes)[*length] = '\0';
    return 1;
}

/** Parse four hexadecimal digits from one JSON escape. */
static int interactive_json_hex(
    const char *bytes,
    size_t length,
    size_t *offset,
    uint16_t *out_unit)
{
    size_t index;
    unsigned value;
    unsigned digit;

    if (*offset > length || length - *offset < 4U) {
        return 0;
    }
    value = 0U;
    for (index = 0U; index < 4U; ++index) {
        unsigned char character;

        character = (unsigned char)bytes[(*offset)++];
        if (character >= '0' && character <= '9') {
            digit = character - '0';
        } else if (character >= 'a' && character <= 'f') {
            digit = character - 'a' + 10U;
        } else if (character >= 'A' && character <= 'F') {
            digit = character - 'A' + 10U;
        } else {
            return 0;
        }
        value = value * 16U + digit;
    }
    *out_unit = (uint16_t)value;
    return 1;
}

/** Parse one quoted JSON string into owned strict UTF-8. */
static int interactive_json_string(
    const char *bytes,
    size_t length,
    size_t *offset,
    char **out_text,
    size_t *out_length)
{
    char *text;
    size_t text_length;
    size_t capacity;
    unsigned char character;
    uint16_t unit;
    uint16_t low;
    uint32_t scalar;

    *out_text = NULL;
    *out_length = 0U;
    if (*offset >= length || bytes[(*offset)++] != '"') {
        return 0;
    }
    text = NULL;
    text_length = 0U;
    capacity = 0U;
    while (*offset < length && bytes[*offset] != '"') {
        character = (unsigned char)bytes[(*offset)++];
        if (character < 0x20U) {
            free(text);
            return 0;
        }
        if (character != '\\') {
            char *replacement;

            replacement = (char *)interactive_reserve(
                text,
                &capacity,
                text_length + 2U,
                sizeof(*text),
                WSH_INTERACTIVE_HISTORY_BYTES + 1U);
            if (replacement == NULL) {
                free(text);
                return 0;
            }
            text = replacement;
            text[text_length++] = (char)character;
            text[text_length] = '\0';
            continue;
        }
        if (*offset >= length) {
            free(text);
            return 0;
        }
        character = (unsigned char)bytes[(*offset)++];
        if (character == '"' || character == '\\' || character == '/') {
            scalar = character;
        } else if (character == 'b') {
            scalar = 0x08U;
        } else if (character == 'f') {
            scalar = 0x0cU;
        } else if (character == 'n') {
            scalar = 0x0aU;
        } else if (character == 'r') {
            scalar = 0x0dU;
        } else if (character == 't') {
            scalar = 0x09U;
        } else if (character == 'u' &&
            interactive_json_hex(bytes, length, offset, &unit)) {
            scalar = unit;
            if (unit >= 0xd800U && unit <= 0xdbffU) {
                if (*offset + 2U > length || bytes[*offset] != '\\' ||
                    bytes[*offset + 1U] != 'u') {
                    free(text);
                    return 0;
                }
                *offset += 2U;
                if (!interactive_json_hex(
                        bytes, length, offset, &low) ||
                    low < 0xdc00U || low > 0xdfffU) {
                    free(text);
                    return 0;
                }
                scalar = 0x10000U +
                    (((uint32_t)unit - 0xd800U) << 10U) +
                    ((uint32_t)low - 0xdc00U);
            } else if (unit >= 0xdc00U && unit <= 0xdfffU) {
                free(text);
                return 0;
            }
        } else {
            free(text);
            return 0;
        }
        if (!interactive_json_scalar(
                &text,
                &text_length,
                &capacity,
                scalar)) {
            free(text);
            return 0;
        }
    }
    if (*offset >= length || bytes[(*offset)++] != '"' ||
        wsh_utf8_validate(
            (wsh_string_view){text, text_length}, NULL) != WSH_OK) {
        free(text);
        return 0;
    }
    if (text == NULL) {
        text = interactive_copy_bytes("", 0U);
    }
    *out_text = text;
    *out_length = text_length;
    return text != NULL;
}

/** Skip JSON whitespace within one retained record. */
static void interactive_json_whitespace(
    const char *bytes,
    size_t length,
    size_t *offset)
{
    while (*offset < length &&
        (bytes[*offset] == ' ' || bytes[*offset] == '\t' ||
         bytes[*offset] == '\r' || bytes[*offset] == '\n')) {
        *offset += 1U;
    }
}

/** Skip one bounded JSON number. */
static int interactive_json_number(
    const char *bytes,
    size_t length,
    size_t *offset)
{
    size_t start;

    start = *offset;
    if (*offset < length && bytes[*offset] == '-') {
        *offset += 1U;
    }
    if (*offset >= length) {
        return 0;
    }
    if (bytes[*offset] == '0') {
        *offset += 1U;
    } else if (bytes[*offset] >= '1' && bytes[*offset] <= '9') {
        do {
            *offset += 1U;
        } while (*offset < length &&
            bytes[*offset] >= '0' && bytes[*offset] <= '9');
    } else {
        return 0;
    }
    if (*offset < length && bytes[*offset] == '.') {
        *offset += 1U;
        if (*offset >= length || bytes[*offset] < '0' ||
            bytes[*offset] > '9') {
            return 0;
        }
        do {
            *offset += 1U;
        } while (*offset < length &&
            bytes[*offset] >= '0' && bytes[*offset] <= '9');
    }
    if (*offset < length &&
        (bytes[*offset] == 'e' || bytes[*offset] == 'E')) {
        *offset += 1U;
        if (*offset < length &&
            (bytes[*offset] == '+' || bytes[*offset] == '-')) {
            *offset += 1U;
        }
        if (*offset >= length || bytes[*offset] < '0' ||
            bytes[*offset] > '9') {
            return 0;
        }
        do {
            *offset += 1U;
        } while (*offset < length &&
            bytes[*offset] >= '0' && bytes[*offset] <= '9');
    }
    return *offset != start;
}

/** Skip one syntactically valid bounded JSON value. */
static int interactive_json_value(
    const char *bytes,
    size_t length,
    size_t *offset,
    unsigned depth)
{
    char *text;
    size_t text_length;
    char open;
    char close;

    interactive_json_whitespace(bytes, length, offset);
    if (*offset >= length || depth > 64U) {
        return 0;
    }
    if (bytes[*offset] == '"') {
        text = NULL;
        text_length = 0U;
        if (!interactive_json_string(
                bytes, length, offset, &text, &text_length)) {
            return 0;
        }
        free(text);
        return 1;
    }
    if (length - *offset >= 4U &&
        (memcmp(bytes + *offset, "true", 4U) == 0 ||
         memcmp(bytes + *offset, "null", 4U) == 0)) {
        *offset += 4U;
        return 1;
    }
    if (length - *offset >= 5U &&
        memcmp(bytes + *offset, "false", 5U) == 0) {
        *offset += 5U;
        return 1;
    }
    if (bytes[*offset] != '{' && bytes[*offset] != '[') {
        return interactive_json_number(bytes, length, offset);
    }
    open = bytes[(*offset)++];
    close = open == '{' ? '}' : ']';
    interactive_json_whitespace(bytes, length, offset);
    if (*offset < length && bytes[*offset] == close) {
        *offset += 1U;
        return 1;
    }
    for (;;) {
        if (open == '{') {
            text = NULL;
            text_length = 0U;
            if (!interactive_json_string(
                    bytes, length, offset, &text, &text_length)) {
                return 0;
            }
            free(text);
            interactive_json_whitespace(bytes, length, offset);
            if (*offset >= length || bytes[(*offset)++] != ':') {
                return 0;
            }
        }
        if (!interactive_json_value(
                bytes, length, offset, depth + 1U)) {
            return 0;
        }
        interactive_json_whitespace(bytes, length, offset);
        if (*offset < length && bytes[*offset] == close) {
            *offset += 1U;
            return 1;
        }
        if (*offset >= length || bytes[(*offset)++] != ',') {
            return 0;
        }
        interactive_json_whitespace(bytes, length, offset);
    }
}

/** Parse one accepted history entry and ignore bounded unknown fields. */
static int interactive_history_record(
    wsh_interactive_session *session,
    const char *line,
    size_t length)
{
    size_t offset;
    char *key;
    size_t key_length;
    char *time;
    size_t time_length;
    char *command;
    size_t command_length;
    int have_time;
    int have_command;
    int result;

    offset = 0U;
    interactive_json_whitespace(line, length, &offset);
    if (offset >= length || line[offset++] != '{') {
        return 0;
    }
    time = NULL;
    command = NULL;
    time_length = 0U;
    command_length = 0U;
    have_time = 0;
    have_command = 0;
    for (;;) {
        interactive_json_whitespace(line, length, &offset);
        if (offset < length && line[offset] == '}') {
            offset += 1U;
            break;
        }
        key = NULL;
        key_length = 0U;
        if (!interactive_json_string(
                line, length, &offset, &key, &key_length)) {
            goto invalid;
        }
        interactive_json_whitespace(line, length, &offset);
        if (offset >= length || line[offset++] != ':') {
            free(key);
            goto invalid;
        }
        interactive_json_whitespace(line, length, &offset);
        if (key_length == 4U && memcmp(key, "time", 4U) == 0) {
            free(key);
            if (have_time || !interactive_json_string(
                    line, length, &offset, &time, &time_length) ||
                !interactive_history_time_valid(time, time_length)) {
                goto invalid;
            }
            have_time = 1;
        } else if (key_length == 7U &&
            memcmp(key, "command", 7U) == 0) {
            free(key);
            if (have_command || !interactive_json_string(
                    line, length, &offset, &command, &command_length)) {
                goto invalid;
            }
            have_command = 1;
        } else {
            free(key);
            if (!interactive_json_value(line, length, &offset, 0U)) {
                goto invalid;
            }
        }
        interactive_json_whitespace(line, length, &offset);
        if (offset < length && line[offset] == ',') {
            offset += 1U;
            interactive_json_whitespace(line, length, &offset);
            if (offset >= length || line[offset] == '}') {
                goto invalid;
            }
            continue;
        }
        if (offset < length && line[offset] == '}') {
            offset += 1U;
            break;
        }
        goto invalid;
    }
    interactive_json_whitespace(line, length, &offset);
    if (!have_time || !have_command || offset != length) {
        goto invalid;
    }
    result = interactive_history_append(
        session, command, command_length, time);
    free(time);
    free(command);
    return result;

invalid:
    {
        free(time);
        free(command);
        return 0;
    }
}

/** Load one bounded complete history file into inert memory. */
static wsh_result interactive_history_read(
    wsh_interactive_session *session)
{
    static const char header[] =
        "{\"format\":\"wsh-history\",\"version\":1}";
    HANDLE file;
    DWORD high;
    DWORD low;
    char *bytes;
    DWORD received;
    size_t offset;
    size_t start;
    size_t line_length;
    int first;
    int malformed;

    file = CreateFileW(
        (LPCWSTR)session->history_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND ? WSH_OK :
            WSH_ERR_MISMATCH;
    }
    high = 0U;
    SetLastError(NO_ERROR);
    low = GetFileSize(file, &high);
    if ((low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) ||
        high != 0U || low > session->options.max_history_bytes) {
        CloseHandle(file);
        return WSH_ERR_RESOURCE;
    }
    bytes = (char *)malloc(low == 0U ? 1U : (size_t)low);
    if (bytes == NULL) {
        CloseHandle(file);
        return WSH_ERR_RESOURCE;
    }
    received = 0U;
    if (low != 0U &&
        (!ReadFile(file, bytes, low, &received, NULL) || received != low)) {
        free(bytes);
        CloseHandle(file);
        return WSH_ERR_MISMATCH;
    }
    CloseHandle(file);
    offset = 0U;
    first = 1;
    malformed = 0;
    while (offset < (size_t)low) {
        start = offset;
        while (offset < (size_t)low && bytes[offset] != '\r' &&
               bytes[offset] != '\n') {
            offset += 1U;
        }
        line_length = offset - start;
        if (offset < (size_t)low && bytes[offset] == '\r') {
            offset += 1U;
        }
        if (offset < (size_t)low && bytes[offset] == '\n') {
            offset += 1U;
        }
        if (first) {
            first = 0;
            if (line_length != sizeof(header) - 1U ||
                memcmp(bytes + start, header, line_length) != 0) {
                malformed = 1;
                break;
            }
        } else if (line_length != 0U &&
            !interactive_history_record(
                session, bytes + start, line_length)) {
            malformed = 1;
        }
    }
    if (first) {
        malformed = 1;
    }
    free(bytes);
    if (malformed && !session->history_warned) {
        session->history_warned = 1;
        (void)interactive_warning(
            session,
            "wsh: malformed or oversized history records were skipped\r\n");
    }
    return WSH_OK;
}

/** Create the two parent directories for the default history path. */
static int interactive_history_create_directories(
    wsh_interactive_session *session)
{
    uint16_t *copy;
    size_t index;
    size_t directory_separator;
    size_t parent_separator;
    int result;

    copy = (uint16_t *)malloc(
        (session->history_path_length + 1U) * sizeof(*copy));
    if (copy == NULL) {
        return 0;
    }
    memcpy(
        copy,
        session->history_path,
        (session->history_path_length + 1U) * sizeof(*copy));
    directory_separator = 0U;
    for (index = session->history_path_length; index != 0U; --index) {
        if (copy[index - 1U] == (uint16_t)'\\' ||
            copy[index - 1U] == (uint16_t)'/') {
            directory_separator = index - 1U;
            break;
        }
    }
    parent_separator = 0U;
    for (index = directory_separator; index != 0U; --index) {
        if (copy[index - 1U] == (uint16_t)'\\' ||
            copy[index - 1U] == (uint16_t)'/') {
            parent_separator = index - 1U;
            break;
        }
    }
    if (directory_separator == 0U || parent_separator == 0U) {
        free(copy);
        return 0;
    }
    copy[parent_separator] = 0U;
    result = CreateDirectoryW((LPCWSTR)copy, NULL) != 0 ||
        GetLastError() == ERROR_ALREADY_EXISTS;
    copy[parent_separator] = (uint16_t)'\\';
    copy[directory_separator] = 0U;
    if (result) {
        result = CreateDirectoryW((LPCWSTR)copy, NULL) != 0 ||
            GetLastError() == ERROR_ALREADY_EXISTS;
    }
    free(copy);
    return result;
}

/** Write one JSON-escaped UTF-8 command to a file. */
static int interactive_history_write_json(
    HANDLE file,
    const char *bytes,
    size_t length)
{
    size_t offset;
    size_t start;
    char escaped[7];
    const char *replacement;
    size_t replacement_length;

    offset = 0U;
    start = 0U;
    while (offset < length) {
        unsigned char character;

        character = (unsigned char)bytes[offset];
        replacement = NULL;
        replacement_length = 0U;
        if (character == '"') {
            replacement = "\\\"";
            replacement_length = 2U;
        } else if (character == '\\') {
            replacement = "\\\\";
            replacement_length = 2U;
        } else if (character == '\n') {
            replacement = "\\n";
            replacement_length = 2U;
        } else if (character == '\r') {
            replacement = "\\r";
            replacement_length = 2U;
        } else if (character == '\t') {
            replacement = "\\t";
            replacement_length = 2U;
        } else if (character < 0x20U) {
            (void)snprintf(escaped, sizeof(escaped), "\\u%04x", character);
            replacement = escaped;
            replacement_length = 6U;
        }
        if (replacement != NULL) {
            if (offset != start &&
                !interactive_write_bytes(
                    file, bytes + start, offset - start)) {
                return 0;
            }
            if (!interactive_write_bytes(
                    file, replacement, replacement_length)) {
                return 0;
            }
            offset += 1U;
            start = offset;
        } else {
            offset += 1U;
        }
    }
    return (offset == start ||
        interactive_write_bytes(file, bytes + start, offset - start));
}

/** Atomically replace the persistent history file when writable. */
static int interactive_history_write(
    wsh_interactive_session *session)
{
    WCHAR suffix[48];
    uint16_t *temporary;
    size_t suffix_length;
    size_t directory_length;
    size_t index;
    HANDLE file;
    static const char header[] =
        "{\"format\":\"wsh-history\",\"version\":1}\r\n";
    size_t entry_index;
    int result;

    if (!session->options.history_enabled ||
        !session->history_writable || session->history_path == NULL) {
        return 1;
    }
    if (!interactive_history_create_directories(session)) {
        return 0;
    }
    directory_length = session->history_path_length;
    for (index = session->history_path_length; index != 0U; --index) {
        if (session->history_path[index - 1U] == (uint16_t)'\\' ||
            session->history_path[index - 1U] == (uint16_t)'/') {
            directory_length = index;
            break;
        }
    }
    suffix_length = interactive_wide_ascii(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        0U,
        "wsh-history-");
    suffix_length = interactive_wide_hex(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        suffix_length,
        (uint32_t)GetCurrentProcessId());
    suffix_length = interactive_wide_ascii(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        suffix_length,
        "-");
    suffix_length = interactive_wide_hex(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        suffix_length,
        (uint32_t)GetTickCount());
    suffix_length = interactive_wide_ascii(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        suffix_length,
        "-");
    session->history_serial += 1U;
    suffix_length = interactive_wide_hex(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        suffix_length,
        session->history_serial);
    suffix_length = interactive_wide_ascii(
        (uint16_t *)suffix,
        sizeof(suffix) / sizeof(suffix[0]),
        suffix_length,
        ".tmp");
    temporary = (uint16_t *)malloc(
        (directory_length + suffix_length + 1U) * sizeof(*temporary));
    if (temporary == NULL) {
        return 0;
    }
    memcpy(
        temporary,
        session->history_path,
        directory_length * sizeof(*temporary));
    memcpy(
        temporary + directory_length,
        suffix,
        (suffix_length + 1U) * sizeof(*temporary));
    file = CreateFileW(
        (LPCWSTR)temporary,
        GENERIC_WRITE,
        0U,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        free(temporary);
        return 0;
    }
    result = interactive_write_bytes(file, header, sizeof(header) - 1U);
    for (entry_index = 0U; result &&
         entry_index < session->history_count; ++entry_index) {
        wsh_history_entry *entry;

        entry = &session->history[entry_index];
        result = interactive_write_bytes(
            file, "{\"time\":\"", 9U);
        result = result && interactive_write_bytes(
            file, entry->time, 20U);
        result = result && interactive_write_bytes(
            file, "\",\"command\":\"", 13U);
        result = result && interactive_history_write_json(
            file, entry->command, entry->length);
        result = result && interactive_write_bytes(file, "\"}\r\n", 4U);
    }
    result = result && FlushFileBuffers(file) != 0;
    result = CloseHandle(file) != 0 && result;
    if (result) {
        result = MoveFileExW(
            (LPCWSTR)temporary,
            (LPCWSTR)session->history_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!result) {
        (void)DeleteFileW((LPCWSTR)temporary);
    }
    free(temporary);
    return result;
}

/** Release current copied prompt strings. */
static void interactive_prompts_destroy(
    wsh_interactive_session *session)
{
    wsh_allocator_release(&session->allocator, session->primary_prompt);
    wsh_allocator_release(
        &session->allocator, session->continuation_prompt);
    session->primary_prompt = NULL;
    session->primary_length = 0U;
    session->continuation_prompt = NULL;
    session->continuation_length = 0U;
}

/** Copy current literal prompt elements from the isolated context. */
static int interactive_prompts_load(
    wsh_interactive_session *session)
{
    const wsh_value *value;
    wsh_string_view primary;
    wsh_string_view continuation;
    wsh_result result;

    primary = wsh_string_view_from_cstr("% ");
    continuation = wsh_string_view_from_cstr("; ");
    value = NULL;
    if (wsh_context_get_variable(
            session->options.context,
            wsh_string_view_from_cstr("prompt"),
            &value) == WSH_OK) {
        if (wsh_value_count(value) > 0U) {
            (void)wsh_value_at(value, 0U, &primary);
        }
        if (wsh_value_count(value) > 1U) {
            (void)wsh_value_at(value, 1U, &continuation);
        }
        if (wsh_value_count(value) > 2U && !session->prompt_warned) {
            session->prompt_warned = 1;
            (void)interactive_warning(
                session,
                "wsh: extra prompt elements are ignored\r\n");
        }
    }
    interactive_prompts_destroy(session);
    result = wsh_utf8_to_utf16(
        &session->allocator,
        NULL,
        primary,
        &session->primary_prompt,
        &session->primary_length);
    if (result == WSH_OK) {
        result = wsh_utf8_to_utf16(
            &session->allocator,
            NULL,
            continuation,
            &session->continuation_prompt,
            &session->continuation_length);
    }
    if (result != WSH_OK) {
        interactive_prompts_destroy(session);
        return 0;
    }
    return 1;
}

/** Return the prior UTF-16 scalar boundary. */
static size_t interactive_previous_scalar(
    const uint16_t *units,
    size_t offset)
{
    if (offset == 0U) {
        return 0U;
    }
    offset -= 1U;
    if (offset != 0U && units[offset] >= 0xdc00U &&
        units[offset] <= 0xdfffU && units[offset - 1U] >= 0xd800U &&
        units[offset - 1U] <= 0xdbffU) {
        offset -= 1U;
    }
    return offset;
}

/** Return the next UTF-16 scalar boundary. */
static size_t interactive_next_scalar(
    const uint16_t *units,
    size_t length,
    size_t offset)
{
    if (offset >= length) {
        return length;
    }
    if (units[offset] >= 0xd800U && units[offset] <= 0xdbffU &&
        offset + 1U < length && units[offset + 1U] >= 0xdc00U &&
        units[offset + 1U] <= 0xdfffU) {
        return offset + 2U;
    }
    return offset + 1U;
}

/** Return whether one scalar begins a word for editor navigation. */
static int interactive_word_unit(uint16_t unit)
{
    return (unit >= (uint16_t)'0' && unit <= (uint16_t)'9') ||
        (unit >= (uint16_t)'A' && unit <= (uint16_t)'Z') ||
        (unit >= (uint16_t)'a' && unit <= (uint16_t)'z') ||
        unit == (uint16_t)'_' || unit >= 0x80U;
}

/** Return the current physical line start. */
static size_t interactive_line_start(
    const wsh_interactive_session *session,
    size_t offset)
{
    while (offset != 0U && session->units[offset - 1U] != (uint16_t)'\n') {
        offset -= 1U;
    }
    return offset;
}

/** Return the current physical line end. */
static size_t interactive_line_end(
    const wsh_interactive_session *session,
    size_t offset)
{
    while (offset < session->length &&
           session->units[offset] != (uint16_t)'\n') {
        offset += 1U;
    }
    return offset;
}

/** Insert UTF-16 units at the cursor under the fixed input ceiling. */
static int interactive_insert_units(
    wsh_interactive_session *session,
    const uint16_t *units,
    size_t count)
{
    uint16_t *replacement;
    size_t maximum;
    size_t remove_end;

    maximum = session->options.max_command_bytes;
    if (count > maximum - session->length) {
        return 0;
    }
    if (session->overwrite && count != 0U &&
        session->cursor < session->length &&
        session->units[session->cursor] != (uint16_t)'\n') {
        remove_end = interactive_next_scalar(
            session->units, session->length, session->cursor);
        memmove(
            session->units + session->cursor,
            session->units + remove_end,
            (session->length - remove_end) * sizeof(*session->units));
        session->length -= remove_end - session->cursor;
    }
    replacement = (uint16_t *)interactive_reserve(
        session->units,
        &session->capacity,
        session->length + count + 1U,
        sizeof(*session->units),
        maximum + 1U);
    if (replacement == NULL) {
        return 0;
    }
    session->units = replacement;
    memmove(
        session->units + session->cursor + count,
        session->units + session->cursor,
        (session->length - session->cursor) * sizeof(*session->units));
    if (count != 0U) {
        memcpy(
            session->units + session->cursor,
            units,
            count * sizeof(*session->units));
    }
    session->cursor += count;
    session->length += count;
    session->units[session->length] = 0U;
    return 1;
}

/** Replace a pending-buffer range with UTF-16 units. */
static int interactive_replace_range(
    wsh_interactive_session *session,
    size_t start,
    size_t end,
    const uint16_t *units,
    size_t count)
{
    size_t removed;
    uint16_t *replacement;

    if (start > end || end > session->length) {
        return 0;
    }
    removed = end - start;
    if (count > session->options.max_command_bytes -
            (session->length - removed)) {
        return 0;
    }
    replacement = (uint16_t *)interactive_reserve(
        session->units,
        &session->capacity,
        session->length - removed + count + 1U,
        sizeof(*session->units),
        session->options.max_command_bytes + 1U);
    if (replacement == NULL) {
        return 0;
    }
    session->units = replacement;
    memmove(
        session->units + start + count,
        session->units + end,
        (session->length - end) * sizeof(*session->units));
    if (count != 0U) {
        memcpy(
            session->units + start,
            units,
            count * sizeof(*session->units));
    }
    session->length = session->length - removed + count;
    session->cursor = start + count;
    session->units[session->length] = 0U;
    return 1;
}

/** Delete one pending-buffer range and place the cursor at its start. */
static void interactive_delete_range(
    wsh_interactive_session *session,
    size_t start,
    size_t end)
{
    if (start > end || end > session->length) {
        return;
    }
    memmove(
        session->units + start,
        session->units + end,
        (session->length - end) * sizeof(*session->units));
    session->length -= end - start;
    session->cursor = start;
    if (session->units != NULL) {
        session->units[session->length] = 0U;
    }
}

/** Write buffer text through the editor's multiline prompt rendering. */
static int interactive_render_range(
    wsh_interactive_session *session,
    size_t end)
{
    size_t start;
    size_t index;
    static const uint16_t newline[] = {'\r', '\n'};

    start = 0U;
    for (index = 0U; index < end; ++index) {
        if (session->units[index] == (uint16_t)'\n') {
            if (index != start &&
                !interactive_write_wide(
                    session->options.output,
                    session->units + start,
                    index - start)) {
                return 0;
            }
            if (!interactive_write_wide(
                    session->options.output, newline, 2U) ||
                !interactive_write_wide(
                    session->options.output,
                    session->continuation_prompt,
                    session->continuation_length)) {
                return 0;
            }
            start = index + 1U;
        }
    }
    return end == start || interactive_write_wide(
        session->options.output,
        session->units + start,
        end - start);
}

/** Redraw the complete pending buffer and restore its logical cursor. */
static int interactive_redraw(wsh_interactive_session *session)
{
    CONSOLE_SCREEN_BUFFER_INFO before;
    CONSOLE_SCREEN_BUFFER_INFO after;
    COORD cursor;
    DWORD filled;
    DWORD width;

    if (!GetConsoleScreenBufferInfo(session->options.output, &before)) {
        return 0;
    }
    width = before.dwSize.X > 0 ? (DWORD)before.dwSize.X : 1U;
    if (session->rendered_cells != 0U) {
        filled = 0U;
        if (!FillConsoleOutputCharacterW(
                session->options.output,
                L' ',
                session->rendered_cells,
                session->origin,
                &filled)) {
            return 0;
        }
    }
    if (!SetConsoleCursorPosition(
            session->options.output, session->origin) ||
        !interactive_render_range(session, session->length) ||
        !GetConsoleScreenBufferInfo(session->options.output, &after)) {
        return 0;
    }
    session->rendered_cells =
        (DWORD)(after.dwCursorPosition.Y - session->origin.Y) * width +
        (DWORD)(after.dwCursorPosition.X - session->origin.X);
    if (!SetConsoleCursorPosition(
            session->options.output, session->origin) ||
        !interactive_render_range(session, session->cursor) ||
        !GetConsoleScreenBufferInfo(session->options.output, &after)) {
        return 0;
    }
    cursor = after.dwCursorPosition;
    return SetConsoleCursorPosition(
        session->options.output, cursor) != 0;
}

/** Convert the pending buffer to strict UTF-8 for submission or parsing. */
static wsh_result interactive_convert_pending(
    wsh_interactive_session *session)
{
    wsh_result result;

    wsh_allocator_release(&session->allocator, session->bytes);
    session->bytes = NULL;
    session->byte_length = 0U;
    result = wsh_utf16_to_utf8(
        &session->allocator,
        NULL,
        session->units,
        session->length,
        &session->bytes,
        &session->byte_length);
    if (result == WSH_OK &&
        session->byte_length > session->options.max_command_bytes) {
        wsh_allocator_release(&session->allocator, session->bytes);
        session->bytes = NULL;
        session->byte_length = 0U;
        return WSH_ERR_RESOURCE;
    }
    return result;
}

/** Return parser completeness for the current pending command. */
static wsh_result interactive_parse_pending(
    wsh_interactive_session *session,
    wsh_syntax_status *out_status)
{
    wsh_source *source;
    wsh_parse_tree *tree;
    wsh_result result;

    result = interactive_convert_pending(session);
    source = NULL;
    tree = NULL;
    if (result == WSH_OK) {
        result = wsh_source_create(
            NULL,
            NULL,
            (const unsigned char *)session->bytes,
            session->byte_length,
            &source);
    }
    if (result == WSH_OK) {
        result = wsh_parse(NULL, source, &tree);
    }
    if (result == WSH_OK) {
        *out_status = wsh_parse_tree_status(tree);
    }
    wsh_parse_tree_destroy(tree);
    wsh_source_destroy(source);
    return result;
}

/** Begin one command by writing the current primary prompt literally. */
static int interactive_begin_command(
    wsh_interactive_session *session)
{
    CONSOLE_SCREEN_BUFFER_INFO information;

    if (!interactive_prompts_load(session) ||
        !interactive_write_wide(
            session->options.output,
            session->primary_prompt,
            session->primary_length) ||
        !GetConsoleScreenBufferInfo(
            session->options.output, &information)) {
        return 0;
    }
    session->origin = information.dwCursorPosition;
    session->rendered_cells = 0U;
    session->length = 0U;
    session->cursor = 0U;
    session->overwrite = 0;
    session->history_index = session->history_count;
    return 1;
}

/** Use cooked ReadConsoleW input while retaining parser continuation rules. */
static wsh_frontend_read_result interactive_read_basic(
    wsh_interactive_session *session,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    WCHAR chunk[256];
    DWORD received;
    DWORD index;
    void *replacement;
    wsh_syntax_status syntax;
    wsh_result result;

    session->length = 0U;
    session->cursor = 0U;
    if (!interactive_prompts_load(session) ||
        !interactive_write_wide(
            session->options.output,
            session->primary_prompt,
            session->primary_length)) {
        return WSH_FRONTEND_READ_ERROR;
    }
    for (;;) {
        received = 0U;
        if (!ReadConsoleW(
                session->options.input,
                chunk,
                sizeof(chunk) / sizeof(chunk[0]),
                &received,
                NULL)) {
            return GetLastError() == ERROR_OPERATION_ABORTED ?
                WSH_FRONTEND_READ_CANCELLED :
                WSH_FRONTEND_READ_ERROR;
        }
        if (received == 0U) {
            return session->length == 0U ? WSH_FRONTEND_READ_EOF :
                WSH_FRONTEND_READ_ERROR;
        }
        for (index = 0U; index < received; ++index) {
            if (chunk[index] == 0x001aU && session->length == 0U) {
                return WSH_FRONTEND_READ_EOF;
            }
            if (chunk[index] == L'\r') {
                continue;
            }
            replacement = interactive_reserve(
                session->units,
                &session->capacity,
                session->length + 2U,
                sizeof(*session->units),
                session->options.max_command_bytes + 1U);
            if (replacement == NULL) {
                return WSH_FRONTEND_READ_RESOURCE;
            }
            session->units = (uint16_t *)replacement;
            session->units[session->length++] = (uint16_t)chunk[index];
        }
        session->cursor = session->length;
        result = interactive_parse_pending(session, &syntax);
        if (result != WSH_OK) {
            if (result == WSH_ERR_ENCODING) {
                session->length = 0U;
                session->cursor = 0U;
                (void)interactive_warning(
                    session,
                    "wsh: invalid UTF-16 input was discarded\r\n");
                if (!interactive_write_wide(
                        session->options.output,
                        session->primary_prompt,
                        session->primary_length)) {
                    return WSH_FRONTEND_READ_ERROR;
                }
                continue;
            }
            return WSH_FRONTEND_READ_RESOURCE;
        }
        if (syntax != WSH_SYNTAX_INCOMPLETE) {
            *out_bytes = (const unsigned char *)session->bytes;
            *out_length = session->byte_length;
            return WSH_FRONTEND_READ_LINE;
        }
        if (!interactive_write_wide(
                session->options.output,
                session->continuation_prompt,
                session->continuation_length)) {
            return WSH_FRONTEND_READ_ERROR;
        }
    }
}

/** Destroy the retained completion set. */
static void interactive_completion_clear(
    wsh_interactive_session *session)
{
    size_t index;

    for (index = 0U; index < session->candidate_count; ++index) {
        free(session->candidates[index].text);
    }
    session->candidate_count = 0U;
    session->completion_active = 0;
    session->completion_quoted = 0;
    session->completion_index = 0U;
}

/** Compare ASCII case-insensitively with an exact-byte tiebreak. */
static int interactive_candidate_compare(const void *left, const void *right)
{
    static wsh_compare_string_ordinal_fn compare_ordinal = NULL;
    static int compare_resolved = 0;
    const wsh_completion_candidate *left_candidate;
    const wsh_completion_candidate *right_candidate;
    WCHAR *left_wide;
    WCHAR *right_wide;
    int left_count;
    int right_count;
    int comparison;
    size_t index;
    size_t common;

    left_candidate = (const wsh_completion_candidate *)left;
    right_candidate = (const wsh_completion_candidate *)right;
    if (!compare_resolved) {
        compare_ordinal = (wsh_compare_string_ordinal_fn)GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "CompareStringOrdinal");
        compare_resolved = 1;
    }
    left_count = MultiByteToWideChar(
        CP_UTF8,
        0U,
        left_candidate->text,
        (int)left_candidate->length,
        NULL,
        0);
    right_count = MultiByteToWideChar(
        CP_UTF8,
        0U,
        right_candidate->text,
        (int)right_candidate->length,
        NULL,
        0);
    left_wide = left_count == 0 ? NULL :
        (WCHAR *)malloc((size_t)left_count * sizeof(*left_wide));
    right_wide = right_count == 0 ? NULL :
        (WCHAR *)malloc((size_t)right_count * sizeof(*right_wide));
    if (compare_ordinal != NULL && left_wide != NULL &&
        right_wide != NULL &&
        MultiByteToWideChar(
            CP_UTF8,
            0U,
            left_candidate->text,
            (int)left_candidate->length,
            left_wide,
            left_count) == left_count &&
        MultiByteToWideChar(
            CP_UTF8,
            0U,
            right_candidate->text,
            (int)right_candidate->length,
            right_wide,
            right_count) == right_count) {
        comparison = compare_ordinal(
            left_wide,
            left_count,
            right_wide,
            right_count,
            TRUE);
        free(left_wide);
        free(right_wide);
        if (comparison == CSTR_LESS_THAN) {
            return -1;
        }
        if (comparison == CSTR_GREATER_THAN) {
            return 1;
        }
        comparison = memcmp(
            left_candidate->text,
            right_candidate->text,
            left_candidate->length < right_candidate->length ?
                left_candidate->length : right_candidate->length);
        if (comparison != 0) {
            return comparison;
        }
        if (left_candidate->length != right_candidate->length) {
            return left_candidate->length < right_candidate->length ?
                -1 : 1;
        }
        return 0;
    }
    free(left_wide);
    free(right_wide);
    common = left_candidate->length < right_candidate->length ?
        left_candidate->length : right_candidate->length;
    for (index = 0U; index < common; ++index) {
        unsigned char left_byte;
        unsigned char right_byte;

        left_byte = (unsigned char)left_candidate->text[index];
        right_byte = (unsigned char)right_candidate->text[index];
        if (left_byte >= 'A' && left_byte <= 'Z') {
            left_byte = (unsigned char)(left_byte + ('a' - 'A'));
        }
        if (right_byte >= 'A' && right_byte <= 'Z') {
            right_byte = (unsigned char)(right_byte + ('a' - 'A'));
        }
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left_candidate->length != right_candidate->length) {
        return left_candidate->length < right_candidate->length ? -1 : 1;
    }
    return memcmp(
        left_candidate->text,
        right_candidate->text,
        left_candidate->length);
}

/** Return whether text begins with a case-insensitive ASCII prefix. */
static int interactive_prefix_matches(
    const char *text,
    size_t length,
    const char *prefix,
    size_t prefix_length)
{
    size_t index;

    if (prefix_length > length) {
        return 0;
    }
    for (index = 0U; index < prefix_length; ++index) {
        unsigned char left;
        unsigned char right;

        left = (unsigned char)text[index];
        right = (unsigned char)prefix[index];
        if (left >= 'A' && left <= 'Z') {
            left = (unsigned char)(left + ('a' - 'A'));
        }
        if (right >= 'A' && right <= 'Z') {
            right = (unsigned char)(right + ('a' - 'A'));
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

/** Append one unique bounded completion candidate. */
static int interactive_candidate_add(
    wsh_interactive_session *session,
    const char *text,
    size_t length,
    const char *prefix,
    size_t prefix_length)
{
    size_t index;
    size_t greatest;
    wsh_completion_candidate incoming;
    wsh_completion_candidate *replacement;
    char *copy;

    if (!interactive_prefix_matches(
            text, length, prefix, prefix_length)) {
        return 1;
    }
    for (index = 0U; index < session->candidate_count; ++index) {
        if (session->candidates[index].length == length &&
            interactive_prefix_matches(
                session->candidates[index].text,
                length,
                text,
                length)) {
            return 1;
        }
    }
    if (session->candidate_count == WSH_INTERACTIVE_CANDIDATES) {
        greatest = 0U;
        for (index = 1U; index < session->candidate_count; ++index) {
            if (interactive_candidate_compare(
                    &session->candidates[greatest],
                    &session->candidates[index]) < 0) {
                greatest = index;
            }
        }
        incoming.text = (char *)text;
        incoming.length = length;
        if (interactive_candidate_compare(
                &incoming, &session->candidates[greatest]) >= 0) {
            return 1;
        }
        copy = interactive_copy_bytes(text, length);
        if (copy == NULL) {
            return 0;
        }
        free(session->candidates[greatest].text);
        session->candidates[greatest].text = copy;
        session->candidates[greatest].length = length;
        return 1;
    }
    replacement = (wsh_completion_candidate *)interactive_reserve(
        session->candidates,
        &session->candidate_capacity,
        session->candidate_count + 1U,
        sizeof(*session->candidates),
        WSH_INTERACTIVE_CANDIDATES);
    if (replacement == NULL) {
        return 0;
    }
    session->candidates = replacement;
    copy = interactive_copy_bytes(text, length);
    if (copy == NULL) {
        return 0;
    }
    session->candidates[session->candidate_count].text = copy;
    session->candidates[session->candidate_count].length = length;
    session->candidate_count += 1U;
    return 1;
}

/** Return whether the cursor token is in command position. */
static int interactive_command_position(
    const wsh_interactive_session *session,
    size_t start)
{
    while (start != 0U) {
        uint16_t prior;

        prior = session->units[start - 1U];
        if (prior == (uint16_t)' ' || prior == (uint16_t)'\t' ||
            prior == (uint16_t)'\r' || prior == (uint16_t)'\n') {
            start -= 1U;
            continue;
        }
        return prior == (uint16_t)';' || prior == (uint16_t)'&' ||
            prior == (uint16_t)'|' || prior == (uint16_t)'{' ||
            prior == (uint16_t)'(';
    }
    return 1;
}

/** Convert a UTF-16 pending-buffer slice to owned strict UTF-8. */
static int interactive_slice_utf8(
    wsh_interactive_session *session,
    size_t start,
    size_t end,
    char **out_bytes,
    size_t *out_length)
{
    return start <= end && end <= session->length &&
        wsh_utf16_to_utf8(
            &session->allocator,
            NULL,
            session->units + start,
            end - start,
            out_bytes,
            out_length) == WSH_OK;
}

/** Collect command/function/library candidates for one prefix. */
static int interactive_complete_commands(
    wsh_interactive_session *session,
    const char *prefix,
    size_t prefix_length)
{
    static const char *builtins[] = {
        ".", "break", "builtin", "cd", "command::external",
        "continue", "echo", "eval", "exec", "exit", "export",
        "local", "rawexec", "return", "shift", "source", "unexport",
        "unset", "wait", "whatis", "~"
    };
    size_t index;
    wsh_string_view name;
    const wsh_library_descriptor *descriptor;

    for (index = 0U;
         index < sizeof(builtins) / sizeof(builtins[0]); ++index) {
        if (!interactive_candidate_add(
                session,
                builtins[index],
                strlen(builtins[index]),
                prefix,
                prefix_length)) {
            return 0;
        }
    }
    for (index = 0U; index < wsh_evaluator_function_count(
             session->options.evaluator); ++index) {
        if (wsh_evaluator_function_at(
                session->options.evaluator, index, &name) != WSH_OK ||
            !interactive_candidate_add(
                session,
                name.data,
                name.length,
                prefix,
                prefix_length)) {
            return 0;
        }
    }
    for (index = 0U; index < wsh_library_descriptor_count(); ++index) {
        descriptor = wsh_library_descriptor_at(index);
        if (descriptor != NULL &&
            !interactive_candidate_add(
                session,
                descriptor->name,
                strlen(descriptor->name),
                prefix,
                prefix_length)) {
            return 0;
        }
    }
    return 1;
}

/** Collect variable candidates including their leading dollar sign. */
static int interactive_complete_variables(
    wsh_interactive_session *session,
    const char *prefix,
    size_t prefix_length)
{
    size_t index;
    wsh_string_view name;
    const wsh_value *value;
    int exported;
    char *candidate;
    int result;

    for (index = 0U; index < wsh_context_variable_count(
             session->options.context); ++index) {
        if (wsh_context_variable_at(
                session->options.context,
                index,
                &name,
                &value,
                &exported) != WSH_OK) {
            return 0;
        }
        (void)value;
        (void)exported;
        candidate = (char *)malloc(name.length + 2U);
        if (candidate == NULL) {
            return 0;
        }
        candidate[0] = '$';
        memcpy(candidate + 1U, name.data, name.length);
        candidate[name.length + 1U] = '\0';
        result = interactive_candidate_add(
            session,
            candidate,
            name.length + 1U,
            prefix,
            prefix_length);
        free(candidate);
        if (!result) {
            return 0;
        }
    }
    return 1;
}

/** Return whether a bare file is an accepted command file type. */
static int interactive_command_extension(
    const char *bytes,
    size_t length)
{
    static const char *extensions[] = {".com", ".exe", ".wsh"};
    size_t index;
    size_t extension_length;

    for (index = 0U;
         index < sizeof(extensions) / sizeof(extensions[0]); ++index) {
        extension_length = strlen(extensions[index]);
        if (length >= extension_length && interactive_prefix_matches(
                bytes + length - extension_length,
                extension_length,
                extensions[index],
                extension_length)) {
            return 1;
        }
    }
    return 0;
}

/** Collect filesystem candidates without implicit UNC enumeration. */
static int interactive_complete_paths(
    wsh_interactive_session *session,
    const char *prefix,
    size_t prefix_length,
    int command_only)
{
    uint16_t *wide;
    size_t wide_length;
    size_t separator;
    size_t display_prefix;
    wsh_string *working;
    uint16_t *working_wide;
    size_t working_length;
    uint16_t *search;
    size_t search_length;
    WIN32_FIND_DATAW data;
    HANDLE find;
    char *candidate;
    size_t candidate_length;
    char *name;
    size_t name_length;
    wsh_result result;
    int success;

    if (prefix_length >= 2U && prefix[0] == '\\' && prefix[1] == '\\') {
        /* The typed UNC prefix explicitly selects a network provider. */
    }
    wide = NULL;
    working = NULL;
    working_wide = NULL;
    working_length = 0U;
    search = NULL;
    result = wsh_utf8_to_utf16(
        &session->allocator,
        NULL,
        (wsh_string_view){prefix, prefix_length},
        &wide,
        &wide_length);
    if (result != WSH_OK) {
        return 0;
    }
    display_prefix = 0U;
    for (separator = 0U; separator < wide_length; ++separator) {
        if (wide[separator] == (uint16_t)'\\' ||
            wide[separator] == (uint16_t)'/') {
            display_prefix = separator + 1U;
        }
    }
    if (wide_length >= 2U && wide[1] == (uint16_t)':' ||
        (wide_length != 0U &&
         (wide[0] == (uint16_t)'\\' || wide[0] == (uint16_t)'/'))) {
        search_length = wide_length + 1U;
        search = (uint16_t *)malloc(
            (search_length + 1U) * sizeof(*search));
        if (search != NULL) {
            memcpy(search, wide, wide_length * sizeof(*search));
        }
    } else {
        result = wsh_windows_runtime_working_directory(
            session->options.runtime, &working);
        if (result == WSH_OK) {
            result = wsh_utf8_to_utf16(
                &session->allocator,
                NULL,
                wsh_string_bytes(working),
                &working_wide,
                &working_length);
        }
        search_length = working_length + 1U + wide_length + 1U;
        if (result == WSH_OK) {
            search = (uint16_t *)malloc(
                (search_length + 1U) * sizeof(*search));
        }
        if (search != NULL) {
            memcpy(
                search,
                working_wide,
                working_length * sizeof(*search));
            search[working_length] = (uint16_t)'\\';
            memcpy(
                search + working_length + 1U,
                wide,
                wide_length * sizeof(*search));
        }
    }
    wsh_string_destroy(working);
    wsh_allocator_release(&session->allocator, working_wide);
    if (search == NULL) {
        wsh_allocator_release(&session->allocator, wide);
        return 0;
    }
    search[search_length - 1U] = (uint16_t)'*';
    search[search_length] = 0U;
    find = FindFirstFileW((LPCWSTR)search, &data);
    success = 1;
    while (find != INVALID_HANDLE_VALUE) {
        if (!(data.cFileName[0] == L'.' &&
              (data.cFileName[1] == 0U ||
               (data.cFileName[1] == L'.' &&
                data.cFileName[2] == 0U)))) {
            name = NULL;
            result = wsh_utf16_to_utf8(
                &session->allocator,
                NULL,
                (const uint16_t *)data.cFileName,
                interactive_wide_length(
                    (const uint16_t *)data.cFileName),
                &name,
                &name_length);
            candidate_length = 0U;
            candidate = NULL;
            if (result == WSH_OK) {
                candidate_length = display_prefix + name_length;
                candidate = (char *)malloc(candidate_length + 2U);
            }
            if (candidate != NULL &&
                (!command_only ||
                 (data.dwFileAttributes &
                  FILE_ATTRIBUTE_DIRECTORY) != 0U ||
                 interactive_command_extension(name, name_length))) {
                memcpy(candidate, prefix, display_prefix);
                memcpy(candidate + display_prefix, name, name_length);
                if ((data.dwFileAttributes &
                     FILE_ATTRIBUTE_DIRECTORY) != 0U) {
                    candidate[candidate_length++] = display_prefix != 0U &&
                        prefix[display_prefix - 1U] == '/' ? '/' : '\\';
                }
                candidate[candidate_length] = '\0';
                success = interactive_candidate_add(
                    session,
                    candidate,
                    candidate_length,
                    prefix,
                    prefix_length);
            } else if (candidate == NULL && result != WSH_OK) {
                success = 0;
            }
            free(candidate);
            wsh_allocator_release(&session->allocator, name);
        }
        if (!success || !FindNextFileW(find, &data)) {
            break;
        }
    }
    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
    }
    free(search);
    wsh_allocator_release(&session->allocator, wide);
    return success;
}

/** Collect bare executable and script names from local `$path` entries. */
static int interactive_complete_search_path(
    wsh_interactive_session *session,
    const char *prefix,
    size_t prefix_length)
{
    const wsh_value *path;
    wsh_string_view directory;
    size_t directory_index;
    uint16_t *wide_directory;
    size_t directory_length;
    uint16_t *wide_prefix;
    size_t wide_prefix_length;
    uint16_t *search;
    size_t search_length;
    size_t offset;
    WCHAR root[4];
    WIN32_FIND_DATAW data;
    HANDLE find;
    char *name;
    size_t name_length;
    wsh_result result;

    path = NULL;
    if (wsh_context_get_variable(
            session->options.context,
            wsh_string_view_from_cstr("path"),
            &path) != WSH_OK) {
        return 1;
    }
    wide_prefix = NULL;
    result = wsh_utf8_to_utf16(
        &session->allocator,
        NULL,
        (wsh_string_view){prefix, prefix_length},
        &wide_prefix,
        &wide_prefix_length);
    if (result != WSH_OK) {
        return 0;
    }
    for (directory_index = 0U;
         directory_index < wsh_value_count(path); ++directory_index) {
        if (wsh_value_at(path, directory_index, &directory) != WSH_OK) {
            wsh_allocator_release(&session->allocator, wide_prefix);
            return 0;
        }
        wide_directory = NULL;
        result = wsh_utf8_to_utf16(
            &session->allocator,
            NULL,
            directory,
            &wide_directory,
            &directory_length);
        if (result != WSH_OK) {
            wsh_allocator_release(&session->allocator, wide_prefix);
            return 0;
        }
        if (directory_length >= 2U &&
            wide_directory[0] == (uint16_t)'\\' &&
            wide_directory[1] == (uint16_t)'\\') {
            wsh_allocator_release(
                &session->allocator, wide_directory);
            continue;
        }
        if (directory_length >= 2U &&
            wide_directory[1] == (uint16_t)':') {
            root[0] = (WCHAR)wide_directory[0];
            root[1] = L':';
            root[2] = L'\\';
            root[3] = 0U;
            if (GetDriveTypeW(root) == DRIVE_REMOTE) {
                wsh_allocator_release(
                    &session->allocator, wide_directory);
                continue;
            }
        }
        search_length = directory_length + 1U +
            wide_prefix_length + 1U;
        search = (uint16_t *)malloc(
            (search_length + 1U) * sizeof(*search));
        if (search == NULL) {
            wsh_allocator_release(
                &session->allocator, wide_directory);
            wsh_allocator_release(&session->allocator, wide_prefix);
            return 0;
        }
        memcpy(
            search,
            wide_directory,
            directory_length * sizeof(*search));
        offset = directory_length;
        if (offset == 0U ||
            (search[offset - 1U] != (uint16_t)'\\' &&
             search[offset - 1U] != (uint16_t)'/')) {
            search[offset++] = (uint16_t)'\\';
        }
        memcpy(
            search + offset,
            wide_prefix,
            wide_prefix_length * sizeof(*search));
        offset += wide_prefix_length;
        search[offset++] = (uint16_t)'*';
        search[offset] = 0U;
        find = FindFirstFileW((LPCWSTR)search, &data);
        free(search);
        while (find != INVALID_HANDLE_VALUE) {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
                name = NULL;
                result = wsh_utf16_to_utf8(
                    &session->allocator,
                    NULL,
                    (const uint16_t *)data.cFileName,
                    interactive_wide_length(
                        (const uint16_t *)data.cFileName),
                    &name,
                    &name_length);
                if (result != WSH_OK ||
                    (interactive_command_extension(name, name_length) &&
                     !interactive_candidate_add(
                         session,
                         name,
                         name_length,
                         prefix,
                         prefix_length))) {
                    wsh_allocator_release(&session->allocator, name);
                    FindClose(find);
                    wsh_allocator_release(
                        &session->allocator, wide_directory);
                    wsh_allocator_release(
                        &session->allocator, wide_prefix);
                    return 0;
                }
                wsh_allocator_release(&session->allocator, name);
            }
            if (!FindNextFileW(find, &data)) {
                break;
            }
        }
        if (find != INVALID_HANDLE_VALUE) {
            FindClose(find);
        }
        wsh_allocator_release(&session->allocator, wide_directory);
    }
    wsh_allocator_release(&session->allocator, wide_prefix);
    return 1;
}

/** Return whether a completion spelling requires apostrophe quotation. */
static int interactive_needs_quote(const char *bytes, size_t length)
{
    size_t index;

    if (length == 0U) {
        return 1;
    }
    for (index = 0U; index < length; ++index) {
        unsigned char character;

        character = (unsigned char)bytes[index];
        if (character <= 0x20U || character == ';' || character == '|' ||
            character == '&' || character == '<' || character == '>' ||
            character == '(' || character == ')' || character == '{' ||
            character == '}' || character == '$' || character == '#' ||
            character == '^' || character == '\'') {
            return 1;
        }
    }
    return 0;
}

/** Quote one literal completion spelling as WSH source. */
static char *interactive_quote_candidate(
    const char *bytes,
    size_t length,
    int force_quote,
    size_t *out_length)
{
    char *quoted;
    size_t apostrophes;
    size_t index;
    size_t offset;

    if (!force_quote && ((length != 0U && bytes[0] == '$') ||
        !interactive_needs_quote(bytes, length))) {
        *out_length = length;
        return interactive_copy_bytes(bytes, length);
    }
    apostrophes = 0U;
    for (index = 0U; index < length; ++index) {
        if (bytes[index] == '\'') {
            apostrophes += 1U;
        }
    }
    if (length > (size_t)-1 - apostrophes - 3U) {
        return NULL;
    }
    quoted = (char *)malloc(length + apostrophes + 3U);
    if (quoted == NULL) {
        return NULL;
    }
    offset = 0U;
    quoted[offset++] = '\'';
    for (index = 0U; index < length; ++index) {
        quoted[offset++] = bytes[index];
        if (bytes[index] == '\'') {
            quoted[offset++] = '\'';
        }
    }
    quoted[offset++] = '\'';
    quoted[offset] = '\0';
    *out_length = offset;
    return quoted;
}

/** Replace the current completion token with one literal candidate. */
static int interactive_completion_insert(
    wsh_interactive_session *session,
    const char *bytes,
    size_t length)
{
    char *quoted;
    size_t quoted_length;
    uint16_t *wide;
    size_t wide_length;
    wsh_result result;
    int inserted;

    quoted = interactive_quote_candidate(
        bytes,
        length,
        session->completion_quoted,
        &quoted_length);
    if (quoted == NULL) {
        return 0;
    }
    wide = NULL;
    result = wsh_utf8_to_utf16(
        &session->allocator,
        NULL,
        (wsh_string_view){quoted, quoted_length},
        &wide,
        &wide_length);
    free(quoted);
    if (result != WSH_OK) {
        return 0;
    }
    inserted = interactive_replace_range(
        session,
        session->completion_start,
        session->cursor,
        wide,
        wide_length);
    wsh_allocator_release(&session->allocator, wide);
    return inserted;
}

/** Perform deterministic forward or reverse completion. */
static int interactive_complete(
    wsh_interactive_session *session,
    int reverse)
{
    size_t start;
    char *prefix;
    size_t prefix_length;
    int command_position;
    int result;
    size_t common;
    size_t candidate_index;
    size_t read_index;
    size_t write_index;
    size_t scan_index;
    int in_quote;

    if (session->completion_active && session->candidate_count != 0U) {
        if (reverse) {
            session->completion_index = session->completion_index == 0U ?
                session->candidate_count - 1U :
                session->completion_index - 1U;
        } else {
            session->completion_index =
                (session->completion_index + 1U) % session->candidate_count;
        }
        return interactive_completion_insert(
            session,
            session->candidates[session->completion_index].text,
            session->candidates[session->completion_index].length);
    }
    interactive_completion_clear(session);
    start = 0U;
    in_quote = 0;
    for (scan_index = 0U;
         scan_index < session->cursor; ++scan_index) {
        uint16_t unit;

        unit = session->units[scan_index];
        if (unit == (uint16_t)'\'') {
            if (in_quote && scan_index + 1U < session->cursor &&
                session->units[scan_index + 1U] == (uint16_t)'\'') {
                scan_index += 1U;
            } else {
                in_quote = !in_quote;
            }
        } else if (!in_quote &&
            (unit == (uint16_t)' ' || unit == (uint16_t)'\t' ||
             unit == (uint16_t)'\r' || unit == (uint16_t)'\n' ||
             unit == (uint16_t)';' || unit == (uint16_t)'|' ||
             unit == (uint16_t)'&' || unit == (uint16_t)'{' ||
             unit == (uint16_t)'(')) {
            start = scan_index + 1U;
        }
    }
    prefix = NULL;
    if (!interactive_slice_utf8(
            session,
            start,
            session->cursor,
            &prefix,
            &prefix_length)) {
        return 0;
    }
    session->completion_quoted = prefix_length != 0U && prefix[0] == '\'';
    if (session->completion_quoted) {
        read_index = 1U;
        write_index = 0U;
        while (read_index < prefix_length) {
            if (prefix[read_index] == '\'' &&
                read_index + 1U < prefix_length &&
                prefix[read_index + 1U] == '\'') {
                prefix[write_index++] = '\'';
                read_index += 2U;
            } else if (prefix[read_index] == '\'' &&
                read_index + 1U == prefix_length) {
                read_index += 1U;
            } else {
                prefix[write_index++] = prefix[read_index++];
            }
        }
        prefix[write_index] = '\0';
        prefix_length = write_index;
    }
    command_position = interactive_command_position(session, start);
    session->completion_start = start;
    if (prefix_length != 0U && prefix[0] == '$') {
        result = interactive_complete_variables(
            session, prefix, prefix_length);
    } else if (command_position) {
        result = interactive_complete_commands(
            session, prefix, prefix_length);
        if (result) {
            result = interactive_complete_paths(
                session, prefix, prefix_length, 1);
        }
        if (result && memchr(prefix, '\\', prefix_length) == NULL &&
            memchr(prefix, '/', prefix_length) == NULL &&
            memchr(prefix, ':', prefix_length) == NULL) {
            result = interactive_complete_search_path(
                session, prefix, prefix_length);
        }
    } else if (prefix_length != 0U && prefix[0] == '/') {
        result = 1;
    } else {
        result = interactive_complete_paths(
            session, prefix, prefix_length, 0);
    }
    wsh_allocator_release(&session->allocator, prefix);
    if (!result || session->candidate_count == 0U) {
        interactive_completion_clear(session);
        return result;
    }
    qsort(
        session->candidates,
        session->candidate_count,
        sizeof(*session->candidates),
        interactive_candidate_compare);
    common = session->candidates[0].length;
    for (candidate_index = 1U;
         candidate_index < session->candidate_count; ++candidate_index) {
        size_t index;

        for (index = 0U; index < common &&
             index < session->candidates[candidate_index].length; ++index) {
            if (!interactive_prefix_matches(
                    session->candidates[0].text + index,
                    1U,
                    session->candidates[candidate_index].text + index,
                    1U)) {
                break;
            }
        }
        common = index;
    }
    while (common > 0U && wsh_utf8_validate(
            (wsh_string_view){session->candidates[0].text, common},
            NULL) != WSH_OK) {
        common -= 1U;
    }
    session->completion_active = 1;
    session->completion_index = reverse ?
        0U : session->candidate_count - 1U;
    if (common > prefix_length) {
        return interactive_completion_insert(
            session, session->candidates[0].text, common);
    }
    return 1;
}

/** Replace pending input with one retained history command. */
static int interactive_history_select(
    wsh_interactive_session *session,
    size_t index)
{
    uint16_t *wide;
    size_t wide_length;
    wsh_result result;

    if (index >= session->history_count) {
        session->length = 0U;
        session->cursor = 0U;
        return 1;
    }
    wide = NULL;
    result = wsh_utf8_to_utf16(
        &session->allocator,
        NULL,
        (wsh_string_view){session->history[index].command,
                          session->history[index].length},
        &wide,
        &wide_length);
    if (result != WSH_OK) {
        return 0;
    }
    result = interactive_replace_range(
        session, 0U, session->length, wide, wide_length) ?
        WSH_OK : WSH_ERR_RESOURCE;
    wsh_allocator_release(&session->allocator, wide);
    return result == WSH_OK;
}

/** Move the cursor one word to the left. */
static void interactive_word_left(wsh_interactive_session *session)
{
    size_t offset;

    offset = session->cursor;
    while (offset != 0U) {
        size_t prior;

        prior = interactive_previous_scalar(session->units, offset);
        if (interactive_word_unit(session->units[prior])) {
            break;
        }
        offset = prior;
    }
    while (offset != 0U) {
        size_t prior;

        prior = interactive_previous_scalar(session->units, offset);
        if (!interactive_word_unit(session->units[prior])) {
            break;
        }
        offset = prior;
    }
    session->cursor = offset;
}

/** Move the cursor one word to the right. */
static void interactive_word_right(wsh_interactive_session *session)
{
    size_t offset;

    offset = session->cursor;
    while (offset < session->length &&
        interactive_word_unit(session->units[offset])) {
        offset = interactive_next_scalar(
            session->units, session->length, offset);
    }
    while (offset < session->length &&
        !interactive_word_unit(session->units[offset])) {
        offset = interactive_next_scalar(
            session->units, session->length, offset);
    }
    session->cursor = offset;
}

/** Move vertically across physical pending-input lines. */
static void interactive_vertical(
    wsh_interactive_session *session,
    int downward)
{
    size_t start;
    size_t end;
    size_t column;
    size_t target_start;
    size_t target_end;

    start = interactive_line_start(session, session->cursor);
    end = interactive_line_end(session, session->cursor);
    column = session->cursor - start;
    if (downward) {
        if (end == session->length) {
            return;
        }
        target_start = end + 1U;
        target_end = interactive_line_end(session, target_start);
    } else {
        if (start == 0U) {
            return;
        }
        target_end = start - 1U;
        target_start = interactive_line_start(session, target_end);
    }
    session->cursor = target_start +
        (column < target_end - target_start ?
         column : target_end - target_start);
}

/** Clear the visible console window and re-anchor the editor. */
static int interactive_clear_screen(
    wsh_interactive_session *session)
{
    CONSOLE_SCREEN_BUFFER_INFO information;
    COORD start;
    DWORD cells;
    DWORD filled;

    if (!GetConsoleScreenBufferInfo(
            session->options.output, &information)) {
        return 0;
    }
    start.X = 0;
    start.Y = information.srWindow.Top;
    cells = (DWORD)information.dwSize.X *
        (DWORD)(information.srWindow.Bottom -
                information.srWindow.Top + 1);
    filled = 0U;
    if (!FillConsoleOutputCharacterW(
            session->options.output,
            L' ',
            cells,
            start,
            &filled) ||
        !SetConsoleCursorPosition(session->options.output, start) ||
        !interactive_write_wide(
            session->options.output,
            session->primary_prompt,
            session->primary_length) ||
        !GetConsoleScreenBufferInfo(
            session->options.output, &information)) {
        return 0;
    }
    session->origin = information.dwCursorPosition;
    session->rendered_cells = 0U;
    return interactive_redraw(session);
}

/** List the stable identifiers of every retained background group. */
static int interactive_background_list(
    wsh_interactive_session *session)
{
    size_t count;
    size_t index;
    uint32_t identifier;
    char decimal[32];
    int length;

    count = wsh_windows_runtime_background_count(session->options.runtime);
    if (!interactive_write_utf8(
            session,
            session->options.output,
            "wsh: background jobs:",
            strlen("wsh: background jobs:"))) {
        return 0;
    }
    for (index = 0U; index < count; ++index) {
        if (wsh_windows_runtime_background_at(
                session->options.runtime,
                index,
                &identifier) != WSH_OK) {
            return 0;
        }
        length = snprintf(
            decimal,
            sizeof(decimal),
            " %lu",
            (unsigned long)identifier);
        if (length <= 0 || (size_t)length >= sizeof(decimal) ||
            !interactive_write_utf8(
                session,
                session->options.output,
                decimal,
                (size_t)length)) {
            return 0;
        }
    }
    return interactive_write_utf8(
        session, session->options.output, "\r\n", 2U);
}

/** Handle an empty Ctrl+Z/Enter transition, including live jobs. */
static wsh_frontend_read_result interactive_eof(
    wsh_interactive_session *session,
    int *out_continue)
{
    size_t jobs;

    *out_continue = 0;
    jobs = wsh_windows_runtime_background_count(session->options.runtime);
    if (jobs == 0U) {
        return WSH_FRONTEND_READ_EOF;
    }
    if (!session->eof_refused) {
        session->eof_refused = 1;
        if (!interactive_background_list(session)) {
            return WSH_FRONTEND_READ_ERROR;
        }
        (void)interactive_write_utf8(
            session,
            session->options.output,
            "\r\nwsh: background jobs remain; repeat EOF to cancel "
            "and exit\r\n",
            strlen("\r\nwsh: background jobs remain; repeat EOF to cancel "
                   "and exit\r\n"));
        if (!interactive_begin_command(session)) {
            return WSH_FRONTEND_READ_ERROR;
        }
        *out_continue = 1;
        return WSH_FRONTEND_READ_LINE;
    }
    if (wsh_windows_runtime_cancel_all(
            session->options.runtime) != WSH_OK) {
        return WSH_FRONTEND_READ_ERROR;
    }
    return WSH_FRONTEND_READ_EOF;
}

/** Handle one pressed key and optionally finish the current read. */
static wsh_frontend_read_result interactive_key(
    wsh_interactive_session *session,
    const KEY_EVENT_RECORD *key,
    int *ctrl_z_pending,
    int *out_finished)
{
    DWORD controls;
    int control;
    int shift;
    uint16_t character;
    size_t start;
    size_t end;
    uint16_t newline;
    wsh_syntax_status syntax;
    wsh_result result;
    int continue_read;

    *out_finished = 0;
    if (!key->bKeyDown) {
        return WSH_FRONTEND_READ_LINE;
    }
    controls = key->dwControlKeyState;
    control = (controls &
        (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0U;
    shift = (controls & SHIFT_PRESSED) != 0U;
    character = (uint16_t)key->uChar.UnicodeChar;
    if (!(control && (character == 0x1aU ||
                      key->wVirtualKeyCode == 'Z')) &&
        !(key->wVirtualKeyCode == VK_RETURN && *ctrl_z_pending)) {
        *ctrl_z_pending = 0;
        session->eof_refused = 0;
    }
    if (control && (character == 0x03U ||
                    key->wVirtualKeyCode == 'C')) {
        session->length = 0U;
        session->cursor = 0U;
        interactive_completion_clear(session);
        *out_finished = 1;
        return WSH_FRONTEND_READ_CANCELLED;
    }
    if (control && (character == 0x1aU ||
                    key->wVirtualKeyCode == 'Z') &&
        session->length == 0U) {
        *ctrl_z_pending = 1;
        return WSH_FRONTEND_READ_LINE;
    }
    if (key->wVirtualKeyCode != VK_TAB) {
        interactive_completion_clear(session);
    }
    switch (key->wVirtualKeyCode) {
    case VK_RETURN:
        if (*ctrl_z_pending && session->length == 0U) {
            result = interactive_eof(session, &continue_read);
            if (!continue_read) {
                *out_finished = 1;
            }
            return result;
        }
        if (control) {
            newline = (uint16_t)'\n';
            if (session->length ==
                session->options.max_command_bytes) {
                (void)interactive_write_utf8(
                    session, session->options.output, "\a", 1U);
                return WSH_FRONTEND_READ_LINE;
            }
            if (!interactive_insert_units(session, &newline, 1U) ||
                !interactive_redraw(session)) {
                *out_finished = 1;
                return WSH_FRONTEND_READ_RESOURCE;
            }
            return WSH_FRONTEND_READ_LINE;
        }
        result = interactive_parse_pending(session, &syntax);
        if (result != WSH_OK) {
            if (result == WSH_ERR_ENCODING) {
                session->length = 0U;
                session->cursor = 0U;
                (void)interactive_warning(
                    session,
                    "wsh: invalid UTF-16 input was discarded\r\n");
                if (!interactive_begin_command(session)) {
                    *out_finished = 1;
                    return WSH_FRONTEND_READ_ERROR;
                }
                return WSH_FRONTEND_READ_LINE;
            }
            *out_finished = 1;
            return WSH_FRONTEND_READ_RESOURCE;
        }
        if (syntax == WSH_SYNTAX_INCOMPLETE) {
            newline = (uint16_t)'\n';
            if (session->length ==
                session->options.max_command_bytes) {
                (void)interactive_write_utf8(
                    session, session->options.output, "\a", 1U);
                return WSH_FRONTEND_READ_LINE;
            }
            if (!interactive_insert_units(session, &newline, 1U) ||
                !interactive_redraw(session)) {
                *out_finished = 1;
                return WSH_FRONTEND_READ_RESOURCE;
            }
            return WSH_FRONTEND_READ_LINE;
        }
        (void)interactive_write_utf8(
            session, session->options.output, "\r\n", 2U);
        *out_finished = 1;
        return WSH_FRONTEND_READ_LINE;
    case VK_LEFT:
        if (control) {
            interactive_word_left(session);
        } else {
            session->cursor = interactive_previous_scalar(
                session->units, session->cursor);
        }
        break;
    case VK_RIGHT:
        if (control) {
            interactive_word_right(session);
        } else {
            session->cursor = interactive_next_scalar(
                session->units, session->length, session->cursor);
        }
        break;
    case VK_HOME:
        session->cursor = control ? 0U :
            interactive_line_start(session, session->cursor);
        break;
    case VK_END:
        session->cursor = control ? session->length :
            interactive_line_end(session, session->cursor);
        break;
    case VK_BACK:
        if (session->cursor != 0U) {
            end = session->cursor;
            if (control) {
                interactive_word_left(session);
                start = session->cursor;
            } else {
                start = interactive_previous_scalar(
                    session->units, session->cursor);
            }
            interactive_delete_range(session, start, end);
        }
        break;
    case VK_DELETE:
        if (session->cursor < session->length) {
            start = session->cursor;
            if (control) {
                interactive_word_right(session);
                end = session->cursor;
                session->cursor = start;
            } else {
                end = interactive_next_scalar(
                    session->units, session->length, start);
            }
            interactive_delete_range(session, start, end);
        }
        break;
    case VK_UP:
        if (control) {
            interactive_vertical(session, 0);
        } else if (interactive_line_end(session, 0U) == session->length &&
            session->history_index != 0U) {
            session->history_index -= 1U;
            if (!interactive_history_select(
                    session, session->history_index)) {
                *out_finished = 1;
                return WSH_FRONTEND_READ_RESOURCE;
            }
        }
        break;
    case VK_DOWN:
        if (control) {
            interactive_vertical(session, 1);
        } else if (interactive_line_end(session, 0U) == session->length &&
            session->history_index < session->history_count) {
            session->history_index += 1U;
            if (!interactive_history_select(
                    session, session->history_index)) {
                *out_finished = 1;
                return WSH_FRONTEND_READ_RESOURCE;
            }
        }
        break;
    case VK_INSERT:
        session->overwrite = !session->overwrite;
        break;
    case VK_ESCAPE:
        session->length = 0U;
        session->cursor = 0U;
        if (session->units != NULL) {
            session->units[0] = 0U;
        }
        break;
    case VK_TAB:
        if (!interactive_complete(session, shift)) {
            *out_finished = 1;
            return WSH_FRONTEND_READ_RESOURCE;
        }
        break;
    default:
        if (control && key->wVirtualKeyCode == 'A') {
            session->cursor = 0U;
        } else if (control && key->wVirtualKeyCode == 'E') {
            session->cursor = session->length;
        } else if (control && key->wVirtualKeyCode == 'U') {
            start = interactive_line_start(session, session->cursor);
            interactive_delete_range(session, start, session->cursor);
        } else if (control && key->wVirtualKeyCode == 'K') {
            end = interactive_line_end(session, session->cursor);
            interactive_delete_range(session, session->cursor, end);
        } else if (control && key->wVirtualKeyCode == 'L') {
            if (!interactive_clear_screen(session)) {
                *out_finished = 1;
                return WSH_FRONTEND_READ_ERROR;
            }
            return WSH_FRONTEND_READ_LINE;
        } else if (!control && character >= 0x20U &&
                   character != 0x7fU) {
            if (session->length ==
                session->options.max_command_bytes) {
                (void)interactive_write_utf8(
                    session, session->options.output, "\a", 1U);
                return WSH_FRONTEND_READ_LINE;
            }
            if (!interactive_insert_units(session, &character, 1U)) {
                *out_finished = 1;
                return WSH_FRONTEND_READ_RESOURCE;
            }
        } else {
            return WSH_FRONTEND_READ_LINE;
        }
        break;
    }
    if (!interactive_redraw(session)) {
        *out_finished = 1;
        return WSH_FRONTEND_READ_ERROR;
    }
    return WSH_FRONTEND_READ_LINE;
}

/** Consume console input records until one complete source is submitted. */
static wsh_frontend_read_result interactive_read_advanced(
    wsh_interactive_session *session,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    DWORD raw_mode;
    INPUT_RECORD records[32];
    DWORD received;
    DWORD index;
    int ctrl_z_pending;
    int finished;
    wsh_frontend_read_result result;

    raw_mode = session->original_input_mode;
    raw_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                  ENABLE_PROCESSED_INPUT);
    raw_mode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(session->options.input, raw_mode)) {
        session->advanced = 0;
        return interactive_read_basic(session, out_bytes, out_length);
    }
    if (!interactive_begin_command(session)) {
        (void)SetConsoleMode(
            session->options.input, session->original_input_mode);
        return WSH_FRONTEND_READ_ERROR;
    }
    ctrl_z_pending = 0;
    result = WSH_FRONTEND_READ_LINE;
    finished = 0;
    while (!finished) {
        received = 0U;
        if (!ReadConsoleInputW(
                session->options.input,
                records,
                sizeof(records) / sizeof(records[0]),
                &received)) {
            result = WSH_FRONTEND_READ_ERROR;
            break;
        }
        for (index = 0U; index < received && !finished; ++index) {
            if (records[index].EventType == KEY_EVENT) {
                result = interactive_key(
                    session,
                    &records[index].Event.KeyEvent,
                    &ctrl_z_pending,
                    &finished);
            } else if (records[index].EventType ==
                       WINDOW_BUFFER_SIZE_EVENT) {
                if (!interactive_redraw(session)) {
                    result = WSH_FRONTEND_READ_ERROR;
                    finished = 1;
                }
            }
        }
    }
    if (!SetConsoleMode(
            session->options.input, session->original_input_mode)) {
        return WSH_FRONTEND_READ_ERROR;
    }
    if (result == WSH_FRONTEND_READ_LINE && finished) {
        *out_bytes = (const unsigned char *)session->bytes;
        *out_length = session->byte_length;
    }
    return result;
}

/** Parse one bounded nonnegative decimal size. */
static int interactive_parse_size(
    wsh_string_view text,
    size_t maximum,
    size_t *out_value)
{
    size_t value;
    size_t index;
    unsigned digit;

    if (text.length == 0U) {
        return 0;
    }
    value = 0U;
    for (index = 0U; index < text.length; ++index) {
        if (text.data[index] < '0' || text.data[index] > '9') {
            return 0;
        }
        digit = (unsigned)(text.data[index] - '0');
        if (value > (maximum - digit) / 10U) {
            return 0;
        }
        value = value * 10U + digit;
    }
    *out_value = value;
    return 1;
}

/** Initialize accepted finite interactive defaults. */
void wsh_interactive_options_init(
    wsh_interactive_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    memset(out_options, 0, sizeof(*out_options));
    out_options->input = GetStdHandle(STD_INPUT_HANDLE);
    out_options->output = GetStdHandle(STD_OUTPUT_HANDLE);
    out_options->error = GetStdHandle(STD_ERROR_HANDLE);
    out_options->max_command_bytes = WSH_INTERACTIVE_INPUT_LIMIT;
    out_options->max_history_entries = WSH_INTERACTIVE_HISTORY_ENTRIES;
    out_options->max_history_bytes = WSH_INTERACTIVE_HISTORY_BYTES;
    out_options->history_enabled = 1;
}

/** Create an idle session without loading persistent history. */
wsh_result wsh_interactive_create(
    const wsh_interactive_options *options,
    wsh_interactive_session **out_session)
{
    wsh_interactive_session *session;

    if (options == NULL || out_session == NULL ||
        options->context == NULL || options->evaluator == NULL ||
        options->runtime == NULL || options->max_command_bytes == 0U ||
        options->max_command_bytes > WSH_INTERACTIVE_INPUT_LIMIT ||
        options->max_history_entries > 100000U ||
        options->max_history_bytes > 67108864U ||
        !interactive_is_console(options->input)) {
        return WSH_ERR_INVALID;
    }
    *out_session = NULL;
    session = (wsh_interactive_session *)calloc(1U, sizeof(*session));
    if (session == NULL) {
        return WSH_ERR_RESOURCE;
    }
    session->options = *options;
    session->allocator = wsh_allocator_default();
    if (!GetConsoleMode(
            options->input, &session->original_input_mode)) {
        free(session);
        return WSH_ERR_MISMATCH;
    }
    session->mode_saved = 1;
    session->advanced = !options->force_basic_input &&
        interactive_is_console(options->output);
    session->history_index = 0U;
    *out_session = session;
    return WSH_OK;
}

/** Load bounded history after profile evaluation. */
wsh_result wsh_interactive_load_history(
    wsh_interactive_session *session)
{
    wsh_result result;

    if (session == NULL) {
        return WSH_ERR_INVALID;
    }
    if (!session->options.history_enabled ||
        session->options.max_history_entries == 0U ||
        session->options.max_history_bytes == 0U) {
        return WSH_OK;
    }
    if (!interactive_history_path(session)) {
        (void)interactive_warning(
            session,
            "wsh: history disabled because APPDATA is unavailable\r\n");
        session->options.history_enabled = 0;
        return WSH_OK;
    }
    if (!interactive_history_lock(session)) {
        (void)interactive_warning(
            session,
            "wsh: history lock unavailable; writes are disabled\r\n");
    }
    result = interactive_history_read(session);
    if (result != WSH_OK) {
        (void)interactive_warning(
            session,
            "wsh: history could not be loaded; continuing empty\r\n");
        interactive_history_clear(session);
    }
    session->history_index = session->history_count;
    session->history_ready = 1;
    return WSH_OK;
}

/** Persist history, restore console state, and destroy the session. */
void wsh_interactive_destroy(wsh_interactive_session *session)
{
    if (session == NULL) {
        return;
    }
    if (!interactive_history_write(session)) {
        (void)interactive_warning(
            session,
            "wsh: history replacement failed; prior history retained\r\n");
    }
    if (session->history_mutex != NULL) {
        (void)ReleaseMutex(session->history_mutex);
        CloseHandle(session->history_mutex);
    }
    if (session->mode_saved) {
        (void)SetConsoleMode(
            session->options.input, session->original_input_mode);
    }
    interactive_completion_clear(session);
    free(session->candidates);
    interactive_history_clear(session);
    free(session->history);
    free(session->history_path);
    interactive_prompts_destroy(session);
    free(session->units);
    wsh_allocator_release(&session->allocator, session->bytes);
    memset(session, 0, sizeof(*session));
    free(session);
}

/** Read one complete edited command, cancellation, or EOF. */
wsh_frontend_read_result wsh_interactive_read(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    wsh_interactive_session *session;
    DWORD cooked_mode;

    session = (wsh_interactive_session *)user_data;
    if (session == NULL || out_bytes == NULL || out_length == NULL) {
        return WSH_FRONTEND_READ_ERROR;
    }
    *out_bytes = NULL;
    *out_length = 0U;
    if (session->advanced) {
        return interactive_read_advanced(session, out_bytes, out_length);
    }
    if (!session->fallback_reported) {
        session->fallback_reported = 1;
        (void)interactive_warning(
            session,
            "wsh: advanced console editing unavailable; using basic input\r\n");
    }
    cooked_mode = session->original_input_mode |
        ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
    if (!SetConsoleMode(session->options.input, cooked_mode)) {
        return WSH_FRONTEND_READ_ERROR;
    }
    return interactive_read_basic(session, out_bytes, out_length);
}

/** Record one evaluated complete submission unless suppressed. */
int wsh_interactive_submitted(
    wsh_interactive_session *session,
    const unsigned char *bytes,
    size_t length,
    int status)
{
    int result;

    (void)status;
    if (session == NULL || (bytes == NULL && length != 0U)) {
        return 1;
    }
    result = 1;
    if (session->options.history_enabled &&
        !session->suppress_current && length != 0U) {
        result = interactive_history_append(
            session, (const char *)bytes, length, NULL);
        if (result) {
            result = interactive_history_write(session);
        }
        if (!result) {
            (void)interactive_warning(
                session,
                "wsh: history update failed; command was not persisted\r\n");
        }
    }
    session->suppress_current = 0;
    return 0;
}

/** Publish status 130 and run sigint after pending-input cancellation. */
int wsh_interactive_cancelled(wsh_interactive_session *session)
{
    wsh_status_list *status;
    wsh_result result;

    if (session == NULL) {
        return 1;
    }
    status = NULL;
    result = wsh_evaluator_invoke_signal(
        session->options.evaluator,
        wsh_string_view_from_cstr("sigint"),
        130U,
        &status);
    wsh_status_list_destroy(status);
    return result == WSH_OK ? 0 : 1;
}

/** Execute one accepted history namespace request. */
wsh_result wsh_interactive_history_invoke(
    wsh_interactive_session *session,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    size_t count;
    size_t start;
    size_t index;
    wsh_string_view argument;
    wsh_result result;

    if (session == NULL || request == NULL || output == NULL ||
        status == NULL || !session->options.history_enabled ||
        !session->history_ready) {
        return WSH_ERR_MISMATCH;
    }
    count = request->arguments == NULL ? 0U :
        wsh_value_count(request->arguments);
    if (wsh_string_view_equal(
            request->subject,
            wsh_string_view_from_cstr("history::suppress"))) {
        if (count != 0U) {
            return WSH_ERR_INVALID;
        }
        session->suppress_current = 1;
        return wsh_status_builder_append(status, 0U);
    }
    if (wsh_string_view_equal(
            request->subject,
            wsh_string_view_from_cstr("history::clear"))) {
        if (count != 0U) {
            return WSH_ERR_INVALID;
        }
        interactive_history_clear(session);
        session->suppress_current = 1;
        result = interactive_history_write(session) ?
            WSH_OK : WSH_ERR_MISMATCH;
        if (result == WSH_OK) {
            result = wsh_status_builder_append(status, 0U);
        }
        return result;
    }
    if (!wsh_string_view_equal(
            request->subject,
            wsh_string_view_from_cstr("history::list")) || count > 1U) {
        return WSH_ERR_INVALID;
    }
    count = session->history_count;
    if (request->arguments != NULL &&
        wsh_value_count(request->arguments) == 1U) {
        result = wsh_value_at(request->arguments, 0U, &argument);
        if (result != WSH_OK || !interactive_parse_size(
                argument, session->history_count, &count)) {
            return WSH_ERR_INVALID;
        }
    }
    start = count < session->history_count ?
        session->history_count - count : 0U;
    result = WSH_OK;
    for (index = start; result == WSH_OK &&
         index < session->history_count; ++index) {
        result = wsh_value_builder_append(
            output,
            (wsh_string_view){session->history[index].command,
                              session->history[index].length});
    }
    if (result == WSH_OK) {
        result = wsh_status_builder_append(status, 0U);
    }
    return result;
}

/** Resolve an accepted evaluator exit against live background jobs. */
int wsh_interactive_resolve_exit(
    wsh_interactive_session *session)
{
    uint32_t status;
    int forced;
    size_t jobs;
    WCHAR response[16];
    DWORD received;

    if (session == NULL || !wsh_evaluator_exit_requested(
            session->options.evaluator, &status, &forced)) {
        return 1;
    }
    (void)status;
    jobs = wsh_windows_runtime_background_count(session->options.runtime);
    if (jobs == 0U) {
        session->stop_requested = 1;
        return 1;
    }
    if (forced) {
        session->stop_requested =
            wsh_windows_runtime_cancel_all(
                session->options.runtime) == WSH_OK;
        return session->stop_requested;
    }
    if (!interactive_is_console(session->options.input) ||
        !interactive_is_console(session->options.output)) {
        (void)interactive_warning(
            session,
            "wsh: exit refused while background jobs remain\r\n");
        wsh_evaluator_clear_exit(session->options.evaluator);
        return 1;
    }
    if (!interactive_background_list(session)) {
        return 0;
    }
    (void)interactive_write_utf8(
        session,
        session->options.output,
        "wsh: background jobs remain; cancel and exit? [y/N] ",
        strlen("wsh: background jobs remain; cancel and exit? [y/N] "));
    received = 0U;
    if (!ReadConsoleW(
            session->options.input,
            response,
            sizeof(response) / sizeof(response[0]),
            &received,
            NULL)) {
        return 0;
    }
    if (received != 0U &&
        (response[0] == L'y' || response[0] == L'Y')) {
        session->stop_requested =
            wsh_windows_runtime_cancel_all(
                session->options.runtime) == WSH_OK;
        return session->stop_requested;
    }
    wsh_evaluator_clear_exit(session->options.evaluator);
    return 1;
}

/** Return nonzero after interactive exit has been accepted. */
int wsh_interactive_should_stop(
    const wsh_interactive_session *session)
{
    return session != NULL && session->stop_requested;
}

/** Invoke sigexit once with the supplied orderly status. */
void wsh_interactive_signal_exit(
    wsh_interactive_session *session,
    uint32_t status)
{
    wsh_status_list *signal_status;

    if (session == NULL || session->sigexit_invoked) {
        return;
    }
    session->sigexit_invoked = 1;
    signal_status = NULL;
    (void)wsh_evaluator_invoke_signal(
        session->options.evaluator,
        wsh_string_view_from_cstr("sigexit"),
        status,
        &signal_status);
    wsh_status_list_destroy(signal_status);
}
