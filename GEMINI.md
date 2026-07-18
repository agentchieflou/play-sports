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
