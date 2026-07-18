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

## Next Steps

1. Create actual Unreal asset mappings and plugin implementation.
2. Build the AI bridge from AgenticLink to the engine's reflection API.
3. Add T3D import helpers, transaction-safe batch operations, and Python escape hatches.
4. Develop the 22-agent behavior system and immersive audio pipeline.
5. Wire the free-tier model slots in `AGENTS.md` into an actual router/bridge once (2) exists.
