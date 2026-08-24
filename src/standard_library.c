/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file standard_library.c
 * @brief Immutable embedded standard-library command registry.
 */

#include "standard_library.h"

#include <string.h>

/** Compact descriptor initializer. */
#define LIBRARY_COMMAND(command, usage, description, command_flags) \
    {command, usage, description, command_flags}

/** Accepted embedded commands in canonical deterministic order. */
static const wsh_library_descriptor wsh_library_commands[] = {
    LIBRARY_COMMAND("fs::compare", "fs::compare [--text encoding] left right",
        "compare two files", 0U),
    LIBRARY_COMMAND("fs::copy",
        "fs::copy [--overwrite] [--recursive] source destination",
        "copy a file or opted-in tree", 0U),
    LIBRARY_COMMAND("fs::exists", "fs::exists path",
        "test whether a path exists", 0U),
    LIBRARY_COMMAND("fs::hash", "fs::hash --sha256 [--into name] path",
        "hash one regular file", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("fs::list",
        "fs::list [--recursive] [--pattern p] [--into name] path",
        "enumerate a directory deterministically", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("fs::mkdir", "fs::mkdir [--parents] path...",
        "create directories", 0U),
    LIBRARY_COMMAND("fs::move", "fs::move [--overwrite] source destination",
        "move or rename one path", 0U),
    LIBRARY_COMMAND("fs::read",
        "fs::read [--encoding name|--bytes] [--into name] path",
        "read bounded text or bytes", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("fs::remove",
        "fs::remove [--force] [--recursive] "
        "[--allow-protected-root] path...",
        "remove explicitly named paths", 0U),
    LIBRARY_COMMAND("fs::stat", "fs::stat --into name path",
        "return stable path metadata", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("fs::temp-dir", "fs::temp-dir [--into name] [prefix]",
        "create a private temporary directory", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("fs::temp-file", "fs::temp-file [--into name] [prefix]",
        "create a private temporary file", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("fs::type", "fs::type --into name path",
        "classify one path", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("fs::write",
        "fs::write [--append] [--encoding name] path values...",
        "write explicit text or bytes", 0U),
    LIBRARY_COMMAND("library::describe", "library::describe command",
        "describe one embedded command", 0U),
    LIBRARY_COMMAND("library::list", "library::list --into name",
        "return canonical embedded command names", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::absolute", "path::absolute --into name path",
        "resolve a logical absolute path", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::change-extension",
        "path::change-extension --into name path extension",
        "replace the final extension", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::directory", "path::directory --into name path",
        "return the directory portion", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::extension", "path::extension --into name path",
        "return the final extension", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::is-root", "path::is-root path",
        "test for a Windows root", 0U),
    LIBRARY_COMMAND("path::is-within", "path::is-within base candidate",
        "test resolved path containment", 0U),
    LIBRARY_COMMAND("path::join", "path::join --into name component...",
        "join path components lexically", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::name", "path::name --into name path",
        "return the final component", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::normalize", "path::normalize --into name path",
        "normalize a path lexically", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("path::relative",
        "path::relative --into name base target",
        "return a lexical relative path", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("process::cancel", "process::cancel pid...",
        "cancel registered process groups", 0U),
    LIBRARY_COMMAND("process::capture",
        "process::capture --stdout name [options] -- command args...",
        "capture structured child output", 0U),
    LIBRARY_COMMAND("process::parallel",
        "process::parallel --jobs n [--fail-fast] -- block...",
        "run bounded independent blocks", 0U),
    LIBRARY_COMMAND("process::raw",
        "process::raw [options] -- executable command-line",
        "run a policy-controlled raw command line", 0U),
    LIBRARY_COMMAND("process::run", "process::run [options] -- command args...",
        "run a structured child", 0U),
    LIBRARY_COMMAND("process::wait", "process::wait [pid...]",
        "wait for registered process groups", 0U),
    LIBRARY_COMMAND("process::which", "process::which [--into name] command...",
        "resolve commands without executing", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("system::architecture",
        "system::architecture --into name",
        "return process and native architecture", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("system::environment", "system::environment --into name",
        "return sorted environment names", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("system::windows-version",
        "system::windows-version --into name",
        "return observed Windows version fields", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("system::wsh-version", "system::wsh-version --into name",
        "return product and ABI versions", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("history::suppress", "history::suppress",
        "suppress the current history entry", 0U),
    LIBRARY_COMMAND("history::list",
        "history::list [--into name] [count]",
        "return newest inert history entries", WSH_LIBRARY_ACCEPTS_INTO),
    LIBRARY_COMMAND("history::clear", "history::clear",
        "clear and persist interactive history", 0U),
    LIBRARY_COMMAND("test::assert", "test::assert [message]",
        "assert successful current status", 0U),
    LIBRARY_COMMAND("test::assert-equal",
        "test::assert-equal expected actual [message]",
        "assert exact string equality", 0U),
    LIBRARY_COMMAND("test::assert-file",
        "test::assert-file expected actual [--text encoding]",
        "assert file equality", 0U),
    LIBRARY_COMMAND("test::assert-list",
        "test::assert-list expected-name actual-name [message]",
        "assert exact list equality", 0U),
    LIBRARY_COMMAND("test::assert-status", "test::assert-status expected...",
        "assert the current ordered status", 0U),
    LIBRARY_COMMAND("test::begin", "test::begin id title",
        "begin one controlled test case", 0U),
    LIBRARY_COMMAND("test::blocked", "test::blocked reason",
        "record a blocked controlled case", 0U),
    LIBRARY_COMMAND("test::end", "test::end",
        "finalize the active test case", 0U),
    LIBRARY_COMMAND("test::fail", "test::fail message",
        "record an unconditional assertion failure", 0U),
    LIBRARY_COMMAND("test::skip", "test::skip reason",
        "record an allowed nonexecution verdict", 0U),
    LIBRARY_COMMAND("text::compare",
        "text::compare [--ordinal-ignore-case] left right",
        "compare exact text", 0U),
    LIBRARY_COMMAND("text::encode",
        "text::encode --from name --to name --output path value",
        "write explicitly encoded text", 0U),
    LIBRARY_COMMAND("text::format",
        "text::format --into name format values...",
        "apply positional text formatting", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("text::join",
        "text::join --separator s --into name values...",
        "join exact text values", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("text::replace",
        "text::replace --old a --new b --into name value",
        "replace nonoverlapping text", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("text::split",
        "text::split --separator s [--keep-empty] --into name value",
        "split on an exact string", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("time::monotonic", "time::monotonic --into name",
        "return monotonic nanoseconds", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("time::now", "time::now --utc --into name",
        "return an RFC 3339 UTC instant", WSH_LIBRARY_REQUIRES_INTO),
    LIBRARY_COMMAND("time::sleep", "time::sleep milliseconds",
        "sleep for a bounded interval", 0U)
};

/** @brief Implements wsh_library_descriptor_count. */
size_t wsh_library_descriptor_count(void)
{
    return sizeof(wsh_library_commands) / sizeof(wsh_library_commands[0]);
}

/** @brief Implements wsh_library_descriptor_at. */
const wsh_library_descriptor *wsh_library_descriptor_at(size_t index)
{
    return index < wsh_library_descriptor_count() ?
        &wsh_library_commands[index] : NULL;
}

/** @brief Implements wsh_library_find. */
const wsh_library_descriptor *wsh_library_find(wsh_string_view name)
{
    size_t index;
    const wsh_library_descriptor *descriptor;
    size_t length;

    for (index = 0U; index < wsh_library_descriptor_count(); ++index) {
        descriptor = &wsh_library_commands[index];
        length = strlen(descriptor->name);
        if (name.length == length &&
            memcmp(name.data, descriptor->name, length) == 0) {
            return descriptor;
        }
    }
    return NULL;
}
