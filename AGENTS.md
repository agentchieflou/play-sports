# AGENTS.md

Cross-tool instructions for AI agents working in this repo (Claude Code, Antigravity, GitHub
Copilot, and any other AGENTS.md-compatible tool). Tool-specific notes live in `CLAUDE.md`
(Claude Code) and `.github/copilot-instructions.md` (Copilot); this file is the shared source of
truth both of those point back to.

## What this project is

`play-sports` is an Unreal Engine 5.8 project scaffold for an AI-driven, physics-based American
football game (`play-sports.uproject`, engine association `5.8`). It is early-stage: most systems
are deliberately thin skeletons meant to be built out incrementally, several by agents.

## Architecture

```
play-sports.uproject          UE5.8 project definition, enables the two plugins below
Source/PlaySports/            Runtime game module ("PlaySports")
Plugins/Autonomix/            Editor-time agent tools: T3D import, Python (opt-in, via AgenticLink)
Plugins/AgenticLink/          External agent bridge: MCP server over engine reflection (opt-in)
Data/                         External data assets consumed by ingestion code
RawAssets/                    Source (non-.uasset) assets with provenance: the world kit (CC0/own GLB, textures, skies)
tools/assets/                 Offline Blender/Node pipeline that made RawAssets/ (never run by CI)
Specs/                        Editor-session handoff specs, plus the imported browser-world knowledge
```

### `Source/PlaySports` (runtime module)

Depends on `Core, CoreUObject, Engine, InputCore, UMG, AIModule, NavigationSystem, Json,
JsonUtilities` (see `PlaySports.Build.cs`). Implemented systems, all real (not stubs):

- `PSPlayerAttributes.h` — `FPlayerAttributes` `USTRUCT` (a `DataTable` row) plus `EPlayerRole`
  (Quarterback, RunningBack, WideReceiver, TightEnd, OffensiveLineman, DefensiveLineman,
  Linebacker, DefensiveBack).
- `PSPlaySimulation` (`UPSPlaySimulation`) — headless play state machine. Cycles
  `PreSnap → Snap → PassRush → BallCarrierMovement → Scoring` on each `AdvancePlay` call. No
  physics/collision resolution yet — it's a phase ticker, not a simulation.
- `PSDataIngestion` (`UPSDataIngestion`) — loads a `Players` JSON array into a `UDataTable` of
  `FPlayerAttributes` via `FJsonObjectConverter`. See `Data/sample_players.json` for the expected
  shape.
- `PSScheduleEngine` (`UPScheduleEngine`) — pure function generating an N-week season schedule
  with configurable bye weeks. No I/O, easy to unit-reason about.
- `PSFunctionalGym` (`APSFunctionalGym`) — `AFunctionalTest` subclass; starting point for
  automated in-editor functional tests. Currently a single trivial test.

### `Plugins/Autonomix`

"Headless AI bridge for T3D injection and Unreal Python operations" (Core 25.1, 25.2). The module
(`Editor` type, `PostEngineInit`: editor targets only, never in a packaged game) serves its tools
through AgenticLink's one MCP server (`FAgenticLinkToolProviders`), each opt-in by its own switch on top of
`-AgenticLinkMcp`: `import_t3d` (`FAutonomixT3D`, `-AutonomixT3D`) spawns actors from T3D text or
changes the ones it names, as one undoable transaction; `run_python` (`FAutonomixPython`,
`-AutonomixPython`) runs a script through the Python Editor Script Plugin and returns its result and
log. Without the switches it only logs. `run_python` needs the Python Editor Script Plugin, which the engine enables by default.
Headless tests: `PlaySports.Autonomix.*`.

### `Plugins/AgenticLink`

"External agent bridge for Model Context Protocol and transaction-safe engine access" (Epic 25).
An MCP server (`FAgenticLinkMcpServer`, JSON-RPC 2.0) served over Streamable HTTP on the engine's
HTTP server (`FAgenticLinkHttpTransport`) at `http://127.0.0.1:<port>/mcp`. It starts **only**
when the editor is launched with `-AgenticLinkMcp` (port 8790) or `-AgenticLinkMcpPort=<port>`;
otherwise the module just logs. Tools (`FAgenticLinkEngineTools`): `list_actors`,
`get_property`, `set_property` (instance-editable properties), `call_function`
(BlueprintCallable functions, on an actor or on a world/game-instance `subsystem`) and
`spawn_actor`, acting on the PIE world while playing, else the editor level. Each actor edit is one
`FScopedTransaction`, so Ctrl+Z undoes an agent's change. A request with a non-localhost `Origin`
is refused; `Config/DefaultEngine.ini` binds the HTTP server to 127.0.0.1. While it serves, the
module is registered as the `AgenticLinkBridge` modular feature: game code gates its model hooks on
that name with no link to the plugin (Epic 82's `UPSGameIntelligenceSubsystem`; relay:
`python -m tools.orchestrator game-hooks`). Headless tests: `PlaySports.AgenticLink.*`.

