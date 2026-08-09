# Prompt — M0 Specification Baseline

**Token budget:** 60,000 (baseline/plan 12k; specify 22k; design 8k;
review 10k; verify 6k; close 2k)

```text
Complete WSH milestone M0 using the mandatory WSP milestone workflow in this
prompt library. Do not write production code.

Objective: turn the proposed WSH 1.0 documentation into an approved,
internally consistent specification and process baseline.

Review every WSH-REQ-0001 through WSH-REQ-0081 obligation, the complete
specification index, ADR-0001 onward, WSH-DFS-0001, adoption record, project
plan, milestone plan, and verification strategy. For each rc-derived behavior,
verify the cited rc manual/paper behavior and decide whether it is adopted,
Windows-adapted, extended, or excluded. Resolve contradictions and ambiguous
normative language; do not leave TBD/TODO placeholders.

Produce requirement-to-specification and planned-verification traceability,
review records, approved tailoring/residual-risk decisions, and a documentation
manifest plan. Validate links, identifiers, duplicate requirements, grammar
examples, CLI/config/registry precedence, and cross-document terminology.

Exit only when the baseline is ready for explicit stakeholder approval and no
implementation milestone depends on an unanswered product decision. Record
actual token use by WSP phase and estimation variance.
```
