# Waughtal Shell 1.0 Language Specification

**Document ID:** `WSH-SPEC-LANG-0001`

**Status:** Accepted

**Target release:** 1.0

## 1. Scope

This document specifies the Waughtal Shell language used by interactive input,
script files, `wsh -c`, sourced files, and the embedding API. All entry modes
shall use the same lexer, parser, value model, and evaluator.

The language adopts the small, list-oriented model of Plan 9 `rc` and adapts
operating-system behavior to Windows. It does not parse `.bat`, `.cmd`,
PowerShell, POSIX shell, or file-association command syntax.

## 2. Source Text

### 2.1 Encoding

Canonical source text is UTF-8. A UTF-8 byte-order mark is accepted and
discarded only at the start of a source. A script beginning with a UTF-16LE or
UTF-16BE byte-order mark is decoded to Unicode and then processed identically
to UTF-8. BOM-less UTF-16 is not guessed.

Invalid encoding, an unpaired surrogate in UTF-16 input, U+0000, and a Unicode
noncharacter produce an input diagnostic before any command from the affected
source is executed. Legacy ANSI and OEM decoding is available only through an
explicit command-line or embedding option.

Internally visible character positions are counted in Unicode scalar values.
Diagnostics also retain the original byte offset. A tab advances to the next
multiple-of-eight display column for diagnostic rendering.

### 2.2 Lines

CRLF, LF, and lone CR each terminate a source line. They have identical grammar
meaning. A final line need not have a terminator. A newline inside an
apostrophe-quoted word is part of that word.

Unlike `rc`, backslash followed by a newline is not a continuation sequence.
Backslash is always an ordinary character so a Windows path ending in
backslash cannot change program structure. Input continues across lines while
a quote, parenthesis, brace, here document, or operator is syntactically
incomplete.

### 2.3 Syntax Characters

Outside quotes, ASCII space, tab, form feed, and a line ending separate tokens.
The following ASCII characters are metacharacters:

```text
# ; & | ^ $ ` ' { } ( ) < > !
```

Backslash, slash, colon, period, and hyphen are ordinary word characters. This
allows native drive, UNC, device, and relative paths without extra escaping.
Non-ASCII whitespace is an ordinary word character; WSH does not make grammar
depend on a changing Unicode character database.

A `#` outside quotation begins a comment through the next line ending. A
comment does not consume the line-ending separator.

## 3. Lexical Forms

### 3.1 Words

An unquoted word is one or more non-separator, non-metacharacter characters.
Adjacent word-producing forms with no separating whitespace receive a free
caret as described in section 5.3.

Keywords are recognized only where the grammar expects the first word of a
command. Quoting or concatenating any part of a keyword produces an ordinary
word. The reserved keywords are:

```text
for in while if not switch case fn ! @
```

Names used for variables and functions shall match:

```text
[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*
```

Names are case-sensitive. Numeric positional names and the special names `*`,
`0`, `status`, `apid`, `pid`, `home`, `ifs`, `path`, `cdpath`, and `prompt` are
defined separately.

### 3.2 Apostrophe Quotation

An apostrophe-quoted word begins and ends with `'`. Every character is literal
except that `''` represents one apostrophe. Quotation may contain line endings.
An empty quoted word, `''`, produces one empty string; it is not an empty list.

WSH has no backslash escapes and no double-quote string syntax.

### 3.3 Operators

The compound operators are:

```text
&& || >> << <>
```

Redirection and pipeline descriptor decorations use square brackets as
specified in section 12. Square brackets otherwise remain ordinary word
characters so Windows wildcard classes can be written without quoting.

## 4. Grammar

The following extended BNF is normative at the syntactic level. Lexical free
caret insertion and assignment recognition occur before these productions.

