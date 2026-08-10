# ADR-0001: Rc-Inspired Language with Explicit Windows Semantics

**Status:** Accepted

**Date:** 2026-08-04

## Context

Plan 9 `rc` obtains part of its behavior from Plan 9 facilities such as `/env`,
notes, namespaces, string-valued exit status, and `rfork`. Windows has a
different process, environment, path, console, and handle model.

## Decision

Waughtal Shell will preserve the documented `rc` language concepts that do not
depend intrinsically on Plan 9: list-valued variables, apostrophe quotation,
caret concatenation, functions, structured commands, substitution, pipelines,
and ordered redirection. Platform-dependent behavior will have a Windows-native
contract and will be identified as compatible, adapted, unsupported, or
deferred in the language reference and tests.

The 1.0 specification uses the dispositions adopted, adapted, extended, and
excluded. Every language change review will first determine what the `rc`
documentation specifies and then whether that behavior is compatible with the
supported Windows range.

The project will not claim that arbitrary Plan 9 `rc` scripts execute unchanged.

## Consequences

- The language remains recognizably `rc` and avoids accumulating unrelated
  PowerShell or Bourne syntax.
- Windows users receive predictable native process and path behavior.
- A compatibility matrix and conformance suite become release artifacts.
- Some Plan 9 features require deliberate analogues or remain unsupported.
- The compatibility record and conformance suite must change with any language
  decision.
