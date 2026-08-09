# Prompt — M6 Standard Library and Self-Use

**Token budget:** 160,000 (baseline/plan 20k; specify 16k; design 20k;
implement 58k; review 16k; verify 26k; close 4k)

```text
Complete WSH milestone M6 using the mandatory WSP milestone workflow.

Objective: implement the embedded filesystem, path, text, process, time,
system, and test namespaces needed for real build and verification scripts.

Create controlled tests for every documented command signature, option,
status, output/--into result, overwrite/recursion rule, deterministic ordering,
encoding, timeout, capture, parallel ordering, and evidence field. Give special
security review to root protection, reparse points, UNC/device paths, races,
temporary permissions, raw launch, secrets, and resource bounds.

Keep the portable distribution single-file and make static/shared registration
identical. Author a representative WSH build/test workflow that configures and
builds a project, launches test cases, captures stdout/stderr/status, compares
artifacts, and produces WSP-valid evidence. Retain required bootstrap scripts
until WSH is itself a verified release tool.

Exit with namespace conformance, destructive negative tests, evidence
validation, end-to-end self-use demonstration, and actual token/defect data.
```
