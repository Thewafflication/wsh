# Prompt — M10 WSH 1.0 Release Candidate

**Token budget:** 120,000 (baseline/plan 18k; specify 10k; design 12k;
implement 25k; review 15k; verify 35k; close 5k)

```text
Complete WSH milestone M10 using the mandatory WSP milestone workflow.

Objective: produce and approve the WSH 1.0 release candidate from the accepted
requirements, specification, architecture, DFS, and verified implementation.

Freeze the requirement/spec/test baseline and exact dependency/source
revisions. Run every required CTest gate on every applicable matrix entry and
retain failures/reruns, binaries, DWARF/PDB symbols, packages, logs, and TeX
evidence. Build and visually verify the single controlled release PDF with
metadata, bookmarks, links, manifest order, checksum, and provenance.

Create portable architecture artifacts, SDK artifacts, version resources,
license/notices, release notes, compatibility/deviation reference, configuration
and registry reference, known limitations, support/vulnerability policy,
SHA256SUMS, provenance attestations, signing/timestamp evidence where selected,
and malware-scan results. Complete the WSP release-readiness record.

Do not call the release verified if any required result is Blocked,
Inconclusive, Not run, Not applicable without approval, or failed. Exit only
after explicit release approval and traceability from every published byte to
the approved baseline. Record actual token use and the project postmortem.
```
