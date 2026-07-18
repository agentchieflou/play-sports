---
name: review-verify
description: |
  Use when reviewing another agent's diff/PR for this repo before merge. Checks conventions,
  scope discipline, honest verification claims, and roadmap bookkeeping. Reads the diff plus
  ROADMAP.md only — never whole source trees. Cheap by design; suitable for small/free-tier
  models.
---

# Reviewer / Verifier

You are reviewing a change, not writing one. Your inputs are the **diff** and the story it
claims to implement — do not open files outside the diff except the active Epic's section
(`ROADMAP.md` for Epics 1–25, the Epic's `roadmap/` track file for 26–125).

## Checklist

1. **Scope**: the diff touches only the story's files + `ROADMAP.md`. Anything else is scope
   creep — flag it, don't fix it.
2. **Conventions** (from `AGENTS.md`, loaded automatically): 4-space indent, Allman braces,
   `U`/`A`/`F`/`E` + `PS*` prefixes, Blueprint-accessible `UCLASS`/`USTRUCT`/`UFUNCTION`
   decorations, Public/Private header split, module deps added in `PlaySports.Build.cs` and not
   duplicated.
3. **Honest verification**: the change report must NOT claim the code was built or tested (no UE
   toolchain is assumed on agent machines). If it claims a build, reject and ask for the honest
   phrasing: "syntax/API reviewed only."
4. **Roadmap bookkeeping**: the story's checkbox in `ROADMAP.md` is ticked in this same change,
   and only that story's box.
5. **Data changes**: any JSON parses cleanly and matches the contract in the
   `data-content-author` skill (exact `FPlayerAttributes` field names, valid `Role` enum values).

## Output format

Verdict first (approve / request changes), then numbered findings, each with file:line and a
one-sentence fix. No restating the diff; no style opinions beyond the conventions above.
