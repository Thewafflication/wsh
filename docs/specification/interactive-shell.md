# Waughtal Shell 1.0 Interactive Shell

**Document ID:** `WSH-SPEC-INTERACTIVE-0001`

**Status:** Proposed

## 1. Mode and Startup

WSH enters interactive mode only when standard input is a usable console or
`--interactive` explicitly obtains one. A console on stdout is not sufficient
when stdin is redirected. `--non-interactive` always disables interactive
behavior.

Startup order is:

1. validate process arguments and mandatory policy;
2. construct the shell context and import the environment;
3. load selected inert configuration;
4. initialize console modes without discarding the caller's settings;
5. load the selected profiles;
6. load bounded history; and
7. display the primary prompt.

Failure before the prompt returns the applicable command-line exit code.
Console modes are restored on orderly exit and after a handled startup error.

## 2. Prompt and Input

`$prompt` is a two-element list. The first element is written before a new
command and the second before each continuation line. Missing elements use `% `
and `; `. Extra elements are ignored with a diagnostic when assigned
interactively and rejected by controlled configuration.

Prompt elements are literal Unicode strings. They are not reparsed, expanded,
or executed. A profile can update `$prompt` between commands. WSH writes the
prompt through wide console APIs and does not include it in redirected input
or history.

Enter submits a complete command. When the parser reports incomplete input,
Enter inserts a logical line ending and displays the continuation prompt.
Ctrl+Enter inserts a literal line ending without requesting execution. A
complete syntax error is displayed immediately and the entire pending input is
discarded without partial execution.

## 3. Editing Contract

The minimum editor works through Windows console input records and requires no
VT terminal support.

| Input | Operation |
| --- | --- |
| Left/Right | Move one Unicode scalar value |
| Ctrl+Left/Ctrl+Right | Move across one word boundary |
| Home/End | Move to start/end of logical line |
| Ctrl+Home/Ctrl+End | Move to start/end of the complete pending command |
| Backspace/Delete | Delete the preceding/following scalar value |
| Ctrl+Backspace/Ctrl+Delete | Delete through a word boundary |
| Up/Down | Navigate history when not in a multiline command |
| Ctrl+Up/Ctrl+Down | Move among physical lines of pending multiline input |
| Insert | Toggle insert and overwrite mode |
| Ctrl+A/Ctrl+E | Move to start/end of pending command |
| Ctrl+U | Delete from cursor to start of logical line |
| Ctrl+K | Delete from cursor to end of logical line |
| Ctrl+L | Redraw the current screen and pending input |
| Esc | Clear the pending command |
| Tab/Shift+Tab | Complete forward/backward |
| Ctrl+C | Cancel pending input or foreground execution |
| Ctrl+Z then Enter | Submit EOF when pending input is empty |

The editor shall not split a UTF-16 surrogate pair or combine invalid UTF-16
into internal text. Combining marks move as scalar values in 1.0; grapheme-
cluster editing may be added compatibly later.

Resize, scroll, selection, and redirected-handle events shall not corrupt the
pending buffer. If advanced editing cannot be initialized, WSH reports the
reason and offers a basic line-input fallback with the same parser semantics.

## 4. Completion

Completion is syntax-aware and never executes code. At command position it
offers functions, built-ins, standard-library commands, explicit `.wsh`
scripts, and resolvable `.exe`/`.com` commands. At argument position it offers
variables or filesystem names according to the token prefix.

Candidates are ordered using WSH's deterministic ordinal ordering. The first
Tab extends through the longest common prefix. A subsequent Tab displays or
cycles candidates; Shift+Tab reverses. Candidate display is columnar only when
console width permits it.

Inserted text is apostrophe-quoted when its literal spelling would be split or
parsed as syntax. Existing quoted input remains quoted and doubles an inserted
apostrophe. Completion preserves the user's path separator style when
possible and never changes `/option` into a path.

No completion provider may inspect network locations until the typed prefix
selects that location. Hosts may add synchronous bounded providers through a
future ABI extension; 1.0 has no script-executed completion hook.

## 5. History

History records complete submitted source, including multiline commands, but
not prompts or completion text. A command cancelled before submission is not
recorded. Adjacent duplicate suppression and limits follow configuration.

The history file is UTF-8 JSON Lines. The first record is:

```json
{"format":"wsh-history","version":1}
```

Each later record contains exactly an RFC 3339 UTC `time` string and a
`command` JSON string. CRLF terminates records; escaped JSON `\n` preserves a
source line ending. Unknown properties are ignored. Malformed or oversized
records are skipped with one bounded warning and are never evaluated as code.

History is loaded into memory up to configured entry and byte limits. Updates
are written to a same-directory temporary file, flushed, and atomically
replaced where the filesystem permits. A cross-process lock prevents two WSH
instances from silently overwriting one another; lock failure disables the
write with a warning.

`history::suppress` prevents the current pending command from being persisted.
WSH cannot reliably detect every secret and documentation shall warn against
placing credentials on command lines. Policy can disable history completely.

## 6. Interruption and Foreground Jobs

Ctrl+C with pending input clears it, writes `^C`, sets `$status=(130)`, and
displays a new prompt. During foreground execution it requests cancellation of
the entire tracked child tree, waits through the specified grace period, and
escalates when required. A cancelled simple command receives status 130. In a
pipeline, a stage already collected retains its code and each cancelled live
stage receives 130, preserving the left-to-right status-list contract.

If `fn sigint` exists, it runs after cleanup in the shell context. A successful
handler may update state but does not retroactively mark the interrupted
command successful unless it explicitly assigns `$status=(0)`.

Interactive exit with live background jobs lists their identifiers and asks
for confirmation only when input and output are consoles. `exit --force`
cancels them without confirmation. EOF refuses once with a warning; a second
consecutive EOF cancels and exits. Batch mode never prompts.

## 7. Error Recovery

An interactive lexical, syntax, expansion, redirection, resolution, or launch
error reports a diagnostic, sets nonzero status, discards only the affected
complete command, and returns to the primary prompt. It does not terminate the
session unless `--exit-on-error`, policy, resource corruption, or an explicit
`exit` requires termination.

Fatal console output failure stops interactive evaluation because subsequent
prompts and diagnostics cannot be observed. History-write failure is nonfatal.

## 8. Compatibility

Interactive facilities may improve on newer terminals, but the editing keys,
history data meaning, completion ordering, Ctrl+C outcome, and language
semantics remain consistent from Windows 2000 through current Windows. A
modern console host is not a requirement for the x86 portable binary.
