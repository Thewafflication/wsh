/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file console_driver.c
 * @brief Native console-input driver for M7 interactive acceptance tests.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/** Maximum milliseconds allowed for an orderly interactive exit. */
#define DRIVER_TIMEOUT 12000U
/** Minimum retained console rows used for observable acceptance markers. */
#define DRIVER_SCREEN_LINES 240

/** One isolated native console test process. */
typedef struct driver_context {
    /** Borrowed original CTest output pipe. */
    HANDLE report;
    /** Owned inheritable CONIN handle. */
    HANDLE input;
    /** Owned inheritable CONOUT screen-buffer handle. */
    HANDLE output;
    /** Owned absolute tested WSH path. */
    WCHAR *wsh;
    /** Owned absolute runtime-probe path. */
    WCHAR *probe;
    /** Owned isolated native-test directory. */
    WCHAR *root;
    /** Owned isolated application-data directory. */
    WCHAR *appdata;
    /** Console input mode expected after WSH cleanup. */
    DWORD expected_mode;
    /** Owned active WSH process handles and identifiers. */
    PROCESS_INFORMATION process;
    /** Number of failed acceptance observations. */
    unsigned failures;
} driver_context;

/** Convert UTF-16 text to one owned UTF-8 string. */
static char *driver_from_wide(const WCHAR *wide);

/** Keep control events inside the owned console test boundary. */
static BOOL WINAPI driver_ignore_control(DWORD control)
{
    (void)control;
    return TRUE;
}

/** Return the number of UTF-16 units before NUL. */
static size_t driver_wide_length(const WCHAR *text)
{
    size_t length;

    length = 0U;
    while (text != NULL && text[length] != 0U) {
        length += 1U;
    }
    return length;
}

/** Convert strict UTF-8 text to an owned UTF-16 string. */
static WCHAR *driver_to_wide(const char *text)
{
    int count;
    WCHAR *wide;

    if (text == NULL) {
        return NULL;
    }
    count = MultiByteToWideChar(CP_UTF8, 0U, text, -1, NULL, 0);
    if (count <= 0) {
        return NULL;
    }
    wide = (WCHAR *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL || MultiByteToWideChar(
            CP_UTF8, 0U, text, -1, wide, count) != count) {
        free(wide);
        return NULL;
    }
    return wide;
}

/** Write one diagnostic line to the original CTest output handle. */
static void driver_report(driver_context *context, const char *text)
{
    DWORD written;

    written = 0U;
    if (context->report != NULL &&
        context->report != INVALID_HANDLE_VALUE) {
        (void)WriteFile(
            context->report,
            text,
            (DWORD)strlen(text),
            &written,
            NULL);
        (void)WriteFile(context->report, "\r\n", 2U, &written, NULL);
    }
}

/** Retain one failed acceptance observation. */
static void driver_fail(driver_context *context, const char *text)
{
    char message[256];

    context->failures += 1U;
    (void)snprintf(message, sizeof(message), "FAIL: %s", text);
    driver_report(context, message);
}

/** Join one child name to a fixed test root. */
static WCHAR *driver_join(const WCHAR *root, const WCHAR *child)
{
    size_t root_length;
    size_t child_length;
    int separator;
    WCHAR *joined;

    root_length = driver_wide_length(root);
    child_length = driver_wide_length(child);
    separator = root_length != 0U && root[root_length - 1U] != L'\\' &&
        root[root_length - 1U] != L'/';
    joined = (WCHAR *)malloc(
        (root_length + (size_t)separator + child_length + 1U) *
            sizeof(*joined));
    if (joined == NULL) {
        return NULL;
    }
    memcpy(joined, root, root_length * sizeof(*joined));
    if (separator) {
        joined[root_length++] = L'\\';
    }
    memcpy(
        joined + root_length,
        child,
        (child_length + 1U) * sizeof(*joined));
    return joined;
}

/** Create one directory when absent. */
static int driver_directory(const WCHAR *path)
{
    return CreateDirectoryW(path, NULL) != 0 ||
        GetLastError() == ERROR_ALREADY_EXISTS;
}

/** Write exact UTF-8 fixture bytes to one file. */
static int driver_write_file(
    const WCHAR *path,
    const char *bytes,
    size_t length)
{
    HANDLE file;
    DWORD written;
    int result;

    file = CreateFileW(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE || length > 0xffffffffUL) {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        return 0;
    }
    written = 0U;
    result = (length == 0U || WriteFile(
        file, bytes, (DWORD)length, &written, NULL)) && written == length;
    result = CloseHandle(file) != 0 && result;
    return result;
}

