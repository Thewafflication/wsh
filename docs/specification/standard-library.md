# Waughtal Shell 1.0 Standard Library

**Document ID:** `WSH-SPEC-STDLIB-0001`

**Status:** Accepted

## 1. Purpose and Packaging

The standard library supplies the cross-version operations required for build
and test scripts without depending on `cmd.exe`, PowerShell, .NET, POSIX tools,
or separately installed utilities.

The official portable `wsh.exe` statically links the native primitives and
embeds any WSH-language library modules. No external library file is required.
The alternative shared build exposes identical behavior through `wshlib.dll`.

Commands use namespaced canonical names. A profile may define short wrapper
functions, but the product shall not reserve common names such as `copy` or
`remove` for aliases.

## 2. Common Conventions

Options precede operands and `--` ends option parsing. Paths are resolved
against the shell context's logical working directory. Query commands accept
`--into name` to assign an exact list without text serialization. Without
`--into`, they emit one UTF-8 CRLF-terminated record per result.

Mutation commands are non-interactive and return `(0)` on complete success.
Partial success returns failure and identifies every affected item. Operations
shall not silently overwrite, merge, recurse, follow reparse points, or cross
volumes unless their documented option permits it.

Library diagnostics use stable `WSH-LIB-*` codes, identify the operation and
path, and do not expose unrelated environment values.

## 3. Filesystem Commands

| Command | Contract |
| --- | --- |
| `fs::exists path` | Succeed when the path exists without following its final reparse point |
| `fs::type --into name path` | Return `missing`, `file`, `directory`, `reparse`, or `other` |
| `fs::stat --into name path` | Return a documented key/value record containing type, size, attributes, and UTC timestamps |
| `fs::list [--recursive] [--pattern p] [--into name] path` | Enumerate deterministically without following directory reparse points by default |
| `fs::mkdir [--parents] path...` | Create directories; existing directories succeed only with `--parents` |
| `fs::copy [--overwrite] [--recursive] source destination` | Copy a file or opted-in directory tree |
| `fs::move [--overwrite] source destination` | Move or rename; cross-volume movement uses verified copy then removal |
| `fs::remove [--force] [--recursive] path...` | Remove named items under the destructive-operation rules below |
| `fs::read [--encoding name] [--into name] path` | Read bounded decoded text; `--bytes` writes raw bytes to stdout and conflicts with `--into` |
| `fs::write [--append] [--encoding name] path values...` | Write explicit text; `--bytes-from path` copies raw input bytes instead |
| `fs::compare [--text encoding] left right` | Compare exact bytes by default or decoded normalized text explicitly |
| `fs::hash --sha256 [--into name] path` | Return lowercase SHA-256 for a regular file |
| `fs::temp-file [--into name] [prefix]` | Atomically create a private empty temporary file |
| `fs::temp-dir [--into name] [prefix]` | Atomically create a private temporary directory |

`fs::remove --recursive` shall resolve the target to an absolute path and
refuse a volume root, UNC share root, the context's initial working directory,
the executable directory, or an empty/current-directory spelling unless
`--allow-protected-root` is separately present. Reparse points themselves are
removed and are never traversed by recursive removal.

`fs::list` sorts using the deterministic path ordering defined by the language
specification. Recursive enumeration detects cycles and has configurable host
resource limits but no configuration-dependent result ordering.

## 4. Path Commands

| Command | Contract |
| --- | --- |
| `path::join --into name component...` | Join components with one native separator without resolving the result |
| `path::normalize --into name path` | Collapse redundant separators and `.` lexically; preserve roots and device prefixes |
| `path::absolute --into name path` | Resolve against the context working directory without requiring existence |
| `path::relative --into name base target` | Produce a lexical relative path when roots are compatible |
| `path::directory --into name path` | Return the directory portion |
| `path::name --into name path` | Return the final component |
| `path::extension --into name path` | Return the final extension including its period, or empty string |
| `path::change-extension --into name path extension` | Replace the final extension lexically |
| `path::is-root path` | Succeed only for a drive, UNC share, or device root |
| `path::is-within base candidate` | Succeed when the resolved candidate remains beneath base under Windows comparison |

Normalization never changes `/` in an argument passed to an external program.
It is performed only when explicitly requested or required by a filesystem
library operation.

## 5. Text and Encoding Commands

