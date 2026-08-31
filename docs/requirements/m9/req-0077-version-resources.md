# M9 Allocation — WSH-REQ-0077

The build injects one WSP-compliant `VERSIONINFO` resource into `wsh.exe` and
`wshlib.dll` after TinyCC linking without changing their import surface. File
and product versions match the CMake project version.

**Verification:** `TC-0077`
