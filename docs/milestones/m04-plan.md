# M4 Work Plan — Evaluator and Language Semantics

**Milestone:** M4

**Work date:** 2026-08-22

**Inherited baseline:** `02d96a4` (`WSH 1.1.0 interactive input`)

**Budget:** 160,000 tokens after the accepted M2/M3 recalibration

## Objective and Scope

Evaluate complete immutable M3 ASTs against an injected deterministic runtime.
M4 owns flat-list expansion, variables and scopes, functions, status logic,
patterns, command substitution, control flow, source, eval, bounded recursion,
and failure-atomic evaluation. It also connects the evaluator to the existing
interactive and redirected-input front end.

Real Windows process creation, descriptor manipulation, pipelines, background
jobs, process substitution, environment publication, path search, registry,
filesystem access, and console policy remain M5 or later work. M4 may request
these operations only through the M2 abstract runtime.

## Baseline and Assumptions

- Only a complete M3 parse tree can enter evaluation.
- Every effect is an explicit abstract-runtime request; tests use the fake
  runtime and cannot perform operating-system effects.
- Plan 9 `rc` list and status behavior is retained except where the accepted
  Windows compatibility record says otherwise.
- The dirty `wsp` submodule worktree is user-owned and shall not be modified,
  staged, or committed.
- The original M4 prompt estimate of 150k is superseded by the accepted 160k
  milestone allocation recorded at M3 closeout.

## Deliverables

1. Public C99 evaluator contract and bounded implementation.
2. Deterministic expansion, scope, function, control, substitution, pattern,
   source, eval, and status behavior.
3. Fake-runtime argument matching and filesystem-match operation.
4. Front-end evaluation integration without direct Windows effects.
5. Fifteen controlled test specifications, implementations, evidence gates,
   traceability, source-quality checks, and cross-configuration results.
6. M4 design, review, work log, closeout, DFS, compatibility, and roadmap
   updates.

## Risks and Controls

| Risk | Control |
| --- | --- |
| Expansion explosion | Per-value, byte, step, depth, and runtime-call ceilings |
| Partial command effects | Expand and validate a command before applying assignments or requesting effects |
| Scope leaks | Snapshot local bindings and restore them on every normal/error/control-transfer path |
| Failed substitution launches outer command | Treat failed substitution as a pre-effect evaluation failure |
| Infinite loop or recursion | Deterministic step and evaluation-depth ceilings |
| Hidden host behavior | Evaluator accepts only context, AST, allocator, limits, and logical source name |
| Pattern nondeterminism | Runtime supplies candidates; evaluator applies matching and stable byte-defined ordering |
| Ownership failure | Allocate-copy-commit plus ordinal allocator-failure sweep |

## Verification and Evidence

CTest remains the top-level dispatcher. The M4 suite covers canonical semantic
partitions, fake-runtime request boundaries, no-effect failures, fault-atomic
ownership, hostile limits, traceability, source quality, and TeX evidence.
Every supported local preset shall build with warnings as errors and run the
complete inherited and M4 test inventory.

## Exit

M4 exits only when all controlled families pass, evidence validates, all M4
requirements have bidirectional traceability, no unresolved review finding
remains, and no evaluator path can invoke an operating-system effect except
through the injected runtime.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 17k | 15k |
| Specify | 17k | 16k |
| Design | 23k | 21k |
| Implement | 61k | 58k |
| Review | 15k | 14k |
| Verify/evidence | 24k | 23k |
| Close | 3k | 3k |
| **Total** | **160k** | **150k** |