```ebnf
input         = [ command_list ], EOF ;
command_list  = and_or,
                { list_separator, [ and_or ] } ;
list_separator= ";" | newline | "&" ;
separator_run = list_separator, { list_separator } ;
and_or        = pipeline, { ( "&&" | "||" ), pipeline } ;
pipeline      = unary, { pipe_operator, unary } ;
unary         = { "!" | "@" }, command ;
command       = simple | block | if_command | while_command
              | for_command | switch_command | function_command ;
block         = "{", [ command_list ], "}" ;
if_command    = "if", "(", [ command_list ], ")", command,
                [ separator_run, "if", "not", command ] ;
while_command = "while", "(", [ command_list ], ")", command ;
for_command   = "for", "(", name,
                [ "in", argument_sequence ], ")", command ;
switch_command= "switch", "(", argument, ")", "{",
                { case_clause }, "}" ;
case_clause   = "case", argument_sequence, list_separator,
                [ command_list ] ;
function_command = "fn", name, [ block ] ;
simple        = simple_item, { simple_item } ;
simple_item   = argument | assignment | redirection ;
assignment    = name, "=", argument ;
argument_sequence = { argument } ;
argument      = word | quoted_word | list_value | variable
              | count | flatten | substitution | process_substitution
              | argument, "^", argument ;
list_value    = "(", argument_sequence, ")" ;
variable      = "$", variable_name, [ subscript ] ;
count         = "$#", variable_name ;
flatten       = "$\"", variable_name ;
substitution  = "`", block ;
process_substitution = ( "<" | ">" | "<>" ), block ;
pipe_operator = "|", [ "[", [ decimal, [ "=", decimal ] ], "]" ] ;
redirection   = ( "<" | ">" | ">>" | "<<" ),
                [ "[", decimal, [ "=", [ decimal ] ], "]" ],
                [ argument ] ;
variable_name = name | decimal | "*" | "0" ;
subscript     = "(", subscript_item, { subscript_item }, ")" ;
subscript_item= decimal | decimal, "-", [ decimal ] ;
```

`simple` shall contain at least one item. An assignment is recognized only
when a valid name begins a simple-command item and `=` follows the name without
whitespace. `name = value` is therefore not an assignment.

Operator precedence, from highest to lowest, is pipeline, unary `!` and `@`,
then `&&` and `||`. Pipelines and conditional operators associate left to
right. Braces override grouping.

## 5. Values and Concatenation

### 5.1 Value Model

Every language value is an ordered list of zero or more immutable Unicode
strings. Lists are flat and do not contain lists. Empty list `()` and
one-element list containing an empty string `('')` are distinct.

Arguments retain boundaries until an external-process serializer or an
explicit flattening operation is invoked. WSH performs no Bourne-style
implicit field splitting on variable expansion.

### 5.2 List Construction

Parenthesized argument sequences concatenate their members into one flat list:

```rc
value=(alpha (beta gamma) 'delta value')
```

This produces four elements. Whitespace separates arguments; it is not stored.

### 5.3 Caret Concatenation

`left^right` concatenates strings without a separator. If both operands have
equal nonzero lengths, elements concatenate pairwise. If one operand has one
element and the other is nonempty, the singleton distributes over the other
operand. Two empty operands yield the empty list. All other cardinality
combinations are evaluation errors.

The lexer inserts a free caret between adjacent word-producing forms that have
no whitespace or operator between them. Thus `C:^$dir^\file` uses the same
operation as explicit carets.

## 6. Variables and Environment

### 6.1 Assignment and Lookup

`name=value` assigns the resulting list to `name`. A standalone assignment
persists in the current context. Assignments preceding a command are restored
after that command completes. `name=()` assigns an empty list. `unset name`
removes the binding and its export attribute.

An unbound variable expands to the empty list. `$name` expands to all elements.
`$#name` expands to one decimal string containing the element count.
`$"name` expands to one string formed by joining elements with one ASCII space.

### 6.2 Subscripts

Subscripts are one-origin. `$name(1 3 5)` selects those elements. A range
`m-n` selects inclusive elements and `m-` selects through the end. A missing
element contributes no value. Reversed, zero, negative, malformed, or
nondecimal subscripts are evaluation errors.

`$number` selects the corresponding element of `$*`. `$0` is the logical
source name: the script path for a script, `-c` for command text, and `wsh` for
an interactive session. `$*` contains only user arguments after the source.

### 6.3 Scope

Functions receive a saved, local `$*`; it is restored on return. `local`
creates function- or block-local bindings that are restored when that dynamic
scope exits. Other assignments affect the current shell context. A sourced
file operates in the caller's context but receives a saved local `$*`.

`@ command` evaluates `command` in a semantic clone of the current context.
The clone includes private variables, exported variables, functions, logical
working directory, and descriptors. Changes are discarded when it finishes.
This is a subshell contract and does not require a Unix-style `fork`.

