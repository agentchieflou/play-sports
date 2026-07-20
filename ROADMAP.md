# ROADMAP.md

Development roadmap for `play-sports`: **138 Epics** — a 25-Epic core (this file, Phases 0–4)
sequenced **vertical-slice first** (Phase 0 produces one crude but complete, watchable play as
early as possible; later phases deepen it), plus **113 expansion Epics (26–138)** in themed
track files under `roadmap/` (see the track index below).

Conventions used throughout:

- **Goal** — one-line definition of done for the Epic.
- **Builds on** — existing code in this repo the Epic extends (see `AGENTS.md` for the map).
- **Depends on** — Epic IDs that should land first. Epics with no unmet dependencies can be
  worked in parallel (including by different agents/tools).
- Stories are checkboxes so progress is trackable in-place via PRs that tick them.
- Agents: read only the section (or track file) for your active Epic — never the whole roadmap.

Status: nothing below is started unless checked. The only implemented code today is what
`AGENTS.md` describes (`PSPlaySimulation` phase ticker, `PSDataIngestion`, `PSScheduleEngine`,
`PSFunctionalGym`, `FPlayerAttributes`, and the two stub plugins).

## Sizing & mode (agent-scoping labels)

Every Epic 26+ carries a **Size / Mode** line; use it to match Epics to agent sessions:

- **Size** — `S` (one short agent session, 2–3 stories, single-file-ish scope), `M` (one solid
  session, 3–4 stories, one system), `L` (multi-session, 4–5 stories, cross-system integration),
  `XL` (a whole subsystem, 5–6 stories, expect several sessions and design decisions).
- **Mode** — `code` (pure C++/data/docs, doable by any agent per the no-UE-toolchain rule),
  `editor` (requires a human- or bridge-driven Unreal Editor session — agents produce specs,
  data, and code scaffolding only), `mixed` (code now, editor pass later; stories say which).

Core Epics 1–25 sizes for reference: 1(L) 2(M-editor) 3(L) 4(M) 5(M) 6(L) 7(L) 8(L) 9(L) 10(M)
11(L) 12(M) 13(L) 14(XL) 15(XL) 16(L) 17(XL) 18(M) 19(L) 20(L) 21(M) 22(XL-editor) 23(L-mixed)
24(L) 25(XL).

## Expansion track index (Epics 26–138)

| Track | File | Epics | Theme |
|---|---|---|---|
| A | `roadmap/broadcast-overlays.md` | 26–37 | Broadcast overlay & telemetry: route ribbons, badges, personnel panels, AR paint |
| B | `roadmap/camera-cinematics.md` | 38–45 | Camera director, skycam, replay, highlights, telestrator |
| C | `roadmap/stadium-atmosphere.md` | 46–55 | Stadium, night lighting, weather, crowd, field surface, branding |
| D | `roadmap/player-visuals.md` | 56–65 | Uniforms, likeness, animation depth, refs, gang tackles |
| E | `roadmap/gameplay-depth.md` | 66–77 | Pre-snap interaction, route/coverage/rush nuance, situational football |
| F | `roadmap/ai-depth.md` | 78–85 | Adaptive opponents, player DNA, run fits, AI observability |
| G | `roadmap/franchise-depth.md` | 86–95 | Draft, contracts, trades, morale, stats, narratives |
| H | `roadmap/audio-depth.md` | 96–100 | Commentary engine, crowd audio, broadcast mix |
| I | `roadmap/ux-ui.md` | 101–106 | Front end, play-call UI, accessibility, input feel |
| J | `roadmap/multiplayer.md` | 107–111 | Local H2H, online architecture, spectating, async leagues |
| K | `roadmap/engineering-infra.md` | 112–120 | CI, determinism, performance, save systems, agentic tooling |
| L | `roadmap/content-generators.md` | 121–125 | Procedural playbooks, rosters, team identities, stadiums |
| M | `roadmap/controller-connectivity.md` | 126–128 | Controller connectivity: Enhanced Input, player controller, Xbox gamepad, human possession, rumble/glyphs |
| N | `roadmap/platform-ports.md` | 129–131 | Platform ports (iOS first): audit, scalability tiers, touch abstraction, build pipeline |
| O | `roadmap/playbook-extraction.md` | 132–134 | One-time playbook extraction: compliance gate, polite resumable scraper, normalized play data |
| P | `roadmap/agent-orchestration.md` | 135–138 | Agent orchestration graph: model clients, worker harness, benchmark duels, supervisor graph |

