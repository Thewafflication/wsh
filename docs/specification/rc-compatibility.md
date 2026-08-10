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

[rc]: https://9fans.github.io/plan9port/man/man1/rc.html
