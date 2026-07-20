# Milestone: First Playable Game (Xbox vs CPU)

This document establishes the critical path required to launch the Unreal Editor and play a complete, end-to-end game of American football (Kickoff -> 4 Quarters -> Final Score) with a human player using an Xbox controller playing against a CPU-controlled opponent.

## Out of Scope for First Game
To keep the launch path focused, the following subsystems are explicitly **not blocking** this milestone:
- Character animations and blend spaces (capsules and placeholder meshes are acceptable)
- Audio system (whistles, crowd, or commentary)
- Multiplayer (online or local H2H)
- Career, franchise, progressions, roster management, injuries, or draft systems
- Procedural content generation (playbooks, stadiums, rosters)
- Visual styling (stadium environment kits, weather systems, uniforms)

---

## Critical Path Checklist

### M0 — Foundations & Field Geometry
- [ ] **Epic 1.5**: Headless PIE-equivalent simulation tests passing (asserting basic game loop)
- [ ] **Epic 2.1 / 2.2**: Field dimensions (120yd x 53.3yd) and markings aligned in-editor
- [ ] **Epic 2.4**: Out-of-bounds and end-zone trigger volumes wired to play state machine

- [x] Epic 1.5 — end-to-end PIE test: headless automation variant per `Specs/PIE_Test_Spec.md`; the visual PIE confirmation is a human escalation item
- [x] Epic 2.4 — out-of-bounds and end-zone trigger volumes; **reconcile first** with Epic 11's already-checked touchdown detection (likely wiring/extending what exists via `APSFieldGrid`, per `Specs/Trigger_Volumes_Spec.md`)
- [x] Epic 2.1 / 2.2 — field geometry and markings: human editor sessions from `Specs/Field_Geometry_Spec.md` / `Specs/Field_Markings_Spec.md` (agents verify/refresh the specs only)

### M2 — Game Clock & Rules
- [ ] **Epic 12.5**: Timeout budgets implemented per team and integrated into clock stopping rules

- [x] Epic 5.1 — UMG scoreboard widget bound to `FPlayState` (down, distance, clock)
- [x] Epic 5.2 — score display fed by the play result
- [x] Epic 5.3 — play-phase indicator
- [x] Epic 5.4 — post-play result banner
  (C++ widget classes + C1-bus bindings per `Specs/HUD_Spec.md`; UMG asset assembly is an editor escalation)

### M4 — Xbox Controller Input
- [ ] **Epic 126**: Enhanced Input module enabled; `APSPlayerController` configured
- [ ] **Epic 127**: Gamepad mapping context active; human control possession handoff with AI resume

- [x] Epic 12.5 — timeout budget per team (last unchecked rules story in Phase 1)

### M6 — Minimal Play Calling
- [ ] **Epic 102 (Stories 1 & 4)**: A debug-grade list widget hosting basic play-selection options, bypassing Epic 101's screen-stack dependencies

- [x] Epic C3 fast-follow A — ball-action logic extracted from `APSPlayerPawn` into a dedicated component
- [x] Epic C3 fast-follow B — single roster source of truth across `PSGameMode`/`PSPlayerPawn`/`PSPlaySimulation`

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
