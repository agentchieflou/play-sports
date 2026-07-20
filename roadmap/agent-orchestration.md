# Track P — Agent Orchestration Graph (Epics 135–138)

Architecting and implementing the Python multi-agent orchestrator under `tools/orchestrator/`. Translates the prose six-role pipeline (Supervisor, Planner, Coder, Tester, Reviewer, Git) into a programmatic orchestrator graph where a Gemini 3.5 Flash high supervisor model assigns tasks to cheap worker models (GPT-OSS-120B on OpenRouter or Gemini 3.5 Flash low). Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** This track extends, but does not duplicate, infrastructure from Epics 119 and 120. Epic 119 (Model Router Service) and Epic 120 (Agent Evaluation Gym) wrap and reuse client layers and scoring mechanisms built here. The prose skills in `.agents/skills/` remain the behavioral source of truth; the harness renders prompts *from* them.

```
tools/orchestrator/
  __init__.py
  __main__.py        # CLI: run | duel | graph | status | resume | check-parallel
  config.py          # .env loading (OLLAMA_HOST, GEMINI_API_KEY, OPENROUTER_API_KEY)
  models/
    base.py          # ModelClient protocol and interfaces
    gemini.py        # Gemini client (using raw HTTP to avoid SDK dependencies)
    openrouter.py    # OpenRouter client (raw HTTP)
    router.py        # Tier-to-client mapper, worker fallback chain
  worker/
    workspace.py     # Git worktree isolation in .worktrees/
    tools.py         # Worker agent tool sandbox (jailed read/write/grep/run)
    harness.py       # Worker prompt formatting, agent loop execution
    prompts.py       # System prompt builders
  supervisor/
    board.py         # Track crawler, dependency graph analyzer
    state.py         # Run state JSON tracker (eval/runs/<run-id>.json)
    graph.py         # Concurrent scheduler, parallel dispatch loop
  duel.py            # Head-to-head worker comparison logic
tools/score_lib.py   # Refactored library extracted from tools/score_agent_run.py
```

### Epic 135: Model Client Layer

**Size/Mode:** M / code
**Goal:** HTTP client implementations for Gemini and OpenRouter are built, providing clean interfaces for the orchestrator.
**Depends on:** —

- [ ] Implement `tools/orchestrator/config.py` parsing `.env` file credentials using standard Python libraries
- [ ] Implement `tools/orchestrator/models/base.py` defining standard response types, message interfaces, and tool call schemas
- [ ] Implement `tools/orchestrator/models/gemini.py` supporting Gemini 3.5 Flash APIs via raw HTTPS requests, handling error codes and backoffs
- [ ] Implement `tools/orchestrator/models/openrouter.py` supporting OpenRouter API integrations (instruct/chat endpoints, tool schema handling)
- [ ] Implement `tools/orchestrator/models/router.py` mapping role demands to client configurations (Supervisor -> Gemini Flash high, Worker -> OpenRouter gpt-oss-120b with fallback to Gemini Flash low)
- [ ] Local tests with mocked HTTP endpoints verifying connection, routing, API key retrieval, and fallback execution

### Epic 136: Worker Harness & Isolated Worktrees

**Size/Mode:** L / code
**Goal:** A sandbox environment executes worker agents in clean git worktrees with strict file boundaries.
**Depends on:** 135

- [ ] Implement `tools/orchestrator/worker/workspace.py` managing git worktrees under a gitignored `.worktrees/` directory, checking out main and naming branches per `git-steward` convention
- [ ] Implement `tools/orchestrator/worker/tools.py` exposing jailed tools (file reads, file writes, grep search, run check-allowlist) limited to the worktree directory
- [ ] Implement `tools/orchestrator/worker/harness.py` combining the system prompt (templated from `.agents/skills/`) and story details into a tool call execution loop
- [ ] Build the harness commit step, committing worker work to the local worktree, pushing, and opening a PR via CLI `gh` commands
- [ ] Expose single-agent runner: `python -m tools.orchestrator run --story <epic.story> [--dry-run]`
- [ ] Offline harness tests utilizing mock agents and scripted file-operation histories

### Epic 137: Benchmark Duel Mode

**Size/Mode:** M / code
**Goal:** Two worker configurations compete on the same story, with their changes compared and judged.
**Depends on:** 136

- [ ] Extract a reusable `tools/score_lib.py` from `tools/score_agent_run.py` that computes scores from local diffs instead of requiring a pushed PR
- [ ] Implement `tools/orchestrator/duel.py` spawning two parallel worker harnesses in independent worktrees for the same story
- [ ] Implement the Supervisor judge prompt comparing both worktree diffs against the plan's acceptance criteria
- [ ] Combine supervisor judgment with objective scores from `score_lib` to choose the winning implementation
- [ ] Write duel reports to `eval/duels/<id>.json` and integrate results into `eval/SCORECARD.md` via `tools/eval_report.py`
- [ ] Automation test: running a mock duel on a doc-only story records scorecard data and clean-merges the winner

### Epic 138: Supervisor Graph Mode

**Size/Mode:** L / code
**Goal:** A coordinator loop reads the roadmap, schedules non-overlapping stories, and dispatches workers concurrently.
**Depends on:** 136, 137

- [ ] Implement `tools/orchestrator/supervisor/board.py` crawling `ROADMAP.md` and track files to construct a story dependency graph
- [ ] Implement `tools/orchestrator/supervisor/state.py` persisting execution state in `eval/runs/<run-id>.json`
- [ ] Implement the scheduler assigning unblocked, disjoint stories to worker sessions based on `roadmap/PARALLEL.md` scope rules
- [ ] Implement supervisor feedback routing: routing failed tests/reviews back to worker harnesses with error contexts
- [ ] Expose graph CLI controls: `python -m tools.orchestrator graph status|resume|check-parallel`
- [ ] Smoke test: coordinate two mock workers completing two disjoint, document-only stories concurrently