/** Read one bounded fixture file into owned bytes. */
static char *driver_read_file(
    const WCHAR *path,
    size_t *out_length)
{
    HANDLE file;
    DWORD high;
    DWORD low;
    DWORD received;
    char *bytes;

    *out_length = 0U;
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    high = 0U;
    low = GetFileSize(file, &high);
    if (high != 0U || low == INVALID_FILE_SIZE) {
        CloseHandle(file);
        return NULL;
    }
    bytes = (char *)malloc((size_t)low + 1U);
    if (bytes == NULL) {
        CloseHandle(file);
        return NULL;
    }
    received = 0U;
    if (low != 0U &&
        (!ReadFile(file, bytes, low, &received, NULL) || received != low)) {
        free(bytes);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    bytes[low] = '\0';
    *out_length = low;
    return bytes;
}

/** Return the number of nonoverlapping byte occurrences. */
static size_t driver_count_text(
    const char *bytes,
    size_t length,
    const char *needle)
{
    size_t count;
    size_t needle_length;
    size_t offset;

    count = 0U;
    needle_length = strlen(needle);
    offset = 0U;
    while (needle_length != 0U && offset + needle_length <= length) {
        if (memcmp(bytes + offset, needle, needle_length) == 0) {
            count += 1U;
            offset += needle_length;
        } else {
            offset += 1U;
        }
    }
    return count;
}

/** Clear the native screen buffer and pending input records. */
static void driver_clear_console(driver_context *context)
{
    CONSOLE_SCREEN_BUFFER_INFO information;
    COORD origin;
    DWORD cells;
    DWORD filled;

    (void)FlushConsoleInputBuffer(context->input);
    if (!GetConsoleScreenBufferInfo(context->output, &information)) {
        return;
    }
    origin.X = 0;
    origin.Y = 0;
    cells = (DWORD)information.dwSize.X * (DWORD)information.dwSize.Y;
    filled = 0U;
    (void)FillConsoleOutputCharacterW(
        context->output, L' ', cells, origin, &filled);
    (void)SetConsoleCursorPosition(context->output, origin);
}

/** Snapshot the used screen buffer into owned UTF-16 cells. */
static WCHAR *driver_screen(driver_context *context, size_t *out_length)
{
    CONSOLE_SCREEN_BUFFER_INFO information;
    COORD origin;
    DWORD cells;
    DWORD received;
    WCHAR *screen;

    *out_length = 0U;
    if (!GetConsoleScreenBufferInfo(context->output, &information)) {
        return NULL;
    }
    cells = (DWORD)information.dwSize.X *
        (DWORD)(information.dwCursorPosition.Y + 1);
    screen = (WCHAR *)malloc(((size_t)cells + 1U) * sizeof(*screen));
    if (screen == NULL) {
        return NULL;
    }
    origin.X = 0;
    origin.Y = 0;
    received = 0U;
    if (!ReadConsoleOutputCharacterW(
            context->output, screen, cells, origin, &received)) {
        free(screen);
        return NULL;
    }
    screen[received] = 0U;
    *out_length = received;
    return screen;
}

/** Find one contiguous literal in screen-buffer cells. */
static size_t driver_screen_index(
    driver_context *context,
    const WCHAR *needle)
{
    WCHAR *screen;
    size_t screen_length;
    size_t needle_length;
    size_t index;

    screen = driver_screen(context, &screen_length);
    needle_length = driver_wide_length(needle);
    if (screen == NULL || needle_length == 0U) {
        free(screen);
        return (size_t)-1;
    }
    for (index = 0U; index + needle_length <= screen_length; ++index) {
        if (memcmp(
                screen + index,
                needle,
                needle_length * sizeof(*screen)) == 0) {
            free(screen);
            return index;
        }
    }
    free(screen);
    return (size_t)-1;
}

/** Count literal occurrences in the used screen buffer. */
static size_t driver_screen_count(
    driver_context *context,
    const WCHAR *needle)
{
    WCHAR *screen;
    size_t screen_length;
    size_t needle_length;
    size_t index;
    size_t count;

    screen = driver_screen(context, &screen_length);
    needle_length = driver_wide_length(needle);
    count = 0U;
    for (index = 0U; screen != NULL && needle_length != 0U &&
         index + needle_length <= screen_length; ++index) {
        if (memcmp(
                screen + index,
                needle,
                needle_length * sizeof(*screen)) == 0) {
            count += 1U;
            index += needle_length - 1U;
        }
    }
    free(screen);
    return count;
}

/** Report nonblank screen rows after a native-session failure. */
static void driver_dump_screen(driver_context *context)
{
    CONSOLE_SCREEN_BUFFER_INFO information;
    WCHAR *screen;
    size_t screen_length;
    size_t row;
    size_t start;
    size_t end;
    int count;
    char *bytes;

    if (!GetConsoleScreenBufferInfo(context->output, &information)) {
        return;
    }
    screen = driver_screen(context, &screen_length);
    if (screen == NULL) {
        return;
    }
    for (row = 0U;
         row <= (size_t)information.dwCursorPosition.Y; ++row) {
        start = row * (size_t)information.dwSize.X;
        if (start >= screen_length) {
            break;
        }
        end = start + (size_t)information.dwSize.X;
        if (end > screen_length) {
            end = screen_length;
        }
        while (end > start && screen[end - 1U] == L' ') {
            end -= 1U;
        }
        if (end == start) {
            continue;
        }
        count = WideCharToMultiByte(
            CP_UTF8,
            0U,
            screen + start,
            (int)(end - start),
            NULL,
            0,
            NULL,
            NULL);
        bytes = count <= 0 ? NULL : (char *)malloc((size_t)count + 9U);
        if (bytes != NULL) {
            memcpy(bytes, "SCREEN: ", 8U);
            if (WideCharToMultiByte(
                    CP_UTF8,
                    0U,
                    screen + start,
                    (int)(end - start),
                    bytes + 8U,
                    count,
                    NULL,
                    NULL) == count) {
                bytes[(size_t)count + 8U] = '\0';
                driver_report(context, bytes);
            }
            free(bytes);
        }
    }
    free(screen);
}

/** Wait until a native console marker becomes observable. */
static int driver_wait_marker(
    driver_context *context,
    const WCHAR *marker,
    DWORD timeout)
{
    DWORD start;

    start = GetTickCount();
    do {
        if (driver_screen_index(context, marker) != (size_t)-1) {
            return 1;
        }
        if (WaitForSingleObject(context->process.hProcess, 0U) ==
            WAIT_OBJECT_0) {
            break;
        }
        Sleep(20U);
    } while (GetTickCount() - start < timeout);
    return 0;
}

/** Wait until a screen marker has appeared the requested number of times. */
static int driver_wait_count(
    driver_context *context,
    const WCHAR *marker,
    size_t required,
    DWORD timeout)
{
    DWORD start;

    start = GetTickCount();
    do {
        if (driver_screen_count(context, marker) >= required) {
            return 1;
        }
        if (WaitForSingleObject(context->process.hProcess, 0U) ==
            WAIT_OBJECT_0) {
            break;
        }
        Sleep(20U);
    } while (GetTickCount() - start < timeout);
    return 0;
}

/** Append UTF-16 text to one fixed command-line buffer. */
static int driver_append(
    WCHAR *buffer,
    size_t capacity,
    size_t *length,
    const WCHAR *text)
{
    size_t count;

    count = driver_wide_length(text);
    if (*length > capacity || count >= capacity - *length) {
        return 0;
    }
    memcpy(buffer + *length, text, count * sizeof(*buffer));
    *length += count;
    buffer[*length] = 0U;
    return 1;
}

/** Append a Windows command-line argument under ordinary path constraints. */
static int driver_append_argument(
    WCHAR *buffer,
    size_t capacity,
    size_t *length,
    const WCHAR *argument)
{
    return driver_append(buffer, capacity, length, L"\"") &&
        driver_append(buffer, capacity, length, argument) &&
        driver_append(buffer, capacity, length, L"\"");
}

/** Launch one interactive WSH in the driver's genuine console. */
static int driver_launch(
    driver_context *context,
    const WCHAR *extra,
    int basic)
{
    SECURITY_ATTRIBUTES security;
    STARTUPINFOW startup;
    WCHAR command[32768];
    size_t length;
    DWORD mode;

    driver_clear_console(context);
    mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
        ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(context->input, mode)) {
        return 0;
    }
    context->expected_mode = mode;
    (void)SetEnvironmentVariableW(L"APPDATA", context->appdata);
    if (basic) {
        (void)SetEnvironmentVariableW(L"WSH_FORCE_BASIC_INPUT", L"1");
    } else {
        (void)SetEnvironmentVariableW(L"WSH_FORCE_BASIC_INPUT", NULL);
    }
    memset(command, 0, sizeof(command));
    length = 0U;
    if (!driver_append_argument(
            command,
            sizeof(command) / sizeof(command[0]),
            &length,
            context->wsh) ||
        (extra != NULL &&
         (!driver_append(
              command,
              sizeof(command) / sizeof(command[0]),
              &length,
              L" ") ||
          !driver_append(
              command,
              sizeof(command) / sizeof(command[0]),
              &length,
              extra)))) {
        return 0;
    }
    memset(&security, 0, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = context->input;
    startup.hStdOutput = context->output;
    startup.hStdError = context->output;
    memset(&context->process, 0, sizeof(context->process));
    return CreateProcessW(
        NULL,
        command,
        NULL,
        NULL,
        TRUE,
        0U,
        NULL,
        context->root,
        &startup,
        &context->process) != 0;
}

/** Write one complete console input record. */
static int driver_record(driver_context *context, INPUT_RECORD *record)
{
    DWORD written;

    written = 0U;
    return WriteConsoleInputW(
        context->input, record, 1U, &written) != 0 && written == 1U;
}

/** Send one key-down record with selected modifiers. */
static int driver_key(
    driver_context *context,
    WORD virtual_key,
    WCHAR character,
    DWORD controls)
{
    INPUT_RECORD record;
    DWORD pending;
    DWORD start;
    int result;

    memset(&record, 0, sizeof(record));
    record.EventType = KEY_EVENT;
    record.Event.KeyEvent.bKeyDown = TRUE;
    record.Event.KeyEvent.wRepeatCount = 1U;
    record.Event.KeyEvent.wVirtualKeyCode = virtual_key;
    record.Event.KeyEvent.uChar.UnicodeChar = character;
    record.Event.KeyEvent.dwControlKeyState = controls;
    result = driver_record(context, &record);
    start = GetTickCount();
    do {
        pending = 0U;
        if (GetNumberOfConsoleInputEvents(context->input, &pending) &&
            pending == 0U) {
            break;
        }
        Sleep(1U);
    } while (GetTickCount() - start < 3000U);
    Sleep(3U);
    return result;
}

/** Send strict UTF-8 text as native Unicode key records. */
static int driver_text(driver_context *context, const char *text)
{
    WCHAR *wide;
    size_t index;
    int result;

    wide = driver_to_wide(text);
    if (wide == NULL) {
        return 0;
    }
    result = 1;
    for (index = 0U; wide[index] != 0U && result; ++index) {
        result = driver_key(context, 0U, wide[index], 0U);
    }
    free(wide);
    return result;
}

/** Send an Enter key, optionally as Ctrl+Enter. */
static int driver_enter(driver_context *context, int control)
{
    int result;

    result = driver_key(
        context,
        VK_RETURN,
        L'\r',
        control ? LEFT_CTRL_PRESSED : 0U);
    Sleep(control ? 20U : 80U);
    return result;
}

/** Submit one ordinary UTF-8 command. */
static int driver_command(driver_context *context, const char *command)
{
    if (!driver_text(context, command) ||
        !driver_key(context, VK_RETURN, L'\r', 0U)) {
        return 0;
    }
    Sleep(100U);
    return 1;
}

/** Send one control-letter editor chord. */
static int driver_control(driver_context *context, WORD key, WCHAR control)
{
    return driver_key(context, key, control, LEFT_CTRL_PRESSED);
}

/** Inject one resize input record without requiring a VT host. */
static int driver_resize(driver_context *context)
{
    INPUT_RECORD record;

    memset(&record, 0, sizeof(record));
    record.EventType = WINDOW_BUFFER_SIZE_EVENT;
    record.Event.WindowBufferSizeEvent.dwSize.X = 100;
    record.Event.WindowBufferSizeEvent.dwSize.Y = DRIVER_SCREEN_LINES;
    return driver_record(context, &record);
}

/** Wait for orderly exit, verify mode restoration, and close handles. */
static int driver_finish(driver_context *context, DWORD expected_exit)
{
    DWORD wait_result;
    DWORD exit_code;
    DWORD mode;
    int result;

    wait_result = WaitForSingleObject(
        context->process.hProcess, DRIVER_TIMEOUT);
    if (wait_result != WAIT_OBJECT_0) {
        (void)TerminateProcess(context->process.hProcess, 250U);
        (void)WaitForSingleObject(context->process.hProcess, 2000U);
        driver_fail(context, "interactive child did not exit in time");
        driver_dump_screen(context);
    }
    exit_code = 0xffffffffUL;
    result = GetExitCodeProcess(
        context->process.hProcess, &exit_code) != 0 &&
        exit_code == expected_exit;
    if (!result) {
        driver_fail(context, "interactive child exit status mismatch");
    }
    mode = 0U;
    if (!GetConsoleMode(context->input, &mode) ||
        mode != context->expected_mode) {
        driver_fail(context, "console input mode was not restored");
        result = 0;
    }
    CloseHandle(context->process.hThread);
    CloseHandle(context->process.hProcess);
    memset(&context->process, 0, sizeof(context->process));
    return result;
}

/** Require one observable screen marker. */
static void driver_expect(
    driver_context *context,
    const WCHAR *marker,
    const char *description)
{
    if (driver_screen_index(context, marker) == (size_t)-1) {
        driver_fail(context, description);
    }
}

/** Exercise startup, every editor family, Unicode, resize, and fallback. */
static void driver_editor_scenario(driver_context *context)
{
    WCHAR *profile_one;
    WCHAR *profile_two;
    WCHAR extra[32768];
    size_t extra_length;
    size_t first;
    size_t second;

    profile_one = driver_join(context->root, L"profile-one.wsh");
    profile_two = driver_join(context->root, L"profile-two.wsh");
    if (profile_one == NULL || profile_two == NULL ||
        !driver_write_file(
            profile_one,
            "echo M7_PROFILE_1\n",
            strlen("echo M7_PROFILE_1\n")) ||
        !driver_write_file(
            profile_two,
            "echo M7_PROFILE_2; "
            "prompt=('M7<$> ' 'M7;CONT> ')\n",
            strlen("echo M7_PROFILE_2; "
                   "prompt=('M7<$> ' 'M7;CONT> ')\n"))) {
        driver_fail(context, "could not create profile fixtures");
        free(profile_one);
        free(profile_two);
        return;
    }
    extra[0] = 0U;
    extra_length = 0U;
    if (!driver_append(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            L"--profile ") ||
        !driver_append_argument(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            profile_one) ||
        !driver_append(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            L" --profile ") ||
        !driver_append_argument(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            profile_two) ||
        !driver_launch(context, extra, 0)) {
        driver_fail(context, "profile editor session did not launch");
        free(profile_one);
        free(profile_two);
        return;
    }
    free(profile_one);
    free(profile_two);
    if (!driver_wait_marker(context, L"M7<$> ", 4000U)) {
        driver_fail(context, "literal profile prompt was not displayed");
    }
    first = driver_screen_index(context, L"M7_PROFILE_1");
    second = driver_screen_index(context, L"M7_PROFILE_2");
    if (first == (size_t)-1 || second == (size_t)-1 || first >= second) {
        driver_fail(context, "explicit profiles did not run in option order");
    }

    (void)driver_text(context, "echo BAD");
    (void)driver_key(context, VK_LEFT, 0U, LEFT_CTRL_PRESSED);
    (void)driver_key(context, VK_DELETE, 0U, LEFT_CTRL_PRESSED);
    (void)driver_text(context, "M7_EDITOR_OK");
    (void)driver_key(context, 0U, (WCHAR)0xd83dU, 0U);
    (void)driver_key(context, 0U, (WCHAR)0xde00U, 0U);
    (void)driver_key(context, VK_BACK, L'\b', 0U);
    (void)driver_enter(context, 0);
    (void)driver_wait_marker(context, L"M7_EDITOR_OK", 3000U);
    driver_expect(context, L"M7_EDITOR_OK", "scalar-safe edit failed");

    (void)driver_text(context, "echo M7_INSERX");
    (void)driver_key(context, VK_LEFT, 0U, 0U);
    (void)driver_key(context, VK_INSERT, 0U, 0U);
    (void)driver_text(context, "T");
    (void)driver_key(context, VK_INSERT, 0U, 0U);
    (void)driver_key(context, VK_RIGHT, 0U, 0U);
    (void)driver_enter(context, 0);
    (void)driver_wait_marker(context, L"M7_INSERT", 3000U);
    driver_expect(context, L"M7_INSERT", "overwrite mode edit failed");

    (void)driver_text(context, "echo M7_MULTI");
    (void)driver_enter(context, 1);
    (void)driver_text(context, "echo M7_SECOND");
    (void)driver_key(context, VK_UP, 0U, LEFT_CTRL_PRESSED);
    (void)driver_key(context, VK_DOWN, 0U, LEFT_CTRL_PRESSED);
    (void)driver_key(context, VK_HOME, 0U, 0U);
    (void)driver_key(context, VK_END, 0U, 0U);
    (void)driver_key(context, VK_HOME, 0U, LEFT_CTRL_PRESSED);
    (void)driver_key(context, VK_END, 0U, LEFT_CTRL_PRESSED);
    (void)driver_resize(context);
    (void)driver_control(context, 'L', 0x0cU);
    (void)driver_enter(context, 0);

    (void)driver_key(context, VK_UP, 0U, 0U);
    (void)driver_key(context, VK_DOWN, 0U, 0U);
    (void)driver_key(context, VK_ESCAPE, 0x1bU, 0U);
    (void)driver_text(context, "discarded");
    (void)driver_control(context, 'A', 0x01U);
    (void)driver_control(context, 'K', 0x0bU);
    (void)driver_text(context, "other");
    (void)driver_control(context, 'E', 0x05U);
    (void)driver_control(context, 'U', 0x15U);
    (void)driver_command(context, "echo M7_KEYS_OK");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);

    driver_expect(context, L"M7_MULTI", "multiline first command failed");
    driver_expect(context, L"M7_SECOND", "multiline second command failed");
    driver_expect(context, L"M7_KEYS_OK", "editor chord recovery failed");
    if (!driver_launch(context, L"--no-profile", 1)) {
        driver_fail(context, "basic fallback session did not launch");
        return;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    (void)driver_command(
        context,
        "fn m7basic { echo M7_BASIC_OK }");
    (void)driver_command(context, "m7basic");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);
    driver_expect(
        context,
        L"advanced console editing unavailable; using basic input",
        "basic fallback diagnostic was absent");
    driver_expect(context, L"M7_BASIC_OK", "basic fallback evaluation failed");

    profile_one = driver_join(context->root, L"profile-exit.wsh");
    profile_two = driver_join(context->root, L"profile-after-exit.wsh");
    if (profile_one == NULL || profile_two == NULL ||
        !driver_write_file(
            profile_one, "exit 0\n", strlen("exit 0\n")) ||
        !driver_write_file(
            profile_two,
            "echo M7_PROFILE_AFTER_EXIT\n",
            strlen("echo M7_PROFILE_AFTER_EXIT\n"))) {
        driver_fail(context, "could not create profile-exit fixtures");
        free(profile_one);
        free(profile_two);
        return;
    }
    extra[0] = 0U;
    extra_length = 0U;
    if (!driver_append(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            L"--profile ") ||
        !driver_append_argument(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            profile_one) ||
        !driver_append(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            L" --profile ") ||
        !driver_append_argument(
            extra,
            sizeof(extra) / sizeof(extra[0]),
            &extra_length,
            profile_two) ||
        !driver_launch(context, extra, 0)) {
        driver_fail(context, "profile-exit session did not launch");
    } else {
        (void)driver_finish(context, 0U);
        if (driver_screen_index(
                context, L"M7_PROFILE_AFTER_EXIT") != (size_t)-1) {
            driver_fail(context, "profile evaluation continued after exit");
        }
    }
    free(profile_one);
    free(profile_two);
}

