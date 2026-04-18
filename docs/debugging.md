# Debugging

For bug-related tasks, follow this order:

1. Reproduce
2. Trace the relevant code path
3. Identify the likely root cause
4. Confirm the fix hypothesis against the code
5. Apply the smallest coherent fix
6. Validate the fix and regression risk

## Rules
- Do not patch symptoms without tracing the likely cause.
- Prefer fixes supported by code evidence.
- If the issue cannot be reproduced, say so clearly.
- State what is observed, assumed, and unknown.
