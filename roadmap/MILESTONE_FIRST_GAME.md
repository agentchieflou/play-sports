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

### M1 — Scoreboard & UI Visuals
- [ ] **Epic 5.1 - 5.4**: UMG scoreboard widget bound to `FPlayState` (displaying down, distance, clock, scores, and play phase)

### M2 — Game Clock & Rules
- [ ] **Epic 12.5**: Timeout budgets implemented per team and integrated into clock stopping rules

### M3 — Core Gameplay Consolidations
- [ ] **C3 Fast-Follow A**: Ball-action component extracted out of `APSPlayerPawn` (handling clean passes/throws/kicks)
- [ ] **C3 Fast-Follow B**: Single roster source of truth configured across pawns and simulation states

### M4 — Xbox Controller Input
- [ ] **Epic 126**: Enhanced Input module enabled; `APSPlayerController` configured
- [ ] **Epic 127**: Gamepad mapping context active; human control possession handoff with AI resume

### M5 — CPU Football Intelligence (Phase 2 AI)
- [ ] **Epic 14**: Skill-position behaviors (QB dropbacks/throws, WR routes, RB run lanes)
- [ ] **Epic 15**: Linemen and defensive pursuit behaviors (OL blocks, DL rush lanes, pursuit angles)
- [ ] **Epic 16**: Playbook data serialization (formations, routes, assignments parsed from JSON)
- [ ] **Epic 17**: 22-agent coordinated play distribution and synchronized snap execution
- [ ] **Epic 18.1 - 18.3**: CPU play-selection logic mapping down/distance to playbooks

### M6 — Minimal Play Calling
- [ ] **Epic 102 (Stories 1 & 4)**: A debug-grade list widget hosting basic play-selection options, bypassing Epic 101's screen-stack dependencies

### M7 — Full-Game Integration Test
- [ ] Scripted headless full-game simulation asserting sequence: Kickoff -> Play loop -> Quarters ticking -> Scoring events -> Game Over
- [ ] Manual Playtest: Launch editor, possess pawn, run/pass/tackle, complete 4 quarters, and confirm scoreboard accuracy
