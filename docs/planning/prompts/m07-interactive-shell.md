# Prompt — M7 Interactive Shell

**Token budget:** 150,000 (baseline/plan 18k; specify 15k; design 20k;
implement 55k; review 15k; verify 24k; close 3k)

```text
Complete WSH milestone M7 using the mandatory WSP milestone workflow.

Objective: deliver the specified interactive shell on legacy and current
Windows consoles without changing batch language semantics.

Specify native console tests first for mode detection, startup/profile order,
wide prompts, multiline input, every editing key, supplementary Unicode,
resize/redraw, basic fallback input, deterministic completion and safe quoting,
history JSONL limits/corruption/locking/atomic replacement, Ctrl+C/Ctrl+Break,
sigint, foreground/background jobs, EOF, and recoverable errors.

Use console input/output APIs available on Windows 2000; modern VT behavior may
be an optional equivalent path. Do not require Windows Terminal. Update the DFS
for history disclosure, completion network access, console control events, and
job cleanup.

Exit only with automated native-standard-input evidence plus controlled manual
demonstrations where automation is infeasible, on oldest and current
representatives. Record actual tokens and interactive defect patterns.
```
