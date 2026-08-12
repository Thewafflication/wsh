# M2 Plan — Portable Core Library

**Milestone:** M2

**Status:** Approved for implementation

**Baseline:** `b8d1e67b63923703467ae4e857d5e025b1c57e9c`

## Objective and Scope

Implement the portable, side-effect-free foundation shared by `wsh.exe` and
the embedding library: strict source decoding, source positions, UTF
conversion, immutable strings and flat lists, allocator control, diagnostics,
limits, contexts, variables and export metadata, status lists, an abstract
runtime boundary, and a deterministic fake runtime.

Grammar, parsing, evaluation, Win32 effects, real files, real process launch,
and mutation of process-global directory, locale, environment, code page, or
standard handles are out of scope.

## Requirements and Dependencies

M2 directly allocates `WSH-REQ-0007`, `0018`, `0019`, `0024`, `0034`, `0035`,
`0036`, `0037`, `0046`, `0048`, `0049`, `0070`, `0074`, and the hostile-input
controls of `0075`. The accepted language and embedding specifications,
ADR-0002, ADR-0005, architecture sections 2, 4, and 5, and WSH-DFS-0001 are
design inputs.

## Deliverables

- reviewed internal core contract in `include/wsh/core.h`;
- portable C99 implementation with no operating-system headers;
- controlled `TC-NNNN` specifications and CTest-dispatched runners;
- allocation-failure, boundary, Unicode, diagnostic, isolation, concurrency,
  runtime-fake, and leak checks;
- automated bidirectional traceability and TeX evidence validation;
- M0/M1 retrospective logs and this M2 closeout/evidence record.

## Roles

The current contributor fills requirements owner, architect, implementer,
verifier, security owner, and process owner roles. Objective automated results
are used where independent review is unavailable. Material residual findings
remain explicit for maintainer approval.

## Risks and Controls

| Risk | Control |
| --- | --- |
| Integer overflow before allocation | checked addition/multiplication and pre-allocation limits |
| Malformed UTF consumes or aliases data | strict scalar decoder; reject overlong forms, surrogates, NUL, noncharacters, and truncation |
| Partial update on allocation failure | allocate/copy/commit builders and fault sweep |
| Context cross-talk | no mutable globals; per-context allocator, limits, variables, diagnostics, and runtime |
| Windows behavior leaks into core | callback-only runtime interface and deterministic fake implementation |
| Test result loses traceability | one CTest case and one isolated TeX evidence record per controlled TC |
| Historical M1 gaps obscure M2 claim | M2 claim limited to portable core and current-host configurations |

Rollback is deletion of the M2-created core/test/evidence artifacts and
restoration of the M2 starter files; no migration or external state is
modified.

## Budget and Forecast

| Phase | Budget |
| --- | ---: |
| Baseline/plan | 15,000 |
| Specify | 10,000 |
| Design | 15,000 |
| Implement | 40,000 |
| Review | 10,000 |
| Verify/evidence | 17,000 |
| Close | 3,000 |
| **Total** | **110,000** |

Token use is tracked approximately at closeout because the execution
environment does not expose an authoritative per-phase counter. The forecast
at plan approval remains within 110,000 tokens and below the 120 percent
replanning threshold.

## Exit Criteria

M2 exits only when all controlled tests pass through CTest, traceability and
evidence validation pass, strict UTF-8/UTF-16 including supplementary
round-trips pass, line positions pass for every line-ending form, allocation
fault sweeps return to zero outstanding allocations, multiple contexts execute
concurrently without cross-talk, the fake runtime is deterministic, source
review finds no M2 process-global effects, and all findings are resolved or
explicitly retained.
