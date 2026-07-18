---
name: gameplay-cpp-story
description: |
  Use when implementing a ROADMAP.md story that adds or changes C++ in Source/PlaySports or the
  plugins (new classes, gameplay systems, physics, state machines). Covers the exact file
  conventions, module boilerplate to copy, context budget, and how to report verification
  honestly in this Unreal Engine 5.8 project.
---

# Gameplay C++ Story

You are implementing exactly one story from `ROADMAP.md`. Do not expand scope beyond it.

## Context recipe (read these, nothing more)

1. `AGENTS.md` — architecture + conventions (loaded automatically; do not re-open source files
   it already summarizes).
2. The active Epic's section — in `ROADMAP.md` for Epics 1–25, or its single `roadmap/` track
   file for Epics 26–125. Never the whole roadmap.
3. Only the source files the story names or directly extends.

## Conventions (non-negotiable)

- 4-space indent, Allman braces (opening brace on its own line).
- Prefixes: `U` (UObject), `A` (Actor), `F` (struct), `E` (enum); gameplay types are `PS*`
  (`UPSPlaySimulation`, `APSPlayerPawn`, `FPlayState`).
- Everything Blueprint-accessible: `UCLASS(Blueprintable)`, `USTRUCT(BlueprintType)`,
  `UFUNCTION(BlueprintCallable)` with a `Category`.
- Headers in `Source/PlaySports/Public/`, implementations in `Source/PlaySports/Private/`.
- New modules copy the boilerplate shape of `Source/PlaySports/Private/PlaySports.cpp`
  (`StartupModule`/`ShutdownModule` + `UE_LOG(LogTemp, Display, ...)` + `IMPLEMENT_MODULE`).
- New module dependencies go in `PlaySports.Build.cs` `PublicDependencyModuleNames` — check
  whether the module is already listed before adding.

## Verification & reporting

- **No UE toolchain is assumed present.** Never claim the change was built or tested. Report:
  "syntax/API reviewed only — no local UE build available."
- Tick the story's checkbox in `ROADMAP.md` in the same change.
- Keep the diff to the story's files + `ROADMAP.md`. If you believe adjacent work is needed,
  note it for a future story instead of doing it.