### 6.4 Export

Variables imported from the Windows environment begin exported. New variables
begin private. `export name` and `unexport name` change the attribute for
future children. A child receives a snapshot; it cannot modify its parent.

An exported variable passed to an ordinary external program shall contain
exactly one string. A multi-element export is an error before process creation,
except for variables with an explicitly specified adapter. `path` is adapted
to Windows `PATH` using semicolon-separated elements. An element containing a
semicolon cannot be exported through `PATH` and is rejected.

Nested WSH processes use a private, versioned environment envelope to preserve
exported lists without loss. The envelope is validated as untrusted input and
never overrides an explicitly supplied ordinary environment value without a
matching parent-instance nonce. Functions and private variables are not
automatically exported.

Private names remain case-sensitive. Exported names shall be unique under
Windows ordinal case-insensitive comparison. Import preserves displayed
spelling, and environment lookup is case-insensitive. Attempting to export
both `name` and `NAME` is an error.

## 7. Special Variables

| Variable | Meaning |
| --- | --- |
| `$*` | Current script, source, or function arguments |
| `$0` | Logical source name |
| `$status` | Ordered decimal exit-code list from the last foreground command |
| `$apid` | Process identifier of the most recently launched background command |
| `$pid` | Current WSH process identifier |
| `$home` | Default directory used by `cd` |
| `$ifs` | Character set used by command substitution; defaults to space, tab, CR, and LF |
| `$path` | Ordered executable and source-file search path |
| `$cdpath` | Optional ordered search path used by `cd` |
| `$prompt` | Primary and continuation interactive prompts |

Special variables are ordinary list variables except where their adapter or
read-only status is specified. `$pid` is read-only. Assigning malformed values
to `$status`, `$path`, or `$prompt` is rejected at the operation that requires
their defined form.

## 8. Status and Conditional Execution

Windows process exit codes are represented as unsigned 32-bit decimal strings
from `0` through `4294967295`. A status list succeeds only when every element
is `0`. An empty status list is invalid and is never produced by execution.

A simple command produces a one-element `$status`. A pipeline produces one
element per stage from left to right. If WSH must return one process exit code,
it returns the first nonzero element, or zero when all elements are zero.

`left && right` executes `right` only if `left` succeeds. `left || right`
executes `right` only if `left` fails. The final status is the status of the
last command actually executed. `! command` produces `(1)` when `command`
succeeds and `(0)` when it fails.

This adopts `rc`'s all-components pipeline truth rule while adapting its
delimiter-combined wait-message representation to a native WSH list.

## 9. Expansion and Matching

### 9.1 Evaluation Order

For each simple command, WSH performs syntactic list construction, variable
expansion, substitution, caret concatenation, and then pathname expansion.
Redirection operands undergo the same expansion and shall produce exactly one
string unless their operator states otherwise.

### 9.2 Command Substitution

`` `{command}` `` executes `command`, captures its standard output, decodes it
as UTF-8, and splits it on Unicode scalar values present in `$ifs`. Leading,
trailing, and repeated separators do not produce empty elements. Invalid UTF-8
is an evaluation error. External bytes can be captured with another encoding
through `process::capture`.

The substitution command's status is retained until the containing command
finishes. A failed substitution prevents the containing external command from
launching and becomes its status. This test-oriented adaptation prevents an
earlier failure from being silently hidden.

### 9.3 Patterns and Globbing

Unquoted `*`, `?`, and `[class]` form patterns. `*` matches zero or more
characters within one path component, `?` matches one character, and a class
matches one listed character or inclusive range. `[~class]` complements a
class. Slash and backslash are path-component separators.

A wildcard does not match a leading period unless the period appears
explicitly in that component's pattern. Windows hidden and system attributes
do not independently suppress a match. Matching uses Windows ordinal
case-insensitive comparison. Results are sorted first ordinal
case-insensitively and then ordinally, making enumeration deterministic.

An unmatched pathname pattern remains literal, as in `rc`. `~ subject pattern
...` performs pattern matching without filesystem expansion and succeeds when
any pattern matches. `~` uses case-sensitive Unicode scalar comparison;
`match::windows` in the standard library provides Windows-insensitive matching.