Cross-cutting planning docs: `roadmap/MILESTONE_FIRST_GAME.md` (the launch-critical path to
one full playable game) and `roadmap/PARALLEL.md` (which epic groups independent agents can
work concurrently — consumed by the Track P supervisor).

---

## Phase 0 — Vertical Slice Foundation

### Epic 1: Minimum Playable Down

**Goal:** One complete snap→result loop runs in PIE using real project systems, however crude.
**Builds on:** `PSPlaySimulation`, `PSPlayerAttributes`, `PSDataIngestion`, `Data/sample_players.json`
**Depends on:** — (this is the slice; Epics 2–5 are its parts and can proceed in parallel)

- [x] Expand `Data/sample_players.json` to 22 players (11 offense, 11 defense) covering every `EPlayerRole`
- [x] `AGameModeBase` subclass (`PSGameMode`) that loads rosters via `UPSDataIngestion` at startup
- [x] Drive `UPSPlaySimulation::AdvancePlay` from the game world tick instead of manual calls
- [x] Produce a play result struct (yards gained, tackle/score/incomplete) even if randomly resolved from attributes
- [x] End-to-end PIE test: game starts → roster loads → play runs phases → result logged on screen

### Epic 2: Field & Stadium Level Scaffolding

**Goal:** A regulation-dimensioned field level exists that all gameplay Epics use.
**Depends on:** —

- [ ] Field geometry: 120yd × 53.3yd playing surface with correct UE unit scaling convention (documented)
- [ ] Yard lines, hash marks, end zones, and sidelines (materials/decals, placeholder art fine)
- [x] Field coordinate helper (`PSFieldGrid` or similar): yard-line ↔ world-position conversion functions
- [x] Out-of-bounds and end-zone trigger volumes
- [x] Default `GameMap` set in project settings so PIE opens into the field

### Epic 3: Player Pawn & Possession Framework

**Goal:** A pawn class represents any player on the field, driven by `FPlayerAttributes`.
**Builds on:** `PSPlayerAttributes.h`
**Depends on:** Epic 2

- [x] `APSPlayerPawn`: capsule + placeholder mesh, initialized from an `FPlayerAttributes` row
- [x] Simple locomotion (move-to-point) with max speed scaled from the `Speed` attribute
- [x] Ball possession state: which pawn holds the ball, handoff/transfer API
- [x] Team/side affiliation and formation spawn points (offense vs. defense lineup)
- [x] Possessable by either an `AIController` or player controller (input mapping via `InputCore`)

### Epic 4: Basic Broadcast Camera System

**Goal:** The play is watchable from a sensible camera without manual control.
**Depends on:** Epics 2, 3

- [x] Broadcast-style side camera that tracks the ball/ball-carrier
- [x] Camera bounds so it never leaves the stadium or crosses the field plane
- [x] Snap-to-formation framing pre-play, follow mode during the play
- [x] Debug free-cam toggle for development

### Epic 5: Core HUD

**Goal:** Down, distance, score, and clock are visible on screen.
**Builds on:** `UMG` dependency already in `PlaySports.Build.cs`; `FPlayState` (Down/Distance/GameTimeSeconds)
**Depends on:** Epic 1

- [x] UMG scoreboard widget bound to `FPlayState` (down, distance, game clock)
- [x] Score display (home/away) fed by the play result from Epic 1
- [x] Play-phase indicator (debug-level: show current `EPlayPhase`)
- [x] Post-play result banner (e.g. "+7 yards", "TOUCHDOWN")

