# M9 Work Plan — Compatibility and Security Closure

**Milestone:** M9

**Work date:** 2026-08-30

**Inherited baseline:** `650c8de` (`Close out M8 embedding SDK milestone`)

**Budget:** 205,000 tokens

## Objective and Scope

Close the WSH 1.0 compatibility matrix and the accepted Design-for-Security
(WSH-DFS-0001) so every claimed operating system, architecture, and threat
control has applicable evidence or an approved residual risk. M9 owns the full
OS/architecture verification matrix, old-Windows-API fallbacks, long / UNC /
device path handling, hostile configuration, registry, history, environment,
named-pipe, and reparse-point inputs, resource-exhaustion bounds, input
fuzzing, child containment, dependency assessment, and the DFS residual-risk
closure.

M9 does not add language features, standard commands, interactive behavior, or
embedding surface beyond what M4–M8 accepted; a compatibility or security fix
that needs a contract change requires a reviewed ADR/requirements amendment.
Release packaging, signing, and the release candidate itself are M10.

## Baseline and Assumptions

- M2–M8 supply the portable core, parser, evaluator, Windows runtime, embedded
  standard library, interactive session, and the embedding SDK, each with
  passing controlled tests and evidence.
- The accepted DFS (WSH-DFS-0001) enumerates assets, the trust model, threats,
  and controls; M9 verifies each control or records an approved residual risk.
- WSH executes user-selected code with the user's authority and is not a
  sandbox; hardening targets accidental leakage, unbounded resource use, and
  injection, not privilege containment.
- The review host is x64 Windows. Native Windows 2000, native ARM64, and any
  hostile-host execution claims require the corresponding environment; results
  produced elsewhere are recorded, not inferred locally.
- The carried-forward M1 dependency, x86-oldest-host, PE/import, DWARF, and
  release-PDF evidence gaps are closed under M9 before a release claim depends
  on them.
- Work remains on `master`. The dirty `wsp` submodule worktree is user-owned
  and remains untouched.

## Deliverables

1. A verification matrix that maps every claimed OS (including the Windows 2000
   ceiling) and architecture (x86, x64, ARM64) to applicable, retained
   evidence, with old-API fallbacks exercised or explicitly bounded.
2. Path-boundary handling and tests for long, UNC, device, and reparse-point
   paths, with a defined refusal or normalization for each unsupported form.
3. Hostile-input hardening and negative tests for configuration, registry,
   history, environment, private WSH envelope, named-pipe peers, and external
   output, plus resource-exhaustion bounds for parsing, expansion, globbing,
   output, and process creation.
4. An input fuzzing harness for the lexer, parser, and evaluator with a triage
   and regression path, and child-containment verification under interruption
   and abnormal exit.
5. A DFS control-verification record: every threat has a passed control test or
   an approved residual risk; a dependency assessment; and the closed
   historical evidence gaps. Controlled tests, TeX evidence, review, and a
   closeout complete the milestone.

## Risks and Controls

| Risk | Control |
| --- | --- |
| A claimed platform lacks real execution evidence | Gate each matrix entry on retained evidence from that environment; never promote an inferred result to Pass |
| Long/UNC/device paths bypass normalization or refusal | Add explicit path-classification tests with defined behavior for each unsupported form |
| Hostile configuration/registry/history enables persistent or bypass execution | Bounded inert parsers, explicit refusal, and negative tests for each untrusted source |
| Environment or credential leakage to children, logs, or history | Verify the private envelope boundary and the documented scalar-join policy with negative tests |
| Unbounded parsing, expansion, globbing, output, or process creation | Assert configured ceilings with resource-exhaustion tests |
| Fuzzing finds an unhandled input | Triage crashes, add a regression case, and fix within the accepted grammar and bounds |
| Orphaned or uncontained children after abnormal exit | Job-object and interrupt tests confirm cleanup on every exit path |
| A dependency introduces an unassessed risk | Record exact pinned versions/digests and assess each against the DFS |
| Security closure is claimed without traceable evidence | Require a DFS threat-to-control map with passed verification or an approved residual risk |

## Roles, Review, Verification, and Rollback

The current contributor fills implementation and review roles under the
single-maintainer exception. Automated matrix builds, negative and
resource-exhaustion tests, fuzzing, traceability, evidence validation,
warnings-as-errors, source quality, and cross-architecture builds provide
objective compensation.

Each control is verified before its threat is marked closed; an unverifiable
control becomes an approved, documented residual risk rather than an assumed
pass. Platform claims without applicable environment evidence remain explicit
gaps rather than local inferences. If hardening requires a contract change, the
response is a reviewed ADR/requirements amendment, not an ad hoc behavior shift.

M9 exits only when every applicable matrix entry has evidence, every DFS threat
has a passed control verification or an approved residual risk, no critical
defect remains, and the release-support limitations are explicit.

## Phase Forecast

| Phase | Budget | Forecast |
| --- | ---: | ---: |
| Baseline/plan | 25k | 25k |
| Specify | 15k | 15k |
| Design | 25k | 25k |
| Implement | 60k | 60k |
| Review | 27k | 27k |
| Verify/evidence | 48k | 48k |
| Close | 5k | 5k |
| **Total** | **205k** | **205k** |
