---
name: planner-story
description: |
  Use when acting as the Planner role in the six-role pipeline: turning one assigned roadmap
  story into a concrete implementation plan (approach, exact file list, acceptance criteria)
  that a Coder session can execute without re-deriving context. Plans only — writes no code.
---

# Planner (Story → Implementation Plan)

Input: a story assignment from the Supervisor. Output: an implementation plan. You write no
code and change no files.

## Context recipe

1. `AGENTS.md` (auto-loaded) — trust its architecture summaries; don't re-verify them.
2. The assigned Epic's section in its track file — only that section.
3. Source files you expect the story to touch — **headers first**; open a `.cpp` only when the
   header leaves a real question. This is the only role that reads code the Coder hasn't
   touched yet; keep it minimal, the plan is what carries the knowledge forward.

## The plan artifact

```
Story: <verbatim>
Approach: <2-6 sentences: what gets built and how it fits existing systems>
Files:
  - <path> — <create|edit> — <what changes>
Acceptance criteria:
  - <observable, checkable statements the Tester and Reviewer will verify>
Out of scope: <adjacent things this story deliberately does NOT do>
Risks/notes: <API uncertainties, convention questions, editor-mode handoffs>
Size check: <confirms the Epic's S/M/L/XL label, or flags mis-sizing to the Supervisor>
```

## Rules

- Acceptance criteria must be checkable without a UE build (JSON parses, class/API shape,
  functional-gym spec exists, doc updated) — per the no-toolchain reality in `AGENTS.md`.
  Once CI (Epic 112) exists, criteria may include "CI compiles/tests green".
- Reuse before invention: if an existing system covers part of the story, the plan says extend
  it, naming the file — never propose a parallel implementation.
- If the story is ambiguous, too big for its size label, or blocked on an unmade decision:
  return a **mis-size/blocked flag** to the Supervisor instead of a speculative plan.
- `editor`-mode stories: the plan's deliverables are specs/data/job-files (see Epic 118's job
  format once it exists), and the plan must say who executes the editor half.