/** Exercise deterministic providers, cycling, and safe source quoting. */
static void driver_completion_scenario(driver_context *context)
{
    WCHAR *space_file;
    WCHAR *apostrophe_file;
    WCHAR *local_script;
    WCHAR *bin;
    WCHAR *path_script;
    char *bin_utf8;
    char path_command[4096];
    int command_length;

    space_file = driver_join(context->root, L"quote space.txt");
    apostrophe_file = driver_join(context->root, L"quote'apos.txt");
    local_script = driver_join(context->root, L"m7command.wsh");
    bin = driver_join(context->root, L"bin");
    path_script = bin == NULL ? NULL : driver_join(bin, L"m7path.wsh");
    if (space_file == NULL || apostrophe_file == NULL ||
        local_script == NULL || bin == NULL || path_script == NULL ||
        !driver_directory(bin) ||
        !driver_write_file(space_file, "", 0U) ||
        !driver_write_file(apostrophe_file, "", 0U) ||
        !driver_write_file(
            local_script,
            "echo M7_LOCAL_COMMAND_OK\n",
            strlen("echo M7_LOCAL_COMMAND_OK\n")) ||
        !driver_write_file(
            path_script,
            "echo M7_PATH_COMMAND_OK\n",
            strlen("echo M7_PATH_COMMAND_OK\n"))) {
        driver_fail(context, "could not create completion fixtures");
        goto cleanup;
    }
    bin_utf8 = driver_from_wide(bin);
    command_length = bin_utf8 == NULL ? -1 : snprintf(
        path_command,
        sizeof(path_command),
        "path=('%s' '\\\\invalid\\m7-share')",
        bin_utf8);
    free(bin_utf8);
    if (command_length <= 0 ||
        (size_t)command_length >= sizeof(path_command)) {
        driver_fail(context, "could not build completion path assignment");
        goto cleanup;
    }

    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "completion session did not launch");
        goto cleanup;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    (void)driver_command(
        context,
        "M7VARIABLE=(M7_VARIABLE_OK)");
    (void)driver_command(
        context,
        "fn m7function { echo M7_FUNCTION_OK }");
    (void)driver_command(context, path_command);

    (void)driver_text(context, "echo 'quote s");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_enter(context, 0);
    if (!driver_wait_count(context, L"quote space.txt", 2U, 3000U)) {
        driver_fail(context, "space quoting failed");
    }
    (void)driver_text(context, "echo 'quote''a");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_enter(context, 0);
    if (!driver_wait_marker(context, L"quote'apos.txt", 3000U)) {
        driver_fail(context, "apostrophe quoting failed");
    }
    (void)driver_text(context, "echo $M7V");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_enter(context, 0);
    if (!driver_wait_marker(context, L"M7_VARIABLE_OK", 3000U)) {
        driver_fail(context, "variable completion failed");
    }
    (void)driver_text(context, "m7fun");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_enter(context, 0);
    if (!driver_wait_marker(context, L"M7_FUNCTION_OK", 3000U)) {
        driver_fail(context, "function completion failed");
    }
    (void)driver_text(context, "m7comm");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_enter(context, 0);
    if (!driver_wait_marker(context, L"M7_LOCAL_COMMAND_OK", 3000U)) {
        driver_fail(context, "local script completion failed");
    }
    (void)driver_text(context, "m7pat");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_enter(context, 0);
    if (!driver_wait_marker(context, L"M7_PATH_COMMAND_OK", 3000U)) {
        driver_fail(context, "PATH script completion failed");
    }
    (void)driver_text(context, "hist");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_key(context, VK_TAB, L'\t', SHIFT_PRESSED);
    (void)driver_key(context, VK_ESCAPE, 0x1bU, 0U);
    (void)driver_command(context, "echo M7_COMPLETION_RECOVERED");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);

    driver_expect(
        context,
        L"M7_COMPLETION_RECOVERED",
        "forward/reverse completion did not recover");

