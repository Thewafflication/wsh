# M2 Design — Portable Core Library

**Milestone:** M2

**Status:** Reviewed

## Interfaces and Ownership

The core exposes length-delimited UTF-8 views and opaque owned objects. Every
owned object copies its allocator callbacks and is destroyed through the
matching core release function. Borrowed views remain valid only while their
owner remains unchanged and alive.

Builders are mutable, unpublished objects. `finish` transfers the complete
allocation graph into one immutable object. A failed append or finish leaves
the builder's published element count and prior contents unchanged. Context
updates allocate replacements before committing pointer or metadata changes.

The default allocator is process-heap-backed through the C runtime. Tests can
provide an allocator with deterministic failure and allocation accounting.
The core never assumes that an allocation can be resized in place.

## Source and Unicode

Source construction applies the configured raw and decoded byte limit before
committing an object. It recognizes only a leading UTF-8, UTF-16LE, or
UTF-16BE BOM. BOM-less input is UTF-8. The strict scalar decoder rejects:

- malformed or truncated sequences;
- overlong UTF-8;
- UTF-16 unpaired surrogates;
- UTF-8 encodings of surrogate values or values above U+10FFFF;
- U+0000; and
- Unicode noncharacters.

CRLF, LF, and lone CR become one internal LF scalar. A position map retains
the original byte offset, normalized UTF-8 byte offset, scalar offset,
one-based line, scalar column, and tab-expanded display column at every scalar
boundary. Supplementary characters count as one scalar and one scalar column.

UTF-8/UTF-16 conversion produces explicit-length buffers with a convenience
zero terminator outside the returned length. The input contract still rejects
U+0000, so the terminator cannot alias content.

## Values, Variables, and Status

A string owns one validated immutable UTF-8 byte sequence. A value owns an
ordered flat array of strings; nesting is impossible in the type system.
Context variables own deep value clones and exact case-sensitive shell names.
New assignments are private. Imports begin exported.

Before export, the context rejects a second exported name that the runtime
reports equal under Windows ordinal case-insensitive comparison. The portable
fallback covers ASCII case only; the Windows runtime milestone must provide
the full operating-system comparator before non-ASCII environment export is a
release claim.

A status list owns unsigned 32-bit elements in pipeline order. It succeeds
only when it is nonempty and every element is zero. The last-element accessor
separately reports whether an element exists.

## Diagnostics and Limits

Diagnostics are structured immutable records containing severity, stable code,
message, optional source name, and optional source span. A context owns a
bounded FIFO queue. When the limit is reached, insertion fails with a resource
result without allocating or replacing prior diagnostics.

Limits cover raw/decoded source bytes, one string, list items, variables,
diagnostics, runtime expectations, and runtime calls. Arithmetic is checked
before allocation. Limit failures have the same commit-or-discard semantics as
allocator failures.

## Runtime Boundary and Fake

The core runtime is a pair of callbacks: an operation callback receiving a
typed operation, subject, immutable argument value, output builder, and status
builder; and an environment-name equality callback. The portable core knows
operation categories but includes no filesystem, process, console, registry,
clock, or Windows header.

The deterministic fake runtime owns an ordered expectation queue. Each call
must match the next operation and subject, then clones scripted output and
statuses through the caller's builders. Missing, extra, or mismatched calls
return a stable mismatch result. No fake call performs an external effect.

## Concurrency and Failure

One context is single-thread-at-a-time. Different contexts share no mutable
core state and may run on different threads. Allocator and runtime callback
thread safety is the host's responsibility when shared. Destruction is
idempotent for pointer-owning helper APIs that accept null, but use after
destruction is outside the contract.

No new ADR is required: the design implements the accepted boundaries in
ADR-0002 and ADR-0005 without changing a durable product decision.

## DFS Update

The DFS threat/control table remains structurally current. For the malicious
source and resource-exhaustion threats, M2 supplies strict pre-allocation
decoding, bounded diagnostics/collections, checked arithmetic, immutable
values, and fault-atomic cleanup. Windows effects, policy, handles, and child
containment remain allocated to later milestones and are not simulated as
M2 passes.
