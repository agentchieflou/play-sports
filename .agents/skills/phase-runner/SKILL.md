---
name: phase-runner
description: |
  Use when asked to run a whole roadmap phase (or Epic range) autonomously in a loop — e.g.
  "run the Phase 0 loop". Executes the six-role pipeline story-by-story inside one session,
  with role-switch discipline, context hygiene between stories, editor-work escalation, and
  hard stop conditions. Loops until the phase is complete or a stop condition fires.
---

# Phase Runner (autonomous pipeline loop)

You are running every remaining story in an assigned phase/Epic range through the six-role
pipeline (`GEMINI.md`), sequentially, in this one session. You are all six roles in turn —
which makes **role discipline** and **context hygiene** your two prime duties.

## Loop

1. **Board setup (Supervisor):** read only the assigned phase's Epics. List remaining unticked
   stories in dependency order (Epic "Depends on" lines first, story order within an Epic).
   This list + a running log are your persistent state for the whole session.
2. **For each story, run the roles in order** — Planner → Coder → Tester → Reviewer → Git —
   loading each role's skill and producing its artifact before moving on. Hold each role's
   rules while in it: the Tester really runs its checks (never assumes), the Reviewer really
   challenges the diff (you are reviewing your own work — be adversarial, check the plan's
   criteria one by one), the Git steward really verifies scope and the checkbox before merging.
3. **CI is the build evidence.** After the Coder pushes the story branch and opens its PR,
   the Tester waits for the `CI` workflow (`gh pr checks <PR> --watch`) — the compile + headless
   automation results are the core of the test report; quote failures from
   `gh run view <run-id> --log-failed`. The Git steward merges only on CI green (its checklist).
   Never invoke UBT or the editor yourself; push and let CI run.
4. **Story complete** = merged to `main` with its checkbox ticked. Append one line to the log:
   story, verdicts, commit hash. Then **drop all file contents and role artifacts from working
   attention** — carry only the log and the escalation list into the next story.
4. Repeat until no stories remain, then write the **phase report** and stop.

## Editor-mode and PIE-dependent work

No local Unreal Editor/UBT is assumed (per `AGENTS.md`). When a story or acceptance criterion
needs the editor (level authoring, PIE runs, visual verification):

- Implement everything code/data-side that the story allows.
- Produce the editor half as a **spec or job file** (Epic 118 format once it exists; a clear
  markdown spec until then) committed with the change.
- Leave the checkbox **unticked** if the story's goal itself is editor work; tick it if the
  code deliverable is the story and only *verification* awaits the editor — and either way,
  add it to the **escalation list** with what a human editor session must do.
- Continue the loop; editor gaps never stall code-side progress on other stories.

## Stop conditions (halt the loop and report — do not push through)

- The same story fails twice at the same stage (test or review).
- A story is mis-sized, ambiguous, or needs a decision the roadmap doesn't answer.
- A merge conflict with work you didn't produce (someone else is in the repo — coordinate).
- Any repo-state surprise: unexpected diffs on `main`, failed pushes, missing files.
- Phase complete (the good one).

## Phase report (always your final output)

```
Phase: <name> — <complete | halted: reason>
Stories merged: <list with commit hashes>
Escalation list (needs human/editor session): <items with what-to-do>
Skipped/blocked: <stories + why>
Observations: <mis-sized epics, convention gaps, anything the roadmap should absorb>
```

## Hard rules

- One story fully lands (or escalates) before the next begins — never interleave.
- Never tick a checkbox for unmerged work; never claim builds/tests without runnable evidence.
- Branch-per-story, PR discipline, and all `git-steward` rules apply on every iteration.
- If the session grows heavy, finish the current story, write the phase report with a
  `halted: session handoff` status, and let a fresh session resume from the log — state lives
  in the repo (checkboxes + commits), so resumption is cheap.
