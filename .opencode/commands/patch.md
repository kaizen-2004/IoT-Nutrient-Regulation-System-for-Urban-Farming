---
description: Implement the chosen solution with the smallest coherent diff
agent: build
---

Use the PATCH step.

Implement the chosen direction with minimal changes.

Rules:
- preserve existing architecture unless the user asked to change it
- reuse existing patterns
- avoid unrelated refactors
- avoid new dependencies unless clearly required
- keep the diff as small as possible
- summarize exactly what changed
