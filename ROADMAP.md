# ROADMAP.md

Development roadmap for `play-sports`: **144 Epics** — a 25-Epic core (this file, Phases 0–4)
sequenced **vertical-slice first** (Phase 0 produces one crude but complete, watchable play as
early as possible; later phases deepen it), plus **119 expansion Epics (26–144)** in themed
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

## Expansion track index (Epics 26–144)

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
| Q | `roadmap/character-combat.md` | 139–141 | Character archetypes & combat rules: hitpoints, death/respawn, no-punting, 4th-down overload, leveling/XP |
| R | `roadmap/world-kit.md` | 142–144 | World kit import (from this-next-please's browser world, 2026-10-08): assets into Content, inclusive character looks, rain/day-night port |

## MVP priority sequencing (code-mode tracks)

Core Epics 1–21 shipped an AI-vs-AI simulation loop (play sim, coaching AI, orchestration,
roster/season/franchise). The human input path is now in: Track M (Enhanced
Input/`PSPlayerController`/gamepad/rumble/glyphs, 126–128) and the front-end shell (101) are done;
Track I's play-call UI (102) is the remaining Tier 0 gap between "impressive AI sim" and "a person
can sit down and play a game" — bigger than any amount of additional AI depth. The tiers below are a
recommended execution order for the entirely-or-mostly-`code`-mode tracks (E, F, G, I, J, K, L, M,
O, P, Q, R) toward an actual playable MVP; they do not change any `depends_on` edge in
`roadmap/PARALLEL.md`, which remains the source of truth for what's actually unblocked. Editor-
heavy tracks (A, B, C, D, H) are intentionally deprioritized here since this repo's agent sessions
have no Unreal Editor access (see `AGENTS.md`).

- **Tier 0 — human-playable core:** Track M in full (126 → 127 → 128), Track I Epics 101 → 102,
  and Track Q (139 → 140 → 141) — the character archetype/hitpoint/combat-rules layer the user
  wants as foundational groundwork before more content layers land on top of it.
- **Tier 1 — deepen the on-field game:** Track E in full (66–77) — the largest pure-code track,
  and the most direct improver of play-to-play feel once a human can call plays.
- **Tier 2 — make the AI opponent and season worth playing against:** Track F (78–85) and Track G
  (86–95) — franchise depth only matters once there's a human loop to embed it in.
- **Tier 3 — content at scale + remaining infra:** Track L (121–125, also unblocks realistic test
  content for Tiers 0–2), remaining Track K infra (114/115/117/118/119), Track J multiplayer
  (107–111, local H2H first since it reuses Tier 0's input work directly).
- **Any tier, in parallel — Track R (142–144):** the world kit's code stories (DataTables, the look
  component, the time-of-day subsystem, the input catalog) depend only on Core 2/22 and C1 and touch
  no MVP file scope; its editor stories wait for an editor session like every other editor story.
- **Tier 4 — polish/reach:** Track O, Track P, Track N, plus the code-only slices of A/B/C/H once
  the on-field game is solid.

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

- [x] `AAIController` + behavior tree scaffolding for offensive skill positions
- [x] QB: dropback, progression reads through eligible receivers, throw/scramble/sack decision driven by `Awareness` *(`UPSSkillPlayerAIComponent` on `APSOffenseController`: drop to the play's spot, read from `MinReadSeconds`, throw to the most open receiver with a lead, under pressure or past `MaxReadSeconds` throw if anyone is open enough or scramble; a sack is the tackle system catching the scramble. `Awareness` sets how open a receiver must look. Tuning: `Data/skill_ai_tuning.json`)*
- [x] WR/TE: route running from route data (Epic 16 feeds this; hardcode 3 routes to start) *(routes come from the route library through `UPSPlayOrchestrator`; no hardcoded routes needed)*
- [x] RB: handoff acceptance, run-lane reading from line-play outcomes (Epic 9), pass-blocking fallback *(the QB meets the back and hands off; the back heads for `PSFieldReads::LargestRunLaneGap`; a player with no route blocks the rusher nearest the QB)*
- [x] Catch-point convergence: receivers adjust to the thrown ball's landing point *(the throw event now carries `LandingLocation`, the spot after the passer's inaccuracy)*

### Epic 15: Line & Defensive Behavior (OL/DL/LB/DB)

**Goal:** The other 14+ players behave credibly per assignment.
**Depends on:** Epics 9, 14

- [x] OL: assignment-based blocking (man/zone scheme selection from play data)
- [x] DL: rush lanes, contain responsibility, run-fit reaction
- [x] LB: run/pass read, zone drop or man assignment, pursuit angles
- [x] DB: man coverage mirroring and zone coverage with ball-hawking on throws (`Awareness`-driven)
- [x] Pursuit system: all defenders converge on the ball-carrier with attribute-scaled angles
- *Live in the game since the defense-AI follow-up to Epic 14: until then these assignments only reached a blackboard no Behavior Tree read, and every defender ran straight at the ball. `UPSDefenderAIComponent` (on `APSDefenseController`) now plays them: rush and contain, man coverage from a cushion, zones that shade to the receiver in them, run-fit run/pass reads, breaking on the throw, and pursuit on the controller's intercept angle, all timed by `Awareness`. Tuning: `Data/defense_ai_tuning.json`.*

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
- [x] Broken-play adaptation: scramble drill, blown coverage reactions, blocked-kick chaos handling *(scramble drill done: a QB's escape is a `Pocket` event on the bus and the orchestrator that handed out the play runs `TriggerScrambleDrill` (Epic 71). Blown coverage, offense side, done: a receiver `BlownCoverageSeparation` from every defender is read whatever his route's timing; the defense's own reaction is `UPSBlownCoverageSubsystem` (#106): the nearest free zone defender leaves his zone and takes the free receiver in man. Blocked-kick chaos done: Epic 75's `UPSSpecialTeamsModel` still decides that a kick is blocked; on a live field `UPSPlaySimulation` announces it (`LooseBall` Blocked) and `UPSLooseBallSubsystem` takes the ball out of the holder's hands onto the ground behind the line (a punt's recoil, a field goal's hold). Every player within `ChaseRadius` goes for it whatever his call (the defense and skill AIs' `LooseBall` action); the nearest to reach it tries with his fumble-recovery chance (a muff squirts it away), a defender in the clear scoops it up and returns it with the kicking team running him down, anyone else falls on it. The dead ball (fallen on, the returner tackled or in the end zone, or the whistle after `MaxLooseSeconds`) becomes the kick's outcome: the spot and the defense's ball, or its touchdown. With no ball to play the model's roll stands. Tuning: `Data/loose_ball.json`; tests: `Tests/PSLooseBallTests.cpp`)*
- [ ] Performance pass: 22 simultaneous behavior trees + physics at target frame rate
  *Code-level pass done: `UPSAIFieldSnapshot` scans the field once a frame for every AI player
  (it used to be several actor scans per decision), the decisions carry `stat PSAI` counters, and
  `PlaySports.AI.Performance.OneFieldScanPerFrame` checks it. Still open: measuring the frame
  rate on a device, with the procedure and budget in `Specs/Platform_Audit.md` section 6.*
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
- [x] Substitution and personnel packages tied into formations (11 personnel, nickel, etc.) — *`UPSPersonnelManager` + `Data/personnel_packages.json` (11/12/21/10 personnel; base 4-3/3-4, nickel, dime, goal line), each package listing the formations that bring it on. The game mode spawns the default packages from `UPSRoster`'s depth chart and the pawns point at the roster's rows; a play call swaps only the players who change and re-lines the side; between plays a sitting-out carrier (Epic 139) or a tired player (19.3's hook) gives way to the next man up. `sample_players.json` gained 9 backups. Bus: `Personnel` event. Tests: `PSPersonnelTests.cpp`. Formation-specific alignment (trips vs twins) is still the role-based `ComputeLineup`.*
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

- [x] Audio event bus mapped to gameplay events (snap, big hit, score, whistle, flag)
  *As built: `UPSAudioSubsystem` (world subsystem) hears the telemetry bus and nothing else: each
  event becomes a trigger with a detail (the snap; the whistle once a play, from a live phase going
  dead; a tackle, a sack; a hit, `Big` at `BigHitDamage`; a throw, `Deep`; a catch, an interception;
  a fumble; a kick; the play's score and result from the simulation's `PlayResult`; a flag; a
  timeout; a goal-line crossing; the crowd's level and reactions; the cadence; a quarter's end).
  `Data/audio_cues.json` maps triggers to cues (soft sound paths, empty until imported; every
  request is logged either way), with per-cue volume, priority and cooldown and per-layer volume
  settings (`EffectsVolume`, `CommentaryVolume`, `MusicVolume`). Flags are new on the bus: the play
  simulation, the authority on penalties, publishes a `Penalty` event when it throws one and when
  it is accepted or declined. The tier's `AudioMaxVoices` caps one-shots (a higher priority takes
  the weakest voice) and `AudioUpdateHz` paces its update (`Data/platform_tiers.json`: 32 voices
  every frame on desktop, 16 at 30 Hz on the iPhone tier); timed as `Audio` and stepped by the
  profiling harness. Tested: `PlaySports.Audio.CueMapping`, `.VoicesAndLoops`, `.FlagFromSimulation`.*
- [x] Dynamic crowd system reacting to play outcomes and home/away context
  *As built: `UPSCrowdExcitementSubsystem` is the one authority on the crowd's excitement; Epic 49
  (reaction animations, noise pressure on the visitors, rivalry intensity through `ApplyStimulus`'s
  scale) and Epic 97 (layered beds, stingers, swells) extend it rather than adding a second model.
  Excitement (0-1) settles toward a resting level that rises late in a close game; moments from the
  bus (a deep ball, a sack, a big hit, a turnover, a goal-line crossing as they happen; the score, a
  field goal, a big gain, a first down, an incompletion, a turnover on downs from the simulation's
  `PlayResult`; a flag) each benefit one team, whose fans and the other team's move the crowd by
  their share of the stadium (`DefaultHomeShare`, or the match's via `SetMatchContext`, set by the
  game mode from `UPSMatchSetup`). A home touchdown erupts; a visitors' touchdown stuns it to a hush.
  Levels Hush to Eruption with hysteresis; every reaction and level change is a `Crowd` bus event,
  from which the audio plays the bed and stinger. `Data/crowd.json`; per-tier `CrowdUpdateHz`.
  Tested: `PlaySports.Crowd.ExcitementModel`, `.HomeAndAway`.*
- [ ] On-field layer: pads, footsteps, QB cadence *(code half in: contact, hits scaled by force,
  tackles, the cadence and the ball map to `Field.*` cues, and `UPSAudioSubsystem::RequestCue` is
  the hook an animation's footstep notifies call (tested: `PlaySports.Audio.FieldHooks`). Left for an
  editor session: the sound assets themselves and the footstep anim notifies on the player rig
  (Epic 22))*
- [ ] Stadium ambience with attenuation/reverb zones in the level *(editor work: the ambience loop
  starts with the match (`StartupLoops`), but its sound, the attenuation settings and the level's
  audio and reverb volumes need an editor session)*
- [x] Commentary hooks: structured play-description events exposed for future TTS/LLM commentary (bridge-gated, like Epic 18's hook)
  *As built: `UPSCommentaryEventModel` reads the game's moments off the bus and publishes each as a
  `Commentary` bus event: what happened (`EPSCommentaryMoment`: the game's start, the snap, a pass,
  a catch, an interception, a sack, a big hit, a fumble, an open receiver, a goal-line crossing, a
  flag, the play's result and score, a timeout, the two-minute warning, a quarter's end, the final
  whistle, a record broken), who (names from the play's live events, ids from the simulation's
  `PlayResult`) and the situation, tied to the bus event it describes. Bridge-gated like Epic 18's
  hook: while Epic 82's bridge is online, the moments in `ModelMoments` are offered as `Commentary`
  requests (`narration` routing, compact JSON within `ModelContextChars`) and answers come back as
  model lines (`OnModelLineMC`); offline nothing is asked. Captions stay Epic 96's: the booth speaks
  through the caption event. `Data/commentary_hooks.json`. Tested:
  `PlaySports.Commentary.PlayDescriptions`, `.BridgeGate`.*

### Epic 24: Automated Testing & Functional Gym Expansion

**Goal:** Every system above has regression coverage runnable headlessly.
**Builds on:** `APSFunctionalGym` (`AFunctionalTest`, currently one trivial test)
**Depends on:** starts alongside Phase 1 and grows with every Epic (listed here, not sequenced last)

- [ ] Gym map + one `APSFunctionalGym`-derived test per core system (movement, ball, tackle, block)
- [x] Headless play-resolution tests: scripted scenarios with asserted outcomes (e.g. "faster DB intercepts this route") *(as built: `PSScriptedGameTests.cpp` scripts seeded games on `UPSPlaySimulation`'s quick sim. `PlaySports.Gym.ScriptedFullGame` runs kickoff → 4 quarters → final score asserted (milestone M7), and `Gym.Scenario.FasterCornerbackGivesUpFewerYards` asserts a faster corner holds the same throws to fewer yards. The physical play can't be driven headlessly: a catch, interception or fumble recovery (`APSBall::OnBallOverlap`) and a tackle (`UPSBallActionComponent::ResolveTackle`) both return without `APSGameMode`, and pawn movement needs engine ticking)*
- [x] Automation spec (unit-level) coverage for pure logic: `PSScheduleEngine`, ingestion validation, drive state transitions *(as built: `PSPureLogicSpec.cpp`. `PlaySports.Spec.DriveState` covers downs, first downs, incompletions, the fourth-down decision, turnover on downs, touchdowns and safeties; `Spec.ScheduleEngine` and `Spec.IngestionValidation` cover the other two)*
- [x] CI recipe: `RunUAT`/`-ExecCmds="Automation RunTests"` command line documented in `AGENTS.md` for environments that do have UE installed *(as built: "Running CI's checks on a machine with UE 5.8" in `AGENTS.md`)*
- [x] Determinism harness reusing Epic 17's seeded replay for regression comparison *(as built: `PlaySports.Gym.SameSeedSameGame` records seeded games as `FPSReplayRecording`s (Epic 115). The new `UPSDeterminism::FindFirstDivergence` finds the first event where two recordings differ. The sim rolls on the global stream (Determinism audit A1), so the harness seeds that stream)*

### Epic 25: Agentic Engine Bridge (Autonomix + AgenticLink)

**Goal:** The two stub plugins become real: external agents can inspect and mutate the project, and free-tier models plug in.
**Builds on:** `Plugins/Autonomix` (stub), `Plugins/AgenticLink` (stub), `.mcp.json`/`.vscode/mcp.json` placeholders, `.env.example` model slots
**Depends on:** — (infrastructure track; can proceed in parallel with everything, unblocks Epics 18/23 hooks)

- [x] Autonomix: T3D import helpers — spawn/mutate level actors from agent-generated T3D text, wrapped in undoable transactions — *`FAutonomixT3D::Import`: each `Begin Actor` block (inside `Begin Map`/`Begin Level` or not) changes the actor it names or spawns one of its class; its property lines and its `Begin Object Name=` definitions (its components) are imported through the engine's text import, arrays written by element start over, what can't be applied comes back as warnings, and the whole text is one `FScopedTransaction`. It doesn't create components a class doesn't make or keep an exported `RootComponent`. Served as `import_t3d` on AgenticLink's server (`FAgenticLinkToolProviders`, no second server), opt-in with `-AutonomixT3D`. Tests: `PlaySports.Autonomix.T3DParse`, `.T3DImport`, `.T3DUndo`, `.McpTools`. Not verified against a live MCP client.*
- [x] Autonomix: Python escape hatch — run agent-supplied scripts via `PythonScriptPlugin` with result capture — *`FAutonomixPython::Run` through `IPythonScriptPlugin::ExecPythonCommandEx` (file, statement or evaluate), one transaction, returning the result, the log and the traceback of a script that raises; served as `run_python`, opt-in with `-AutonomixPython`. The project doesn't enable the Python plugin, so CI exercises only the clean refusal (`PlaySports.Autonomix.McpTools`); running a script is untested until a session with Python runs that test.*
- [x] AgenticLink: MCP server exposing engine reflection (list actors, get/set properties, invoke `UFUNCTION`s) with transaction safety — *`FAgenticLinkMcpServer` (JSON-RPC 2.0, MCP 2024-11-05 to 2025-06-18) on Streamable HTTP (`FAgenticLinkHttpTransport`, `http://127.0.0.1:8790/mcp`, opt-in with `-AgenticLinkMcp`/`-AgenticLinkMcpPort=`, localhost-only Origin). Tools: `list_actors`, `get_property`, `set_property` (instance-editable only), `call_function` (BlueprintCallable only), `spawn_actor`; each edit is one `FScopedTransaction` (Undo works) on the game thread. Tests: `PlaySports.AgenticLink.*`. Not verified against a live MCP client: no editor here.*
- [ ] Register the real server in `.mcp.json` + `.vscode/mcp.json`, and document the Antigravity global-config entry in `AGENTS.md` — *documented in `AGENTS.md` (all three entries); the two repo config files are left for the owner to register*
- [x] Model router honoring the `.env` contract (`OLLAMA_HOST`, `GEMINI_API_KEY`, `OPENROUTER_API_KEY`) so bridge tasks can be delegated to free-tier models — *consumes Epic 135's router rather than twinning it: an `OllamaClient` (`OLLAMA_HOST`, stdlib HTTP) and a `bridge` tier (Ollama, then Gemini low, then an OpenRouter `:free` model, each skipped when unconfigured), driven by `python -m tools.orchestrator delegate`. Tests use mocked HTTP; no live model was called*
- [ ] Agent smoke test: an external agent connects over MCP, spawns an actor in the gym map, runs an Epic 24 test, reports results
