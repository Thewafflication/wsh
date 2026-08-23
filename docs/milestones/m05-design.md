# M5 Design — Windows Runtime Boundary

**Status:** Reviewed for implementation

**Date:** 2026-08-22

## Portable Launch Contract

The core runtime request gains borrowed typed orchestration data. A launch
command contains a subject, structured arguments, ordered descriptor actions,
and a raw/structured flag. A pipeline contains commands and descriptor edges.
Requests also identify foreground/background/wait/cancel behavior and borrow
the isolated context whose exported variables form the child snapshot.

The evaluator expands every ordinary operand and redirection before requesting
an effect. It validates descriptor numbers and scalar operands, preserves
redirection order, and publishes no request after a preparation error. Runtime
outputs carry captures or launched process identifiers; runtime statuses carry
one code per foreground stage.

## Windows Runtime Ownership

One `wsh_windows_runtime` owns its logical UTF-16 working directory, current
executable path, policy/capabilities, a bounded child registry, pipe providers,
and a launch lock. It never changes the process current directory, process
environment, or process standard handles. The front end destroys the evaluator
before the runtime, then runtime shutdown cancels and collects every known
child and closes every owned handle.

Every prepared command owns its converted application path, mutable command
line, sorted double-NUL environment block, directory, and descriptor table.
Ownership transfers only after successful `CreateProcessW`; parent pipe ends
and thread handles close as soon as their last use ends. Cleanup is idempotent.

## Resolution and Serialization

Explicit names containing a separator or drive prefix are checked directly.
Bare names search the logical current directory unless safe-path is active,
then each `path` element. Extensionless names try exact, `.exe`, then `.com`.
Only regular executable candidates pass. The resolver does not consult
`PATHEXT`, App Paths, associations, URL handlers, `SearchPath`,
`ShellExecute`, `cmd`, or PowerShell.

An explicit resolved `.wsh` candidate replaces the application with the
current WSH executable and prepends the script path to structured arguments.
The Microsoft C-runtime serializer quotes empty/whitespace arguments and
doubles backslashes only where required before a quote or closing quote.
`lpApplicationName` is always the separately resolved executable. `rawexec`
uses the same resolver but passes one caller-supplied mutable command line.

## Environment and Directory

Startup imports the Windows UTF-16 environment after strict conversion.
`PATH` becomes the WSH `path` list adapter. Child blocks include only exported
names, reject `=`, NUL, scalar violations, semicolons in path elements, and
ordinal case-insensitive collisions, then sort case-insensitively with an
ordinal tiebreak. A private bounded versioned envelope preserves list exports
for a child WSH and is correlated with a per-runtime instance nonce.

The logical directory is captured once and changed only by the runtime `cd`
operation after existence/type validation. All relative resolution,
redirection, source, and glob operations use it explicitly. Every child launch
passes it as `lpCurrentDirectory`.

## Descriptors, Pipelines, and Capture

Descriptor actions start from inherited logical 0, 1, and 2, then apply open,
append, duplicate, close, and here-data operations left to right. Files use
wide `CreateFileW`; here documents use anonymous pipes and exact UTF-8 CRLF
bytes. Descriptors 3 through 9 travel through the versioned WSH/WCRT map.

Pipelines allocate all edges first, prepare all stages, create every stage
suspended, and assign containment before resuming any stage. Each stage gets
only its selected endpoints. The parent closes unused endpoints before waits.
Launch failure terminates and collects all started stages. Wait results are
read in source order, not completion order. Capture is a bounded pipe drained
concurrently so a producing child cannot block on a full buffer.

## Jobs, Cancellation, and Process Substitution

Foreground groups and background commands enter a kill-on-close job when the
host permits assignment. A nested-job rejection activates the tracked-process
fallback and is exposed through runtime capability metadata. Cancellation
first requests a console control event for the new process group, waits the
configured grace interval, then terminates the job or each tracked process and
collects it. Timeouts use the same path.

Background launch returns the root process identifier without replacing the
foreground status. `wait` accepts known uncollected identifiers or all jobs in
launch order. Process substitution creates one unique local named-pipe first
instance under the process token's default DACL. A provider worker connects the
expected peer, launches its prepared provider, and remains in the same registry
for wait/cancel/shutdown accounting.

## Old and New Windows Paths

Vista-or-newer `STARTUPINFOEXW` handle lists are found dynamically and used
when all three attribute-list functions are present. They never enter the
static import table. The Windows 2000 fallback serializes WSH launch setup,
marks only selected WSH handles inheritable, calls `CreateProcessW`, and clears
them before releasing the lock. WSH-created non-selected handles are always
non-inheritable. Embedded hosts are required not to expose unrelated
inheritable handles during this legacy fallback; this residual host boundary
is explicit rather than silently described as a kernel-enforced allowlist.

## Compatibility and Security

This design implements the already accepted `rc` adaptations and changes no
language disposition. Plan 9 descriptors and `/dev/fd` cannot map directly to
Windows, so logical descriptors, byte pipes, named-pipe paths, numeric status
lists, and tracked jobs preserve their observable purposes. All path,
environment, pipe-peer, child-output, and child-status inputs remain untrusted
and bounded. ADR-0008 records the durable inheritance fallback decision.
