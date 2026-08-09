# Waughtal Shell 1.0 Command-Line Interface

**Document ID:** `WSH-SPEC-CLI-0001`

**Status:** Proposed

## 1. Invocation Forms

```text
wsh [options]
wsh [options] script.wsh [arguments ...]
wsh [options] -c command [arguments ...]
wsh --help
wsh --version
```

Options are parsed only before the script operand, except that `--` ends option
processing. Arguments following a script or the `-c` command string populate
`$*` without further WSH command-line parsing. Windows structured argument
boundaries are authoritative.

With no script or `-c`, WSH reads standard input. It is interactive when input
is a console and `--non-interactive` is absent. Redirected or piped input is
batch input. `--interactive` requires a usable console and fails rather than
silently changing mode.

## 2. Options

| Short | Long | Operand | Behavior |
| --- | --- | --- | --- |
| `-c` | `--command` | text | Execute command text; `$0` is `-c` |
| `-i` | `--interactive` | none | Require interactive mode |
| `-I` | `--non-interactive` | none | Disable prompts, editing, and interactive recovery |
| `-l` | `--login` | none | Load the machine and user login profiles before the selected input |
| none | `--profile` | path | Load this profile after normal profile selection; repeatable |
| none | `--no-profile` | none | Load no machine, user, or explicit profile |
| none | `--config` | path | Use exactly this data configuration plus mandatory policy |
| none | `--no-config` | none | Use compiled defaults plus mandatory policy |
| none | `--portable` | none | Use `wsh.ini` beside the resolved executable and no default user or machine data file |
| none | `--encoding` | name | Decode the primary script or stdin as `utf-8`, `utf-16le`, `utf-16be`, `ansi`, or `oem` |
| `-e` | `--exit-on-error` | none | Exit batch evaluation after an unhandled failed simple command |
| `-s` | `--print-failed-status` | none | Print a status diagnostic after a failed foreground command |
| `-v` | `--trace-input` | none | Write source lines to descriptor 2 as read |
| `-x` | `--trace-execution` | none | Write expanded structured commands to descriptor 2 before execution |
| none | `--safe-path` | none | Do not perform the implicit current-directory search for bare commands |
| none | `--dump-config` | none | Print effective non-secret configuration and its source, then exit |
| none | `--print-abi` | none | Print the embedding ABI version and capabilities, then exit |
| `-h` | `--help` | none | Print usage and option documentation, then exit |
| `-V` | `--version` | none | Print product and dependency identity, then exit |
| none | `--` | none | End option processing |

Long options require a separate operand or `--name=value`. Short options that
take no operand may be grouped. `-c`, `--config`, `--profile`, and `--encoding`
shall not consume an operand from a grouped short option.

Mutually exclusive modes, missing operands, an unknown option, an unsupported
encoding, and extra operands after an information-only option are usage errors.

`ansi` means the process ANSI code page and `oem` means the active console OEM
code page captured at startup. They are explicit legacy modes and do not alter
internal UTF-8 storage or redirected output encoding.

## 3. Profile Rules

An ordinary interactive session loads the user profile unless disabled. A
login session loads the machine profile and then the user profile. Batch,
script, `-c`, and embedding evaluation load no profile unless `--login` or
`--profile` is explicit. `--no-profile` conflicts with `--profile` and
`--login`.

Profiles are WSH code and may have side effects. Data configuration is not
code. Their locations and precedence are specified separately.

## 4. Exit Codes

When a script, command string, or input stream completes, WSH returns the first
nonzero element of its final `$status`, or zero. `exit n` and a foreground
external program may therefore return any unsigned 32-bit Windows exit code.

WSH-generated failures use these stable codes:

| Code | Meaning |
| ---: | --- |
| `0` | Success |
| `1` | Evaluated command reported general failure |
| `2` | Command-line usage error |
| `3` | Lexical or syntax error |
| `4` | Configuration or policy error |
| `5` | Source, redirection, or filesystem I/O error |
| `6` | Text encoding error |
| `7` | Command, script, or library item not found |
| `8` | Process creation or embedding-host failure |
| `9` | Resource limit or timeout |
| `130` | Interrupted by Ctrl+C or equivalent cancellation |

An external process may return the same numeric code; process exit codes do
not carry a separate origin tag. Structured diagnostics identify WSH-generated
failures.

## 5. Help and Version

`--help`, `--version`, and `--print-abi` shall not load configuration, profiles,
history, or startup scripts and shall not write persistent state.

`wsh --version` follows the established WPM presentation: an ASCII banner,
product name and semantic version, then a `Dependencies:` inventory. It shall
include WCRT, the WSH standard library and link mode, embedding ABI, compiler,
and directly used Windows system libraries. Development builds also show the
source revision and dirty state. Output goes to stdout with CRLF and the option
returns zero.

Example, informative:

```text
=================================================================
Waughtal Shell (wsh) Version 1.0.0
=================================================================
Dependencies:
  wcrt 1.0.0 (statically linked runtime library)
  wsh standard library 1.0.0 (statically linked)
  wsh embedding ABI 1
  kernel32 5.0.2195.1 (Windows system library)
```

The displayed system-library versions are queried from the running system and
are not release dependencies beyond the documented minimum API contract.

## 6. Diagnostics and Streams

Normal information goes to stdout. Usage, configuration, syntax, trace, and
runtime diagnostics go to stderr. Information-only output is UTF-8 with CRLF
when redirected and uses wide console output when attached to a console.

`--trace-execution` renders argument boundaries unambiguously and redacts any
value marked secret by an embedding host. It shall not reconstruct a string
that could be mistaken for the exact raw Windows command line.
