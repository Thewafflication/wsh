# M9 Platform Verification Matrix

**Support contract:** `WSH-SPEC-PLATFORM-0001`

**Status vocabulary:** `Passed`, `Open`, and `Not applicable`. A current-host
pass does not satisfy an oldest-host row.

| Architecture | Representative | Status | Evidence |
| --- | --- | --- | --- |
| x86 | Windows 2000 SP4 minimum | Open | Native or approved-equivalent final-binary execution required |
| x86 | Current Windows under WOW64 | Passed | GitHub Actions run `33347868400`, job `99355598885`, revision `13846295f0da73712f16896fd0d38b1ad3e3390c` |
| x64 | Windows XP Professional x64 / Server 2003 x64 minimum | Open | Native or approved-equivalent final-binary execution required |
| x64 | Current Windows x64 | Passed | GitHub Actions run `33347868400`, job `99355598913`, revision `13846295f0da73712f16896fd0d38b1ad3e3390c` |
| ARM64 | First supported Windows 10 ARM64 | Open | Native oldest-supported ARM64 execution required |
| ARM64 | Current Windows ARM64 | Passed | Native GitHub Actions run `33347868400`, job `99355598938`, revision `13846295f0da73712f16896fd0d38b1ad3e3390c` |

The cited run passed source quality and the complete Debug build/test/package
jobs on x86, x64, and a runner that explicitly verified `RUNNER_ARCH=ARM64`.
It predates the current traceability repair and is a comparison baseline, not
evidence for the changes after that revision.
