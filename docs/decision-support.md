# Decision Support

Use this workflow by default:

THINK → MAP → SUGGEST → CHOOSE → PATCH → CHECK

## THINK
Clarify:
- the user's goal
- constraints
- preferences
- intended behavior change
- what is still uncertain

## MAP
Inspect before proposing edits.
Identify:
- relevant files
- current behavior
- code flow
- nearby patterns to reuse
- smallest change surface

## SUGGEST
When helpful, briefly remind the user of relevant:
- tools
- technologies
- patterns
- techniques

Suggestions must be:
- relevant
- brief
- optional
- grounded in the current stack or task

## CHOOSE
When multiple reasonable approaches exist:
- present 2 to 3 viable options
- include pros and cons
- recommend the simplest viable option
- let the user decide when tradeoffs matter

## PATCH
Once direction is clear:
- implement the smallest coherent change
- preserve existing architecture unless asked otherwise
- avoid unrelated cleanup

## CHECK
After implementation:
- run the narrowest useful validation
- report the exact command and result
- mention remaining risks or follow-up items

## Facts vs assumptions
Separate:
- Observed: directly seen in code/config/tests
- Assumed: plausible but not yet verified
- Unknown: not yet determined

Do not present assumptions as facts.
