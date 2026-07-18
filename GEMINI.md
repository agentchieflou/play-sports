# Antigravity-specific notes

Shared context (architecture, conventions, roadmap workflow) is in `AGENTS.md` at repo root —
Antigravity loads that automatically alongside this file; this file only adds what is specific to
Antigravity. Read `ROADMAP.md` before starting work and follow the story-picking rule in
`AGENTS.md`'s "Roadmap" section.

## Connectivity

- **MCP servers:** Antigravity does not read this repo's `.mcp.json` or `.vscode/mcp.json`
  (those belong to Claude Code and VS Code/Copilot respectively). Antigravity's MCP config is
  global: `~/.gemini/config/mcp_config.json`, key `mcpServers`, remote servers use `serverUrl`
  (not `url`). No server exists yet to register — once Epic 25 (`ROADMAP.md`) ships the real
  AgenticLink MCP bridge, merge an entry there; a template is in `AGENTS.md` under "MCP servers".
- **Models:** Antigravity already runs on Gemini natively — the `GEMINI_API_KEY` slot in
  `.env.example` is for *other* tools reaching Gemini, not for Antigravity itself. Additional
  models (Ollama, OpenRouter) attach via Settings → Customizations, not via repo config.

## Working in this repo

- Windows dev environment; no confirmed local Unreal Editor/UnrealBuildTool install. Per
  `AGENTS.md`'s "Build & verification reality check": do not claim C++ changes were built or
  tested — review for syntax/API correctness and say that's what you did.
- Match existing UE conventions exactly (4-space indent, Allman braces, `PS*` type prefixes,
  Blueprint-accessible `UCLASS`/`USTRUCT` patterns) — see `AGENTS.md` "Conventions".
- When completing a roadmap story, tick its checkbox in `ROADMAP.md` in the same change.

## Agent scoping playbook

Token efficiency is a project requirement, not a preference. Every agent session follows these
rules:

**One roadmap story = one agent = one conversation.** Pick the earliest unblocked story per
`ROADMAP.md`'s dependency rules. Never assign a whole Epic, or multiple stories, to one session.

**Pick the matching archetype and load only its skill** (skills live in `.agents/skills/`; they
load on demand — that is the point, don't preload them all):

| Archetype | Skill | Works in | Must NOT read |
|---|---|---|---|
| Gameplay C++ engineer | `gameplay-cpp-story` | `Source/PlaySports/`, plugin C++ | `Data/` beyond the story's needs; unrelated systems |
| Data/content author | `data-content-author` | `Data/` only | `Source/` (the skill carries the full data contract) |
| AI/behavior specialist | `ai-behavior-specialist` | Phase 2 (Epics 14–18) AI/playbook work | Rendering, audio, meta-game files |
| Reviewer/verifier | `review-verify` | The diff under review + `ROADMAP.md` | Whole source trees |

**Context budget rules:**

- Never re-read what `AGENTS.md` already summarizes (architecture, dependency lists, data
  shapes) — trust the summary; open source files only when the story touches them.
- Never load `ROADMAP.md` wholesale into working context — read only the active Epic's section.
- Prefer the cheapest sufficient model for Data/content and Reviewer work (free-tier slots per
  `AGENTS.md` once the Epic 25 router exists; until then, the smallest model in the picker).

**Agent Manager (parallel agents):**

- Parallelize only stories with non-overlapping file scopes; use each Epic's "Depends on" line
  to find parallel-safe work.
- The Epic 25 infrastructure track (`Plugins/`) never overlaps gameplay stories — always safe to
  run alongside.
- One agent per scope: never point two agents at the same files in the same workspace.
