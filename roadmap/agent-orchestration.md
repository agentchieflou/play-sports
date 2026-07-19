# Track P — Agent Orchestration Graph (Epics 135–138)

The prose six-role pipeline (`GEMINI.md`, `.agents/skills/`) becomes a runnable program
under `tools/orchestrator/`: model clients honoring the `.env` contract, a worker harness
that lets cheap models complete roadmap stories in isolated worktrees, a benchmark duel mode
that races two workers on the same story, and a supervisor graph that dispatches N workers
concurrently from `roadmap/PARALLEL.md`. Model tiers: supervisor = Gemini 3.5 Flash (high)
via `GEMINI_API_KEY`; workers = GPT-OSS-120B via `OPENROUTER_API_KEY`, falling back to
Gemini 3.5 Flash (low) on the same Gemini key. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** This track **extends, never twins,** Epics 119/120 and
Core 25: Epic 119's future MCP router service *wraps* Epic 135's client layer
(`tools/orchestrator/models/`), and Core 25's ".env model router" story consumes it; Epic
137 refactors (does not fork) `tools/score_agent_run.py` into an importable `tools/score_lib.py`.
The `.agents/skills/` prose remains the behavioral source of truth — the worker harness
renders its system prompts *from* the matching coder skill, and the supervisor enforces the
`supervisor-orchestrator`/`phase-runner` rules (one story = one run, failure→fresh-worker,
hard stops) in code. Nothing here touches UE C++; the whole track is Python, locally
testable, CI-safe.

Agreed module layout:

```
tools/orchestrator/
  __init__.py  __main__.py        # CLI: run | duel | graph | status | resume | check-parallel
  config.py                       # .env loading, model-tier table
  models/   base.py gemini.py openrouter.py router.py
  worker/   workspace.py tools.py harness.py prompts.py
  supervisor/ board.py state.py graph.py
  duel.py
tools/score_lib.py                # extracted from score_agent_run.py (Epic 137)
```

Run-state schema (implemented in Epic 138, sketched here as the contract):
`{version, run_id, mode: graph|duel|single, started_at, parallel_matrix_sha, stories:
{"<epic>.<story>": {status: pending|assigned|coding|testing|review|pr_open|merged|failed|escalated,
worker, worktree, branch, attempts, pr, score, events[]}}, duels[], escalations[]}` stored at
`eval/runs/<run-id>.json`.

### Epic 135: Model Client Layer

**Size/Mode:** M / code
**Goal:** One importable Python layer turns the `.env` contract into working model calls with tiers, retries, and fallback.
**Depends on:** — (seed of Epic 119; Core 25's router story consumes this)

- [x] `tools/orchestrator/` scaffold + `config.py`: `.env` parsing (stdlib only), model-tier table — supervisor = Gemini 3.5 Flash high (`GEMINI_API_KEY`); worker = GPT-OSS-120B (`OPENROUTER_API_KEY`); worker fallback = Gemini 3.5 Flash low (same Gemini key); `OLLAMA_HOST` slot documented as reserved, unimplemented
- [x] `models/base.py`: `ModelClient` protocol — chat completion with tool/function-calling, usage accounting, retry with rate-limit backoff
- [x] Concrete clients `models/gemini.py` and `models/openrouter.py` (stdlib HTTP, no SDK dependencies)
- [x] `models/router.py`: tier→client resolution with health checks and the worker fallback chain (openrouter → gemini-flash-low) — documented as the layer Epic 119's MCP service will wrap
- [x] Unit tests with mocked HTTP for both clients and the fallback chain

### Epic 136: Worker Harness & Isolated Worktrees

**Size/Mode:** L / code
**Goal:** A cheap model can actually complete one roadmap story: an agentic tool loop jailed to its own git worktree, with the harness (not the model) owning git.
**Depends on:** 135

- [ ] `worker/workspace.py`: worktree lifecycle — create `.worktrees/<branch>` off `main` (branch named per the `git-steward` convention), diff capture via `git diff main...HEAD`, cleanup; `.worktrees/` gitignored
- [ ] `worker/tools.py`: model-facing tools — `read_file`, `write_file`, `list_dir`, `grep`, `run_check` (allowlist only: `lint_conventions.py`, `validate_data.py`, `pytest`), `finish(summary)`; all paths resolved and jailed to the worktree root, per-file size caps, per-run write caps
- [ ] `worker/harness.py`: the loop — system prompt rendered from the matching coder-skill prose plus the story assignment; executes tool calls until `finish` or budget exhaustion (max iterations, token cap); full transcript persisted for scoring
- [ ] Single-run CLI `python -m tools.orchestrator run --story <epic>.<story>`: worker completes the story; the harness commits, pushes, and opens the PR via `gh` — the model never runs git
- [ ] Dry-run mode (diff printed, no push/PR) + harness tests driven by a scripted fake model

### Epic 137: Benchmark Duel Mode

**Size/Mode:** M / code
**Goal:** Two workers race the same story in separate worktrees; objective scores plus a supervisor-model judgment pick the branch that proceeds to PR — Epic 120's eval gym, made head-to-head.
**Depends on:** 136 (extends Epic 120)

- [ ] Refactor `tools/score_agent_run.py`: extract scoring into importable `tools/score_lib.py` (CLI behavior byte-identical) and add a local-diff path — scope/lint/data/tests-added computable pre-PR without `gh`
- [ ] `duel.py`: same story, two worker configs, two worktrees; both diffs and transcripts captured
- [ ] Judge pass: the supervisor model compares both diffs against the story's acceptance criteria → structured verdict, combined with objective local scores into a winner (tie → cheaper model wins)
- [ ] Duel records in `eval/duels/<id>.json`; `tools/eval_report.py` extended to fold duel outcomes into `eval/SCORECARD.md`
- [ ] Winner flow: winning branch proceeds to PR via the 136 pipeline; loser worktree archived to the transcript store, then removed

### Epic 138: Supervisor Graph Mode

**Size/Mode:** L / code
**Goal:** A Gemini-Flash supervisor reads `roadmap/PARALLEL.md` and the board, dispatches N concurrent workers on disjoint stories, and survives restarts via a run-state file.
**Depends on:** 136, 137

- [ ] `supervisor/board.py`: roadmap crawler — parse `ROADMAP.md` + all track files into epics/stories with checkbox state and the Depends-on graph; includes a `check-parallel` command validating `roadmap/PARALLEL.md` against the crawl (CI-runnable drift check)
- [ ] `supervisor/state.py`: versioned run-state JSON at `eval/runs/<run-id>.json` — crash-safe writes, load-and-resume
- [ ] `supervisor/graph.py`: supervisor loop — Gemini Flash (high) gets the PARALLEL.md groups + board state, assigns unblocked disjoint stories to a worker pool (thread pool over 136 harnesses); one-story-one-run and file-scope disjointness enforced in code, not just in the prompt
- [ ] Failure routing per the `supervisor-orchestrator` skill: stage failure → fresh worker attempt with findings (max 2) → escalation entry; hard stop conditions mirrored from `phase-runner` (repo-state surprises, merge conflicts, mis-sized stories)
- [ ] `status`/`resume` CLI + end-to-end smoke: a 2-worker graph run over two trivially disjoint doc-only stories