| Command | Contract |
| --- | --- |
| `text::join --separator s --into name values...` | Join without implicit escaping |
| `text::split --separator s --keep-empty --into name value` | Split on an exact string; `--keep-empty` is optional |
| `text::replace --old a --new b --into name value` | Replace nonoverlapping exact occurrences |
| `text::compare [--ordinal-ignore-case] left right` | Return success on equality |
| `text::encode --from name --to name --output path value` | Write encoded bytes under explicit policy; non-UTF-8 bytes are not stored in a WSH string |
| `text::format --into name format values...` | Apply the documented positional formatter without evaluating code |

Unicode operations are defined in scalar values. `ordinal-ignore-case` uses the
project's versioned Windows-compatible folding table, not the current user's
locale. No command performs normalization unless it explicitly names NFC or
NFD in a future compatible extension.

## 6. Process Commands

| Command | Contract |
| --- | --- |
| `process::which [--into name] command...` | Resolve using WSH command-search rules without executing |
| `process::run [options] -- command args...` | Run structured arguments with working-directory, environment, timeout, and capture controls |
| `process::raw [options] -- executable command-line` | Invoke the raw command-line facility subject to policy |
| `process::capture --stdout name [--stderr name] [options] -- command args...` | Capture exact decoded output into variables |
| `process::parallel --jobs n [--fail-fast] -- block...` | Run an explicit list of blocks with bounded concurrency and ordered results |
| `process::wait [pid...]` | Wait using the language child registry |
| `process::cancel pid...` | Request graceful cancellation, then apply the documented timeout and tree termination |

Common options are `--cwd path`, repeated `--set name=value`, repeated
`--unset name`, `--timeout milliseconds`, `--stdin path`, `--stdout path`,
`--stderr path`, and `--merge-stderr`. Timeout zero means no library-imposed
deadline. Capture requires an explicit encoding for non-WSH output; its default
is UTF-8 and invalid input fails.

Parallel results are returned in input order, not completion order. Each block
has an independent cloned shell context. The overall status is the ordered
concatenation of block statuses and succeeds only when all blocks succeed.

## 7. Time and System Commands

| Command | Contract |
| --- | --- |
| `time::now --utc --into name` | Return RFC 3339 UTC with seven fractional decimal places |
| `time::monotonic --into name` | Return monotonic elapsed nanoseconds as an unsigned decimal value |
| `time::sleep milliseconds` | Sleep interruptibly for at least the requested duration |
| `system::architecture --into name` | Return process and native architecture as two elements |
| `system::windows-version --into name` | Return major, minor, build, service-pack, and product-type fields |
| `system::wsh-version --into name` | Return product, standard-library, WCRT, and ABI versions |
| `system::environment --into name` | Return environment names in deterministic order without values |

Version detection uses documented native mechanisms and capability probing; it
is not used to choose language semantics.

## 8. Test Commands

The test namespace supports test runners but does not replace controlled
`TC-NNNN` specifications or CTest dispatch.

| Command | Contract |
| --- | --- |
| `test::begin id title` | Begin one controlled case and capture baseline metadata |
| `test::assert [message]` | Record failure unless the current status succeeds |
| `test::assert-equal expected actual [message]` | Compare exact strings |
| `test::assert-list expected-name actual-name [message]` | Compare element count and exact elements |
| `test::assert-status expected...` | Compare the current ordered status list |
| `test::assert-file expected actual [--text encoding]` | Compare file bytes or explicit decoded text |
| `test::fail message` | Record an unconditional failure |
| `test::blocked reason` | End with WSP `Blocked` status |
| `test::skip reason` | End with WSP `Not applicable` or `Not run` only when the test specification permits it |
| `test::end` | Finalize evidence and return pass/fail status |

Evidence records identify the test ID and revision, requirements, source
revision, architecture, OS, toolchain, dependencies, times, commands, exit
codes, verdict, and diagnostics required by WSP. A missing `test::end`, duplicate
ID, malformed metadata, or incomplete evidence is a test failure.

## 9. Library Introspection

`whatis namespace::command` prints a WSH-readable declaration and one-line
summary. `library::list --into name` returns canonical command names.
`library::describe command` prints version, signature, options, status codes,
and policy requirements. Library versioning follows the product semantic
version and is reported by `wsh --version`.

## 10. Interactive History Commands

| Command | Contract |
| --- | --- |
| `history::suppress` | Prevent the current submitted command from being persisted |
| `history::list [--into name] [count]` | Return the newest bounded history entries without evaluating them |
| `history::clear` | Clear in-memory history and atomically replace the persistent file after confirmation policy is satisfied |

These commands fail outside an interactive context except that `history::clear`
may be invoked by an embedding host that explicitly supplies a history store.
Policy denial takes precedence over every history command.
