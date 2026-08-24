# M7 Allocation — WSH-REQ-0056

M7 assigns status 130 for cancelled input or foreground work, collects the
child group, and invokes `sigint` only after cleanup.

**Verification:** `TC-0020`, `TC-0056`
