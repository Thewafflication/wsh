# M7 Allocation — WSH-REQ-0020

M7 returns from interactive foreground interruption only after the tracked
child tree is cancelled and collected, then restores the prompt.

**Verification:** `TC-0020`, `TC-0056`
