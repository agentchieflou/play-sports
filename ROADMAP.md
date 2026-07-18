# ROADMAP.md

Development roadmap for `play-sports`. 25 Epics across 5 phases, sequenced **vertical-slice
first**: Phase 0 produces one crude but complete, watchable play (roster → snap → physics →
result) as early as possible; every later phase deepens a piece of that slice.

Conventions used below:

- **Goal** — one-line definition of done for the Epic.
- **Builds on** — existing code in this repo the Epic extends (see `AGENTS.md` for the map).
- **Depends on** — Epic IDs that should land first. Epics with no unmet dependencies can be
  worked in parallel (including by different agents/tools).
- Stories are checkboxes so progress is trackable in-place via PRs that tick them.

Status: nothing below is started unless checked. The only implemented code today is what
`AGENTS.md` describes (`PSPlaySimulation` phase ticker, `PSDataIngestion`, `PSScheduleEngine`,
`PSFunctionalGym`, `FPlayerAttributes`, and the two stub plugins).

---

## Phase 0 — Vertical Slice Foundation

### Epic 1: Minimum Playable Down

**Goal:** One complete snap→result loop runs in PIE using real project systems, however crude.
**Builds on:** `PSPlaySimulation`, `PSPlayerAttributes`, `PSDataIngestion`, `Data/sample_players.json`
**Depends on:** — (this is the slice; Epics 2–5 are its parts and can proceed in parallel)

- [ ] Expand `Data/sample_players.json` to 22 players (11 offense, 11 defense) covering every `EPlayerRole`
- [ ] `AGameModeBase` subclass (`PSGameMode`) that loads rosters via `UPSDataIngestion` at startup
- [ ] Drive `UPSPlaySimulation::AdvancePlay` from the game world tick instead of manual calls
- [ ] Produce a play result struct (yards gained, tackle/score/incomplete) even if randomly resolved from attributes
- [ ] End-to-end PIE test: game starts → roster loads → play runs phases → result logged on screen

### Epic 2: Field & Stadium Level Scaffolding

**Goal:** A regulation-dimensioned field level exists that all gameplay Epics use.
**Depends on:** —

- [ ] Field geometry: 120yd × 53.3yd playing surface with correct UE unit scaling convention (documented)
- [ ] Yard lines, hash marks, end zones, and sidelines (materials/decals, placeholder art fine)
- [ ] Field coordinate helper (`PSFieldGrid` or similar): yard-line ↔ world-position conversion functions
- [ ] Out-of-bounds and end-zone trigger volumes
- [ ] Default `GameMap` set in project settings so PIE opens into the field

### Epic 3: Player Pawn & Possession Framework

**Goal:** A pawn class represents any player on the field, driven by `FPlayerAttributes`.
**Builds on:** `PSPlayerAttributes.h`
**Depends on:** Epic 2

- [ ] `APSPlayerPawn`: capsule + placeholder mesh, initialized from an `FPlayerAttributes` row
- [ ] Simple locomotion (move-to-point) with max speed scaled from the `Speed` attribute
- [ ] Ball possession state: which pawn holds the ball, handoff/transfer API
- [ ] Team/side affiliation and formation spawn points (offense vs. defense lineup)
- [ ] Possessable by either an `AIController` or player controller (input mapping via `InputCore`)

### Epic 4: Basic Broadcast Camera System

**Goal:** The play is watchable from a sensible camera without manual control.
**Depends on:** Epics 2, 3

- [ ] Broadcast-style side camera that tracks the ball/ball-carrier
- [ ] Camera bounds so it never leaves the stadium or crosses the field plane
- [ ] Snap-to-formation framing pre-play, follow mode during the play
- [ ] Debug free-cam toggle for development

### Epic 5: Core HUD

**Goal:** Down, distance, score, and clock are visible on screen.
**Builds on:** `UMG` dependency already in `PlaySports.Build.cs`; `FPlayState` (Down/Distance/GameTimeSeconds)
**Depends on:** Epic 1

- [ ] UMG scoreboard widget bound to `FPlayState` (down, distance, game clock)
- [ ] Score display (home/away) fed by the play result from Epic 1
- [ ] Play-phase indicator (debug-level: show current `EPlayPhase`)
- [ ] Post-play result banner (e.g. "+7 yards", "TOUCHDOWN")

---

## Phase 1 — Core Physics & Rules

### Epic 6: Physics-Based Player Movement

**Goal:** Player motion is physically simulated and attribute-driven, replacing Epic 3's move-to-point.
**Builds on:** `APSPlayerPawn` (Epic 3), `FPlayerAttributes` (`Speed`, `Agility`, `Acceleration`, `WeightKg`)
**Depends on:** Epic 3

