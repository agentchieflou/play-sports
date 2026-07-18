# Copilot instructions for play-sports

Full architecture and agentic-workflow details live in [`AGENTS.md`](../AGENTS.md) at the repo
root — read that for anything not covered here. This file is the condensed version because
Copilot doesn't load `AGENTS.md` automatically.

## What this is

Unreal Engine 5.3 project scaffold for an AI-driven American football game. Early-stage: most
systems are thin, deliberate skeletons.

- `Source/PlaySports/` — runtime module. Real, working systems: `PSPlayerAttributes` (DataTable
  row struct), `PSPlaySimulation` (headless play-phase state machine, no physics yet),
  `PSDataIngestion` (JSON → DataTable loader), `PSScheduleEngine` (season schedule generator),
  `PSFunctionalGym` (functional test actor).
- `Plugins/Autonomix/` and `Plugins/AgenticLink/` — **both are stub modules** that only log on
  startup/shutdown. They're the intended homes for, respectively, an in-editor Python/T3D AI
  bridge and an external MCP agent bridge — neither is implemented. Don't assume they do anything.
- `Data/sample_players.json` — sample payload for `PSDataIngestion`.

## Conventions

- UE prefixes: `U`/`A`/`F`/`E`, gameplay types prefixed `PS*`.
- `USTRUCT(BlueprintType)` / `UCLASS(Blueprintable)` + `UFUNCTION(BlueprintCallable)` — everything
  is meant to be Blueprint-accessible.
- 4-space indent, Allman braces, matching module boilerplate (`StartupModule`/`ShutdownModule`
  logging via `UE_LOG(LogTemp, Display, ...)`) already present in every module.

## Do not claim to have built or tested C++ changes

No Unreal Editor/UBT toolchain should be assumed available. Review for syntax/API correctness and
say so explicitly rather than claiming a build or test run.
