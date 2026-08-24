# Waughtal Shell 1.0 Implementation Milestones

**Document ID:** `WSH-PLAN-MILESTONES-0001`

**Status:** Accepted

## 1. Planning Model

Each milestone is an independently reviewed WSP work package. It repeats the
applicable lifecycle activities:

```text
Baseline -> Plan -> Specify -> Design -> Implement -> Review -> Verify -> Close
```

No implementation begins until the milestone's allocated requirements,
interfaces, risks, and controlled test specifications are reviewable. No
milestone exits on code completion alone; its CTest results, traceability, TeX
evidence, documentation, and unresolved findings must satisfy its exit gate.

Token budgets are estimated Codex working tokens for analysis, tool results,
implementation, review, and verification. They are planning controls, not a
reason to waive an exit criterion. Actual use shall be recorded in the
milestone closeout. A forecast overrun greater than 20 percent requires
replanning scope or budget before continuing.

## 2. Roadmap and Token Budget

| Milestone | Outcome | Dependencies | Token budget |
| --- | --- | --- | ---: |
| M0 | Accepted specification and WSP baseline | None | 60,000 |
| M1 | Reproducible toolchain, repository, and smoke-test skeleton | M0 | 80,000 |
| M2 | Portable core library: source, Unicode, values, diagnostics, context | M1 | 110,000 |
| M3 | Complete lexer, parser, AST, and grammar conformance | M2 | 150,000 |
| M4 | Evaluator, variables, expansion, functions, and control flow | M3 | 160,000 |
| M5 | Windows processes, arguments, redirection, pipelines, and jobs | M4 | 180,000 |
| M6 | Embedded standard library and WSH-authored build/test orchestration | M5 | 160,000 |
| M7 | Interactive console, editor, history, completion, and interruption | M5, M6 | 150,000 |
| M8 | Stable embedding ABI, static SDK, DLL, and second-language host | M4--M7 | 135,000 |
| M9 | Cross-version/architecture hardening and security closure | M1--M8 | 205,000 |
| M10 | 1.0 release candidate, evidence, documentation, and trust | M9 | 120,000 |
| **Total** | | | **1,510,000** |

Budgets should be recalibrated after M1, M3, M5, and M8 using actual token,
time, size, and defect data. A milestone may be divided into approved child
work packages, but the parent exit gate remains intact.

## 3. Phase Allocation

The recommended allocation is a starting forecast:

| Milestone | Baseline/plan | Specify | Design | Implement | Review | Verify/evidence | Close | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| M0 | 12k | 22k | 8k | 0 | 10k | 6k | 2k | 60k |
| M1 | 12k | 8k | 12k | 28k | 8k | 10k | 2k | 80k |
| M2 | 15k | 10k | 15k | 40k | 10k | 17k | 3k | 110k |
| M3 | 17k | 15k | 20k | 55k | 15k | 25k | 3k | 150k |
| M4 | 18k | 15k | 22k | 60k | 15k | 27k | 3k | 160k |
| M5 | 22k | 18k | 25k | 65k | 18k | 28k | 4k | 180k |
| M6 | 20k | 16k | 20k | 58k | 16k | 26k | 4k | 160k |
| M7 | 18k | 15k | 20k | 55k | 15k | 24k | 3k | 150k |
| M8 | 16k | 14k | 22k | 45k | 14k | 21k | 3k | 135k |
| M9 | 25k | 15k | 25k | 60k | 27k | 48k | 5k | 205k |
| M10 | 18k | 10k | 12k | 25k | 15k | 35k | 5k | 120k |

## 4. Milestone Definitions

### M0 — Specification and Process Baseline

**Scope:** Review and disposition the complete specification, 81 product
requirements, ADRs, DFS, WSP adoption record, verification strategy, support
matrix, configuration, registry contract, and milestone plan. Resolve every
normative contradiction and every 1.0 `rc` compatibility row.

**Exit:** Accepted documents; approved WSP tailoring; complete requirement-to-
specification mapping; planned test families; no `TBD`, ambiguous normative
term, or unapproved residual-risk decision.

### M1 — Toolchain and Repository Skeleton

**Scope:** Pin WCRT/KerTeX/cv2pdb through WPM; prove Windows 2000-compatible
static imports; add CMake presets, CTest/evidence skeleton, version generation,
`wsh --version`, placeholder static/shared library targets, and CI matrices.

**Exit:** Clean provision/configure/build/test on initial x86/x64 hosts; smoke
CTest and negative evidence test; PE/version/import inspection; KerTeX release-
PDF path resolved or formally tailored.

### M2 — Portable Core Library

**Scope:** Source decoding, UTF conversion, spans, diagnostics, allocators,
immutable strings/lists, contexts, variables, abstract runtime interfaces,
limits, and deterministic fake runtime.

**Exit:** Boundary and fault-injection tests pass with no leak; public internal
contracts reviewed; Unicode including supplementary characters round-trips.

**M2 recalibration (2026-08-11):** The completed foundation required roughly
3,400 core contract/implementation lines and 1,300 native test lines. Strict
source maps, allocator ownership, fault-atomic builders, and traceable evidence
showed more downstream interface and negative-test complexity than the initial
parser/evaluator/ABI/security estimates allowed. M3 and M4 each gain 10k
tokens, M8 and M9 each gain 5k, concentrated in implementation, review, and
verification. M5--M7 and M10 remain unchanged pending their scheduled
recalibration points.

### M3 — Lexer, Parser, and AST

**Scope:** Every grammar production, quotation, comments, line endings, free
carets, incomplete-input detection, immutable AST, pretty inspection, malformed
input, and parser resource limits. No evaluation or process launch.