---

## Phase 1 — Core Physics & Rules

### Epic 6: Physics-Based Player Movement

**Goal:** Player motion is physically simulated and attribute-driven, replacing Epic 3's move-to-point.
**Builds on:** `APSPlayerPawn` (Epic 3), `FPlayerAttributes` (`Speed`, `Agility`, `Acceleration`, `WeightKg`)
**Depends on:** Epic 3

- [x] Acceleration/deceleration curves derived from `Acceleration` and `Speed` attributes
- [x] Turning radius / change-of-direction cost derived from `Agility` and `WeightKg`
- [x] Momentum model: mass-scaled velocity that other systems (tackling, blocking) can query
- [x] Fatigue/burst hooks (data only — full stamina system deferred to Epic 19)
- [x] Tuning data table so movement feel is editable without recompiling

### Epic 7: Ball Physics

**Goal:** The ball is a physical actor: snapped, carried, thrown, caught, and dropped.
**Depends on:** Epics 3, 6

- [x] `APSBall` actor with projectile physics (spiral trajectory, gravity, bounce)
- [x] Snap: ball transfer from center to QB triggering `EPlayPhase::Snap`
- [x] Pass: throw with velocity/arc computed from target point and thrower attributes
- [x] Catch resolution: receiver radius + attribute check; drop/incompletion on failure
- [x] Handoff and pitch (lateral) transfers
- [x] Fumble state: live ball on ground, recoverable by either team

### Epic 8: Tackling & Collision Resolution

**Goal:** Defender contact with the ball carrier resolves physically into tackle outcomes.
**Builds on:** `UPSPlaySimulation` (replaces the placeholder `BallCarrierMovement → Scoring` transition)
**Depends on:** Epics 6, 7

