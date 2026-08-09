# Prompt — M3 Lexer, Parser, and AST

**Token budget:** 140,000 (baseline/plan 17k; specify 15k; design 20k;
implement 50k; review 15k; verify 20k; close 3k)

```text
Complete WSH milestone M3 using the mandatory WSP milestone workflow.

Objective: implement every accepted lexical and grammar rule as an immutable
AST without runtime side effects.

Before implementation, turn each language-spec grammar and rc-compatibility row
allocated to parsing into reviewed positive, negative, boundary, malformed,
incomplete-input, and resource-limit TC cases. Include UTF source forms,
comments, apostrophe/doubled apostrophe, Windows backslashes, line endings,
free carets, assignments, lists, variables/subscripts, substitution forms,
redirections, pipelines, precedence, blocks, functions, and control flow.

Design lexer/parser ownership, recovery, depth/size limits, source spans, and a
test-only AST inspection format. The parser shall have no filesystem, registry,
environment, console, or process capability.

Exit after grammar conformance, fuzz corpus, deterministic AST, incomplete-
input, failure-atomicity, and leak evidence passes. Update the rc matrix for any
approved clarification and record actual tokens and parser defect patterns.
```
