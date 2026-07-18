# Track K — Engineering, Infrastructure & Agentic Tooling (Epics 112–120)

The machinery that keeps a 125-Epic project buildable, testable, performant, and workable by
agents. Highest-leverage track for the agentic workflow: several epics here multiply every
other agent's effectiveness. Sizing/mode legend: see `ROADMAP.md`.

### Epic 112: UE Build CI Pipeline

**Size/Mode:** L / code
**Goal:** Every push gets compiled and tested by a real UE toolchain in the cloud — ending the "syntax review only" era.
**Depends on:** — (unblocks honest verification for every C++ epic)

- [ ] CI environment selection and setup (GitHub Actions with UE container/self-hosted runner — documented tradeoff)
- [ ] Compile job: `UnrealBuildTool` build of the project + plugins on push/PR
- [ ] Test job: headless automation-test run (Core 24 suites) with reported results
- [ ] PR gating + status surfacing agents can read via `gh`
- [ ] `AGENTS.md` update: verification claims may cite CI results once this lands

### Epic 113: Asset & Convention Validation Automation

**Size/Mode:** S / code
**Goal:** Naming/structure conventions are enforced by tooling, not reviewer vigilance.
**Depends on:** —

- [ ] Convention linter script (PS* prefixes, Public/Private placement, Allman/indent checks) runnable locally and in CI (112)
- [ ] Data validation runner: all `Data/` JSON against contracts (extends Core 21's validation as a CLI)
- [ ] Hook into review flow: `review-verify` skill delegates mechanical checks to the linter

### Epic 114: Performance Budget & Profiling Harness

**Size/Mode:** M / code
**Goal:** Frame-time budgets per system, measured continuously — 22 physics agents + crowd + overlays must coexist.
**Depends on:** Core 17

- [ ] Budget definition per subsystem (sim, animation, crowd, overlays, audio) vs. 60fps target
- [ ] Automated profiling scenario: standard play under full load, per-system timings captured
- [ ] Budget regression detection in CI (112) with trend history
- [ ] `stat`-command custom counters for the game's own systems (telemetry bus, BT evaluations)

### Epic 115: Determinism & Replay Serialization Format

**Size/Mode:** L / code
**Goal:** One canonical, versioned format for recorded plays — the backbone of replay (41), debugging (85), calibration (83), and online (108).
**Depends on:** Core 17, 26

- [ ] Deterministic-simulation audit: RNG discipline, float stability, tick-order guarantees (findings doc)
- [ ] Serialization schema: initial state + input/event stream + version header
- [ ] Record/playback round-trip test: identical outcomes or diagnosed divergence report
- [ ] Migration policy for format versioning across releases
- [ ] Divergence bisection tool: find the first tick where two runs differ

### Epic 116: Save System Architecture

**Size/Mode:** M / code
**Goal:** One versioned save architecture for franchise state, settings, profiles, and replays — before Track G scatters ad-hoc `SaveGame`s.
**Depends on:** — (should land before Core 20's save story is implemented)

- [ ] Save architecture design: slots, categories (profile/franchise/replay), versioning + migration
- [ ] Serialization implementation with corruption detection and backup-on-write
- [ ] Async save/load with UI states (Track I)
- [ ] Schema-migration test harness (old saves load forever)

### Epic 117: Crash Reporting & Session Telemetry

**Size/Mode:** S / code
**Goal:** When the game breaks in the field, we learn about it — crash capture and anonymous session health.
**Depends on:** —

- [ ] Crash reporter configuration with symbolized stacks and repo issue routing
- [ ] Session telemetry (opt-in): mode usage, play counts, perf percentiles
- [ ] Privacy policy + data-minimization documentation

### Epic 118: Autonomix Editor Automation Depth

**Size/Mode:** L / code
**Goal:** Expands core Epic 25's Autonomix into a batch-capable editor automation platform — the "editor-mode" epics' force multiplier.
**Depends on:** Core 25

- [ ] T3D template library: parameterized actor-spawn templates (stadium modules, formation markers, camera rigs)
- [ ] Batch operation engine: manifest-driven multi-asset operations in one transaction
- [ ] Python operation catalog: import/configure/validate scripts agents can invoke via the bridge
- [ ] Editor-session job format: an agent authors a job file; an editor-equipped session (human or bridged) executes and reports
- [ ] Result verification: post-operation state dump compared against the job's expected outcome

### Epic 119: Model Router Service

**Size/Mode:** M / code
**Goal:** The `.env` free-tier contract becomes a running service — tasks route to Ollama/Gemini/OpenRouter by cost and capability.
**Depends on:** Core 25

- [ ] Router service honoring `OLLAMA_HOST`/`GEMINI_API_KEY`/`OPENROUTER_API_KEY` with health checks
- [ ] Capability/cost routing table (narration→cheap local, strategy→better remote) consumed by 82/96
- [ ] Fallback chains and rate-limit handling across providers
- [ ] MCP surface: registered in `.mcp.json`/`.vscode/mcp.json`/Antigravity global config per `AGENTS.md`

### Epic 120: Agent Evaluation Gym

**Size/Mode:** M / code
**Goal:** Agent contributions are themselves measured — scored tasks that tell us which models/archetypes produce mergeable work.
**Depends on:** 112, 113

- [ ] Benchmark task set: representative stories (C++ system, data authoring, review) with objective scoring rubrics
- [ ] Harness: run a task through an agent/model combo, score via CI + linter + review checklist
- [ ] Scorecard history informing the GEMINI.md playbook's model-assignment guidance
- [ ] Regression alerts when a model/archetype pairing degrades
