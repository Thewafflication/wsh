# WSH 1.0 and Plan 9 `rc` Compatibility

**Document ID:** `WSH-SPEC-RC-0001`

**Status:** Accepted

## 1. Method

This record annotates the [Plan 9 from User Space `rc(1)` manual][rc]. It is a
design trace, not a claim that arbitrary `rc` scripts run unchanged. Each row
shall trace to requirements and conformance tests before 1.0 acceptance.

| `rc` area | WSH disposition | WSH 1.0 decision |
| --- | --- | --- |
| Command lists, `;`, newline | Adopted | Execute complete commands left to right |
| Background `&` and `$apid` | Adapted | Windows process ID and tracked child registry |
| `#` comments | Adopted | Comment through the next source line ending |
| Backslash-newline continuation | Excluded | Backslash always remains a Windows path character; incomplete syntax continues input |
| Simple-command resolution | Adapted | Functions, built-ins, explicit path, current directory, then `$path`; native extensions only |
| Shebang scripts | Excluded | Windows kernel has no shebang contract; `.wsh` invocation is explicit |
| Apostrophe quoting and doubled apostrophe | Adopted | Unicode strings, including newlines |
| Flat list values | Adopted | Every value is an ordered flat string list |
| Variable expansion and one-origin subscripts | Adopted | Numeric range errors are explicit |
| `$#name` | Adopted | Count list elements, not characters |
| `$\"name` flattening | Adopted | Join with one ASCII space |
| Parenthesized list construction | Adopted | Flat concatenation |
| Explicit and free caret | Adopted | Pairwise/singleton distribution; incompatible lengths fail |
| Backquote command substitution | Adapted | UTF-8 capture and `$ifs`; failed substitution prevents containing launch |
| `<{}`, `>{}`, `<>{}` | Adapted | Access-controlled Windows named pipes replace `/dev/fd` |
| File globbing | Adapted | Both Windows separators, deterministic Windows comparison, unmatched pattern remains literal |
| `<`, `>`, `>>`, `<<` | Adopted/adapted | Ordered Win32 handle/file operations; here text is UTF-8 CRLF |
| Decorated descriptors | Adapted | 0--2 universal; 3--9 guaranteed only for WSH/WCRT peers |
| Pipelines | Adopted/adapted | Byte streams; ordered numeric status list; all stages must succeed |
| `&&`, `||`, `!` | Adopted | Operate on the all-zero status truth rule |
| `@` subshell | Adapted | Semantic context clone; no `fork` requirement |
| `if`, `if not`, `for`, `while`, `switch` | Adopted | Same structured model with Windows statuses |
| Braced command blocks | Adopted | Group commands and define function bodies |
| `fn` functions | Adopted | Saved local `$*`; Unicode names follow WSH identifier grammar |
| Note functions | Adapted | Only `sigint` and `sigexit` Windows analogues |
| Command-local assignments | Adopted | Restored after the command |
| `. file` | Adopted | `source` synonym; caller context and saved `$*` |
| `builtin` | Adopted | Bypass a same-named function |
| `cd` and `$cdpath` | Adapted | Windows drive, UNC, and logical-context semantics |
| `eval` | Adopted | Explicitly documented injection boundary |
| `exec` | Adapted | Run child, wait, and end context; Windows cannot replace process identity |
| `exit` | Adapted | Unsigned Windows code derived from ordered status |
| `shift` | Adopted | Remove one or a requested count from `$*` |
| `wait` | Adapted | Known Windows child processes only |
| `whatis` | Adopted/extended | Variables, functions, built-ins, library commands, and resolved executables |
| `~` | Adopted | Case-sensitive non-filesystem pattern matching |
| `flag` | Excluded | Stable CLI options and configuration replace mutable single-letter flags |
| `rfork` and namespaces | Excluded | No Windows analogue in the language |
| Automatic export of all variables/functions | Adapted | Explicit export; functions and private variables do not cross processes |
| SOH list encoding | Adapted | Versioned private WSH envelope; ordinary children receive scalar environment |
| `$*` | Adopted | Script, source, and function argument list |
| `$home`, `$ifs`, `$path`, `$prompt`, `$pid` | Adopted/adapted | Windows values and adapters |
| `$status` wait-message string | Adapted | One unsigned numeric element per command or pipeline stage |
| Interactive detection and prompts | Adopted/adapted | Windows console-handle detection and wide console APIs |
| `-c`, `-e`, `-i`, `-I`, `-l`, `-s`, `-v`, `-x` | Adopted/adapted | Long-name equivalents and fixed Windows behavior |
| `-d`, `-p`, `-r` no-op/debug flags | Excluded | No compatibility no-ops; unsupported options fail visibly |
| Plan 9 `/env`, notes, `rfork`, `/dev/fd` | Excluded/adapted | Explicit Windows contracts described above |

