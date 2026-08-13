# M3 Work Plan — Lexer, Parser, and AST

**Milestone:** M3

**Work date:** 2026-08-12

**Inherited baseline:** `53a6d95e` (`M2 complete`)

**Budget:** 150,000 tokens after the approved M2 recalibration

## Objective and Scope

Implement the accepted WSH lexical forms and normative grammar as an immutable
AST. M3 includes tokens, comments, quotation, free carets, assignments, lists,
variables and subscripts, substitution forms, redirections and here-document
capture, pipelines, precedence, blocks, functions, control flow, diagnostics,
incomplete-input classification, resource limits, deterministic inspection,
fault-atomic ownership, and a deterministic fuzz corpus.

Evaluation, expansion, filesystem matching, named-pipe creation, descriptor
effects, process launch, environment access, registry access, console access,
and function execution remain outside M3.

## Baseline and Assumptions

- M2 supplies strict decoded `wsh_source` text and byte/scalar/display spans.
- The EBNF in `WSH-SPEC-LANG-0001` section 4 is the normative syntactic
  authority. No accepted grammar or compatibility row is changed by M3.
- Redirection and pipeline order are preserved structurally; M5 owns their
  operating-system behavior.
- Process substitution produces an AST node only; M5 owns access-controlled
  Windows named pipes.
- The dirty `wsp` submodule worktree is user-owned and shall not be modified,
  staged, or committed. Its parent gitlink remains pinned at `2198ccab`.
- M1 historical release-provisioning gaps remain outside M3.

## Deliverables

1. Public portable lexer/parser/AST contract and C99 implementation.
2. Immutable token stream, parse tree, syntax diagnostics, and canonical
   test-only AST formatter.
3. Token, AST-node, parse-depth, and diagnostic ceilings.
4. Fifteen controlled test specifications and bidirectional requirement
   allocations.
5. CTest dispatch, traceability, Doxygen/source-quality, TeX evidence, and
   evidence validation.
6. M3 design, review, closeout, DFS, compatibility, and project-plan records.

## Risks and Controls

| Risk | Control |
| --- | --- |
| Ambiguous incomplete versus malformed input | Explicit tri-state syntax status and state-transition tests |
| Recursive resource exhaustion | Depth guard plus token/node ceilings before allocation |
| Partial AST publication | Tree-owned arena and publish only after a complete parse |
| Here-document text parsed as commands | Lexer captures bodies against exact markers before tokenizing later commands |
| Free-caret ambiguity | Insert logically only between adjacent argument-producing forms using source offsets |
| Grammar drift | Canonical AST tests, rc-row review, traceability, and fuzz determinism |
| Hidden operating-system effects | Parser API accepts only source, allocator, and numeric limits |
| Allocation failure leaks | Ordinal fault sweep through lexer and parser object graphs |

## Roles and Review

The current contributor performs requirements, architecture, implementation,
and verification work. Automated traceability, warnings-as-errors, Doxygen,
fault injection, canonical AST comparisons, and fuzz replay provide objective
checks where an independent reviewer is unavailable. Material findings remain
open until recorded resolution or explicit stakeholder disposition.

## Verification and Evidence

CTest is the top-level dispatcher. Each supported local preset shall build with
warnings as errors and run M2 plus M3 smoke, traceability, source-quality,
controlled, and evidence-validation gates. M3 TeX evidence records the exact
test binary, specification, source-input manifest, architecture,
configuration, toolchain, timestamps, command, output, exit code, and verdict.
Failures are archived before reruns.

## Rollback and Exit

M3 is isolated to portable parser source/header, tests, CMake, and controlled
records. Rollback removes those units and their CMake registration without
changing M2 contracts. Exit requires all 15 controlled families, traceability,
source quality, deterministic AST/fuzz, incomplete input, failure atomicity,
leak, and resource-limit gates to pass with no unresolved M3 finding.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 17k | 14k |
| Specify | 15k | 15k |
| Design | 20k | 18k |
| Implement | 55k | 52k |
| Review | 15k | 13k |
| Verify/evidence | 25k | 24k |
| Close | 3k | 3k |
| **Total** | **150k** | **139k** |

The initial forecast is below the 120-percent replanning threshold. The
execution environment does not expose an authoritative phase token counter;
closeout shall distinguish measured values from reconstruction estimates.