cleanup:
    free(space_file);
    free(apostrophe_file);
    free(local_script);
    free(path_script);
    free(bin);
}

/** Return the isolated default history path. */
static WCHAR *driver_history_path(driver_context *context)
{
    return driver_join(context->appdata, L"Waughtal\\WSH\\history.txt");
}

/** Create and own the same path-derived history mutex as WSH. */
static HANDLE driver_history_mutex(const WCHAR *path)
{
    uint32_t hash;
    size_t index;
    WCHAR name[64];
    static const WCHAR prefix[] = L"Local\\WSH-History-";
    static const WCHAR digits[] = L"0123456789abcdef";
    size_t offset;
    int shift;
    uint16_t unit;

    hash = 2166136261UL;
    for (index = 0U; path[index] != 0U; ++index) {
        unit = (uint16_t)path[index];
        if (unit >= (uint16_t)'A' && unit <= (uint16_t)'Z') {
            unit = (uint16_t)(unit + ((uint16_t)'a' - (uint16_t)'A'));
        }
        hash ^= unit;
        hash *= 16777619UL;
    }
    offset = driver_wide_length(prefix);
    memcpy(name, prefix, offset * sizeof(*name));
    for (shift = 28; shift >= 0; shift -= 4) {
        name[offset++] = digits[(hash >> (unsigned)shift) & 0x0fU];
    }
    name[offset] = 0U;
    return CreateMutexW(NULL, TRUE, name);
}

