# M4 Design — Bounded Semantic Evaluator

**Status:** Reviewed for implementation

**Date:** 2026-08-22

## Boundary and Ownership

`wsh_evaluator` borrows one isolated `wsh_context` and copies evaluator
options. A call accepts only a complete immutable parse tree and publishes one
owned nonempty status list. ASTs remain M3-owned. Persistent function bodies
are deep-copied into evaluator-owned immutable records so an interactive parse
tree may be destroyed after evaluation.

All values, statuses, scope snapshots, functions, and captures use the
configured allocator. Each operation prepares replacement state before
commit. On resource, expansion, substitution, or runtime failure, an affected
simple command makes no runtime request and publishes no partial assignment.

## Evaluation Model

Values remain ordered flat lists of strict UTF-8 strings. Empty list and one
empty string remain distinct. Quoted words produce exactly one element;
parenthesized values flatten; variable subscripts are one-origin; `$#name`
produces a decimal count; `$^name` joins using one ASCII space. Explicit and
free carets share one cardinality rule: pairwise for equal lengths, singleton
distribution on either side, empty for two empty operands, and an error for
other unequal cardinalities.

Unquoted wildcard-bearing words request candidates from the abstract runtime,
then use the WSH matcher and deterministic ASCII-folded/byte fallback sorting.
Quoted words never glob. Command substitution evaluates in a capture frame,
splits successful UTF-8 output on the Unicode scalar set in `$ifs`, and aborts
the containing command if substitution fails.

## State, Scope, and Functions

Ordinary assignment is dynamic. `local` records the prior exact binding in the
current braced, function, or sourced scope and restores it at scope exit.
Function calls install `$*` arguments and restore the prior value on every
exit. Subshell evaluation snapshots all variables, export metadata, and
functions, evaluates against that state, then restores the caller while
retaining the resulting status. `source` uses the caller state with a local
scope; explicit `.wsh` execution remains an abstract external launch.

New variables are private. `export` changes metadata only after collision
checking through the context's Windows-name comparator. `unexport` and
`unset` use exact private-name identity.

## Status and Control

Every evaluated command produces a nonempty unsigned status list and mirrors
it into `$status` as decimal strings. Success means all elements are zero.
`&&`, `||`, and `!` use that rule. `if`, `while`, `for`, and `switch` consume
the same rule and matcher. `break`, `continue`, and `return` are internal
control signals checked against loop/function depth and never cross the public
API. Function removal uses `fn name` without a body.

## Abstract Effects

External simple commands request `WSH_RUNTIME_LAUNCH` with a subject and
structured arguments. `echo` requests `WSH_RUNTIME_WRITE`. `source` requests
`WSH_RUNTIME_READ_SOURCE`; globbing requests `WSH_RUNTIME_MATCH_PATHS`.
Pipeline, redirection, background, and process-substitution ASTs are rejected
as M5-owned semantics before any request. The fake runtime can require exact
arguments as well as operation and subject.

## Limits and Diagnostics

Evaluation steps, recursion depth, list length, string size, variables,
diagnostics, and runtime calls are bounded. Default evaluator limits are
conservative and callers can only provide finite nonzero values. Failures add
a source-named structured diagnostic with the responsible AST span when one is
available. Unsupported M5 nodes are explicit evaluation errors, never silent
success.

## Compatibility and Security Review

The evaluator uses portable C99 and no operating-system API. Unicode handling
is byte-stable over already validated UTF-8; syntax identifiers remain ASCII
as fixed by M3. This design closes the M4 portions of `WSH-DFS-0001`: bounded
hostile expansion and recursion, no-effect failed substitution, isolated
subshell state, explicit effect mediation, and fault-atomic teardown. No new
durable architecture decision is introduced; ADR-0002 remains controlling.