- [ ] Acceleration/deceleration curves derived from `Acceleration` and `Speed` attributes
- [ ] Turning radius / change-of-direction cost derived from `Agility` and `WeightKg`
- [ ] Momentum model: mass-scaled velocity that other systems (tackling, blocking) can query
- [ ] Fatigue/burst hooks (data only — full stamina system deferred to Epic 19)
- [ ] Tuning data table so movement feel is editable without recompiling

### Epic 7: Ball Physics

**Goal:** The ball is a physical actor: snapped, carried, thrown, caught, and dropped.
**Depends on:** Epics 3, 6

- [ ] `APSBall` actor with projectile physics (spiral trajectory, gravity, bounce)
- [ ] Snap: ball transfer from center to QB triggering `EPlayPhase::Snap`
- [ ] Pass: throw with velocity/arc computed from target point and thrower attributes
- [ ] Catch resolution: receiver radius + attribute check; drop/incompletion on failure
- [ ] Handoff and pitch (lateral) transfers
- [ ] Fumble state: live ball on ground, recoverable by either team

### Epic 8: Tackling & Collision Resolution

**Goal:** Defender contact with the ball carrier resolves physically into tackle outcomes.
**Builds on:** `UPSPlaySimulation` (replaces the placeholder `BallCarrierMovement → Scoring` transition)
**Depends on:** Epics 6, 7