/** Exercise inert JSONL history, deduplication, suppression, and locking. */
static void driver_history_scenario(driver_context *context)
{
    static const char corpus[] =
        "{\"format\":\"wsh-history\",\"version\":1}\r\n"
        "{\"unknown\":{\"nested\":[1,true,null]},"
        "\"command\":\"echo M7_LOADED_SAFE\","
        "\"time\":\"2026-08-24T12:00:00Z\"}\r\n"
        "{malformed}\r\n"
        "{\"time\":\"2026-08-24T12:00:01Z\","
        "\"command\":\"echo M7_INERT_ONLY\"}\r\n";
    WCHAR *history;
    char *bytes;
    char *before;
    size_t length;
    size_t before_length;
    HANDLE mutex;

    history = driver_history_path(context);
    if (history == NULL || !driver_write_file(
            history, corpus, sizeof(corpus) - 1U)) {
        driver_fail(context, "could not create history corpus");
        free(history);
        return;
    }
    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "history session did not launch");
        free(history);
        return;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    (void)driver_key(context, VK_UP, 0U, 0U);
    (void)driver_key(context, VK_ESCAPE, 0x1bU, 0U);
    (void)driver_command(context, "echo M7_HISTORY_ONCE");
    (void)driver_command(context, "echo M7_HISTORY_ONCE");
    (void)driver_command(
        context,
        "history::suppress; echo M7_SECRET_OUTPUT");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);
    driver_expect(
        context,
        L"malformed or oversized history records were skipped",
        "malformed history warning was not coalesced");
    driver_expect(
        context, L"M7_HISTORY_ONCE", "history command did not evaluate");
    driver_expect(
        context, L"M7_SECRET_OUTPUT", "suppressed command did not evaluate");

    bytes = driver_read_file(history, &length);
    if (bytes == NULL ||
        driver_count_text(bytes, length, "M7_HISTORY_ONCE") != 1U ||
        driver_count_text(bytes, length, "M7_SECRET_OUTPUT") != 0U ||
        driver_count_text(bytes, length, "M7_LOADED_SAFE") != 1U ||
        driver_count_text(bytes, length, "{malformed}") != 0U) {
        driver_fail(context, "history rewrite/dedup/suppression mismatch");
    }
    free(bytes);

    before = driver_read_file(history, &before_length);
    mutex = driver_history_mutex(history);
    if (before == NULL || mutex == NULL ||
        !driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "history contention setup failed");
    } else {
        (void)driver_wait_marker(context, L"% ", 3000U);
        (void)driver_command(context, "echo M7_LOCKED_MUTATION");
        (void)driver_command(context, "exit");
        (void)driver_finish(context, 0U);
        driver_expect(
            context,
            L"history lock unavailable; writes are disabled",
            "history contention warning was absent");
        bytes = driver_read_file(history, &length);
        if (bytes == NULL || length != before_length ||
            memcmp(bytes, before, length) != 0) {
            driver_fail(context, "contended history replaced prior bytes");
        }
        free(bytes);
    }
    if (mutex != NULL) {
        (void)ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    free(before);
    free(history);
}

