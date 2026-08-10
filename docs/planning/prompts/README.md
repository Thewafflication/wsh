# WSH Milestone Prompt Library

**Document ID:** `WSH-PLAN-PROMPTS-0001`

**Status:** Accepted

## Use

Run one milestone prompt at a time. Each prompt applies the mandatory workflow
in [WSP milestone workflow](wsp-milestone-workflow.md). It does not authorize
work from later milestones. Before use, replace any bracketed execution data
and confirm the current approved requirement/specification baseline.

| Prompt | Budget |
| --- | ---: |
| [M0 specification baseline](m00-specification-baseline.md) | 60,000 tokens |
| [M1 toolchain skeleton](m01-toolchain-skeleton.md) | 80,000 tokens |
| [M2 portable core](m02-portable-core.md) | 110,000 tokens |
| [M3 lexer and parser](m03-lexer-parser.md) | 140,000 tokens |
| [M4 evaluator](m04-evaluator.md) | 150,000 tokens |
| [M5 Windows execution](m05-windows-execution.md) | 180,000 tokens |
| [M6 standard library](m06-standard-library.md) | 160,000 tokens |
| [M7 interactive shell](m07-interactive-shell.md) | 150,000 tokens |
| [M8 embedding SDK](m08-embedding-sdk.md) | 130,000 tokens |
| [M9 compatibility/security](m09-compatibility-security.md) | 200,000 tokens |
| [M10 release candidate](m10-release-candidate.md) | 120,000 tokens |

Token budgets include tool output and rework. They do not permit skipping WSP
gates. If a budget forecast exceeds 120 percent, stop at a safe reviewable
checkpoint, preserve evidence, and propose a revised plan.
