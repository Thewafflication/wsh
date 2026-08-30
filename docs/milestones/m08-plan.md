# M8 Work Plan — Embedding SDK

**Milestone:** M8

**Work date:** 2026-08-30

**Inherited baseline:** `60b8061` (`Completed M7`)

**Budget:** 135,000 tokens

## Objective and Scope

Freeze the WSH embedding surface as a stable ABI 1 SDK so the same parser,
evaluator, standard commands, and runtime orchestration can be hosted from C
and from one second language without depending on any internal type. M8 owns
the frozen `wsh/*.h` public header set, the curated exported-symbol list for
`wsh_shared`, context options, streams, values, diagnostics, policy,
cancellation, and synchronous namespaced host-command callbacks, plus a C host
example and one second-language FFI host.

Per ADR-0005, the executable links the static library and the shared
`wshlib.dll` alternative exposes ABI 1 with opaque handles, UTF-8, explicit
allocation, isolated contexts, cancellation, and synchronous host-command
callbacks. Arbitrary native DLL plugins loaded by WSH scripts remain out of
scope for 1.0.

The full M9 operating-system/architecture matrix, hostile-configuration and
security closure, and the M10 release artifacts are out of scope. M8 consumes
the M4–M7 context, evaluator, standard-library registry, Windows runtime, and
the executable-owned interactive boundary without re-opening their semantics.

## Baseline and Assumptions

- M4–M7 supply the isolated context, evaluator, 59-command embedded registry,
  filesystem/process primitives, child ownership, and the executable-owned
  interactive session. Interactive state belongs only to `wsh.exe`.
- The repository already builds `wsh_static` and `wsh_shared` from
  `src/wshlib.c`, defines `WSH_EMBEDDING_ABI 1u`, and installs the public
  `include/wsh/` headers. `wsh_shared` currently links with
  `-Wl,-export-all-symbols`; M8 replaces that with a curated export surface.
- The existing public headers (`core.h`, `wsh.h`, `evaluator.h`, `parser.h`,
  `windows_runtime.h`) are the starting point. M8 designates the ABI 1 subset,
  marks internal-only declarations, and guarantees no public example includes a
  non-public type.
- Process-global directory, environment, console, and locale mutation remain
  prohibited in the core library (ADR-0005). Embedding hosts receive no hidden
  profile, history, or console behavior.
- Trusted host callbacks can crash a host and are not a sandbox boundary; this
  is documented, not defended.
- Work remains on `master`, matching current repository practice. The dirty
  `wsp` submodule worktree is user-owned and remains untouched.

## Deliverables

1. A frozen ABI 1 public header set with an explicit version macro, opaque
   context/value/diagnostic handles, UTF-8 string views, explicit allocator and
   limits, result codes, and a documented stability contract.
2. A curated exported-symbol list for `wsh_shared` (no export-all), a generated
   version resource, and verification that static, shared, and executable
   builds expose the same conformance behavior.
3. Context options, streams, values, diagnostics, policy, and cooperative
   cancellation reachable only through the public surface, plus synchronous
   namespaced host-command callbacks.
4. A C host example and one second-language FFI host, each using only public
   types, exercised by an ABI/misuse test suite and a cross-linkage conformance
   suite.
5. Native SDK build/link evidence, deterministic ABI/misuse and conformance
   tests, controlled TeX specification/evidence, source quality, review, and a
   closeout record. The post-M8 roadmap recalibration is recorded.

## Risks and Controls

| Risk | Control |
| --- | --- |
| Export-all leaks internal symbols into ABI 1 | Replace `-Wl,-export-all-symbols` with an explicit export list; verify the exported set against an approved manifest |
| Public example depends on an internal type | A header-inclusion test compiles each example against the installed public headers only |
| Static and shared hosts diverge in behavior | One conformance suite runs against the static archive, the shared DLL, and the executable and compares results |
| Caller misuse (null handle, wrong order, double free) corrupts host | ABI functions validate handles and ordering and return `WSH_ERR_INVALID`/`WSH_ERR_MISMATCH` without crashing |
| Cancellation races leave work running | Cancellation is a cooperative flag checked at bounded points; tests prove a cancelled call returns promptly |
| Host callback mutates process-global state | Callbacks operate on context-scoped state only; process-global mutation stays prohibited and is tested |
| Encoding corruption at the FFI boundary | UTF-8 in and out with validated views; round-trip tests cover invalid input behavior |
| ABI drift breaks a shipped host | A single `WSH_EMBEDDING_ABI` value gates the surface; SONAME/version resource track it and a test asserts the reported value |
| Second-language host masks a C-only defect | The FFI host reuses the same conformance assertions rather than a reduced subset |

## Roles, Review, Verification, and Rollback

The current contributor fills implementation and review roles under the
single-maintainer exception. Automated ABI/misuse tests, cross-linkage
conformance, exported-symbol verification, header-only example compilation,
CTest, warnings-as-errors, source quality, traceability, evidence validation,
and cross-architecture builds provide objective compensation.

The curated export list and version resource are applied before any host
example is accepted. Public examples are compiled against the installed headers
in isolation so an accidental internal include fails the build. The ABI value
is asserted at runtime so a silent bump cannot ship. If the frozen surface
proves insufficient, the response is a reviewed ADR/requirements amendment, not
an ad hoc symbol addition.

M8 exits only when the ABI/misuse suite passes, static/shared/executable
conformance results match, the exported symbol set and version resources are
verified against the approved manifest, every public example uses no internal
type, and the post-M8 recalibration is recorded.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 16k | 16k |
| Specify | 14k | 14k |
| Design | 22k | 22k |
| Implement | 45k | 45k |
| Review | 14k | 14k |
| Verify/evidence | 21k | 21k |
| Close | 3k | 3k |
| **Total** | **135k** | **135k** |