/** Convert UTF-16 text to one owned UTF-8 string. */
static char *driver_from_wide(const WCHAR *wide)
{
    int count;
    char *bytes;

    count = WideCharToMultiByte(
        CP_UTF8, 0U, wide, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        return NULL;
    }
    bytes = (char *)malloc((size_t)count);
    if (bytes == NULL || WideCharToMultiByte(
            CP_UTF8,
            0U,
            wide,
            -1,
            bytes,
            count,
            NULL,
            NULL) != count) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

/** Format one quoted runtime-probe command for the shell language. */
static int driver_probe_command(
    driver_context *context,
    char *buffer,
    size_t capacity,
    const char *suffix)
{
    char *probe;
    int length;

    probe = driver_from_wide(context->probe);
    if (probe == NULL || strchr(probe, '\'') != NULL) {
        free(probe);
        return 0;
    }
    length = snprintf(buffer, capacity, "'%s' %s", probe, suffix);
    free(probe);
    return length > 0 && (size_t)length < capacity;
}

/** Exercise pending/foreground cancellation, signal order, jobs, and EOF. */
static void driver_control_scenario(driver_context *context)
{
    char command[4096];
    size_t sigint_count;

    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "control-event session did not launch");
        return;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    (void)driver_command(
        context,
        "fn sigint { echo M7_SIGINT; return 130 }");
    (void)driver_command(
        context,
        "fn sigexit { echo M7_SIGEXIT }");

    if (!driver_probe_command(
            context, command, sizeof(command), "sleep 10000")) {
        driver_fail(context, "could not format foreground probe command");
    } else {
        sigint_count = driver_screen_count(context, L"M7_SIGINT") + 1U;
        (void)driver_command(context, command);
        Sleep(250U);
        if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0U)) {
            driver_fail(context, "Ctrl+Break delivery failed");
        }
        if (!driver_wait_count(
                context, L"M7_SIGINT", sigint_count, 5000U)) {
            driver_fail(context, "Ctrl+Break did not invoke sigint");
        }
        (void)driver_command(context, "echo M7_AFTER_BREAK $status");
    }

    if (driver_probe_command(
            context, command, sizeof(command), "sleep 10000")) {
        sigint_count = driver_screen_count(context, L"M7_SIGINT") + 1U;
        (void)driver_command(context, command);
        Sleep(250U);
        if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0U)) {
            driver_fail(context, "Ctrl+C delivery failed");
        }
        if (!driver_wait_count(
                context, L"M7_SIGINT", sigint_count, 5000U)) {
            driver_fail(context, "Ctrl+C did not invoke sigint");
        }
        (void)driver_command(context, "echo M7_AFTER_CTRL_C $status");
    }

    (void)driver_text(context, "echo M7_MUST_NOT_EXECUTE");
    (void)driver_control(context, 'C', 0x03U);
    Sleep(100U);
    (void)driver_command(context, "echo M7_PENDING_RECOVERED $status");

    if (driver_probe_command(
            context, command, sizeof(command), "sleep 10000 &")) {
        (void)driver_command(context, command);
        Sleep(200U);
        (void)driver_control(context, 'Z', 0x1aU);
        (void)driver_enter(context, 0);
        (void)driver_wait_marker(
            context, L"repeat EOF to cancel and exit", 3000U);
        (void)driver_control(context, 'Z', 0x1aU);
        (void)driver_enter(context, 0);
    }
    (void)driver_finish(context, 0U);
    driver_expect(context, L"M7_SIGINT", "sigint marker was absent");
    driver_expect(
        context, L"M7_AFTER_BREAK", "prompt did not recover after break");
    driver_expect(
        context, L"M7_AFTER_CTRL_C", "prompt did not recover after Ctrl+C");
    driver_expect(
        context,
        L"M7_PENDING_RECOVERED",
        "pending-input cancellation did not recover");
    if (driver_screen_count(context, L"M7_MUST_NOT_EXECUTE") != 1U) {
        driver_fail(context, "cancelled pending source executed");
    }
    driver_expect(
        context,
        L"background jobs:",
        "EOF did not list retained background identifiers");
    driver_expect(
        context,
        L"repeat EOF to cancel and exit",
        "first EOF was not refused with live work");
    driver_expect(context, L"M7_SIGEXIT", "sigexit was not invoked");

    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "force-exit session did not launch");
        return;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    if (driver_probe_command(
            context, command, sizeof(command), "sleep 10000 &")) {
        (void)driver_command(context, command);
        Sleep(200U);
        (void)driver_command(context, "exit --force");
    }
    (void)driver_finish(context, 0U);
}

