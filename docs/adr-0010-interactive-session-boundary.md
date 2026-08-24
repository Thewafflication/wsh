# ADR-0010: Executable-Owned Interactive Session Boundary

**Status:** Accepted

**Date:** 2026-08-24

## Context

M7 requires mutable console/editor/history state and asynchronous console
control handling. Placing that state in the portable evaluator would make
batch and embedded hosts acquire implicit terminal behavior, while a second
process launcher would bypass M5 child ownership.

## Decision

The executable owns the interactive session and native console editor. The
session borrows one context, evaluator, and Windows runtime created by the
ordinary front end. Small internal interfaces expose evaluator function
introspection/signal invocation and Windows-runtime interruption/background
operations. History library requests are intercepted by the executable's
runtime adapter and never become implicit behavior in another host.

The editor uses Windows console input records and wide output APIs available on
Windows 2000. VT sequences are not required. Basic cooked input is an explicit,
diagnosed fallback rather than a different parser or language path.

## Consequences

- Interactive facilities cannot change batch or embedding semantics.
- Foreground cancellation continues through the one M5/M6 child registry.
- Static/shared hosts may explicitly implement history in M8 but receive no
  process-global store.
- Console and history ownership has one cleanup path and can be tested through
  a native executable driver plus deterministic component state transitions.
