# M8 Allocation — WSH-REQ-0071

An embedding host can register and unregister an exact namespaced synchronous
command. Its callback receives an isolated context, structured arguments,
bounded output and status builders, and borrowed host state. Duplicate,
non-namespaced, failed, and reentrant use has defined behavior.

**Verification:** `TC-0100`, `TC-0103`
