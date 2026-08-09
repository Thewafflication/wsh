# Prompt — M4 Evaluator and Language Semantics

**Token budget:** 150,000 (baseline/plan 18k; specify 15k; design 20k;
implement 55k; review 15k; verify 24k; close 3k)

```text
Complete WSH milestone M4 using the mandatory WSP milestone workflow.

Objective: implement all language semantics against the deterministic abstract
runtime, without real Windows child-process orchestration.

Specify and verify list construction, empty list/string distinction,
assignments, dynamic/local/function/source/subshell scope, explicit export,
subscripts, count/flatten, caret cardinalities, status truth/reduction,
&&/||/!, patterns/glob ordering through fake files, substitutions, functions,
if/while/for/switch, break/continue/return, source/eval, and error atomicity.

For each behavior, ask what rc does and whether the accepted WSH adaptation is
Windows-compatible. Do not introduce Bourne/PowerShell splitting or quoting.
Update requirements/ADR/DFS before any semantic change.

Exit with deterministic semantic and conformance evidence, hostile expansion
limits, no-effect parse/evaluation failures, and requirement coverage. Record
actual tokens and recalibrate M5--M8 based on evaluator size/defects.
```