AgenticLink's server is off unless its switch is given, and Autonomix's tools are off unless
theirs are. Both plugins' modules are `Editor` type, so a packaged game carries neither (Epic 145).

### `Data/`

`sample_players.json` — example payload for `PSDataIngestion`, two rows (`QB_001`, `OL_001`)
matching the `FPlayerAttributes` field names exactly (`PlayerId`, `DisplayName`, `Role`,
`WeightKg`, `HeightCm`, `Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness`).

A packaged build stages all of `Data/` as loose files at the same place under its project
directory (`PlaySports.Build.cs`), so loaders keep reading `FPaths::ProjectDir() / "Data/..."`.
A loader's default data file must be listed in `PSDataPaths.cpp` (`tools/tests/test_data_staging.py`
checks the source against it; the packaged smoke test checks the build carries every one).

### `RawAssets/` and `tools/assets/` (the world kit, imported 2026-10-08)

The 3D "world" built for the fleet desk in `agentchieflou/this-next-please` was semi-scrapped and its
assets and knowledge moved here: `RawAssets/world/` (CC0 Poly Haven materials, skies and props; a CC0
MakeHuman character and crowd already on the Unreal body bone names; procedurally grown trees, cars and
office furniture; the three.js reference implementation with its reasoning notes), `tools/assets/world/`
(the pipeline that made them), and `Specs/Browser_World_Lessons.md`, `Specs/Character_Customization_Spec.md`,
`Specs/Input_Architecture.md`, `Specs/Weather_DayNight_Spec.md`. Track R (`roadmap/world-kit.md`,
Epics 142–144) is the work that consumes them. Rules: nothing under `RawAssets/` or `tools/assets/` is
loaded at runtime or built by CI; every subfolder carries a `LICENSE` naming each file's source; `Content/`
stays empty until an editor session imports (`RawAssets/world/README.md` §Importing into Unreal).

## Conventions

Follow the patterns already established in `Source/PlaySports` and the plugins:

- UE naming prefixes: `U` for `UObject`-derived, `A` for `AActor`-derived, `F` for plain structs,
  `E` for enums, module-prefixed as `PS*` for gameplay types (`PSPlayerAttributes`,
  `PSPlaySimulation`, ...).
- `USTRUCT(BlueprintType)` / `UCLASS(Blueprintable)` + `UFUNCTION(BlueprintCallable)` throughout —
  systems are designed to be Blueprint-accessible, not C++-only.
- Module boilerplate (`StartupModule`/`ShutdownModule` with `UE_LOG(LogTemp, Display, ...)` and an
  `IMPLEMENT_MODULE` in the `.cpp`) is consistent across `PlaySports`, `Autonomix`, and
  `AgenticLink` — match it for any new module.
- Tabs vs. spaces / brace style: 4-space indent, Allman braces (opening brace on its own line), as
  seen in every existing `.h`/`.cpp`.

## Build & verification reality check

