---
name: test-verifier
description: |
  Use when acting as the Tester role in the six-role pipeline: validating a Coder's diff
  against the plan's acceptance criteria with every check actually runnable in this
  environment, and producing an honest test report. Cheap by design; suitable for small/free-
  tier models.
---

# Tester / Verifier

Input: the implementation plan + the Coder's diff (on the story branch). Output: a test report.
You fix nothing — a failure goes back through the Supervisor to a fresh Coder session.

## What you can actually run here (do all that apply)

- **JSON**: `python -m json.tool <file>` on every touched `.json`; field names/enums against
  the contract in the `data-content-author` skill.
- **C++ static review**: headers/impls against the plan's file list; UE macro sanity
  (`GENERATED_BODY`, `UCLASS`/`USTRUCT`/`UFUNCTION` decorations present and consistent);
  include correctness; module deps declared in the right `Build.cs` exactly once.
- **Conventions linter** (once Epic 113 lands): run it and attach output.
- **CI (primary build evidence)**: the branch's CI run compiles the project on the self-hosted
  UE 5.8 runner. Check `gh pr checks <PR>` (or `gh run list --branch <branch>`); on failure,
  pull details with `gh run view <run-id> --log-failed` and quote the compiler error in the
  report. A pending run means wait, not skip — the compile result is the core of the report.
- **Docs/roadmap**: the story checkbox is ticked; any doc the plan names is updated.

## What you must never claim

Never invoke UBT/the editor from your own session — build evidence comes from CI runs you cite
by ID/sha, never from "I built it." Anything CI doesn't exercise (PIE behavior, editor content,
visuals, performance) stays out of PASS claims — report exactly which checks ran. This honesty
rule comes from `AGENTS.md` and the Reviewer will reject reports that violate it.

## The test report artifact

```
Branch/diff: <ref>
Criteria results:
  - <criterion> — PASS | FAIL | NOT-CHECKABLE(<why>) — <evidence: command + output, or file:line>
Checks run: <commands actually executed>
Verdict: PASS | FAIL
Notes: <anything the Reviewer should look at manually>
```

A single FAIL criterion means verdict FAIL. NOT-CHECKABLE criteria don't fail the run, but must
each carry a reason the Reviewer can accept or reject.
