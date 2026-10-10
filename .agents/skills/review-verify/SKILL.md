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
claims to implement — plus, when running as role 5 of the six-role pipeline (`GEMINI.md`), the
Planner's implementation plan and the Tester's test report. Do not open files outside the diff
except the active Epic's section (`ROADMAP.md` for Epics 1–25, the Epic's `roadmap/` track file
for 26–125).

When pipeline artifacts are present, also verify: the diff matches the plan's file list and
approach; every acceptance criterion is covered by the test report (challenge weak
NOT-CHECKABLE reasons); and the test report's honesty holds (no build claims without CI
evidence). A rejected review returns to the Supervisor, never directly to the Coder.

## Checklist

1. **Scope**: the diff touches only the story's files + `ROADMAP.md`. Anything else is scope
   creep — flag it, don't fix it.
2. **Conventions — delegate the mechanical part**: run `python tools/lint_conventions.py` and
   `python tools/validate_data.py` (CI runs them too; a red lint step is an automatic
   request-changes). Only review by eye what the linter can't see: Blueprint-accessible
   `UCLASS`/`USTRUCT`/`UFUNCTION` decorations, module deps added in `PlaySports.Build.cs`
   exactly once, and sensible naming beyond prefixes. Linter warnings (style tier) are
   non-blocking context, not rejection grounds.
3. **Honest verification**: the change report must NOT claim the code was built or tested (no UE
   toolchain is assumed on agent machines). If it claims a build, reject and ask for the honest
   phrasing: "syntax/API reviewed only."
4. **Roadmap bookkeeping**: the story's checkbox in `ROADMAP.md` is ticked in this same change,
   and only that story's box.
5. **Data changes**: any JSON parses cleanly and matches the contract in the
   `data-content-author` skill (exact `FPlayerAttributes` field names, valid `Role` enum values).

6. **Architecture rules** (AGENTS.md "Architecture rules" - each is rejection grounds):
   new mechanics get their own class (no unjustified >~50-line growth of GameMode/PlayerPawn/
   PlaySimulation); story ships an automation test or the plan justifies its absence; no new
   hardcoded tuning numbers; no new Cast<APSGameMode> reach-through once the telemetry bus
   exists; no duplicated state copies.

7. **User-facing text** (Epic 106): anything the player reads is localizable. It comes through
   `UPSLocalization`: a `Data/ui_text.csv` key via `GetText`/`Format`, a UI data file string via
   `GetDataText`, or `Verbatim` for names. Reject `FText::FromString`, `FText::AsCultureInvariant`,
   `LOCTEXT`/`NSLOCTEXT`/`INVTEXT` and `FString::Printf` building display text in any
   user-facing file, including new widgets, overlays and HUD code outside the prefixes
   `validate_data.py` gates (`PSUI*`, `PSMenu*`, `PSHUD*`, `PSLoading*`, `PSSettings*`,
   `PSPlayCall*`). Changes to `ui_menus.json`, `ui_settings.json`, `loading_tips.json` or
   `defensive_adjustments.json` must carry a regenerated
   `Data/ui_text_data.csv` (`python tools/ui_text.py --write`). Log lines and validation
   messages for developers are exempt.

## Output format

Verdict first (approve / request changes), then numbered findings, each with file:line and a
one-sentence fix. No restating the diff; no style opinions beyond the conventions above.