/** Exercise recoverable errors without changing valid batch meaning. */
static void driver_recovery_scenario(driver_context *context)
{
    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "recovery session did not launch");
        return;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    (void)driver_command(context, "echo )");
    (void)driver_command(context, "echo $M7_UNDEFINED_VARIABLE");
    (void)driver_command(context, "M7_COMMAND_DOES_NOT_EXIST");
    (void)driver_command(context, "echo data > 'Z:\\m7-missing\\file'");
    (void)driver_command(context, "echo M7_RECOVERY_OK");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);
    driver_expect(
        context, L"M7_RECOVERY_OK", "valid command after errors failed");
    driver_expect(context, L"wsh:", "interactive diagnostics were absent");
}

/** Exercise oversized history and deterministic completion truncation. */
static void driver_limits_scenario(driver_context *context)
{
    WCHAR *history;
    HANDLE file;
    WCHAR name[64];
    WCHAR *path;
    unsigned index;

    history = driver_history_path(context);
    file = history == NULL ? INVALID_HANDLE_VALUE : CreateFileW(
        history,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE || SetFilePointer(
            file, (4L * 1024L * 1024L) + 1L, NULL, FILE_BEGIN) ==
            INVALID_SET_FILE_POINTER || !SetEndOfFile(file)) {
        driver_fail(context, "could not create oversized history fixture");
    }
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    for (index = 0U; index < 2055U; ++index) {
        name[0] = L'l';
        name[1] = L'i';
        name[2] = L'm';
        name[3] = L'i';
        name[4] = L't';
        name[5] = L'-';
        name[6] = L'f';
        name[7] = L'i';
        name[8] = L'l';
        name[9] = L'e';
        name[10] = L'-';
        name[11] = (WCHAR)(L'0' + (index / 1000U) % 10U);
        name[12] = (WCHAR)(L'0' + (index / 100U) % 10U);
        name[13] = (WCHAR)(L'0' + (index / 10U) % 10U);
        name[14] = (WCHAR)(L'0' + index % 10U);
        name[15] = L'.';
        name[16] = L't';
        name[17] = L'x';
        name[18] = L't';
        name[19] = 0U;
        path = driver_join(context->root, name);
        if (path == NULL || !driver_write_file(path, "", 0U)) {
            driver_fail(context, "completion limit fixture creation failed");
            free(path);
            break;
        }
        free(path);
    }
    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "limit session did not launch");
        free(history);
        return;
    }
    (void)driver_wait_marker(context, L"% ", 4000U);
    (void)driver_text(context, "echo limit-file-");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_key(context, VK_ESCAPE, 0x1bU, 0U);
    (void)driver_command(context, "echo M7_LIMIT_RECOVERED");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);
    driver_expect(
        context,
        L"history could not be loaded; continuing empty",
        "oversized history was not rejected visibly");
    driver_expect(
        context,
        L"M7_LIMIT_RECOVERED",
        "candidate/history limit did not permit recovery");
    free(history);
}

