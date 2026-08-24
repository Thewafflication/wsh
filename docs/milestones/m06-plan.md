# M6 Work Plan — Standard Library and Self-Use

**Milestone:** M6

**Work date:** 2026-08-23

**Inherited baseline:** `1187b1e` (`WSH 1.3.3 ARM64 loader fix`)

**Budget:** 160,000 tokens

## Objective and Scope

Implement the accepted filesystem, path, text, process, time, system, and test
namespaces as one embedded standard library. Add protected destructive
operations, deterministic query results, structured process controls, WSP
evidence helpers, introspection, and one representative `.wsh` build/test
workflow. Retain PowerShell bootstrap and evidence wrappers.

M7 interactive history, M8 public host-command registration, configuration and
registry loading, and final M9 cross-version hardening remain out of scope.

## Baseline and Assumptions

- M5 owns Windows paths, child launch, capture, timeout, cancellation, and
  cleanup; M6 extends that boundary instead of creating another launcher.
- The accepted standard-library specification defines every canonical name,
  option, result, and status. M6 changes no `rc` compatibility disposition.
- Queries assign exact lists through `--into`; ordinary output uses the
  existing mediated write operation.
- Destructive tests operate only below test-owned temporary roots.
- Work remains directly on `master` by stakeholder direction.
- The dirty `wsp` submodule is user-owned and remains untouched.

## Deliverables

1. A portable command registry and typed library runtime request.
2. Evaluator dispatch, `--into`, diagnostics, and introspection.
3. Windows filesystem, path, process, clock, system, encoding, and hashing
   primitives with bounded ownership and cleanup.
4. Test-state and evidence helpers plus a representative `.wsh` self-use flow.
5. Controlled specifications, CTest dispatch, traceability, TeX evidence,
   security/source review, and closeout records.

## Risks and Controls

| Risk | Control |
| --- | --- |
| Recursive root deletion | Canonical protected-root checks before traversal |
| Reparse escape | Inspect/delete links themselves; never traverse by default |
| Partial mutation | Explicit overwrite/recursion and itemized failure status |
| Path ambiguity | Wide absolute resolution and architecture-neutral lexical rules |
| Output/encoding exhaustion | Existing finite value/capture limits and strict UTF conversion |
| Process duplication | Reuse M5 launch plans, registry, waits, and cancellation |
| Secret disclosure | Environment enumeration returns names only; stable scoped diagnostics |
| Nondeterminism | Ordinal Windows ordering and input-order parallel results |
| Evidence spoofing | Stateful IDs, required finalization, and WSP validation |
| Legacy incompatibility | Static-import gate and documented optional capability fallback |

## Roles, Review, Verification, and Rollback

The current contributor fills implementation and review roles under the
single-maintainer exception. Objective CTest, warnings-as-errors, source lint,
traceability, WSP evidence validation, destructive negative tests, and native
architecture CI compensate but do not replace later release approval.

CTest is the sole dispatcher. Failures preserve diagnostics and test-owned
artifacts. Library preparation publishes no effect until options, paths, and
limits validate. Partial trees and children use idempotent M5 cleanup. M6 exits
only after namespace conformance, root/reparse negatives, self-use, evidence,
and architecture gates pass with no unresolved milestone defect.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 20k | 20k |
| Specify | 16k | 16k |
| Design | 20k | 20k |
| Implement | 58k | 58k |
| Review | 16k | 16k |
| Verify/evidence | 26k | 26k |
| Close | 4k | 4k |
| **Total** | **160k** | **160k** |
