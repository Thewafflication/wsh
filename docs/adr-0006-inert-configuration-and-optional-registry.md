# ADR-0006: Inert Configuration and Optional Registry Integration

**Status:** Proposed

**Date:** 2026-08-09

## Context

Cross-version predictability is weakened when startup files execute
implicitly, current directories inject configuration, or architecture-specific
registry views produce different settings.

## Decision

WSH uses a strict, inert UTF-8 INI format for preferences and separate `.wsh`
profiles for executable customization. Batch modes load no profile by default.
Portable adjacent configuration is loaded only by `--portable`.

Registry settings are optional. Product settings use a versioned key, policy
uses the Windows Policies key, and all architectures select one documented
native registry view. Policy can restrict but never broaden capability.

## Consequences

- A standalone executable works with an empty registry and no files.
- Configuration can be validated without executing code.
- Profiles remain powerful and therefore explicit in batch use.
- Installation and file association are package-manager responsibilities, not
  ordinary shell startup behavior.