## 10. Compound Commands and Functions

`if (list) command` runs `command` when `list` succeeds. `if not command` may
immediately follow and runs only when the associated condition failed.

`while (list) command` repeats while `list` succeeds. `for (name in values)
command` assigns each element to `name`. Omitting `in values` iterates `$*`.
`break` and `continue` affect the nearest active loop.

`switch (argument) { case patterns; commands ... }` requires `argument` to
produce one string. The first matching top-level `case` runs through the next
case or closing brace; there is no implicit fallthrough across a later case.

`fn name { list }` defines a function and `fn name` removes it. Calling a
function saves `$*`, assigns its arguments, and restores `$*` on return.
`return [status]` exits the current function or sourced file. With no operand
it preserves `$status`; with one decimal operand it sets that status.

The signal functions `sigint` and `sigexit` are supported. `sigint` is invoked
after foreground cancellation when defined. `sigexit` runs during orderly
context shutdown. They shall not run after process corruption or forced
operating-system termination. Plan 9 note and `rfork` semantics are excluded.

## 11. Simple Commands and Resolution

Resolution occurs in this order:

1. a function;
2. a built-in or embedded standard-library command;
3. an explicit path containing a separator or drive prefix;
4. the current directory for a bare name; and
5. each `$path` element in order.

`builtin command ...` bypasses a same-named function. `command::external`
bypasses functions and built-ins and performs external resolution.

For a name without an extension, external resolution tests the exact name,
then `.exe`, then `.com`. It does not use `PATHEXT`. `.bat`, `.cmd`, PowerShell
scripts, document associations, and URL handlers are never launched
implicitly. An explicit `.wsh` path starts a new instance of the current WSH
executable in script mode. Other scripts require their interpreter to be named
explicitly.

Structured external arguments are serialized with the documented Microsoft C
runtime quoting algorithm and launched with `CreateProcessW`, passing the
resolved executable as `lpApplicationName`. No intermediate command
interpreter reparses them.

`rawexec executable complete-command-line` resolves `executable` separately
and passes the second argument as the complete `lpCommandLine` string without
argument reconstruction. It requires exactly two strings, supports normal
redirection and waiting, and shall be disabled when policy forbids raw launch.
It is unsafe for untrusted interpolated text.

## 12. Redirection and Pipelines

### 12.1 Ordered Redirection

The operators `<file`, `>file`, and `>>file` open standard input, truncate
standard output, or append standard output. Redirections are applied from left
to right. A failed redirection prevents command execution.

`>[n]file` and `<[n]file` select logical descriptor `n`.
`>[to=from]` or `<[to=from]` duplicates an existing descriptor, and
`>[n=]` or `<[n=]` closes it. Descriptors 0, 1, and 2 are guaranteed for every
child. Descriptors 3 through 9 are guaranteed only between WSH and cooperating
WCRT/WSH children using the versioned inherited-handle map. Other programs may
ignore them.

No redirection performs text or newline translation. A target path is opened
using wide Windows APIs and the current context's logical working directory.

### 12.2 Here Documents

`<<marker` supplies lines through a line containing only `marker`. A quoted
marker disables expansion. An unquoted marker expands `$name` forms and
removes a caret immediately following an expansion, following `rc`. Source
line endings are normalized internally; bytes supplied to the command are
UTF-8 with CRLF endings unless a binary standard-library operation is used.

### 12.3 Pipelines

`left | right` connects descriptor 1 of the left stage to descriptor 0 of the
right stage. `|[out]` and `|[in=out]` select other logical descriptors.
Pipeline transport is a byte stream and performs no encoding conversion.

All stages start before WSH waits for completion. `$status` receives every
stage's exit code in left-to-right order. Handle inheritance is restricted to
the handles required by each stage.

### 12.4 Process Substitution

`<{command}`, `>{command}`, and `<>{command}` use unique, access-controlled
Windows named pipes rather than `/dev/fd`. Their values are one or two
`\\.\pipe\...` path strings usable by programs that can open named pipes.
Provider commands run asynchronously and are included in waiting,
cancellation, and status accounting for the containing command.

## 13. Background Commands and Waiting

