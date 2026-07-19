# Milestone: First Full Game

**Definition of done:** Boot the Unreal Editor, press Play, and complete one full game —
kickoff → four quarters → final score — with a human on an Xbox controller versus the CPU,
using real project systems end to end. Bugs that only live testing can reveal are expected;
missing systems are not.

This is the launch-critical path through the roadmap. Every box below cites the specific
epic/story that satisfies it; boxes are ticked in the PR that completes the cited story.
Groups M0–M3 map to `roadmap/PARALLEL.md` group G1, M4 to G6, M5 to G7.

**Explicitly NOT needed for this milestone** (do not let scope creep in): Epic 19–21
(roster depth/franchise/pipeline), Epic 22 (animation — capsules are fine), Epic 23 (audio),
Tracks A–D, F–H, J (overlays, cameras beyond Core 4, stadium, visuals, AI depth, franchise,
audio, multiplayer), Epics 101/103/105/106, Epic 128 (rumble/glyphs — nice-to-have), all of
Tracks N/O/P.

## M0 — Foundations verified

Already sufficient and done: Epics 3, 4, 6–11, 13, C1, C2, C4.

- [ ] Epic 1.5 — end-to-end PIE test: headless automation variant per `Specs/PIE_Test_Spec.md`; the visual PIE confirmation is a human escalation item
- [ ] Epic 2.4 — out-of-bounds and end-zone trigger volumes; **reconcile first** with Epic 11's already-checked touchdown detection (likely wiring/extending what exists via `APSFieldGrid`, per `Specs/Trigger_Volumes_Spec.md`)
- [ ] Epic 2.1 / 2.2 — field geometry and markings: human editor sessions from `Specs/Field_Geometry_Spec.md` / `Specs/Field_Markings_Spec.md` (agents verify/refresh the specs only)

## M1 — You can see the game

- [ ] Epic 5.1 — UMG scoreboard widget bound to `FPlayState` (down, distance, clock)
- [ ] Epic 5.2 — score display fed by the play result
- [ ] Epic 5.3 — play-phase indicator
- [ ] Epic 5.4 — post-play result banner
  (C++ widget classes + C1-bus bindings per `Specs/HUD_Spec.md`; UMG asset assembly is an editor escalation)

## M2 — Rules complete

- [x] Epic 12.5 — timeout budget per team (last unchecked rules story in Phase 1)

## M3 — Consolidation gate closed (Phase 2 unlock)

- [ ] Epic C3 fast-follow A — ball-action logic extracted from `APSPlayerPawn` into a dedicated component
- [ ] Epic C3 fast-follow B — single roster source of truth across `PSGameMode`/`PSPlayerPawn`/`PSPlaySimulation`

## M4 — A human holds a controller *(parallel with M5 once M3 is done)*

- [ ] Epic 126 — Enhanced Input foundation & `APSPlayerController` (all 5 stories)
- [ ] Epic 127 — Xbox gamepad bring-up & human possession (all 5 stories)

## M5 — The CPU can play football

- [ ] Epic 14 — skill-position behavior (QB/RB/WR/TE)
- [ ] Epic 15 — line & defensive behavior (OL/DL/LB/DB)
- [ ] Epic 16 — playbook & play data system
- [ ] Epic 17 — 22-agent coordinated play orchestration
- [ ] Epic 18.1–18.3 — coaching & play-selection AI (situation model, tendency profiles, 4th-down/clock logic; 18.4's LLM hook is not launch-blocking)

## M6 — You can call a play

- [ ] Epic 102 minimal subset — offensive formation/concept picker (102.1) and defensive call flow (102.4) as a **debug-grade list widget hosted by the Epic 5 HUD**. Waiver recorded here: the Epic 101 screen-stack dependency is deliberately bypassed for this milestone; the 102 story checkboxes are NOT ticked by this work — only this milestone box is

## M7 — Full-game validation

- [ ] Headless scripted full game: kickoff → 4 quarters → final score asserted (lands as Epic 24's "headless play-resolution tests" story — cite it, don't invent a twin)
- [ ] Human playtest on the pad: one full game start to finish, issues filed (human escalation item)
