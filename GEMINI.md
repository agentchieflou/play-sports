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

**One roadmap story = one pipeline run.** Pick the earliest unblocked story per `ROADMAP.md`'s
dependency rules. Never assign a whole Epic, or multiple stories, to one pipeline run.

### The six-role pipeline

Every story flows through six roles, each a **separate, stateless agent session** with its own
skill and an explicit handoff artifact. This is what makes the architecture scale: any role can
be re-run, swapped to a different model, or parallelized across stories without contaminating
the others' context.

| # | Role | Skill | Produces (handoff artifact) | Model tier |
|---|---|---|---|---|
| 1 | Supervisor | `supervisor-orchestrator` | Story assignment: story ID, coder specialization, branch name | Strong |
| 2 | Planner | `planner-story` | Implementation plan: approach, file list, acceptance criteria | Strong |
| 3 | Coder | one domain skill (below) | The diff on the story branch | Matched to size/mode |
| 4 | Tester | `test-verifier` | Test report: what was validated, how, pass/fail | Cheap |
| 5 | Reviewer | `review-verify` | Verdict + findings against plan and conventions | Mid |
| 6 | Git | `git-steward` | Clean commit(s), PR, ticked roadmap checkbox, merge | Cheap |

**Model assignment is measured, not guessed:** consult `eval/SCORECARD.md` when picking a
model tier for an archetype. After notable pipeline runs (new model, new archetype, or any
failure), the Supervisor scores the PR: `python tools/score_agent_run.py --pr <N> --agent <a>
--model <m> --archetype <arch> --task <benchmark-id>`, then `python tools/eval_report.py` —
commit both outputs. Regression alerts in the report mean: stop assigning that pairing until a
human reviews.

Failures flow backward, not forward: a failed test report returns to a **fresh** Coder session
with the plan + report; a rejected review returns likewise. The Supervisor only escalates to the
human when a story fails twice at the same stage or a plan flags a mis-sized story.

**Autonomous loop variant:** when asked to complete a whole phase/Epic range unattended, use the
`phase-runner` skill — one session plays the roles sequentially per story with role-switch
discipline, context hygiene between stories, editor-work escalation, and hard stop conditions.
Two phase-runners may run in parallel only on disjoint Epic ranges with non-overlapping files.

### Coder specializations (role 3 picks exactly one)

| Specialization | Skill | Works in | Must NOT read |
|---|---|---|---|
| Gameplay C++ engineer | `gameplay-cpp-story` | `Source/PlaySports/`, plugin C++ | `Data/` beyond the story's needs; unrelated systems |
| Data/content author | `data-content-author` | `Data/` only | `Source/` (the skill carries the full data contract) |
| AI/behavior specialist | `ai-behavior-specialist` | Core Phase 2, Track E/F AI work | Rendering, audio, meta-game files |

**Context budget rules:**

- Never re-read what `AGENTS.md` already summarizes (architecture, dependency lists, data
  shapes) — trust the summary; open source files only when the story touches them.
- Never load the roadmap wholesale into working context — read only the active Epic's section
  of `ROADMAP.md` (Epics 1–25) or its single track file under `roadmap/` (Epics 26–125).
- Match Epic **size/mode labels** (legend in `ROADMAP.md`) to the session: `S`/`M` code-mode
  Epics suit cheap models; `editor`-mode Epics produce specs and job files only (no local UE).
- Prefer the cheapest sufficient model for Data/content and Reviewer work (free-tier slots per
  `AGENTS.md` once the Epic 25 router exists; until then, the smallest model in the picker).

**Agent Manager (parallel agents):**

- Parallelism happens at the **pipeline-run level** (multiple stories in flight), never by
  splitting one story's roles across concurrent agents — roles within a run are sequential.
- Parallelize only stories with non-overlapping file scopes; use each Epic's "Depends on" line
  to find parallel-safe work. Each parallel run gets its own branch (git-steward names it).
- The infrastructure track (core Epic 25, Track K in `roadmap/engineering-infra.md`) never
  overlaps gameplay stories — always safe to run alongside.
- One agent per scope: never point two agents at the same files in the same workspace.
- One Supervisor session owns the board at a time — it dispatches runs; it never competes with
  another Supervisor.
