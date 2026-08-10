# Waughtal Shell 1.0 Platform Compatibility

**Document ID:** `WSH-SPEC-PLATFORM-0001`

**Status:** Accepted

## 1. Compatibility Promise

Each architecture has one release binary that is byte-for-byte identical
across its supported Windows versions. WSH does not ship per-OS executables or
select language behavior by Windows version.

| Binary | Minimum | Continuing target |
| --- | --- | --- |
| x86 | Windows 2000, NT 5.0 | Every later 32-bit Windows and WOW64 where Microsoft supports x86 execution |
| x64 | Windows XP Professional x64 / Windows Server 2003 x64 | Every later x64 Windows client and server |
| ARM64 | First generally released Windows 10 on ARM64 | Later Windows on ARM64 |

Windows 2000 IA-64, Windows CE, Windows 9x/Me, Xbox, and kernel mode are not
targets. Running x86 under ARM emulation is an additional configuration, not a
substitute for native ARM64 evidence.

The release verification matrix identifies exact editions, service packs,
architectures, bare-metal or virtualized environments, and equivalence
justifications. A version is claimed only when its final WCRT-linked binary has
native or approved-equivalent execution evidence.

## 2. Binary and Runtime

The official artifact is a console-subsystem PE executable named `wsh.exe`.
It statically links the pinned WCRT release and the WSH standard library. It
shall not require UCRT, a Visual C++ redistributable, .NET, PowerShell,
`cmd.exe`, a POSIX layer, registration, installation, or administrator rights.

The x86 PE subsystem and imported API baseline shall be compatible with
Windows 2000. Newer APIs are located dynamically after capability detection;
they shall not appear in the static import table of an older-target binary.
Missing optional APIs select a specified fallback, not a different language
meaning.

Release `.exe` and `.dll` files contain WSP-compliant version resources. The
product and file version are consistent across architectures. `wsh --version`
reports WCRT and library linkage.

## 3. Console Model

WSH is a Windows console application, not a component hosted inside
`cmd.exe`. When launched by a console process it inherits valid standard
handles and attaches to that console. When launched without a console it does
not create one unless interactive mode is explicitly required.

Interactive console text uses wide console APIs. Redirected WSH text is UTF-8
with CRLF. External bytes are passed unchanged. Rendering depends on the
console host and font; lossless Unicode handling is required, but visible glyph
availability on Windows 2000 is not.

Interactive editing uses console input records on legacy systems and may use
equivalent modern terminal facilities after capability detection. ANSI/VT
support is never required for basic operation. Ctrl+C cancels the current edit
or foreground process tree and returns status 130. Ctrl+Break requests the
same cancellation with an immediate escalation after the configured grace
period. Ctrl+Z followed by Enter at an empty prompt means EOF.

## 4. Unicode Boundary

The language core, variables, diagnostics, and embedding API use validated
UTF-8. Win32 paths, environment blocks, registry strings, console text, and
process command lines use UTF-16 and wide APIs. WSH shall not call an ANSI API
when a wide equivalent exists in the minimum target.

Conversion is lossless for every Unicode scalar value. Supplementary
characters use UTF-16 surrogate pairs. Invalid input is diagnosed rather than
replaced silently. Locale and active code pages do not affect grammar,
comparison, numeric parsing, or output encoding.

## 5. Paths and Filesystems

WSH accepts drive-absolute, drive-relative, rooted, relative, UNC, verbatim,
and device paths:

```text
C:\directory\file
C:relative\file
\rooted\file
.\relative\file
\\server\share\file
\\?\C:\long\path
\\.\pipe\name
```

Forward slashes are accepted by WSH path-using built-ins where Windows
semantics permit them. WSH does not rewrite arguments passed to external
programs. Verbatim and device paths are preserved exactly.

The logical current directory includes Windows' current drive and per-drive
current-directory behavior. Each shell context owns a logical directory;
embedding does not require changing the host process's global directory.
External launch passes the context directory explicitly.

Path operations use dynamically sized buffers. `MAX_PATH` is not a language
limit. Operations that require a verbatim path add or preserve its prefix only
inside the platform layer and only when doing so cannot change relative,
device, or normalization semantics. Filesystems may impose smaller limits.

File matching and ordering use the specification's locale-independent Windows
comparison. Reparse points are never followed by recursive destructive
operations unless an explicit operation says so.

## 6. Executable Discovery and Launch

Bare commands search functions, built-ins, the current directory, and `$path`
in the specified order. `--safe-path` and mandatory policy may suppress the
implicit current-directory step. `.` remains a valid explicit `$path` element.

External resolution considers exact names, `.exe`, and `.com`; it does not use
`PATHEXT`, App Paths, file associations, `ShellExecute`, or implicit script
interpreters. `.cmd` and `.bat` require `cmd /c`; PowerShell scripts require an
explicit PowerShell executable.

Structured launches call `CreateProcessW` with a separately resolved
application name, an explicitly constructed environment block, explicit
working directory, and an allowlist of inheritable handles. The serializer is
stable across OS versions. A raw launch is explicit and policy-controlled.

## 7. Process Trees, Jobs, and Interruption

WSH tracks every child it creates. It uses a Windows job object when the host
environment permits assignment. On systems or hosts where nested-job rules
prevent assignment, WSH uses a documented tracked-descendant fallback and
reports reduced containment through diagnostics and test metadata.

Timeout and interrupt handling first requests the applicable console control
event for a new child process group, waits a bounded grace interval, then
terminates remaining tracked descendants. WSH waits for handle closure and
does not report cancellation complete while known children remain live.

Background children are collected explicitly by `wait` or during orderly
shutdown according to policy. Interactive shell exit warns about live jobs;
batch exit cancels and collects children unless they were explicitly detached
through a future, separately specified facility. WSH 1.0 has no detach command.

## 8. Environment

Windows environment blocks are case-insensitive name maps of UTF-16 strings.
WSH validates names, rejects `=`, U+0000, and case-folded duplicates, sorts the
block as required by Windows, and creates it explicitly for each child.

The internal environment-list envelope used between WSH processes is
versioned, bounded, authenticated to the parent instance with an unguessable
nonce, and ignored safely by non-WSH programs. Ordinary external programs see
only the scalar mapping defined by the language specification.

## 9. Time and Version Detection

Language behavior never depends on localized date, time, or version strings.
UTC timestamps use explicit Gregorian/RFC 3339 formatting. Monotonic duration
uses the best available monotonic counter and documents its resolution.

WSH uses capability detection for optional APIs. Reported Windows identity is
obtained through an API not affected by application compatibility version
lies where available, with a documented fallback on Windows 2000. The version
is diagnostic and verification metadata, not a semantic switch.

## 10. Portability Acceptance

A release is portable only when copying the single architecture-appropriate
`wsh.exe` to a supported machine is sufficient for `--version`, interactive
input, `-c`, script execution, filesystem built-ins, and native child launch.
Optional configuration, history, profiles, documentation, symbols, packages,
and signatures may accompany it but are not startup dependencies.
