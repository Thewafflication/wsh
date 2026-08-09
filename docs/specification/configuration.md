# Waughtal Shell 1.0 Configuration

**Document ID:** `WSH-SPEC-CONFIG-0001`

**Status:** Proposed

## 1. Principles

WSH separates inert data configuration from executable profiles. Configuration
shall not change the lexer, grammar, quoting rules, list model, status truth
rule, structured argument serializer, or other 1.0 language semantics.

WSH is usable with compiled defaults and no configuration, registry access, or
profile. Missing optional files are not errors. A named file that exists but is
unreadable or invalid is an error; WSH shall not silently fall back.

## 2. Configuration File Format

The file name is `wsh.ini`. It is UTF-8 with an optional BOM. CRLF, LF, and
lone CR are accepted. Maximum file size is 1 MiB, maximum physical line length
is 16 KiB, and U+0000 is prohibited.

Syntax is:

```ini
[section]
key = value
```

Section and key names are ASCII case-insensitive and use letters, digits, and
hyphens. Leading and trailing ASCII whitespace around sections, keys, `=`, and
bare values is ignored. A line whose first non-whitespace character is `#` or
`;` is a comment. Inline comments are not recognized.

A bare value is the remaining trimmed text. An apostrophe-quoted value may
contain leading or trailing whitespace, `#`, `;`, or `=`; doubled apostrophes
represent one apostrophe. A quoted value shall occupy the entire value field.
There are no escapes, interpolation, includes, environment expansion, or code
execution.

Boolean values are exactly `true` or `false`, ASCII case-insensitively.
Integers are unsigned decimal without separators. Relative path values resolve
against the containing configuration file's directory.

An unknown section, unknown key, malformed value, or duplicate scalar key is a
configuration error. Keys explicitly documented as repeatable retain file
order. This strictness prevents spelling mistakes from changing behavior
silently.

## 3. Locations

Known folders are obtained through wide shell APIs when available and through
documented environment fallbacks only when required on an older system.

| Scope | Default path |
| --- | --- |
| Machine data | `<CommonAppData>\Waughtal\WSH\wsh.ini` |
| User data | `<AppData>\Waughtal\WSH\wsh.ini` |
| Portable data | `<directory-containing-wsh.exe>\wsh.ini` |
| Machine profile | `<CommonAppData>\Waughtal\WSH\profile.wsh` |
| User profile | `<AppData>\Waughtal\WSH\profile.wsh` |
| User history | `<AppData>\Waughtal\WSH\history.txt` |

`CommonAppData` maps to the all-users application-data known folder on Windows
2000 and later; `AppData` maps to the roaming per-user application-data known
folder. WSH never constructs a Vista path by appending `Application Data` to
`%ALLUSERSPROFILE%`.

Portable configuration is loaded only when `--portable` is present. Merely
placing an untrusted `wsh.ini` beside the executable has no effect.

## 4. Selection and Precedence

From lowest to highest precedence, effective data comes from:

1. compiled defaults;
2. machine data configuration;
3. user data configuration;
4. a file selected by `WSH_CONFIG`; and
5. command-line options.

A machine or user registry `ConfigFile` value replaces that scope's default
path. `WSH_CONFIG` replaces the user data file rather than loading both.
`--config` loads only the named file above compiled defaults and mandatory
policy. `--portable` loads only the adjacent portable file above compiled
defaults and mandatory policy. `--no-config` loads no data file. These three
options are mutually exclusive.

Machine and user policy always applies and has higher authority than data or
command-line requests. A policy denial is reported explicitly.

## 5. Defined Keys

### 5.1 `[interactive]`

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `prompt-primary` | string | `% ` | First-line prompt text |
| `prompt-continuation` | string | `; ` | Incomplete-input prompt text |
| `color` | enum | `auto` | `auto`, `always`, or `never` diagnostic color |
| `completion` | boolean | `true` | Enable Tab completion |
| `print-failed-status` | boolean | `false` | Print failed foreground status interactively |
| `bell` | boolean | `false` | Permit an audible completion/editing bell |

Prompt strings are assigned as the two elements of `$prompt` after profile
loading unless a profile explicitly assigns `$prompt`.

### 5.2 `[history]`

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `enabled` | boolean | `true` | Enable interactive history |
| `file` | path | user history path | History file |
| `max-entries` | integer | `5000` | Entries retained, range 0--100000 |
| `max-bytes` | integer | `4194304` | File-size limit, range 0--67108864 |
| `deduplicate-adjacent` | boolean | `true` | Collapse consecutive identical entries |

History is never loaded or written in non-interactive mode. Failure to write
history is a visible warning but does not change a completed command's status.

### 5.3 `[diagnostics]`

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `trace-input` | boolean | `false` | Equivalent diagnostic behavior to `-v` |
| `trace-execution` | boolean | `false` | Equivalent diagnostic behavior to `-x` |
| `source-context-lines` | integer | `1` | Lines displayed around a source error, range 0--5 |
| `absolute-paths` | boolean | `false` | Display absolute rather than source-relative diagnostic paths |

### 5.4 `[library]`

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `temporary-root` | path | system temporary folder | Root used by `fs::temp-*` |
| `test-evidence-root` | path | `output\test-evidence` relative to initial directory | Default evidence destination for `test::*` |
| `clock` | enum | `system` | `system`; deterministic hosts may override through the embedding API |

## 6. Profiles

Profiles are normal WSH scripts and follow the language encoding rules. The
machine and user profile paths may be replaced by registry values. Explicit
`--profile` paths are evaluated in option order after normal profiles.

An interactive session loads the user profile. `--login` loads machine then
user profiles. Batch modes load none unless requested. A profile failure stops
startup and returns its status; WSH does not continue with a partially applied
profile.

Profiles should define functions, variables, path elements, prompts, and
aliases. Security policy may disable all profiles. A profile shall not be
loaded merely because the current directory contains one.

## 7. Environment Inputs

`WSH_CONFIG` names one data file and is ignored when `--config`, `--portable`,
or `--no-config` is present. `WSH_HOME` may supply `$home` only when the
ordinary Windows `USERPROFILE` and known-folder lookup do not yield a usable
directory. No other `WSH_*` environment name changes language behavior in 1.0.

Environment-provided paths are treated as untrusted. Relative `WSH_CONFIG` is
rejected; the selected file must be an absolute local or UNC path.

## 8. Effective Configuration Output

`--dump-config` prints canonical section and key names, effective values,
source (`default`, machine file, user file, environment, command line, or
policy), and ignored values blocked by policy. Paths are absolute. Values
classified as secrets by a host are printed as `<redacted>`.
