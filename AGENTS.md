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
Plugins/Autonomix/            Editor-time AI bridge plugin (stub)
Plugins/AgenticLink/          External agent bridge plugin (stub)
Data/                         External data assets consumed by ingestion code
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

"Headless AI bridge for T3D injection and Unreal Python operations" — **stub only**. The module
(`Developer` type, `PostEngineInit`) currently just logs on startup/shutdown. Depends on
`EditorScriptingUtilities` and `PythonScriptPlugin`, signaling the intended direction: this is
where in-editor agent actions (spawning/mutating actors via T3D text import, Python scripting)
are meant to live once built.

### `Plugins/AgenticLink`

"External agent bridge for Model Context Protocol and transaction-safe engine access" — **stub
only**, same shape as Autonomix. Depends on `Slate`/`SlateCore` (likely for an in-editor status
UI). This is the intended home for an actual MCP server/bridge connecting external agents to the
engine's reflection API — not implemented yet.

**Do not assume either plugin does anything beyond logging a startup message.** If a task requires
real editor automation or an MCP bridge, that is new implementation work, not a wiring task.

### `Data/`

`sample_players.json` — example payload for `PSDataIngestion`, two rows (`QB_001`, `OL_001`)
matching the `FPlayerAttributes` field names exactly (`PlayerId`, `DisplayName`, `Role`,
`WeightKg`, `HeightCm`, `Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness`).

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
They're placeholders: once `Plugins/AgenticLink` actually exposes engine operations over MCP, or a
model-router server is built, register it in both files using each one's own key name.

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

(Note `serverUrl`, not `url` — Antigravity's key name differs from VS Code/Cursor.)

### Free-tier / local model slots

No bridge code wires these in yet — this is just the env-var contract future connector code (or
each tool's own model picker) should read from `.env` (copy `.env.example`):

- `OLLAMA_HOST` — local Ollama instance (default `http://localhost:11434`), zero cost, works
  offline. Useful for CI or throwaway/parallel agent tasks.
- `GEMINI_API_KEY` — Google AI Studio free tier. Antigravity itself runs on Gemini already; this
  is for reaching Gemini from *other* tools (e.g. a future MCP server, or Claude Code shelling out
  for a second opinion).
- `OPENROUTER_API_KEY` — OpenRouter's free-tier community models via one API, for whichever
  provider isn't otherwise covered.

Until a real router/bridge exists, the practical way to use these today is each tool's own
bring-your-own-key model picker (e.g. VS Code Copilot Chat → Manage Models; Antigravity Settings →
Customizations) rather than anything in this repo.

## Roadmap

All open work is tracked in `ROADMAP.md` (repo root): a 25-Epic vertical-slice-first core
(Phases 0–4, inline) plus 100 expansion Epics (26–125) in themed track files under `roadmap/` —
the index table, size (S/M/L/XL) and mode (code/editor/mixed) legend are at the top of
`ROADMAP.md`. When picking up work, choose a story from the earliest unblocked Epic (per its
"Depends on" line), match the Epic's size/mode to the session, and tick its checkbox in the same
PR that completes it. Read only your active Epic's section or track file — never the whole
roadmap. The agentic-infrastructure work (core Epic 25 and Track K, `roadmap/engineering-infra.md`)
has no gameplay dependencies and can proceed in parallel; Track K's Epic 112 (UE build CI) is the
single highest-leverage unblocker for honest C++ verification.