- [ ] Contact detection between defender and ball-carrier pawns
- [ ] Tackle resolution: momentum + `Strength` vs. `Strength`/`Agility` contest with physics impulse
- [ ] Broken-tackle branch: carrier continues with speed penalty
- [ ] Down-by-contact: play-end signal into the play state machine with final ball spot
- [ ] Fumble chance on high-impact hits (feeds Epic 7's fumble state)

### Epic 9: Blocking & Line-Play Physics

**Goal:** OL/DL engagements are physically contested instead of nonexistent.
**Builds on:** `EPlayPhase::PassRush` (currently a pass-through phase)
**Depends on:** Epics 6, 8

- [ ] Engagement pairing: OL matches up against DL/blitzers at snap
- [ ] Push/leverage contest driven by `Strength`, `WeightKg`, and momentum
- [ ] Block shedding: DL win condition releases the rusher toward the QB/carrier
- [ ] Pocket formation: net result of line play shapes where the QB can stand
- [ ] Run lanes: blocking outcomes open/close gaps that the RB AI (Epic 14) can read

### Epic 10: Drive & Game State Machine

**Goal:** Consecutive plays chain into drives with real football bookkeeping.
**Builds on:** `UPSPlaySimulation` / `FPlayState` (`Down`, `Distance`)
**Depends on:** Epic 1

- [ ] Ball spot tracking: line of scrimmage, first-down marker, yards-to-go updates from play results
- [ ] Down progression: 1st–4th, turnover on downs, first-down resets
- [ ] Possession changes: punts (stub until Epic 13), turnovers, post-score
- [ ] Drive summary data (plays, yards, result) for HUD and future stats
- [ ] Between-play reset: pawns re-form at the new line of scrimmage

### Epic 11: Scoring, Rules & Penalties Engine

**Goal:** Points and rule infractions are detected and applied correctly.
**Builds on:** `EPlayPhase::Scoring`, end-zone volumes (Epic 2)
**Depends on:** Epics 2, 10

- [ ] Touchdown detection via end-zone volume + possession check (6 pts)
- [ ] Field goal / extra point / two-point conversion outcomes (kick physics from Epic 13 can stub as probability first)
- [ ] Safety detection (2 pts, possession change)
- [ ] Penalty framework: flag, yardage, replay/loss-of-down semantics — start with offsides + holding
- [ ] Rules config data asset so rule variants are data-driven, not hardcoded

### Epic 12: Game & Play Clock Management

**Goal:** A full game has quarters, a running clock, and clock-stopping rules.
**Builds on:** `FPlayState.GameTimeSeconds`
**Depends on:** Epic 10

- [ ] Quarter/half structure with configurable lengths
- [ ] Clock run/stop rules (incompletions, out of bounds, scores, timeouts)
- [ ] 40-second play clock with delay-of-game hook into the penalty framework (Epic 11)
- [ ] Two-minute warning and end-of-half/game handling
- [ ] Timeout budget per team

### Epic 13: Special Teams

**Goal:** Kickoffs, punts, and field goal attempts are playable.
**Depends on:** Epics 7, 10, 11

- [ ] Kick physics: power/angle model reusing `APSBall` trajectory work
- [ ] Kickoff phase: kick, coverage, return, touchback handling
- [ ] Punt with fair-catch and downed-ball outcomes
- [ ] Field goal attempt: snap-hold-kick chain, good/no-good detection via upright zone
- [ ] Special-teams formations added to the formation system (Epic 3)

---

## Phase 2 — AI & Playbook

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

- [ ] OL: assignment-based blocking (man/zone scheme selection from play data)
- [ ] DL: rush lanes, contain responsibility, run-fit reaction
- [ ] LB: run/pass read, zone drop or man assignment, pursuit angles
- [ ] DB: man coverage mirroring and zone coverage with ball-hawking on throws (`Awareness`-driven)
- [ ] Pursuit system: all defenders converge on the ball-carrier with attribute-scaled angles

### Epic 16: Playbook & Play Data System

**Goal:** Plays are data assets, not code — routes, blocking schemes, coverages, formations.
**Builds on:** `PSDataIngestion` patterns (JSON → engine data), `UDataTable` usage
**Depends on:** Epic 14 (informed by what the AI actually consumes)

- [ ] Play definition schema: formation, per-position assignment (route/block/coverage), snap trigger
- [ ] Route library: waypoint-based route shapes reusable across plays
- [ ] Defensive play schema: front, coverage shell, blitz packages
- [ ] JSON ingestion path for playbooks (mirroring `LoadPlayerAttributesFromJson`)
- [ ] Starter playbook: ~10 offensive plays, ~6 defensive calls, enough to exercise every AI branch

### Epic 17: 22-Agent Coordinated Play Orchestration

**Goal:** All 22 on-field agents execute one play call coherently — the README's "22-agent behavior system."
**Depends on:** Epics 14, 15, 16

- [ ] Play-call distribution: one selected play resolves into 22 individual assignments
- [ ] Synchronized phase transitions: all agents react to snap/throw/turnover events from the play state machine
- [ ] Broken-play adaptation: scramble drill, blown coverage reactions, blocked-kick chaos handling
- [ ] Performance pass: 22 simultaneous behavior trees + physics at target frame rate
- [ ] Determinism/replay hooks: seedable decisions so a play can be re-simulated for debugging

### Epic 18: Coaching & Play-Selection AI

**Goal:** The CPU opponent (and optional suggestion engine for the player) calls sensible plays.
**Depends on:** Epics 16, 17

- [ ] Situation model: down/distance/clock/score → play-category weighting
- [ ] Tendency profiles per opponent team (aggressive/conservative archetypes as data)
- [ ] 4th-down, 2-point, and clock-management decision logic
- [ ] Optional LLM hook: expose the situation model so an external model (via the Epic 25 bridge) can be consulted for play-calling — designed but gated behind the bridge existing

---

## Phase 3 — Meta-Game & Content

### Epic 19: Roster, Depth Chart & Player Progression

**Goal:** Teams are full rosters with depth, substitution, and growth — not 22 hardcoded rows.
**Builds on:** `FPlayerAttributes`, `EPlayerRole`
**Depends on:** Epic 1

- [ ] Team/roster model: 53-player rosters, depth chart per position
- [ ] Substitution and personnel packages tied into formations (11 personnel, nickel, etc.)
- [ ] Stamina/fatigue consuming the hooks left in Epic 6, driving rotation
- [ ] Progression/regression: attribute changes from play, age, and training
- [ ] Injury model (probability, severity, recovery timeline)

### Epic 20: Season / Franchise Mode

**Goal:** The already-working schedule generator becomes a playable season loop.
**Builds on:** `UPScheduleEngine::GenerateSeasonSchedule` (complete, unused)
**Depends on:** Epics 12, 19

- [ ] League model: teams, divisions, standings
- [ ] Season loop: `PSScheduleEngine` schedule → play/sim each week → standings update
- [ ] Quick-sim: resolve non-played games headlessly via the play simulation (no rendering)
- [ ] Save/load season state (`SaveGame` objects)
- [ ] Playoff bracket generation from final standings

### Epic 21: Data & Content Pipeline Expansion

**Goal:** All game content (players, teams, playbooks, rules) flows through one validated ingestion path.
**Builds on:** `UPSDataIngestion`, `Data/sample_players.json`
**Depends on:** Epics 16, 19

- [ ] Generalize `PSDataIngestion` beyond players: teams, playbooks, league config
- [ ] Schema validation with actionable error reporting (bad field, bad row, bad enum value)
- [ ] Full sample league dataset: 4+ teams with complete rosters for testing
- [ ] Editor utility (or commandlet) to re-import all `Data/` content in one action
- [ ] Document the data contract in `Data/README.md` so external tools/agents can generate content

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
