# ADR-0002: Separate Parser, Evaluator, and Windows Runtime

**Status:** Accepted

**Date:** 2026-08-04

## Context

A shell combines a language implementation with security-sensitive operating
system orchestration. Mixing parsing, expansion, and process creation makes
semantic testing difficult and increases injection and resource-lifetime risk.

## Decision

The implementation will use a lexer and explicit grammar to construct an AST.
Evaluation will operate on list values and abstract runtime services. Win32
process, console, filesystem, environment, and handle behavior will be isolated
behind interfaces in `src/platform/windows/`.

The core will be a context-based library used by `wsh.exe` and the embedding
ABI. It will not depend on process-global current directory, locale,
environment, console, or standard-stream mutation.

## Consequences

- Lexer, parser, expansion, and evaluation can be unit-tested without child
  processes.
- Windows behavior can be tested with purpose-built helper executables.
- Interfaces must carry explicit allocation, ownership, encoding, and error
  contracts, adding modest up-front design work.
- Static, shared, executable, and foreign-language hosts can run the same
  conformance suite.
