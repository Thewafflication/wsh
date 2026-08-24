# ADR-0009: Embedded Standard-Library Runtime Boundary

**Status:** Accepted

**Date:** 2026-08-23

## Context

M6 needs many namespaced commands, but duplicating path and process ownership
outside the M5 runtime would create inconsistent directory, launch, limit, and
cleanup policy. Implementing registration only in the executable would also
make static and shared hosts observe a different library.

## Decision

The portable core owns one immutable standard-library descriptor table and the
evaluator owns command recognition and exact-list assignment. The official
host's isolated Windows runtime owns effectful library and test-record state.
Operating-system effects cross one `WSH_RUNTIME_LIBRARY` request and are
implemented by the existing Windows runtime owner. Process commands reuse M5
launch plans and the child registry. The table is compiled into `wsh_core` and
therefore shared by executable, static, and shared artifacts.

## Consequences

- Standard-library names and descriptions cannot drift between artifacts.
- Hosts that supply another runtime must implement or explicitly deny the
  library operation; the core never performs an implicit platform effect.
- Windows ownership and legacy fallbacks remain centralized.
- A library failure can use ordinary runtime status and stable diagnostics
  without inventing a second evaluator or process protocol.
