# ADR-0003: WPM-Provisioned CMake, CTest, and TeX Toolchain

**Status:** Accepted

**Date:** 2026-08-04

## Context

The project requires plain C, Windows targets, CMake, CTest, TeX test results,
and WPM installation of WCRT, KerTeX, and cv2pdb. WSP requires reproducible
test evidence and GDB-compatible Debug symbols.

## Decision

WPM will provision pinned WCRT, KerTeX, and cv2pdb packages. Official portable
artifacts will statically link WCRT. CMake presets will describe architecture
and configuration. CTest will dispatch every automated
test and evidence check. Test wrappers will generate TeX evidence and KerTeX
will compile the assembled report. TinyCC Debug builds will retain DWARF;
cv2pdb will generate an additional PDB where supported.

A milestone-zero spike must prove package discovery, CMake integration,
KerTeX compatibility with the WSP report inputs, and dual-symbol retention.

## Consequences

- Developer and CI provisioning share the WPM dependency path.
- Test reporting is a tested build product rather than a manual afterthought.
- Generated PDBs improve native debugging without weakening WSP evidence.
- Package metadata and KerTeX compatibility are early schedule risks.
