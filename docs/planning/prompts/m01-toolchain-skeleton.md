# Prompt — M1 Toolchain and Repository Skeleton

**Token budget:** 80,000 (baseline/plan 12k; specify 8k; design 12k;
implement 28k; review 8k; verify 10k; close 2k)

```text
Complete WSH milestone M1 using the mandatory WSP milestone workflow. Work only
from the accepted M0 baseline.

Objective: establish the reproducible WPM/WCRT/CMake/CTest/evidence foundation
without implementing shell language behavior beyond the specified --version
and smoke interfaces.

Pin and inventory WCRT, KerTeX, cv2pdb, compilers, and build tools. Create
reviewed TC specifications before build implementation. Establish architecture
and configuration presets, static and shared target skeletons, generated
version resources, exact WPM-style wsh --version behavior, Debug DWARF/PDB
retention, CTest dispatch, traceability validation, TeX evidence, and CI matrix
shape. Prove the x86 static import/PE baseline is compatible with Windows 2000.

Do not implement a lexer, parser, evaluator, process launcher, standard library,
or interactive editor. Resolve the KerTeX release-PDF issue or record approved
WSP tailoring; a DVI alone is not the release-documentation exit gate.

Exit with clean provision/configure/build/test evidence on initial x86 and x64
hosts, controlled negative evidence tests, reviewed dependency inventory, and
actual token/effort/defect closeout.
```