**CI compiles every PR.** Unreal Engine 5.8 is installed on the dev machine, which also hosts
the self-hosted GitHub Actions runner (labels `self-hosted, windows, unreal`, engine path in the
runner's `UE_ROOT`) — see `Specs/ADR_CI_Environment.md`. The `CI` workflow runs a Win64
Development Editor compile plus a headless automation-test job on every PR and push to `main`.

Verification rules for agents:

- **Cite CI, don't claim builds yourself**: check status with `gh pr checks <PR>` or
  `gh run list --branch <branch>`; on failure, `gh run view <run-id> --log-failed`. "CI compile
  green on <sha>" is valid build evidence; "I built it" without a run to cite is not.
- Agent sessions themselves still have **no direct UE toolchain access** — do not invoke the
  editor/UBT from an agent session; push and let CI run.
- The no-unverified-claims rule still applies to everything CI does not exercise: PIE behavior,
  editor-authored content, visuals, performance. Editor specs in `Specs/` remain the handoff
  for that work.

### Running CI's checks on a machine with UE 5.8 (Epic 24)

These are the commands `.github/workflows/ci.yml` runs, from the repo root in PowerShell, with
`UE_ROOT` set to the engine install.

**Without Unreal:**

```
python tools/lint_conventions.py
python tools/validate_data.py                     # every data contract and cross-file reference
python -m unittest discover -s tools/tests -t .   # the tools' own tests
python tools/content.py report                    # content sanity report (warnings only)
```

**Build and test:**

```
& "$env:UE_ROOT\Engine\Build\BatchFiles\Build.bat" PlaySportsEditor Win64 Development -project="$PWD\play-sports.uproject" -WaitMutex
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\play-sports.uproject" -ExecCmds="Automation RunTests PlaySports" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$PWD\Saved\AutomationReport" -unattended -nullrhi -nosplash -nop4 -log
python tools/crash_report.py summarize            # if the editor crashed
```

**Reading the result:**

- The editor can exit 0 with failing tests, so read `Saved\AutomationReport\index.json`.
  `failed` must be 0.
- Narrow a run with a longer test prefix, for example
  `-ExecCmds="Automation RunTests PlaySports.Gym"` for the scripted games or
  `PlaySports.Spec.DriveState` for one spec.
- `python tools/content.py import` runs the content commandlet the same way.

## Architecture rules (learned from the Phase 0/1 review, 2026-07-19)

A review of the first ~35 merged story PRs found the pipeline's *process* solid but its
*architecture* drifting: story-by-story agents optimize locally. These rules are therefore
mandatory story-level requirements — Planners encode them in plans, Reviewers reject diffs
that violate them:

1. **New system = new class/component.** A story introducing a mechanic creates a named
   `UActorComponent`/class (the plan names it). Adding more than ~50 lines to `APSGameMode`,
   `APSPlayerPawn`, or `UPSPlaySimulation` requires explicit justification — these three
   absorbed all of Epics 8–9 and are already god-class risks.
2. **Every C++ story ships a headless automation test** (`Source/PlaySports/Private/Tests/`)
   unless the plan states why it can't. Core gameplay currently has near-zero coverage; the
   save system (4 tests) is the pattern to follow.
3. **Consume what exists.** Before writing new code, check whether an existing system covers
   the story — wiring an orphan beats writing a twin. Known orphans as of the review:
   `APSFieldGrid` (unused; GameMode hardcodes a spawn grid) and `APSBroadcastCamera`
   (`TargetActor` never assigned).
4. **Tuning lives in DataTables.** `FMovementTuningRow` is the pattern; no new gameplay magic
   numbers in code (the review found hardcoded throw/catch/interception/completion formulas).
   JSON loading goes through `UPSDataIngestion` — never a new ad-hoc parser.
5. **Communicate via events, not casts.** Once Phase 1.5's `UPSTelemetryBus` (C1) lands,
   cross-system communication subscribes/publishes on the bus; `Cast<APSGameMode>`
   reach-through is a review-rejection.
6. **One authority per fact.** The play's outcome, possession, and roster each have exactly
   one source of truth (Phase 1.5 C2/C3 establish them); duplicating state into copies is a
   review-rejection.

## Agentic workflow / tool connectors

This repo is set up so multiple AI coding tools can work in it with shared context:

| Tool | Reads | Notes |
|---|---|---|
| Claude Code | `CLAUDE.md` (which imports this file via `@AGENTS.md`) | Project-scoped MCP servers: `.mcp.json` |
| Antigravity | This file (`AGENTS.md`) directly (v1.20.3+), plus `GEMINI.md` for Antigravity-specific overrides (higher priority) | MCP servers are configured **globally**, not per-repo — see below. Antigravity work runs through the six-role pipeline (supervisor→planner→coder→tester→reviewer→git) defined in `GEMINI.md`, with on-demand role/domain skills in `.agents/skills/` |
| GitHub Copilot | `.github/copilot-instructions.md` (repo-wide) | Doesn't read `AGENTS.md`; VS Code agent mode MCP servers: `.vscode/mcp.json` |

### MCP servers

`.mcp.json` (Claude Code — key `mcpServers`) and `.vscode/mcp.json` (VS Code/Copilot — key
`servers`, **not** `mcpServers`, a common copy-paste mistake) currently declare zero servers.
The AgenticLink server exists but isn't registered in them yet (registering it is the repo
owner's call). While an editor runs with `-AgenticLinkMcp`, the entries are:

```json
{ "mcpServers": { "agenticlink": { "type": "http", "url": "http://127.0.0.1:8790/mcp" } } }
```

for `.mcp.json`, and the same object under `"servers"` for `.vscode/mcp.json`.

Antigravity does not read a repo-local MCP config — its config is global, at
`~/.gemini/config/mcp_config.json`. To add a server there, merge an entry shaped like:

```json
{
  "mcpServers": {
    "example-server": {
      "serverUrl": "https://example.com/mcp",
      "headers": { "X-Goog-Api-Key": "${GEMINI_API_KEY}" }
    }
  }
}
```

(Note `serverUrl`, not `url` — Antigravity's key name differs from VS Code/Cursor.) For
AgenticLink the entry is `"agenticlink": { "serverUrl": "http://127.0.0.1:8790/mcp" }`, with no
headers.

The model router service (Epic 119, `tools/orchestrator/mcp_server.py`) is a stdio server, run
from the repo root so it reads the repo's `.env`. Like AgenticLink, it isn't registered yet; that is
the owner's call. Its tools are:
- `route_task`: a task's request goes to the model its capability and cost call for, with fallback
  across providers;
- `list_routes`;
- `router_health`.

The entries are:

```json
{ "mcpServers": { "model-router": { "type": "stdio", "command": "python", "args": ["-m", "tools.orchestrator", "mcp"] } } }
```

for `.mcp.json`, and the same object under `"servers"` for `.vscode/mcp.json`. In Antigravity's
global config the entry is
`"model-router": { "command": "python", "args": ["-m", "tools.orchestrator", "mcp"], "cwd": "<repo path>" }`.

### Free-tier / local model slots

The env-var contract every connector reads from `.env` (copy `.env.example`). The orchestrator's
model router (`tools/orchestrator/models/`, Track P and Core 25) consumes all three:

- `OLLAMA_HOST` — local Ollama instance (default `http://localhost:11434`), zero cost, works
  offline. Useful for CI or throwaway/parallel agent tasks.
- `GEMINI_API_KEY` — Google AI Studio free tier. Antigravity itself runs on Gemini already; this
  is for reaching Gemini from *other* tools (e.g. a future MCP server, or Claude Code shelling out
  for a second opinion).
- `OPENROUTER_API_KEY` — OpenRouter's free-tier community models via one API, for whichever
  provider isn't otherwise covered.

To hand a task to a free-tier model, use `python -m tools.orchestrator delegate "<task>"`, or
pipe the task on stdin. It walks the `bridge` chain: Ollama first, then Gemini's free tier, then
an OpenRouter `:free` model. It skips any link whose host or key isn't set and prints the answer,
with the model that gave it on stderr. `python -m tools.orchestrator models` shows the chain.

To route by what a task needs rather than by provider, use the model router service (Epic 119).
`python -m tools.orchestrator route <task> "<prompt>"` sends a task to the model that fits it, and
`python -m tools.orchestrator routes` shows each task's chain with no network call.
`tools/orchestrator/routing.json` holds the tasks: `narration`, `summary`, `analysis`, `strategy`
and `delegate`. `narration` goes to the cheapest model first (local Ollama). `strategy` goes to the
best first (Gemini Flash high).

The service keeps each model within its requests-per-minute budget. After a rate limit or repeated
failures it rests that model and falls through to the next provider. When every model is resting,
it waits briefly for one to come free.
Each tool's own bring-your-own-key model picker (VS Code Copilot Chat → Manage Models;
Antigravity Settings → Customizations) still works alongside it.

## Roadmap

**North star (re-pointed 2026-10-10):** one game, playable start to finish on iOS, Xbox and PC.
It is the same game on all three; only packaging, input defaults, the scalability tier and platform
services differ. The ladder to each platform, and the owner decisions it waits on, are in
`roadmap/MILESTONES_PLATFORMS.md`. The Epics it adds are Track S (`roadmap/platform-release.md`,
145–153). Platform rules: no `#if PLATFORM_*` in gameplay systems (differences live in data or
behind `UPSPlatformServices`), and a rung counts only on a packaged build on that platform's
hardware.

All open work is tracked in `ROADMAP.md` (repo root): a 25-Epic vertical-slice-first core
(Phases 0–4, inline) plus 128 expansion Epics (26–153) in themed track files under `roadmap/` —
the index table, size (S/M/L/XL) and mode (code/editor/mixed) legend are at the top of
`ROADMAP.md`. When picking up work, follow the platform tiers in `ROADMAP.md` (Tier 0, getting a
packaged game out of the editor, comes first), then choose a story from the earliest unblocked Epic
(per its "Depends on" line and `roadmap/PARALLEL.md`), match the Epic's size/mode to the session,
and tick its checkbox in the same PR that completes it. Read only your active Epic's section or
track file — never the whole roadmap.
