# Track K — Engineering, Infrastructure & Agentic Tooling (Epics 112–120)

The machinery that keeps a 125-Epic project buildable, testable, performant, and workable by
agents. Highest-leverage track for the agentic workflow: several epics here multiply every
other agent's effectiveness. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** Epics 112/113/116/120 are **done and live** (CI +
linter/validator + save architecture + eval gym). Epic 115 builds on C1's ring-buffer history;
Epic 114's counters include bus throughput and per-system tick cost (the review flagged
per-frame cast/copy patterns ×22 pawns — C3 fixes the known ones, 114 guards regressions).
Epic 118's job format serves the growing `Specs/` editor backlog. Track P
(`roadmap/agent-orchestration.md`) seeds Epic 119's client layer (`tools/orchestrator/models/`
— 119's MCP service wraps it, never twins it) and extends Epic 120's scorer via the
`tools/score_lib.py` refactor.

### Epic 112: UE Build CI Pipeline

**Size/Mode:** L / code
**Goal:** Every push gets compiled and tested by a real UE toolchain in the cloud — ending the "syntax review only" era.
**Depends on:** — (unblocks honest verification for every C++ epic)

- [x] CI environment selection and setup (self-hosted Windows runner + local UE 5.8 — tradeoff documented in `Specs/ADR_CI_Environment.md`)
- [x] Compile job: `UnrealBuildTool` build of the project + plugins on push/PR
- [x] Test job: headless automation-test run (Core 24 suites) with reported results
- [x] PR gating + status surfacing agents can read via `gh`
- [x] `AGENTS.md` update: verification claims may cite CI results once this lands

### Epic 113: Asset & Convention Validation Automation

**Size/Mode:** S / code
**Goal:** Naming/structure conventions are enforced by tooling, not reviewer vigilance.
**Depends on:** —

- [x] Convention linter script (PS* prefixes, Public/Private placement, Allman/indent checks) runnable locally and in CI (112)
- [x] Data validation runner: all `Data/` JSON against contracts (extends Core 21's validation as a CLI)
- [x] Hook into review flow: `review-verify` skill delegates mechanical checks to the linter

### Epic 114: Performance Budget & Profiling Harness

**Size/Mode:** M / code
**Goal:** Frame-time budgets per system, measured continuously — 22 physics agents + crowd + overlays must coexist.
**Depends on:** Core 17

- [x] Budget definition per subsystem (sim, animation, crowd, overlays, audio) vs. 60fps target *(per tier in `Data/platform_tiers.json`: a `TargetFrameRate` and a game-thread ms budget for each of Simulation, AI, Telemetry, Overlays, UI, Animation, Crowd and Audio, validated to fit the frame; the table and its rationale are `Specs/Platform_Audit.md` section 7. Proposed figures until a device measures them)*
- [x] Automated profiling scenario: standard play under full load, per-system timings captured *(`UPSPerfHarness`: 22 AI players, the ball, snap, rush, throw, catch, pursuit, tackle and the next pre-snap, each frame stepping the game's systems at the tier's AI rate; `PSPerf` times each system exclusively into Epic 117's frame-time histograms and reports mean, p50, p95 and max against the tier's budgets. Headless in CI (`PlaySports.Perf.StandardPlayProfile`), and on a device through `PS.Perf.RunHarness` or `PS.Perf.Capture <seconds>` for real play; no device numbers yet)*
- [x] Budget regression detection in CI (112) with trend history *(CI's Performance budgets step, `tools/perf_budget.py`: over budget warns, over `HardFailMultiplier` times it fails, a p95 regression against the median of the last runs on main warns; pushes to main append to the runner's history; the report is the `perf-report` artifact and the step summary's table. Tolerances in `Data/perf_harness.json`)*
- [x] `stat`-command custom counters for the game's own systems (telemetry bus, BT evaluations) *(`stat PlaySports`: a cycle counter per budgeted system, and per-frame counts of bus events, AI decisions (what BT evaluations would be: the controllers' trees are unused mirrors) and field scans; `stat PSAI` and `stat PSTelemetrySampling` keep their finer counters)*

### Epic 115: Determinism & Replay Serialization Format

**Size/Mode:** L / code
**Goal:** One canonical, versioned format for recorded plays — the backbone of replay (41), debugging (85), calibration (83), and online (108).
**Depends on:** Core 17, 26

