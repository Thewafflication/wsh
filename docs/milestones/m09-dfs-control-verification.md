# M9 DFS Control Verification Record

**Controlled design:** `WSH-DFS-0001`

**Status vocabulary:** `Passed` means cited controlled evidence passed;
`Approved residual` requires an explicit approval record; `Open` is an M9 exit
gap and is never interpreted as a pass.

| Threat | Disposition | Evidence or required closure |
| --- | --- | --- |
| Command injection | Passed | M5 `TC-0011`, `TC-0012`, `TC-0042`, `TC-0044`, `TC-0045` |
| Malicious source | Passed | M2 `TC-0075`; M3 `TC-0023`, `TC-0085`; M9 `TC-0074` |
| Path traversal/deletion | Passed | M6 `TC-0066`, `TC-0075` |
| Executable planting | Passed | M5 `TC-0040`, `TC-0041`, `TC-0042` |
| Environment collision/injection | Passed | M5 `TC-0046`, `TC-0047`, `TC-0048`, `TC-0075` |
| Handle leakage | Passed | M5 `TC-0015`, `TC-0016`, `TC-0024`, `TC-0075` |
| Orphaned process trees | Passed | M5 `TC-0050`, `TC-0075`; M7 `TC-0056` |
| Named-pipe substitution | Passed | M5 `TC-0051`, `TC-0075` |
| Profile/config persistence | Open | Complete hostile INI, registry, portable-config, and batch-profile cases in M9 |
| History disclosure | Passed | M7 `TC-0054`, `TC-0075` |
| ABI memory corruption | Passed | M8 `TC-0100`, `TC-0103`, `TC-0104` |
| Resource exhaustion | Passed | M2--M7 `TC-0074`; M9 `TC-0074` |
| Dependency/release compromise | Open | Complete dependency assessment and obtain the obsolete-Windows trust decision |

## Residual-risk decisions

The six decisions listed in DFS section 10 remain `Open`; no automated result
or current-platform run may approve them. M9 cannot close until each is either
resolved by a control or linked to an owner-approved residual-risk record.
