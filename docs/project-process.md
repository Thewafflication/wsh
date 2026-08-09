# Waughtal Shell Project Process

**Document ID:** `WSH-PROC-0001`

**Status:** Proposed

## 1. Purpose

WSH applies the pinned Waughtal Software Process (WSP) to planning,
specification, design, implementation, review, verification, release, support,
and improvement. This document maps the common process to WSH work.

## 2. Work Organization

The [milestone plan](planning/milestones.md) is the product work breakdown. One
milestone is the largest independently closable work package. A milestone may
contain smaller reviewed work items but retains one requirements/evidence exit
gate.

Every milestone uses the [mandatory WSP workflow](planning/prompts/wsp-milestone-workflow.md):
Baseline, Plan, Specify, Design, Implement, Review, Verify, and Close. M0 is
documentation-only and omits production implementation.

## 3. Roles

| Role | Responsibility | Initial assignment |
| --- | --- | --- |
| Product owner | Scope, compatibility, requirements, release approval | Project maintainer |
| Requirements owner | Requirement quality, identifiers, traceability, change impact | Project maintainer |
| Architect | Interfaces, ADRs, platform compatibility, DFS | Project maintainer/contributor |
| Implementer | Traceable source, tests, build, and documentation | Assigned contributor |
| Verifier | Controlled test specifications, execution, evidence, findings | Maintainer or designated reviewer |
| Security owner | DFS, threat/control traceability, vulnerability response | Project maintainer |
| Configuration owner | Baselines, dependencies, artifacts, release identity | Project maintainer |
| Process owner | WSP tailoring, metrics, retrospective, improvement | Project maintainer |

One person may fill multiple roles. Material release findings receive another
reviewer when available. A single-maintainer exception is recorded in release
readiness and does not waive objective verification.

## 4. Specify and Design

Requirements use stable `WSH-REQ-NNNN` identifiers. Controlled tests use
`TC-NNNN`. A requirement is ready for implementation only when its scope,
normative obligation, source/rationale, dependencies, specification allocation,
and planned verification are reviewable.

For every `rc`-derived change, review records answer what `rc` specifies and
whether that behavior is compatible with Windows. Durable decisions use ADRs.
Security-relevant changes update the DFS before approval.

## 5. Change Control

A change to an accepted requirement, grammar, public option, configuration or
registry contract, embedding ABI, supported platform, security control, test
obligation, or release artifact requires impact analysis. The analysis covers
requirements, design, implementation, tests/evidence, compatibility, security,
documentation, schedule/token forecast, and released versions.

Identifiers are never reused. Accepted ADRs are superseded, not rewritten.
Proposed documents may change through M0 review.

## 6. Review and Defects

Each work item identifies review inputs, reviewer, checklist, findings, and
completion conditions. Findings are resolved, approved for deferral, or
accepted as residual risk; they are not silently closed by their author.

Defects record observed/expected behavior, baseline, environment, severity,
priority, status, owner, reproduction, affected requirements, resolution, and
verification. Security findings use access appropriate to their sensitivity.

## 7. Verification and Evidence

CTest is the sole top-level automated dispatcher. Controlled tests follow the
project [test strategy](test-strategy.md). Generated evidence preserves every
execution, including failures and reruns, and supports bidirectional
requirement/test/result traceability.

No status other than Pass satisfies a required release gate. Platform claims
require final-artifact execution on the claimed architecture/OS or an approved
equivalence analysis with residual risk.

## 8. Token and Effort Control

Each milestone starts with the approved token budget and phase allocation in
the milestone plan. Closeout records actual agent tokens, elapsed human/tool
time, artifact size, tests, defects, review findings, and estimation variance.
Forecast use above 120 percent triggers replanning; it never waives scope or
quality gates.

## 9. Release, Support, and Improvement

Release readiness covers artifact identity, requirements, tests, defects,
risks, supported platforms, documentation, provenance, signing, malware scan,
digests, rollback, support, and vulnerability response. The approved release
record identifies exact source and dependency baselines.

M1, M3, M5, M8, and M10 include estimation/process retrospectives. Repeated
defects update review checklists. Generally reusable improvements are proposed
upstream to WSP.
