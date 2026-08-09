# ADR-0004: One Portable WCRT Binary per Architecture

**Status:** Proposed

**Date:** 2026-08-09

## Context

The product exists to provide stable shell behavior across Windows releases
whose bundled command shells and scripting runtimes differ. Requiring an
installed runtime would recreate that deployment problem.

## Decision

The official x86 release is one unchanged Windows 2000-compatible executable
for all supported x86 environments. x64 and ARM64 each use one unchanged
binary across their architecture's supported Windows range. Every official
executable statically links pinned WCRT and the WSH standard library.

Newer Windows APIs are optional dynamically resolved capabilities. The product
does not require UCRT, Visual C++ redistributables, .NET, PowerShell, `cmd`, or
installation.

## Consequences

- The minimum OS constrains the PE and static import baseline.
- Compatibility fallbacks require direct testing on old and new systems.
- A copied executable is sufficient for core interactive and batch use.
- Modern mitigations unavailable on old Windows require explicit residual-risk
  review rather than silently narrowing the compatibility claim.
