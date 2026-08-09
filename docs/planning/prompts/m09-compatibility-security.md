# Prompt — M9 Compatibility and Security Closure

**Token budget:** 200,000 (baseline/plan 25k; specify 15k; design 25k;
implement 60k; review 25k; verify 45k; close 5k)

```text
Complete WSH milestone M9 using the mandatory WSP milestone workflow.

Objective: close every supported-platform, compatibility, security, and
resource-risk claim before release candidacy.

Freeze the exact x86/x64/ARM64 OS matrix and equivalence classes. Execute final
statically linked artifacts on oldest, intermediate, and current systems.
Verify static imports, WCRT, registry views, Unicode, consoles, long/UNC/device
paths, filesystems/reparse points, environment, serializer differences, named
pipes, job nesting, cancellation, clocks, and version reporting.

Execute DFS abuse cases: hostile source/config/registry/history/environment,
argument injection, executable planting, raw-policy denial, handle leakage,
pipe impersonation, recursive root deletion, resource exhaustion, fuzzing,
callback misuse, and evidence corruption. Assess pinned dependencies and old-
OS residual risk. Resolve every critical/high finding or obtain explicit risk
approval where release policy permits it.

Exit only when each matrix claim has evidence and every accepted threat maps to
a passed control verification or approved residual risk. Record actual tokens
and final M10 forecast.
```
