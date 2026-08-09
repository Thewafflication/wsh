# Prompt — M5 Windows Execution and Composition

**Token budget:** 180,000 (baseline/plan 22k; specify 18k; design 25k;
implement 65k; review 18k; verify 28k; close 4k)

```text
Complete WSH milestone M5 using the mandatory WSP milestone workflow.

Objective: implement the Windows runtime boundary for exact, cross-version
process execution and command composition.

Specify tests before implementation for resolution order, safe path, exact/
.exe/.com/.wsh behavior, structured serializer edge cases, explicit
lpApplicationName, rawexec, scalar/list environment and case collisions,
nested WSH envelope, logical working directories, descriptor inheritance,
ordered redirection, here documents, pipelines/status order, named-pipe process
substitution, background jobs, wait, timeouts, Ctrl cancellation hooks, job
objects, old-Windows fallback, and cleanup after partial launch.

Test empty/space/quote/trailing-backslash arguments exhaustively with WSH/WCRT
and representative Windows runtimes. Prove no cmd, PowerShell, PATHEXT,
ShellExecute, App Paths, or association fallback. Treat paths, registry,
environment, pipe peers, and child output as hostile under the DFS.

Exit only with native Windows integration/resource/security evidence, no handle
leaks or known live descendants, and approved old/new OS fallback analysis.
Record actual token use and recalibrate remaining milestones.
```
