# Prompt — M2 Portable Core Library

**Token budget:** 110,000 (baseline/plan 15k; specify 10k; design 15k;
implement 40k; review 10k; verify 17k; close 3k)

```text
Complete WSH milestone M2 using the mandatory WSP milestone workflow.

Objective: implement the portable, side-effect-free foundation shared by the
executable and embedding library.

Specify tests first for source buffers, UTF-8/UTF-16 validation, BOM handling,
CRLF/LF/CR positions, supplementary characters, immutable strings and flat
lists, allocation failure, diagnostics, resource limits, context ownership,
variables/export metadata, status lists, and abstract runtime interfaces.
Design explicit ownership and fault-atomic builders; update the DFS for hostile
encoding/size inputs.

Implement no grammar beyond test fixtures and launch no real process. Do not
mutate process-global current directory, locale, environment, code page, or
standard handles. Supply a deterministic fake runtime for later evaluator
tests.

Exit only after boundary, allocation-failure, leak, Unicode, concurrency, and
diagnostic evidence passes through CTest and traceability. Record actual token
use and revise later milestone estimates with observed core complexity.
```
