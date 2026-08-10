# ADR-0005: Context-Based Library and Stable Embedding API

**Status:** Accepted

**Date:** 2026-08-09

## Context

The same commands may need to be tested or hosted from another language. A
monolithic console program would make that reuse unreliable and encourage
tests of a different implementation.

## Decision

Parser, evaluator, standard commands, and runtime orchestration will form a
context-based `wshlib`. The portable executable links it statically. A shared
`wshlib.dll` alternative exposes stable ABI 1 with opaque handles, UTF-8,
explicit allocation, isolated contexts, cancellation, and synchronous
namespaced host-command callbacks.

WSH scripts will not load arbitrary native DLL plugins in 1.0.

## Consequences

- Executable and embedded behavior share one conformance suite.
- Process-global directory, environment, console, and locale mutation are
  prohibited in the core library.
- ABI design and foreign-language smoke testing become 1.0 work.
- Trusted callbacks can crash a host and are not a sandbox boundary.