- [x] Contact detection between defender and ball-carrier pawns
- [x] Tackle resolution: momentum + `Strength` vs. `Strength`/`Agility` contest with physics impulse
- [x] Broken-tackle branch: carrier continues with speed penalty
- [x] Down-by-contact: play-end signal into the play state machine with final ball spot
- [x] Fumble chance on high-impact hits (feeds Epic 7's fumble state)

### Epic 9: Blocking & Line-Play Physics

**Goal:** OL/DL engagements are physically contested instead of nonexistent.
**Builds on:** `EPlayPhase::PassRush` (currently a pass-through phase)
**Depends on:** Epics 6, 8

- [x] Engagement pairing: OL matches up against DL/blitzers at snap
- [x] Push/leverage contest driven by `Strength`, `WeightKg`, and momentum
- [x] Block shedding: DL win condition releases the rusher toward the QB/carrier
- [x] Pocket formation: net result of line play shapes where the QB can stand
- [x] Run lanes: blocking outcomes open/close gaps that the RB AI (Epic 14) can read

### Epic 10: Drive & Game State Machine

**Goal:** Consecutive plays chain into drives with real football bookkeeping.
**Builds on:** `UPSPlaySimulation` / `FPlayState` (`Down`, `Distance`)
**Depends on:** Epic 1

- [x] Ball spot tracking: line of scrimmage, first-down marker, yards-to-go updates from play results
- [x] Down progression: 1st–4th, turnover on downs, first-down resets
- [x] Possession changes: punts (stub until Epic 13), turnovers, post-score
- [x] Drive summary data (plays, yards, result) for HUD and future stats
- [x] Between-play reset: pawns re-form at the new line of scrimmage

### Epic 11: Scoring, Rules & Penalties Engine

**Goal:** Points and rule infractions are detected and applied correctly.
**Builds on:** `EPlayPhase::Scoring`, end-zone volumes (Epic 2)
**Depends on:** Epics 2, 10

- [x] Touchdown detection via end-zone volume + possession check (6 pts)
- [x] Field goal / extra point / two-point conversion outcomes (kick physics from Epic 13 can stub as probability first)
- [x] Safety detection (2 pts, possession change)
- [x] Penalty framework: flag, yardage, replay/loss-of-down semantics — start with offsides + holding
- [x] Rules config data asset so rule variants are data-driven, not hardcoded

### Epic 12: Game & Play Clock Management

**Goal:** A full game has quarters, a running clock, and clock-stopping rules.
**Builds on:** `FPlayState.GameTimeSeconds`
**Depends on:** Epic 10

- [x] Quarter/half structure with configurable lengths
- [x] Clock run/stop rules (incompletions, out of bounds, scores, timeouts)
- [x] 40-second play clock with delay-of-game hook into the penalty framework (Epic 11)
- [x] Two-minute warning and end-of-half/game handling
- [x] Timeout budget per team

### Epic 13: Special Teams

**Goal:** Kickoffs, punts, and field goal attempts are playable.
**Depends on:** Epics 7, 10, 11

- [x] Kick physics: power/angle model reusing `APSBall` trajectory work
- [x] Kickoff phase: kick, coverage, return, touchback handling
- [x] Punt with fair-catch and downed-ball outcomes
- [x] Field goal attempt: snap-hold-kick chain, good/no-good detection via upright zone
- [x] Special-teams formations added to the formation system (Epic 3)

---

## Phase 1.5 — Consolidation Checkpoint (clear before Phase 2)

Added after the 2026-07-19 architecture review of the first ~35 merged story PRs (findings in
`AGENTS.md` → "Architecture rules"). Phase 1 built real systems fast, but coupling, duplicated
state, and untested core gameplay must be consolidated before 22-agent AI work compounds them.
**Phase 2 must not start until C1–C4 are complete.** All four are code-mode and test-required.

### Epic C1: Telemetry/Event Bus Foundation

**Size/Mode:** M / code
**Goal:** One event layer replaces hard-cast reach-through — the foundation Track A's Epic 26 (now re-scoped as its consumer) was always going to need.
**Depends on:** —

- [x] `UPSTelemetryBus` `UWorldSubsystem`: publish/subscribe for gameplay events (snap, throw, catch, tackle, fumble, score, phase-change) with typed payloads
- [x] Ring-buffer event history with timestamps (Epic 41 replay and Epic 115 serialization consume this)
- [x] Migrate `APSBall::OnBallOverlap` phase-forcing and pawn→GameMode calls onto bus events
- [x] Migrate GameMode scoring reads to bus subscription
- [x] Automation tests: publish/subscribe round-trip, history query, event ordering

### Epic C2: Single Outcome Authority

**Size/Mode:** M / code
**Goal:** `UPSPlaySimulation` becomes the sole authority on play outcomes, fed by physical events; the competing statistical roll survives only as an explicit headless quick-sim mode.
**Depends on:** C1

- [x] Sim consumes C1 events (catch/tackle/score) instead of independent statistical rolls during physical play
- [x] `ResolvePlayResult` statistical path preserved behind an explicit quick-sim flag (Epic 20's headless season sim needs it)
- [x] Scoring, down/distance, and drive state advance from the single authority -- delete the duplicate/desynced paths
- [x] Automation tests: physical-event-driven outcome, quick-sim flag equivalence, no dual-write

### Epic C3: De-God-Class & Orphan Wiring

**Size/Mode:** L / code
**Goal:** The three overloaded classes shed responsibilities into components; built-but-unwired systems get consumed or deleted.
**Depends on:** C1

- [x] Extract `UPSPossessionComponent` (possession state + transfer API) out of `APSPlayerPawn`
- [x] Formation spawning goes through `APSFieldGrid` (`SpawnPlayersFromRoster` replaces GameMode's hardcoded spawn loop); `QBDropbackDistance`/`FormationLateralSpacing` constants shared with `ResetPawnPositions` so formation math isn't duplicated
- [x] Wire `APSBroadcastCamera` (`TargetActor` assigned from `UPSTelemetryBus::OnCatch` via C1)
- [x] Cache pawn lookups (`APSGameMode::CachedPawns`, no per-call `GetAllActorsOfClass` in `PairLinemen`/`FindPlayerPawnByRole`/`GetLargestRunLaneGap`/`ResetPawnPositions`); no per-frame tuning copies (`APSPlayerPawn::CachedGameMode` cached in `BeginPlay`, `Tick`/`InitializePlayer` reference `MovementTuningSettings` instead of re-casting+copying every call)
- [x] Naming cleanup: `UPScheduleEngine` → `UPSScheduleEngine` (struct prefix already conformant -- `tools/lint_conventions.py` only enforces `PS`/`APS` on `UCLASS` types; generic `F*` structs like `FSeasonWeek` are documented as accepted usage)
- [x] Automation tests: possession component transfer, formation spawn via FieldGrid
- [x] Fast-follow (tracked separately): extract ball-action logic (`ThrowPass`/`ExecuteHandoff`/`ExecutePitch`/`ExecuteKick`/`FumbleBall`/`ResolveTackle`, ~300 lines of physics + tackle/fumble-chance formulas) out of `APSPlayerPawn` into a dedicated component. Deferred because it's large, gameplay-critical math with no local UBT to verify against -- scope it as its own story with careful CI-round-trip iteration rather than pushing it through blind.
- [x] Fast-follow (tracked separately): true single roster source of truth -- `PlaySimulation` and each `APSPlayerPawn` currently hold independent `FPlayerAttributes` copies rather than referencing one source. Fixing this touches dozens of call sites across `PSGameMode`/`PSPlayerPawn`/`PSPlaySimulation`; deferred as its own story for the same reason as above.

### Epic C4: Core Gameplay Test Retrofit

**Size/Mode:** M / code
**Goal:** The untested core loop gets regression coverage; rules logic becomes testable by extraction.
**Depends on:** C2, C3

- [x] Extract catch/interception/fumble probability rules from `APSBall::OnBallOverlap` into pure, testable functions (`PSBallResolutionHelpers`), tuning via `FCatchTuningRow` DataTable/JSON (`Data/catch_tuning.json`) per AGENTS.md rule 4
- [x] Automation tests: movement math (accel/turn/momentum), possession transfer (C3), catch resolution, phase progression, down/distance advancement
- [x] `APSFunctionalGym` asserts real behavior (scripted snap -> quick-sim phase progression -> Scoring check) instead of auto-succeeding
- [x] Async save `LastAsyncLoadResult` single-slot hazard fixed (`LoadFromSlotAsync` returns a per-request ID; `GetAsyncLoadResult(RequestId)`)

---

## Phase 2 — AI & Playbook

**Depends on: Phase 1.5 (C1–C4) complete.** Behavior trees subscribe to the C1 bus and trust the C2 single authority — building 22-agent AI on the pre-consolidation coupling is explicitly forbidden.

### Epic 14: Skill-Position Behavior (QB/RB/WR/TE)

**Goal:** Skill players make credible decisions autonomously during a play.
**Builds on:** `AIModule`/`NavigationSystem` (declared in `PlaySports.Build.cs`, unused so far)
**Depends on:** Epics 6, 7, 9

- [ ] `AAIController` + behavior tree scaffolding for offensive skill positions
- [ ] QB: dropback, progression reads through eligible receivers, throw/scramble/sack decision driven by `Awareness`
- [ ] WR/TE: route running from route data (Epic 16 feeds this; hardcode 3 routes to start)
- [ ] RB: handoff acceptance, run-lane reading from line-play outcomes (Epic 9), pass-blocking fallback
- [ ] Catch-point convergence: receivers adjust to the thrown ball's landing point

### Epic 15: Line & Defensive Behavior (OL/DL/LB/DB)

**Goal:** The other 14+ players behave credibly per assignment.
**Depends on:** Epics 9, 14

- [x] OL: assignment-based blocking (man/zone scheme selection from play data)
- [x] DL: rush lanes, contain responsibility, run-fit reaction
- [x] LB: run/pass read, zone drop or man assignment, pursuit angles
- [x] DB: man coverage mirroring and zone coverage with ball-hawking on throws (`Awareness`-driven)
- [x] Pursuit system: all defenders converge on the ball-carrier with attribute-scaled angles

### Epic 16: Playbook & Play Data System

**Goal:** Plays are data assets, not code — routes, blocking schemes, coverages, formations.
**Builds on:** `PSDataIngestion` patterns (JSON → engine data), `UDataTable` usage
**Depends on:** Epic 14 (informed by what the AI actually consumes)

- [x] Play definition schema: formation, per-position assignment (route/block/coverage), snap trigger
- [x] Route library: waypoint-based route shapes reusable across plays
- [x] Defensive play schema: front, coverage shell, blitz packages
- [x] JSON ingestion path for playbooks (mirroring `LoadPlayerAttributesFromJson`)
- [x] Starter playbook: ~10 offensive plays, ~6 defensive calls, enough to exercise every AI branch

### Epic 17: 22-Agent Coordinated Play Orchestration

**Goal:** All 22 on-field agents execute one play call coherently — the README's "22-agent behavior system."
**Depends on:** Epics 14, 15, 16

- [x] Play-call distribution: one selected play resolves into 22 individual assignments
- [x] Synchronized phase transitions: all agents react to snap/throw/turnover events from the play state machine
- [ ] Broken-play adaptation: scramble drill, blown coverage reactions, blocked-kick chaos handling
- [ ] Performance pass: 22 simultaneous behavior trees + physics at target frame rate
- [x] Determinism/replay hooks: seedable decisions so a play can be re-simulated for debugging

### Epic 18: Coaching & Play-Selection AI

**Goal:** The CPU opponent (and optional suggestion engine for the player) calls sensible plays.
**Depends on:** Epics 16, 17

- [x] Situation model: down/distance/clock/score → play-category weighting
- [x] Tendency profiles per opponent team (aggressive/conservative archetypes as data)
- [x] 4th-down, 2-point, and clock-management decision logic
- [x] Optional LLM hook: expose the situation model so an external model (via the Epic 25 bridge) can be consulted for play-calling — designed but gated behind the bridge existing

---

## Phase 3 — Meta-Game & Content

### Epic 19: Roster, Depth Chart & Player Progression

**Goal:** Teams are full rosters with depth, substitution, and growth — not 22 hardcoded rows.
**Builds on:** `FPlayerAttributes`, `EPlayerRole`
**Depends on:** Epic 1

- [x] Team/roster model: 53-player rosters, depth chart per position
- [ ] Substitution and personnel packages tied into formations (11 personnel, nickel, etc.)
- [x] Stamina/fatigue consuming the hooks left in Epic 6, driving rotation
- [x] Progression/regression: attribute changes from play, age, and training
- [x] Injury model (probability, severity, recovery timeline)

### Epic 20: Season / Franchise Mode

**Goal:** The already-working schedule generator becomes a playable season loop.
**Builds on:** `UPScheduleEngine::GenerateSeasonSchedule` (complete, unused)
**Depends on:** Epics 12, 19

- [x] League model: teams, divisions, standings
- [x] Season loop: `PSScheduleEngine` schedule → play/sim each week → standings update
- [x] Quick-sim: resolve non-played games headlessly via the play simulation (no rendering)
- [x] Save/load season state (`SaveGame` objects)
- [x] Playoff bracket generation from final standings

### Epic 21: Data & Content Pipeline Expansion

**Goal:** All game content (players, teams, playbooks, rules) flows through one validated ingestion path.
**Builds on:** `UPSDataIngestion`, `Data/sample_players.json`
**Depends on:** Epics 16, 19

- [x] Generalize `PSDataIngestion` beyond players: teams, playbooks, league config
- [x] Schema validation with actionable error reporting (bad field, bad row, bad enum value)
- [x] Full sample league dataset: 4+ teams with complete rosters for testing
- [x] Editor utility (or commandlet) to re-import all `Data/` content in one action
- [x] Document the data contract in `Data/README.md` so external tools/agents can generate content

---

## Phase 4 — Presentation & Agentic Infrastructure

### Epic 22: Player Animation Integration

**Goal:** Physics states drive real character animation instead of placeholder capsules.
**Depends on:** Epics 6, 7, 8

- [ ] Skeletal mesh + animation blueprint for the shared player rig
- [ ] Locomotion blend spaces driven by the Epic 6 movement model
- [ ] Contextual animations: throw, catch, handoff, tackle (both roles), block engage
- [ ] Physical animation blending on contact (hit reactions layered over anim)
- [ ] Camera-facing polish pass: celebrations, huddle, pre-snap stances

### Epic 23: Immersive Audio Pipeline

**Goal:** The game sounds like football — crowd, contact, whistle, ambience.
**Depends on:** Epics 8, 11 (events to react to)

- [ ] Audio event bus mapped to gameplay events (snap, big hit, score, whistle, flag)
- [ ] Dynamic crowd system reacting to play outcomes and home/away context
- [ ] On-field layer: pads, footsteps, QB cadence
- [ ] Stadium ambience with attenuation/reverb zones in the level
- [ ] Commentary hooks: structured play-description events exposed for future TTS/LLM commentary (bridge-gated, like Epic 18's hook)

### Epic 24: Automated Testing & Functional Gym Expansion

**Goal:** Every system above has regression coverage runnable headlessly.
**Builds on:** `APSFunctionalGym` (`AFunctionalTest`, currently one trivial test)
**Depends on:** starts alongside Phase 1 and grows with every Epic (listed here, not sequenced last)

- [ ] Gym map + one `APSFunctionalGym`-derived test per core system (movement, ball, tackle, block)
- [ ] Headless play-resolution tests: scripted scenarios with asserted outcomes (e.g. "faster DB intercepts this route")
- [ ] Automation spec (unit-level) coverage for pure logic: `PSScheduleEngine`, ingestion validation, drive state transitions
- [ ] CI recipe: `RunUAT`/`-ExecCmds="Automation RunTests"` command line documented in `AGENTS.md` for environments that do have UE installed
- [ ] Determinism harness reusing Epic 17's seeded replay for regression comparison

### Epic 25: Agentic Engine Bridge (Autonomix + AgenticLink)

**Goal:** The two stub plugins become real: external agents can inspect and mutate the project, and free-tier models plug in.
**Builds on:** `Plugins/Autonomix` (stub), `Plugins/AgenticLink` (stub), `.mcp.json`/`.vscode/mcp.json` placeholders, `.env.example` model slots
**Depends on:** — (infrastructure track; can proceed in parallel with everything, unblocks Epics 18/23 hooks)

- [ ] Autonomix: T3D import helpers — spawn/mutate level actors from agent-generated T3D text, wrapped in undoable transactions
- [ ] Autonomix: Python escape hatch — run agent-supplied scripts via `PythonScriptPlugin` with result capture
- [ ] AgenticLink: MCP server exposing engine reflection (list actors, get/set properties, invoke `UFUNCTION`s) with transaction safety
- [ ] Register the real server in `.mcp.json` + `.vscode/mcp.json`, and document the Antigravity global-config entry in `AGENTS.md`
- [ ] Model router honoring the `.env` contract (`OLLAMA_HOST`, `GEMINI_API_KEY`, `OPENROUTER_API_KEY`) so bridge tasks can be delegated to free-tier models
- [ ] Agent smoke test: an external agent connects over MCP, spawns an actor in the gym map, runs an Epic 24 test, reports results