/** Exercise inert hostile history, UNC refusal, and event storms. */
static void driver_security_scenario(driver_context *context)
{
    WCHAR *history;
    WCHAR *sentinel;
    char *sentinel_utf8;
    char corpus[8192];
    int length;
    unsigned index;
    DWORD start;

    history = driver_history_path(context);
    sentinel = driver_join(context->root, L"history-executed.sentinel");
    sentinel_utf8 = sentinel == NULL ? NULL : driver_from_wide(sentinel);
    length = sentinel_utf8 == NULL ? -1 : snprintf(
        corpus,
        sizeof(corpus),
        "{\"format\":\"wsh-history\",\"version\":1}\r\n"
        "{\"time\":\"2026-08-24T12:00:00Z\","
        "\"command\":\"echo attack > '%s'\","
        "\"ignored\":[{\"deep\":false}]}\r\n",
        sentinel_utf8);
    free(sentinel_utf8);
    if (history == NULL || sentinel == NULL || length <= 0 ||
        (size_t)length >= sizeof(corpus) ||
        !driver_write_file(history, corpus, (size_t)length)) {
        driver_fail(context, "hostile history fixture creation failed");
        free(history);
        free(sentinel);
        return;
    }
    (void)DeleteFileW(sentinel);
    if (!driver_launch(context, L"--no-profile", 0)) {
        driver_fail(context, "security session did not launch");
        free(history);
        free(sentinel);
        return;
    }
    (void)driver_wait_marker(context, L"% ", 3000U);
    (void)driver_command(
        context, "path=('\\\\invalid\\m7-share')");
    start = GetTickCount();
    (void)driver_text(context, "m7security-no-match");
    (void)driver_key(context, VK_TAB, L'\t', 0U);
    (void)driver_key(context, VK_ESCAPE, 0x1bU, 0U);
    for (index = 0U; index < 24U; ++index) {
        (void)driver_resize(context);
        (void)driver_control(context, 'C', 0x03U);
        Sleep(40U);
    }
    (void)driver_command(context, "echo M7_SECURITY_RECOVERED");
    (void)driver_command(context, "exit");
    (void)driver_finish(context, 0U);
    if (GetTickCount() - start > 5000U) {
        driver_fail(context, "unselected UNC completion was not bounded");
    }
    if (GetFileAttributesW(sentinel) != INVALID_FILE_ATTRIBUTES) {
        driver_fail(context, "history data executed as source");
    }
    driver_expect(
        context,
        L"M7_SECURITY_RECOVERED",
        "hostile event sequence did not recover");
    free(history);
    free(sentinel);
}

/** Attach an isolated genuine console and prepare the history root. */
static int driver_setup(
    driver_context *context,
    const char *wsh,
    const char *probe,
    const char *root)
{
    SECURITY_ATTRIBUTES security;
    CONSOLE_SCREEN_BUFFER_INFO information;
    COORD size;
    WCHAR *waughtal;
    WCHAR *shell;

    memset(context, 0, sizeof(*context));
    context->report = GetStdHandle(STD_OUTPUT_HANDLE);
    context->wsh = driver_to_wide(wsh);
    context->probe = driver_to_wide(probe);
    context->root = driver_to_wide(root);
    if (context->wsh == NULL || context->probe == NULL ||
        context->root == NULL) {
        return 0;
    }
    context->appdata = driver_join(context->root, L"AppData");
    waughtal = context->appdata == NULL ? NULL :
        driver_join(context->appdata, L"Waughtal");
    shell = waughtal == NULL ? NULL : driver_join(waughtal, L"WSH");
    if (context->appdata == NULL || waughtal == NULL || shell == NULL ||
        !driver_directory(context->root) ||
        !driver_directory(context->appdata) ||
        !driver_directory(waughtal) ||
        !driver_directory(shell)) {
        free(waughtal);
        free(shell);
        return 0;
    }
    free(waughtal);
    free(shell);

    (void)FreeConsole();
    if (!AllocConsole()) {
        return 0;
    }
    memset(&security, 0, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    context->input = CreateFileW(
        L"CONIN$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        0U,
        NULL);
    context->output = CreateFileW(
        L"CONOUT$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        0U,
        NULL);
    if (context->input == INVALID_HANDLE_VALUE ||
        context->output == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (GetConsoleScreenBufferInfo(context->output, &information)) {
        size = information.dwSize;
        if (size.Y < DRIVER_SCREEN_LINES) {
            size.Y = DRIVER_SCREEN_LINES;
            (void)SetConsoleScreenBufferSize(context->output, size);
        }
    }
    (void)SetConsoleCtrlHandler(driver_ignore_control, TRUE);
    (void)SetEnvironmentVariableW(L"_WSH_ENV_V1", NULL);
    (void)SetEnvironmentVariableW(L"_WSH_PARENT_NONCE", NULL);
    return 1;
}

/** Release the isolated native console without touching test fixtures. */
static void driver_cleanup(driver_context *context)
{
    (void)SetConsoleCtrlHandler(driver_ignore_control, FALSE);
    if (context->input != NULL && context->input != INVALID_HANDLE_VALUE) {
        CloseHandle(context->input);
    }
    if (context->output != NULL && context->output != INVALID_HANDLE_VALUE) {
        CloseHandle(context->output);
    }
    (void)FreeConsole();
    free(context->wsh);
    free(context->probe);
    free(context->root);
    free(context->appdata);
}

/** Dispatch one controlled M7 native-console scenario. */
int main(int argc, char **argv)
{
    driver_context context;
    char summary[128];

    memset(&context, 0, sizeof(context));
    context.report = GetStdHandle(STD_OUTPUT_HANDLE);
    if (argc != 5) {
        driver_report(
            &context,
            "usage: console-driver WSH PROBE SCENARIO TEST-ROOT");
        return 2;
    }
    if (!driver_setup(&context, argv[1], argv[2], argv[4])) {
        driver_report(&context, "FAIL: native console setup failed");
        driver_cleanup(&context);
        return 3;
    }
    if (strcmp(argv[3], "editor") == 0) {
        driver_editor_scenario(&context);
    } else if (strcmp(argv[3], "completion") == 0) {
        driver_completion_scenario(&context);
    } else if (strcmp(argv[3], "history") == 0) {
        driver_history_scenario(&context);
    } else if (strcmp(argv[3], "control") == 0) {
        driver_control_scenario(&context);
    } else if (strcmp(argv[3], "recovery") == 0) {
        driver_recovery_scenario(&context);
    } else if (strcmp(argv[3], "limits") == 0) {
        driver_limits_scenario(&context);
    } else if (strcmp(argv[3], "security") == 0) {
        driver_security_scenario(&context);
    } else {
        driver_fail(&context, "unknown native console scenario");
    }
    if (context.failures != 0U) {
        driver_dump_screen(&context);
    }
    (void)snprintf(
        summary,
        sizeof(summary),
        "%s: scenario=%s failures=%lu native-standard-input=yes",
        context.failures == 0U ? "PASS" : "FAIL",
        argv[3],
        (unsigned long)context.failures);
    driver_report(&context, summary);
    driver_cleanup(&context);
    return context.failures == 0U ? 0 : 1;
}