**Exit:** Grammar conformance/negative/fuzz corpus passes; every syntax clause
maps to tests; parser cannot invoke runtime effects.

### M4 — Evaluator and Language Semantics

**Scope:** Lists, assignments, scope, explicit export metadata, expansion,
subscripts, caret, patterns, command substitution against fake runtime,
functions, blocks, conditionals, loops, switch, source, status, and control
transfer.

**Exit:** Deterministic semantic suite passes without real child processes;
`rc` adopted/adapted cases are traced; failure atomicity and resource limits
are demonstrated.

### M5 — Windows Execution and Composition

**Scope:** Resolution, exact serializer, environment blocks/envelope,
`CreateProcessW`, raw launch, working directories, handles, ordered redirection,
here documents, pipelines, named-pipe process substitution, background jobs,
timeouts, and cancellation.

**Exit:** Argument matrix and representative runtimes pass; no inherited-handle
leak; stage statuses are ordered; old/new Windows job fallbacks have evidence;
no implicit interpreter or association path exists.

### M6 — Standard Library and Self-Use

**Scope:** Filesystem, path, text, process, time, system, and test namespaces;
protected destructive operations; WSP evidence helpers; migrate representative
build and test orchestration to `.wsh` without removing required bootstrap
PowerShell prematurely.

**Exit:** Namespace conformance passes; reparse/root negative tests pass; a
representative project configures, builds, launches tests, captures results,
and generates evidence using WSH.

### M7 — Interactive Shell

**Scope:** Legacy console input/output, editor, prompts, multiline parsing,
history JSONL, completion, Ctrl+C/Ctrl+Break, background-job exit behavior,
resize/redraw, and basic fallback input.

**Exit:** Native console tests pass on oldest and current representatives;
Unicode is lossless; terminal feature differences do not alter language
semantics; interruption leaves no known child.

### M8 — Embedding SDK

**Scope:** Freeze ABI 1 header, static library, shared DLL/import library,
context options, streams, values, diagnostics, policy, cancellation,
synchronous host commands, C host, and one second-language FFI host.

**Exit:** ABI/misuse tests pass; static/shared/executable conformance results
match; exported symbols and version resources are verified; public examples
use no internal type.

### M9 — Compatibility and Security Closure

**Scope:** Full OS/architecture matrix; old-API fallbacks; long/UNC/device
paths; hostile configuration, registry, history, environment, named pipes,
reparse points, resource exhaustion, fuzzing, child containment, dependency
assessment, and DFS residual risks.

**Exit:** Every claimed matrix entry has applicable evidence; every DFS threat
has a passed control verification or approved residual risk; no critical
defect; release-support limitations are explicit.

### M10 — WSH 1.0 Release Candidate

**Scope:** Freeze requirements/specification, execute all release gates,
assemble one release PDF, packages and portable binaries, SDK, symbols,
checksums, provenance, signatures, malware results, release notes, support and
vulnerability policy, and release-readiness record.

**Exit:** Every applicable requirement is verified; every required matrix job
passes; release artifacts trace to the approved source/dependency baseline;
the release receives explicit approval.

## 5. Milestone Status Record

| Milestone | Status | Evidence |
| --- | --- | --- |
| M0 | Accepted at `3ea772b` | `docs/milestones/m00-work-log.md` |
| M1 | Historically complete at `b8d1e67`; evidence gaps retained | `docs/milestones/m01-work-log.md` |
| M2 | Complete on 2026-08-11; Windows CI added | `docs/milestones/m02-closeout.md`, `docs/milestones/m02-ci-log.md` |
| M3 | Complete on 2026-08-12 | `docs/milestones/m03-closeout.md`, `docs/milestones/m03-work-log.md` |
| M4 | Complete on 2026-08-22 | `docs/milestones/m04-closeout.md`, `docs/milestones/m04-work-log.md` |
| M5 | Complete on 2026-08-22 | `docs/milestones/m05-closeout.md`, `docs/milestones/m05-work-log.md` |
| M6 | Complete on 2026-08-23 | `docs/milestones/m06-closeout.md`, `docs/milestones/m06-work-log.md` |

**M3 recalibration (2026-08-12):** The 132k reconstructed M3 estimate is 12
percent below its 150k control budget. Parser failure atomicity, source
equivalence, and controlled evidence matched the recalibrated complexity
forecast, so M4--M10 budgets and the 1,510,000-token roadmap total remain
unchanged. Recalibration will run again after M5 as planned.

**M4 checkpoint (2026-08-22):** The reconstructed M4 estimate remains within
its accepted 160k control budget. The roadmap stays at 1,510,000 tokens and
the scheduled recalibration remains after M5.

**M5 recalibration (2026-08-22):** The reconstructed M5 estimate is 176k,
2.2 percent below its accepted 180k budget. Native-runtime implementation and
negative verification matched the phase forecast. No downstream estimate has
crossed its replanning threshold, so the roadmap remains 1,510,000 tokens.
The next scheduled recalibration remains after M8.

## 6. Cross-Milestone Controls

- Preserve user-owned work and use milestone-specific branches.
- Do not accept implementation that creates an undocumented language behavior.
- Ask first: what does `rc` specify, and is it compatible with Windows?
- Update requirements, ADRs, DFS, tests, examples, and compatibility matrix
  together when a decision changes.
- Use controlled defects for failed gates; never overwrite failure evidence.
- Record planned/actual tokens, elapsed time, changed size, test count, defects,
  review findings, and residual follow-up at closeout.
