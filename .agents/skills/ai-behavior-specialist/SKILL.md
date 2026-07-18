---
name: ai-behavior-specialist
description: |
  Use for ROADMAP Phase 2 stories (Epics 14-18): behavior trees, AI controllers, playbook/play
  data systems, and 22-agent play orchestration in this Unreal Engine 5.8 football project.
  Defines the context recipe and boundaries for AI-focused work.
---

# AI / Behavior Specialist

You are implementing one AI story: core Phase 2 (Epics 14–18 in `ROADMAP.md`), Track E gameplay
depth (`roadmap/gameplay-depth.md`), or Track F AI depth (`roadmap/ai-depth.md`) — player
decision-making, behavior trees, playbook data, orchestration, or adaptive systems.

## Context recipe (read these, nothing more)

1. `AGENTS.md` — loaded automatically.
2. The active Epic's section only — `ROADMAP.md` for core Epics, its `roadmap/` track file for
   Epics 26–125.
3. `Source/PlaySports/Public/PSPlaySimulation.h` (+ `.cpp` if changing it) — play phases and
   state your AI reacts to.
4. `Source/PlaySports/Public/PSPlayerAttributes.h` — the attributes that drive decisions
   (`Awareness` gates read/reaction quality by convention).
5. Play/route data schemas from Epic 16 once they exist — read the data contract, not the whole
   ingestion implementation.

Do **not** read rendering, camera, HUD, audio, or meta-game files — they are other archetypes'
scope.

## Technical ground rules

- `AIModule` and `NavigationSystem` are already dependencies in `PlaySports.Build.cs` — do not
  re-add them.
- Behavior tree support code follows the same conventions as all gameplay C++ (`PS*` prefixes,
  Blueprint-accessible, Allman braces — the `gameplay-cpp-story` skill has the full list if you
  are also writing C++).
- AI must react to play-state **events/phases** from `UPSPlaySimulation`, not poll private
  state — extend the simulation's public surface if the story requires new signals.
- Decisions should be seedable/deterministic where practical (Epic 17 requires replayable plays
  for debugging).

## Verification & reporting

- No UE toolchain assumed: report "syntax/API reviewed only — no local UE build available" for
  C++; behavior tree assets that require the editor should be described as specs for an
  editor-equipped session to author.
- Tick the story's checkbox in `ROADMAP.md` in the same change.
