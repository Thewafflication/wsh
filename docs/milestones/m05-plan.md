# M5 Work Plan — Windows Execution and Composition

**Milestone:** M5

**Work date:** 2026-08-22

**Inherited baseline:** `af0c039` (`WSH 1.2.0 M4 evaluator`)

**Budget:** 180,000 tokens

## Objective and Scope

Implement the concrete Windows runtime for deterministic executable discovery,
structured and raw process launch, exported environments, logical working
directories, ordered descriptors, here documents, pipelines, capture, process
substitution, background jobs, wait, timeout, cancellation, and containment.
The portable evaluator will prepare typed launch plans; only the platform layer
will own Win32 paths, handles, processes, pipes, jobs, and waits.

Configuration parsing, registry loading, the M6 process namespace, and M7
console-event policy remain outside M5. M5 supplies the raw-launch policy and
cancellation hooks those milestones consume.

## Baseline and Assumptions

- M4 structured words and status lists are the only semantic input to launch.
- The x86 Windows 2000 import baseline controls every static Win32 dependency.
- Optional modern handle-list support is dynamically resolved. The legacy
  fallback inherits only WSH-created handles and requires hosts not to expose
  unrelated inheritable handles during a launch.
- Plan 9 `rc` behavior is retained only where the accepted compatibility table
  marks it adopted; named pipes, numeric statuses, and jobs are Windows
  adaptations.
- The stakeholder requires all work directly on `master`; this overrides the
  general milestone-branch convention.
- The dirty `wsp` submodule worktree is user-owned and remains untouched.

## Deliverables

1. Typed portable launch-plan and orchestration request contracts.
2. Isolated `src/platform/windows/` runtime with resolution, serialization,
   environment, directory, descriptor, process, pipe, job, and cleanup logic.
3. Executable integration for `-c`, script, standard input, structured child
   execution, source, globbing, capture, and runtime diagnostics.
4. Native helper programs and controlled M5 integration/resource/security
   tests dispatched by CTest.
5. Requirement allocations, controlled specifications, traceability, TeX
   evidence, source quality, compatibility/DFS review, and closeout records.

## Roles and Review

The current contributor fills requirements, architecture, implementation,
verification, security, and configuration roles. Automated warnings-as-errors,
controlled native tests, handle/process observations, traceability, evidence,
and cross-configuration results compensate for unavailable independent review
but do not constitute later release approval.

## Risks and Controls

| Risk | Control |
| --- | --- |
| Argument injection | Separate application name and reviewed deterministic serializer |
| Executable planting | Exact documented resolution decision table and safe-path mode |
| Environment ambiguity | Validated explicit sorted UTF-16 block and collision rejection |
| Handle leakage | Modern explicit handle list; serialized legacy inheritance fallback |
| Partial pipeline launch | Create suspended, assign containment, then resume all or cancel/collect |
| Pipe deadlock | Start every pipeline stage before waiting and close parent copies promptly |
| Orphan descendants | Job object where assignable; tracked-process fallback and bounded cleanup |
| Named-pipe racing | Default-token DACL, local-only unique first-instance names, bounded ownership |
| Resource exhaustion | Finite commands, descriptors, children, capture, command-line, and wait limits |
| Old API imports | Dynamic capability lookup and static import inspection |

## Verification and Evidence

CTest remains the sole dispatcher. Controlled tests cover generated argument
partitions, resolution decisions, hostile environments, actual child launches,
redirection ordering, here bytes, pipeline concurrency/status order, process
substitution, background collection, timeout/cancellation, partial-launch
cleanup, inherited-handle sentinels, and old/new capability paths. Executed
x86/x64 Debug and Release configurations must agree. ARM64 is cross-built and
is not called executed without a native runner.

## Rollback and Exit

Every platform owner has idempotent teardown; a preparation failure has no
effect, and a partial launch is cancelled and collected. M5 exits only when all
allocated tests and quality/evidence gates pass, no known WSH handle or live
descendant remains, stage statuses retain left-to-right order, the legacy
fallback analysis is approved, and inspection proves no implicit interpreter,
association, App Paths, or `PATHEXT` route.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 22k | 20k |
| Specify | 18k | 18k |
| Design | 25k | 24k |
| Implement | 65k | 64k |
| Review | 18k | 18k |
| Verify/evidence | 28k | 28k |
| Close | 4k | 4k |
| **Total** | **180k** | **176k** |
