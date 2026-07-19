# Antigravity run prompt — Phase 0 → 1.5 cleanup slate

Paste this prompt (everything below the line) into an Antigravity session in this repo.

---

Load the `phase-runner` skill. Run it over the **explicit ordered story list below** — not a
phase range. One story per pipeline iteration, all six roles per run, CI cited as build
evidence (`gh pr checks`), and the roadmap checkbox (plus the matching box in
`roadmap/MILESTONE_FIRST_GAME.md`) ticked in the same PR that completes each story. Stop
conditions and failure routing per the skill; escalate to human after two failures at the
same stage.

## Story slate (work strictly in this order)

1. **Epic 12, story 5 — timeout budget per team.** Coder: `gameplay-cpp-story`. Extends the
   clock-management system; rules config stays data-driven (rule 4).

2. **Epic C3 fast-follow A — extract ball-action logic out of `APSPlayerPawn`** into a
   dedicated component (`ThrowPass`/`ExecuteHandoff`/`ExecutePitch`/`ExecuteKick`/
   `FumbleBall`/`ResolveTackle`, ~300 lines). The roadmap's own instruction applies
   verbatim: "large, gameplay-critical math with no local UBT to verify against — scope it
   as its own story with careful CI-round-trip iteration rather than pushing it through
   blind." Must land **before any Track M work** (`roadmap/PARALLEL.md` conflict entry).

3. **Epic C3 fast-follow B — single roster source of truth.** `PlaySimulation` and each
   `APSPlayerPawn` currently hold independent `FPlayerAttributes` copies; converge on one
   authority (rule 6). Touches `PSGameMode`/`PSPlayerPawn`/`PSPlaySimulation`; serialize
   strictly after fast-follow A.

4. **Epic 1, story 5 — end-to-end test: roster loads → play runs phases → result logged.**
   Implement the headless automation variant per `Specs/PIE_Test_Spec.md`. The visible-PIE
   confirmation goes on the escalation list. Tick the checkbox only if the spec states the
   automation test satisfies the story; otherwise leave it unticked and escalate.

5. **Epic 2, story 4 — out-of-bounds and end-zone trigger volumes.** Planner must first
   reconcile with Epic 11's already-checked touchdown detection: determine how TD/safety
   detection works today in `Source/` before writing anything — this is likely a
   wire-or-extend job (probably spawning volumes from `APSFieldGrid` per
   `Specs/Trigger_Volumes_Spec.md`), not greenfield. Any editor-side placement escalates.

6. **Epic 5, stories 1–4 — HUD (scoreboard, score display, phase indicator, result
   banner).** C++ `UUserWidget` classes with bindings fed by `UPSTelemetryBus`
   subscriptions per `Specs/HUD_Spec.md` (rule 5 — no GameMode casts from UI). UMG asset
   assembly is editor-mode: put it on the escalation list; checkbox handling per
   phase-runner rules for mixed stories.

7. **Epic 2, stories 1–2 — field geometry and markings.** **Editor-mode: do not attempt
   the geometry.** Verify `Specs/Field_Geometry_Spec.md` and `Specs/Field_Markings_Spec.md`
   are current against today's code (coordinate conventions, `APSFieldGrid` helpers),
   refresh them if stale, and place both stories on the escalation list as human editor
   jobs.

## Constraints

- Architecture rules 1–6 from `AGENTS.md` apply to every diff; Reviewer rejects violations.
- Never invoke UBT or the editor locally — push and cite CI (`Specs/ADR_CI_Environment.md`).
- Do not start Phase 2 work regardless of how much of this slate completes.
- Final output: the phase-runner's phase report, including the full escalation list (the
  human editor jobs for `roadmap/MILESTONE_FIRST_GAME.md` M0/M1: field geometry, markings,
  UMG assembly, visual PIE pass).
