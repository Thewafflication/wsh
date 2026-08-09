# ADR-0007: Structured Windows Process and Status Model

**Status:** Proposed

**Date:** 2026-08-09

## Context

Windows passes one command-line string to a child, while WSH and `rc` model a
command as a list of arguments. Windows exit codes are numeric, while Plan 9
`rc` uses string wait messages and combines pipeline statuses.

## Decision

WSH retains structured argument lists and uses one deterministic Microsoft C
runtime-compatible serializer with an explicit `CreateProcessW` application
name. `rawexec` is a separate policy-controlled escape hatch for programs with
nonstandard parsing. No intermediate command interpreter or file association
is implicit.

`$status` is an ordered list of unsigned Windows exit codes. A pipeline has one
element per stage and succeeds only if every element is zero, preserving
`rc`'s pipeline truth behavior in a Windows-native representation.

## Consequences

- WSH/WCRT argument boundaries can round-trip exactly and be tested.
- Applications with custom parsing may require `rawexec` and careful quoting.
- Pipeline failures cannot be hidden by a successful final stage.
- Returning one process code requires a documented reduction: first nonzero,
  or zero.