When `&` is the list separator after a command, it starts that command in the
background, sets `$apid` to its decimal process
identifier, and does not replace `$status`. `wait pid` waits for one known
child. `wait` waits for all outstanding children in launch order and returns
their statuses as a list. Waiting for an unknown or already-collected process
is an error.

Foreground interruption and explicit timeout cancellation apply to the whole
descendant tree started for that command, subject to the documented platform
fallback when nested Windows job objects are unavailable.

## 14. Built-in Commands

Built-ins execute in the current context because they inspect or change shell
state. Unless stated otherwise, an invalid operand count or option produces a
nonzero status and no state change.

| Command | Normative behavior |
| --- | --- |
| `. file args...`, `source file args...` | Evaluate a searched or explicit file in the caller context with saved `$*` |
| `builtin command args...` | Invoke the built-in meaning while bypassing a same-named function |
| `command::external command args...` | Resolve and launch only an external executable or explicit `.wsh` script |
| `cd [directory]` | Change the context's logical directory; use `$home` when omitted and `$cdpath` for eligible relative names |
| `echo [-n] args...` | Write arguments separated by one space and, unless `-n`, one CRLF |
| `eval args...` | Join arguments with one space and evaluate them as WSH source |
| `exec command args...` | Run an external command as the final action of the current context |
| `exit [status]` | End the shell context with an explicit or reduced current status |
| `export name...`, `unexport name...` | Change export attributes without changing values |
| `local name[=value]...` | Create bindings restored when the current function, source, or braced dynamic scope exits |
| `unset name...` | Remove bindings and export attributes |
| `shift [count]` | Remove `count` elements, default one, from the start of `$*` |
| `wait [pid...]` | Collect known background children and return ordered statuses |
| `whatis name...` | Describe variables, functions, built-ins, library commands, or resolved executables in WSH-readable form |
| `~ subject pattern...` | Perform non-filesystem pattern matching and set success/failure |
| `rawexec executable command-line` | Perform the explicit policy-controlled raw Windows launch |
| `break`, `continue` | Transfer control within the nearest active loop |
| `return [status]` | Return from the current function or sourced file |

`cd` accepts both path separators where the target form permits them. A drive-
relative path uses that drive's context directory. A `cdpath` search is not
performed for an explicit, rooted, drive-prefixed, dot-prefixed, UNC, or device
path. On success, `$status=(0)`; failure leaves the directory unchanged.

`whatis` output uses apostrophe quotation and stable canonical names so it can
be evaluated to reproduce variable/function definitions where permitted. It
does not print secret-marked host values.

## 15. Sourcing, Evaluation, and Exit

`. file args...` and `source file args...` are synonyms. They search an
explicit path directly or search `$path`, evaluate the file in the current
context, save and restore `$*`, and return its status. A sourced `exit` exits
the shell context; `return` returns only from the source.

`eval args...` joins arguments with one ASCII space and evaluates the result in
the current context. It is explicitly unsafe for untrusted strings.

`exec command...` runs an external command and ends the current shell context
with its result. Windows cannot replace a process identity as Unix does; the
observable contract is that WSH waits, propagates interruption, returns the
child's exit code, and performs no later shell command.

`exit [status]` exits with the supplied unsigned 32-bit code or the first
nonzero current status. Interactive EOF behaves as `exit`.

## 16. Diagnostics and Failure Atomicity

Lexical and syntax errors identify source, one-origin line and column, byte
offset, error code, and a bounded excerpt. A complete simple command containing
a lexical, parse, expansion, redirection, environment-conversion, or launch
error shall not partially execute.

Earlier complete commands in the same input remain executed. A pipeline launch
failure cancels already-started stages, waits for cleanup, and returns a
documented shell-generated nonzero status. Diagnostics go to descriptor 2 and
are UTF-8 with CRLF when redirected.

## 17. Interactive Equivalence

Interactive input may provide history, editing, completion, and continuation
prompts, but those facilities shall not change language meaning. Profiles may
define functions and variables but cannot modify the lexer, grammar, status
truth rule, quoting, or process serialization.

## 18. Exclusions

WSH 1.0 excludes Plan 9 namespaces, `/env`, notes other than the defined
Windows analogues, `rfork`, Unix job-control process groups, kernel shebang
handling, implicit `cmd.exe`, automatic file associations, and a promise that
arbitrary Plan 9 `rc` scripts execute unchanged.
