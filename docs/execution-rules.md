# Execution Rules

## General
- Follow the surrounding code style first.
- Prefer clarity over cleverness.
- Keep control flow straightforward.
- Use local conventions over generic best practices when they conflict.
- Keep diffs small and reviewable.

## Change design
- Prefer the smallest fix or feature addition that solves the problem cleanly.
- Do not over-engineer.
- Do not create new abstractions unless they reduce real complexity.
- Do not mix bug fixes with unrelated refactors unless necessary.

## Working style
- Inspect first, then implement.
- Reuse existing utilities and patterns.
- Avoid speculative cleanup.
- Keep changes easy to review and revert.

## Validation
Choose the smallest useful check:
- focused test
- package/module test
- typecheck
- lint for touched files
- narrow build step

If nothing can be run, say so explicitly.