- [x] Deterministic-simulation audit: RNG discipline, float stability, tick-order guarantees (findings doc: `Specs/Determinism_Audit.md`)
- [x] Serialization schema: initial state + input/event stream + version header (`PSReplayFormat.h`, JSON via `FJsonObjectConverter`)
- [ ] Record/playback round-trip test: identical outcomes or diagnosed divergence report
- [x] Migration policy for format versioning across releases (policy in `Specs/Determinism_Audit.md`; step-wise version gate implemented + tested)
- [x] Divergence bisection tool: find the first tick where two runs differ *(as built: `UPSDeterminism::FindFirstDivergence` / `DescribeDivergence` (Epic 24) report the first event, tick and field where two `FPSReplayRecording`s differ)*

### Epic 116: Save System Architecture

**Size/Mode:** M / code
**Goal:** One versioned save architecture for franchise state, settings, profiles, and replays — before Track G scatters ad-hoc `SaveGame`s.
**Depends on:** — (should land before Core 20's save story is implemented)

- [x] Save architecture design: slots, categories (profile/franchise/replay), versioning + migration
- [x] Serialization implementation with corruption detection and backup-on-write
- [x] Async save/load with UI states (Track I)
- [x] Schema-migration test harness (old saves load forever)

### Epic 117: Crash Reporting & Session Telemetry

**Size/Mode:** S / code
**Goal:** When the game breaks in the field, we learn about it — crash capture and anonymous session health.
**Depends on:** —

- [x] Crash reporter configuration with symbolized stacks and repo issue routing *(as built: `Config/DefaultGame.ini` ships the crash client and .pdb files, `[CrashReportClient]` in `DefaultEngine.ini` uploads nothing; `FPSCrashContext` puts the session and bus breadcrumbs in the report's game data; `tools/crash_report.py` summarizes reports (CI prints them after the tests) and files one `crash` issue per signature, reopening regressions)*
- [x] Session telemetry (opt-in): mode usage, play counts, perf percentiles *(as built: `UPSSessionTelemetrySubsystem` counts plays from the bus and frame-time percentiles per game world; an opted-in player's sessions go to `UPSSessionTelemetrySave`, saved open and closed at cleanup so a crash reads as an unclean session; `BuildReport` gives mode usage and session health; tuning in `Data/session_telemetry.json`. The consent prompt itself is front-end UI, not built)*
- [x] Privacy policy + data-minimization documentation *(as built: `Specs/Privacy_Telemetry.md`)*

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

- [x] Router service honoring `OLLAMA_HOST`/`GEMINI_API_KEY`/`OPENROUTER_API_KEY` with health checks *(`tools/orchestrator/service.py` `RouterService` wraps Epic 135's `ModelRouter` (the routing table's tasks become extra tiers; `chat` gained a gate and an observer instead of a second fallback loop). Health from configuration, cooldowns and budgets with no network call, or `live` with one minimal call per configured model cached for `health_ttl_seconds`. CLI: `routes`, `route <task>`. Tests mock every client; no live model was called)*
- [x] Capability/cost routing table (narration→cheap local, strategy→better remote) consumed by 82/96 *(`tools/orchestrator/routing.json`: models are config.py's env-driven specs with a capability, a cost and a request budget; tasks `narration`, `summary`, `analysis`, `strategy` and `delegate` name their consumers (Epics 82 and 96) and route cheapest-first or best-first from a minimum capability)*
- [x] Fallback chains and rate-limit handling across providers *(on top of each call's Retry-After retries: per-model requests-per-minute budgets, a cooldown after a 429 or repeated failures so the chain skips that provider, and a bounded wait when every configured model is resting; Gemini high and low share one budget)*
- [ ] MCP surface: registered in `.mcp.json`/`.vscode/mcp.json`/Antigravity global config per `AGENTS.md` — *the stdio MCP server is built (`python -m tools.orchestrator mcp`: `route_task`, `list_routes`, `router_health`; tested over in-memory stdio) and its three entries are documented in `AGENTS.md`; registering it in the repo files waits on the owner, as 25.4's does*

### Epic 120: Agent Evaluation Gym

**Size/Mode:** M / code
**Goal:** Agent contributions are themselves measured — scored tasks that tell us which models/archetypes produce mergeable work.
**Depends on:** 112, 113

- [x] Benchmark task set: representative stories (C++ system, data authoring, review) with objective scoring rubrics
- [x] Harness: run a task through an agent/model combo, score via CI + linter + review checklist
- [x] Scorecard history informing the GEMINI.md playbook's model-assignment guidance
- [x] Regression alerts when a model/archetype pairing degrades