## 2. WSH Extensions

The following are not attributed to `rc`:

- Windows-native paths, executable discovery, environment, and raw command
  line launch;
- strict Unicode and cross-version encoding rules;
- `export`, `unexport`, `local`, `unset`, `return`, `break`, and `continue`;
- embedded filesystem, path, process, text, system, time, and test libraries;
- inert INI configuration and optional registry policy;
- safe-path mode and raw-launch policy;
- stable C embedding ABI and synchronous host commands; and
- deterministic test evidence integration.

## 3. Review Questions

Every language change review shall answer:

1. What does the referenced `rc` documentation specify?
2. Is that behavior directly compatible with Windows 2000 through current
   Windows?
3. If not, what observable purpose is preserved by the Windows adaptation?
4. Does the change alter an accepted 1.0 script meaning or configuration?
5. Which requirements, ADRs, tests, examples, and compatibility rows change?

## 4. M3 Grammar Verification Record

M3 did not change an accepted compatibility disposition. Its lexer/parser
implements and verifies the syntax-bearing rows for command lists, comments,
apostrophe quotation, variables and subscripts, lists, carets, substitution,
redirection, pipelines, logical/unary operators, blocks, functions, and
structured control flow. `TC-0008` through `TC-0085` provide the applicable
controlled conformance, malformed-input, ownership, limits, source-
equivalence, and fuzz evidence. Runtime meaning remains assigned to M4 and M5;
an accepted M3 parse is not an execution-compatibility claim.

## 5. M4 Semantic Verification Record

M4 did not change an accepted compatibility disposition. The evaluator
implements the adopted list, quotation-result, caret, variable, subscript,
count, flatten, status, conditional, function, block, loop, switch, source,
eval, pattern, and substitution meanings. Windows adaptations remain explicit:
unsigned status elements, case-sensitive private names, collision-checked
export identity, scalar ordinary-program exports, semantic context cloning,
and abstract runtime requests instead of Unix process effects.

`TC-0007`, `TC-0009`, `TC-0014`, `TC-0017`, `TC-0037`, `TC-0043`,
`TC-0046`, `TC-0048`, `TC-0049`, and `TC-0052` provide controlled semantic
evidence. M5 retains process resolution, environment serialization, real
filesystem enumeration, redirection, pipelines, background jobs, and process
substitution; M4 makes no execution-compatibility claim for those rows.

## 6. M5 Windows Execution Verification Record

M5 changes no accepted compatibility disposition. Structured commands resolve
and launch directly on Windows; logical descriptors, anonymous and named byte
pipes, ordered unsigned status lists, logical working directories, background
identifiers, and nested WSH metadata implement the already accepted Windows
adaptations. No implicit command interpreter, file association, App Paths, or
`PATHEXT` route is used.

`TC-0011`, `TC-0012`, `TC-0013`, `TC-0015`, `TC-0016`, `TC-0018`, `TC-0024`,
`TC-0039`, `TC-0042`, `TC-0043`, `TC-0045`, `TC-0047`, `TC-0050`, `TC-0051`,
`TC-0052`, `TC-0070`, `TC-0074`, and `TC-0075` provide native x86/x64 Debug
and Release evidence for resolution, arguments, environment, descriptors,
pipelines, jobs, capture, and process substitution. ARM64 is cross-built only;
M5 makes no native ARM64 execution claim from the current host.

## 7. M6 Standard-Library Verification Record

M6 changes no accepted `rc` compatibility disposition. Its namespaced
filesystem, path, text, process, time, system, test, and library commands are
WSH extensions rather than aliases for `rc` commands. They preserve the
accepted list/status model while making Windows path, encoding, process,
resource, and destructive-operation policy explicit.

`TC-0024`, `TC-0037`, `TC-0065`, `TC-0066`, `TC-0067`, `TC-0068`, `TC-0070`,
`TC-0074`, and `TC-0075` provide native x86/x64 Debug and Release evidence for
the embedded registry, Unicode, protected filesystem mutation, process
wrappers, WSP test state, isolation, limits, and deterministic traversal.
ARM64 Debug and Release compile from the same sources; native ARM64 execution
remains a CI and later compatibility gate rather than a local claim.

[rc]: https://9fans.github.io/plan9port/man/man1/rc.html
