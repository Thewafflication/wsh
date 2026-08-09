# Prompt Component — Mandatory WSP Milestone Workflow

Use this workflow with every WSH milestone prompt.

```text
Work only on the named WSH milestone. Treat the approved WSH requirements,
specification, ADRs, DFS, and pinned WSP submodule as authoritative. Preserve
unrelated and user-owned changes.

1. Baseline
   - Inspect repository/submodule status and approved baselines.
   - Read the allocated requirements, specification sections, WSP lifecycle,
     requirements management, project process, test strategy, and applicable
     security/style profiles.
   - Record assumptions, inherited defects, and out-of-scope work.

2. Plan
   - Create a milestone work plan with scope, dependencies, risks, roles,
     deliverables, review, verification, evidence, rollback, and exit criteria.
   - Record the milestone token budget, phase allocation, tokens used to date,
     and forecast at completion. Replan before exceeding 120%.

3. Specify
   - Review every allocated requirement for necessity, atomicity, ambiguity,
     feasibility, and objective verification.
   - Create/review controlled TC-NNNN specifications before implementation.
   - For language behavior, answer: what does rc specify, and is it compatible
     with every targeted Windows version? Update the compatibility disposition.
   - Stop for stakeholder approval if a choice changes product scope or an
     accepted compatibility promise.

4. Design
   - Define interfaces, ownership, limits, failure behavior, compatibility
     fallback, and security controls before implementation.
   - Create or supersede an ADR for a durable new decision. Update the DFS for
     changed assets, entry points, boundaries, threats, controls, or risk.
   - Review the design against the oldest OS and every architecture.

5. Implement
   - Implement only reviewed milestone scope and trace it to requirements.
   - Keep generated outputs out of controlled source locations.
   - Do not hide a missing feature behind a silent fallback or later TODO.

6. Review
   - Perform requirements, design, source, test, documentation, dependency,
     compatibility, and security review proportional to risk.
   - Record findings and resolve, defer with approval, or accept risk. Authors
     do not silently close material findings.

7. Verify
   - Run CTest as the top-level dispatcher for every automated gate.
   - Generate and validate required TeX evidence and traceability.
   - Preserve failures and reruns. Do not classify Blocked, Inconclusive, Not
     run, or Not applicable as Pass.
   - Execute every applicable OS/architecture configuration or document an
     approved equivalence class and residual risk.

8. Close
   - Confirm every exit criterion and identify remaining defects/risks.
   - Record actual token use by phase, elapsed effort, size, tests, defects,
     review findings, estimation variance, and process improvements.
   - Update the project plan and hand off a concise evidence-backed status.

Do not mark the milestone complete because the token budget is nearly used.
Completion means every required artifact, review, verification, and evidence
gate is satisfied.
```
