# M6 Design — Embedded Standard Library Boundary

**Status:** Reviewed for implementation

**Date:** 2026-08-23

## Registry and Evaluator Contract

One immutable portable descriptor table contains every canonical command,
signature, summary, flags, and policy classification. It is linked through
`wsh_core`, so executable, static-library, and shared-library builds expose the
same registration. The evaluator recognizes only table entries; unknown
namespaced words remain ordinary external commands.

The evaluator expands operands before library dispatch. It validates the
common `--into name` form, removes it from the borrowed argument list, invokes
the command, then atomically assigns the returned list or writes one record per
element through the existing output boundary. Failed calls do not alter the
destination variable. Diagnostics use stable `WSH-LIB-*` message prefixes.

## Runtime Operation

`WSH_RUNTIME_LIBRARY` carries a canonical command subject, immutable arguments,
the isolated context, and optional typed launch data. Portable text and
introspection semantics remain described by the portable registry; their
concrete execution shares the Windows runtime dispatch with filesystem, path,
process, time, and system commands. That runtime reuses its logical directory,
allocator, limits, resolver, launch registry, capture, wait, cancellation, and
cleanup owners.

## Filesystem and Path Safety

All effectful paths become absolute UTF-16 paths against the runtime logical
directory. Metadata opens do not follow the final reparse point. Enumeration
and recursive copy/removal never descend through a reparse point. Results use
ordinal case-insensitive ordering with an ordinal tiebreak.

Overwrite, recursion, and parent creation are opt-in. Recursive removal rejects
empty and current-directory spellings, volume and share roots, the initial
logical directory, and executable directory before enumeration. Reparse
entries are removed as entries. Cross-volume move copies, verifies, and only
then removes. Temporary objects use an unpredictable process-local name and an
exclusive create operation under the user temporary directory.

## Process, Time, System, and Test State

Process commands translate options into M5 structured commands and plans.
Capture drains bounded pipes; timeouts and cancellation use the existing child
registry. Parallel receives explicit quoted WSH block values, starts isolated
nested WSH hosts in bounded batches, and publishes status in input order. Raw
launch remains subject to the M5 runtime policy option.

UTC time uses system time; monotonic time uses a dynamically available precise
counter with the documented legacy fallback. Version and architecture queries
report observed fields rather than changing semantics. Environment enumeration
returns sorted names only.

Test state belongs to one isolated Windows runtime, which is paired one-to-one
with the command evaluator in the official host. `test::begin` captures
immutable baseline metadata; assertions accumulate bounded failure state;
`test::end` emits one complete record. Duplicate/open IDs, missing finalization,
and malformed required fields fail. Evidence output remains compatible with
the pinned WSP validator and is never accepted as Pass merely because execution
was blocked or incomplete.

## Compatibility and Failure

No standard-library command is derived from `rc`; these are namespaced Windows
extensions and therefore do not alter adopted language semantics. Every API
needed on the oldest x86 baseline is either already imported by M5 or resolved
dynamically with a documented fallback. Allocation, conversion, option, and
limit failures occur before effect where possible. Cleanup never replaces the
primary diagnostic and is idempotent.
