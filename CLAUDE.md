@AGENTS.md

## Claude Code specifics

- No custom `.claude/agents` or skills exist in this repo yet — general-purpose behavior applies.
- Project-scoped MCP servers live in `.mcp.json` at repo root (currently empty — see
  "Agentic workflow / tool connectors" in `AGENTS.md`).
- This is a Windows dev environment (PowerShell primary, Bash tool also available) with no
  confirmed local Unreal Editor/UBT install — see "Build & verification reality check" in
  `AGENTS.md` before claiming any C++ change was built or tested.

## Shared-checkout hazard (learned 2026-07-18)

Antigravity's phase-runner works directly in the primary checkout
(`AntiRepo/play-sports`) and switches branches mid-run. While any Antigravity loop is
active, do all Claude Code repo work from a separate git worktree (e.g.
`git worktree add ../wt-claude origin/main -b <branch>`) — never branch from or commit in
the primary checkout, or you'll base work on an unmerged story branch and collide with the
runner's checkouts.
