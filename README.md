# play-sports

AI-driven, physics-based football game project scaffold for Unreal Engine 5.

## Project Structure

- `play-sports.uproject` - Unreal Engine project definition.
- `Source/PlaySports` - main game module.
- `Plugins/Autonomix` - AI editor bypass plugin stub for T3D injection and Python scripting.
- `Plugins/AgenticLink` - external agent bridge plugin stub.
- `Data` - external data assets and ingestion sources.

## Implemented Systems

- `PSPlayerAttributes` - player attribute `USTRUCT` for data tables.
- `PSPlaySimulation` - headless play simulation state machine.
- `PSDataIngestion` - JSON-to-DataTable ingestion utility.
- `PSScheduleEngine` - programmatic 18-week season schedule generator.
- `PSFunctionalGym` - starting point for automated functional tests.

## Agentic Workflow

This repo is set up to be worked on by multiple AI coding tools (Claude Code, Antigravity,
GitHub Copilot, and free-tier models like Ollama/Gemini/OpenRouter) with shared context. See
[`AGENTS.md`](AGENTS.md) for the architecture map, conventions, and how each tool's connectors
are wired up.

## Roadmap

Development is tracked in [`ROADMAP.md`](ROADMAP.md): a 25-Epic core across 5 phases, sequenced
vertical-slice first (one crude but complete play end-to-end, then deepen each system), plus 100
expansion Epics (26–125) in themed track files under [`roadmap/`](roadmap/) — broadcast overlays,
cameras, stadium atmosphere, animation depth, gameplay nuance, AI, franchise, audio, UX,
multiplayer, engineering infrastructure, and content generation. Each expansion Epic is sized
(S/M/L/XL) and moded (code/editor/mixed) for agent scoping.
