# M7 Work Plan — Interactive Shell

**Milestone:** M7

**Work date:** 2026-08-24

**Inherited baseline:** `07c209d` (`Complete M6 embedded standard library`)

**Budget:** 150,000 tokens

## Objective and Scope

Deliver the accepted native Windows interactive shell without changing batch
language meaning. M7 owns console mode selection and restoration, literal
Unicode prompts, multiline editing, deterministic completion, bounded JSONL
history, Ctrl+C/Ctrl+Break cancellation, signal functions, background-job exit
policy, resize/redraw, and a visible basic-input fallback.

Full inert configuration and registry policy parsing, public host completion
providers, the M8 embedding ABI, and the final M9 operating-system matrix are
out of scope. M7 consumes compiled defaults and the accepted profile locations.

## Baseline and Assumptions

- M6 supplies the isolated context, evaluator, standard-library registry,
  filesystem/process primitives, and child ownership.
- The inherited front end already detects console stdin, uses wide console
  text, accepts multiline input, and recovers from interactive syntax errors.
- Interactive state belongs only to `wsh.exe`; batch, static, and shared hosts
  receive no hidden profile, history, or console behavior.
- Native current-host console automation plus legacy-API inspection is local
  evidence. Windows 2000 execution remains a required M9/release matrix gate.
- Work remains on `master`, matching the current repository practice. The
  dirty `wsp` submodule worktree is user-owned and remains untouched.

## Deliverables

1. An executable-owned editor and console session using input records and
   wide console output, with a bounded basic line-input fallback.
2. Prompt, multiline, scalar-safe editing, completion, resize, and recovery.
3. Inert bounded UTF-8 JSONL history with locking and atomic replacement plus
   the three accepted `history::` commands.
4. Foreground interruption, signal-function dispatch, and background exit/EOF
   policy built on the M5/M6 child registry.
5. Native console driver, deterministic component tests, controlled TeX
   specifications/evidence, source quality, review, and closeout records.

## Risks and Controls

| Risk | Control |
| --- | --- |
| UTF-16 corruption | Cursor and deletion operations move over validated scalar boundaries |
| Console-mode leakage | Snapshot once, restore on every orderly/error cleanup path |
| Hidden code execution | Completion enumerates inert names and never evaluates a provider |
| Network disclosure | Filesystem completion refuses implicit UNC enumeration |
| History disclosure/corruption | Bounded inert parser, warning coalescing, explicit suppression, same-directory atomic replacement |
| Concurrent history loss | One path-derived cross-process mutex disables writes on contention |
| Orphaned foreground child | Console handler sets an atomic request consumed by bounded runtime waits |
| Orphaned background child | Interactive exit confirms or requires force; consecutive EOF escalates predictably |
| Legacy console failure | Windows 2000 input/output APIs only, with reported cooked-line fallback |
| Test false pass | Native input driver distinguishes key records, control events, EOF, and redirected input |

## Roles, Review, Verification, and Rollback

The current contributor fills implementation and review roles under the
single-maintainer exception. Automated state-transition tests, native console
integration, CTest, warnings-as-errors, source quality, traceability, evidence
validation, and cross-architecture builds provide objective compensation.

Console modes and handlers are installed only after validation and are removed
in one cleanup path. History preparation publishes no replacement until the
complete temporary file is flushed. An interrupted process group is collected
before the evaluator runs `sigint`. Failed startup/profile loading reaches no
prompt and restores console state.

M7 exits only when all controlled cases pass, every required record validates,
the native console test proves real input consumption and interruption, no
known child survives interruption/exit, and residual platform claims are
stated without being promoted to Pass.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 18k | 18k |
| Specify | 15k | 15k |
| Design | 20k | 20k |
| Implement | 55k | 55k |
| Review | 15k | 15k |
| Verify/evidence | 24k | 24k |
| Close | 3k | 3k |
| **Total** | **150k** | **150k** |
