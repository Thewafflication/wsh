# ADR-0008: Explicit Modern Handles and Serialized Legacy Inheritance

**Status:** Accepted

**Date:** 2026-08-22

## Context

Windows Vista added process-thread attribute handle lists, but WSH also targets
Windows 2000. Passing inheritable handles without coordination can disclose an
unrelated pipe, file, or host handle to a child.

## Decision

WSH dynamically resolves the modern attribute-list APIs and supplies an exact
handle list when available. On older systems it holds a process-wide launch
lock, temporarily enables inheritance only for selected WSH-owned handles,
calls `CreateProcessW`, and clears those flags before releasing the lock. WSH
never creates an inheritable handle except inside this serialized interval.

An embedding host using the legacy fallback shall not keep unrelated handles
inheritable across a WSH launch. Capability metadata and verification evidence
identify which path ran.

## Consequences

- Modern systems receive a kernel-enforced explicit allowlist.
- The release binary retains a Windows 2000-compatible static import table.
- WSH-owned handle leakage is preventable and testable on both paths.
- A legacy embedding host can violate the precondition with its own inheritable
  handles; this is an explicit residual host-boundary risk for M9 review.
