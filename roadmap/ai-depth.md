# Track F — AI & Simulation Depth (Epics 78–85)

Deepens Phase 2's behavior systems into adaptive, individualized, inspectable AI — plus the
calibration machinery that keeps the simulation statistically honest. Pure code; prime territory
for the AI/behavior-specialist archetype. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** all AI perception in this track subscribes to the C1
`UPSTelemetryBus` (never polls or casts); Epic 83's statistical mode is C2's explicit quick-sim
flag; Epic 85's debug overlay renders C1 event/decision streams; Epic 82 plugs into the live
eval gym (`tools/score_agent_run.py`, `eval/SCORECARD.md`) for model routing evidence.
Phase 2 + Phase 1.5 completion are hard prerequisites for this entire track.

### Epic 78: Adaptive Opponent Learning

**Size/Mode:** L / code
**Goal:** The AI notices your tendencies within and across games and counters them.
**Depends on:** Core 18, 26

- [x] Tendency tracker: user play-calling distributions by situation (down/distance/personnel)
  *As built: `UPSOpponentModel` (world subsystem) counts each play the human calls and runs,
  from the bus: by his side, the down, the distance bucket, the offense's personnel and the
  category. `ReadTendency` reads the narrowest situation with `MinSamples` calls behind it. Earlier
  games' calls are kept in the profile save and count at `PriorGameWeight`
  (`Data/opponent_model.json`).*
- [x] Counter-selection: defensive call weighting shifts against observed tendencies
  *As built: when `UPSPlayCallSubsystem` calls for the CPU against a human, the side's tendency
  gets `CounterWeights` from the data's `Counters`. Against the run the defense stays in base and
  out of prevent; against deep shots it plays prevent. The other way round, against a blitzing
  human defense the CPU offense screens. `UPSCoachingAI` weighs them, with the reason "Countering
  your tendencies".*
- [x] In-game adjustment moments (halftime adaptation step-change)
  *As built: the CPU leans on its read at `FirstHalfStrength` until `HalftimeQuarter`, then at
  `SecondHalfStrength`. Its first call that leans harder announces the adjustment on the bus
  (`OpponentAdjustment`: the side, the strengths and what it saw most).*
- [x] Guardrails: adaptation strength as a difficulty dial, never psychic (only observed data)
  *As built: everything scales by `SetAdaptationDial` (0 never adapts; Epic 84's difficulty sets
  it), and no counter leaves `MinMultiplier`..`MaxMultiplier`. A call counts only at its snap, so
  the CPU's own call for a play never sees it: tested.*

### Epic 79: Player DNA & Individual Tendency Profiles

**Size/Mode:** M / code
**Goal:** Two players with identical ratings play differently — per-athlete style profiles drive behavior variation.
**Depends on:** Core 14, Core 15, Core 19

- [x] DNA schema: style axes per role (scrambler vs. statue QB, finesse vs. power rusher, ball-hawk vs. blanket DB)
  *As built: `FPSPlayerDNA` is part of `FPlayerAttributes` (optional `DNA` in a roster file), six
  axes from -1 to 1: `Mobility` and `Gunslinger` (QB), `RunPower` (RB), `RouteStyle` (WR, TE),
  `RushPower` (DL, LB), `BallHawk` (DB, LB). `Data/player_dna.json` says which roles each applies
  to and names the trait at each end.*
- [x] Behavior-tree parameter binding so DNA visibly changes decisions, not just stats
  *As built: the catalog's `Bindings` scale named fields of the AI tunings (`SkillAI`, `Pocket`,
  `DefenderAI`, `RouteRunning`) by the axis. Each AI component applies its player's bindings as a
  play starts, through `UPSPlayerDNASubsystem`. The rush plan weights power and finesse moves by
  `RushPower`. Rated alike, a scrambler leaves the pocket where a pocket passer stands in and
  throws, a ball hawk jumps a throw a blanket corner stays off (and bites longer on a pump fake),
  an elusive back cuts where a power back runs on, and a power rusher bulls where a finesse
  rusher swims. Ratings and win chances are untouched.*
- [x] DNA in the data pipeline (Track L generates plausible profiles at roster scale)
  *As built: `tools/player_dna.py` generates a profile from a player's ratings, leaned from his
  role's league average, plus seeded variation by PlayerId. `--write` fills every roster without
  one, and the shipped rosters carry its output. Epic 122's generator calls `generate_profile`.
  `validate_data.py` checks the catalog and every player's `DNA`.*
- [x] Scouting-visible traits surface (Track G consumes)
  *As built: `UPSPlayerDNASubsystem::GetScoutingTraits` (`PSPlayerDNA::GetScoutingTraits`) gives
  a player's pronounced traits, strongest first, named through the string tables
  (`Trait.<TraitId>.Label` and `.Description`, generated into `ui_text_data.csv`). The scouting
  screens that show them are Track G's.*

### Epic 80: Formation & Play Recognition AI

**Size/Mode:** M / code
**Goal:** Defenders genuinely *read* — recognizing formations, motions, and play-development cues at attribute-gated speed.
**Depends on:** Core 15, 68, 72

- [x] Formation classifier from offensive alignment data (personnel + splits + backfield set)
  *As built: `UPSPlayRecognitionSubsystem` reads the offense's alignment at the snap from
  `UPSAIFieldSnapshot` (`PSPlayRecognition::ClassifyFormation`): personnel ("11"), the receivers'
  splits and strength (inline tight ends, split receivers, strong and weak side), the QB's
  alignment (under center, pistol, shotgun) and the backfield set (empty, single, offset, I, split,
  full). The first matching class in `Data/play_recognition.json` names it and gives its run lean.
  It goes on the bus (`Recognition` Formation) on the first look after the snap. Every formation
  still lines up in `APSFieldGrid`'s default lineup, so in a game the read follows the personnel
  package and any motion.*
- [x] Key-reading: run/pass diagnosis from line behavior and backfield flow post-snap
  *As built: each look after the snap reads the keys (`ReadKeys`). A hand-off or the line firing
  off its stance reads run; the QB dropping with the ball (not a shotgun snap alone) or the line
  setting back reads pass. Backs flowing downhill read run, but a fake can show that too. A
  defender's diagnosis is the keys he has had time to read: a true key beats flow, and the latest
  true key wins (a draw). `UPSDefenderAIComponent`'s run fit plays it. Reading pass he drops; this
  replaces its `PassReadDepth` rule. Reading run he fills his Epic 81 gap before the hand-off. Each
  new diagnosis goes on the bus. The sim's linemen block runs and passes alike, so the line keys
  show only when the line moves decisively.*
- [x] Recognition latency scaled by `Awareness` + DNA (79) — elite defenders jump routes, poor ones bite on fakes
  *As built: a defender reads in his reaction (Awareness, with Epic 84's difficulty applied) times
  a scale per read: pass, run, throw and fake. `player_dna.json` binds the scales through a new
  `Recognition` target: a ball hawk breaks on throws sooner and sees through fakes later. A read
  against what he expects takes longer (`ExpectationWeight`). He expects the formation's lean,
  pulled toward the offense's recent run share. Coverage defenders break on a throw in their throw
  read, so an elite one jumps it.*
- [x] Feeds the deception-resistance rules in Epic 72 (replaces its interim bite model)
  *As built: `UPSDeceptionSubsystem` no longer rolls `BiteChance`. As it sells a play-action fake,
  a run-fit defender bites when his fake read is longer than the fake. The read varies by up to
  `LatencyJitter`, seeded per snap. He holds for the rest of it, at most `MaxBiteSeconds`. Its
  bite fields left `deception.json`. Tests: `Tests/PSPlayRecognitionTests.cpp`.*

### Epic 81: Run-Fit & Gap Integrity System

**Size/Mode:** M / code
**Goal:** Run defense is a coordinated gap-accounting system, not eleven independent chasers.
**Depends on:** Core 15, Core 9

- [x] Gap assignment model (A/B/C/D gaps mapped from front + call)
  *As built: `UPSDefenderGapSubsystem` is the one authority for gap ownership. At the snap it
  maps the call's `Front` (`Data/run_fits.json`) onto the front defenders, left to right per
  role. The call then takes defenders in coverage out of the fit, which leaves their gaps open.
  Gap spots follow the live offensive line, with an inline tight end extending it.*
- [x] Fit maintenance vs. blockers (spill/box responsibilities, force player rules)
  *As built: the outermost fitter on each side forces (box: outside leverage), and everyone inside
  him spills (inside leverage). On a run read, `UPSDefenderAIComponent` fits the gap (new `Fit`
  action) until the carrier comes to it or crosses the line, then attacks. A blocked fitter works
  across the blocker's face to his leverage point, and sheds with Epic 70's move library.*
- [x] Linebacker flow/scrape-exchange coordination with the line
  *As built: second-level fitters flow toward the carrier across the field
  (`FlowWeight`). When the gap the carrier heads for has a blocked owner, or none, the nearest
  free linebacker scrapes into it and the two swap gaps (once per gap per play).*
- [ ] Integrity telemetry: visualize gap coverage live (consumes Track A iconography for debug)
  *Telemetry half built: `GetIntegrity()` gives every gap's owner and whether it is filled, and a
  `GapIntegrity` bus event fires whenever the open gaps change or an exchange happens. Overlay
  code built: `UPSDefenderGapOverlaySubsystem` keeps a marker per gap on its spot (filled,
  blocked, open or unowned; colors in `Data/gap_overlay.json`). It emphasizes the owner of an
  open gap through Epic 36's `UPSOverlayEmphasisSubsystem`. Development builds draw the markers
  as debug rings; `ps.Overlay.GapIntegrity 1` turns it on. Still open, so unticked: the
  editor-made marker and a PIE check (`Specs/Gap_Integrity_Overlay_Spec.md`).*

### Epic 82: LLM Game-Intelligence Hooks

**Size/Mode:** M / code
**Goal:** Structured game-state surfaces that external models consume for analysis, play suggestion, and narration — all bridge-gated.
**Depends on:** Core 25, 26, Core 18

- [x] Game-state serialization contract (situation, personnel, tendencies) sized for model context windows
  *As built: `PSGameStateSerializer::Serialize` writes one compact JSON object
  (`play-sports.game-state/1`) from the authorities: the situation from the play simulation's
  `GameState` on the bus, the personnel from `UPSPersonnelManager`, the human's tendencies from
  `UPSOpponentModel` (Epic 78) and the game's team lines and leaders from `UPSStatsEngine` (Epic
  92). It never runs over its budget (`ContextBudgetChars` in `Data/game_intelligence.json`, at
  least `MinBudgetChars` = 1024): the leaders, the players on the field and the tendency shares go
  first, then whole sections, named in `trimmed`; the situation always stays. Tested:
  `PlaySports.GameIntelligence.StateContractSizeBound`.*
- [x] Play-call consultation endpoint (Epic 18's hook made real once the bridge exists)
  *As built: `UPSGameIntelligenceSubsystem` is the coaching AI's `IPSCoachingSuggestionProvider`.
  An agent turns consultation on per CPU side (`SetConsultation`); each call window then opens a
  `PlayCall` request (the game state, the side's plays this down as the only answers), and the
  side's call waits in `PollReadyToSnap` until it is answered or `PlayCallTimeoutSeconds` pass
  (never past the quick-call point of the play clock). An answer outside the choices is refused;
  an answered play is the CPU's call unless the clock or special teams call one outright
  (`Overruled`); no answer means the CPU's own call. Agents poll `GetPendingRequestsJson` and
  answer with `AnswerRequest` through AgenticLink's `call_function`, which now also reaches world
  and game-instance subsystems; in-process code binds `OnRequestOpenedMC`. Everything is gated on
  AgenticLink's `AgenticLinkBridge` modular feature (registered while its MCP server serves): with
  no bridge every hook is refused and the CPU calls at once. Tested:
  `PlaySports.GameIntelligence.PlayCallConsultation`, `.BridgeGate`,
  `PlaySports.AgenticLink.EngineReflection`. Not exercised against a live editor or model.*
- [x] Post-game analysis generation (drive summaries, key-play identification from Epic 42's scoring)
  *As built: the drives come from the `GameState` events (each finished drive's team, quarter,
  plays, yards and result); the key plays are `UPSHighlightSubsystem`'s reel, the most important
  first (`MaxKeyPlays`). At the final whistle, with the bridge online, a `DriveSummary` and a
  `GameAnalysis` request carry `PSGameStateSerializer::SerializeAnalysis` (within the same budget,
  oldest drives dropped first) and their answers are kept in `FPSGameAnalysis`. Tested:
  `PlaySports.GameIntelligence.PostGameAnalysis`.*
- [x] Model-slot routing per `AGENTS.md` free-tier contract (cheap models for narration, better for strategy)
  *As built: each request names a task of Epic 119's `tools/orchestrator/routing.json` (data:
  `PlayCallTask` strategy, `DriveSummaryTask` summary, `GameAnalysisTask` analysis), checked by
  `tools/validate_data.py` (the play call's task at least as capable as the summary's).
  `python -m tools.orchestrator game-hooks` is the relay: it polls the game over AgenticLink and
  sends each request through `RouterService.complete` by its task, no second router. Tested:
  `PlaySports.GameIntelligence.ModelRouting`, `tools/orchestrator/tests/test_game_hooks.py`
  (fake engine and router; no live model called).*

### Epic 83: Simulation Calibration Harness

**Size/Mode:** L / code
**Goal:** Simulated football produces statistically plausible football — measured, not vibes.
**Depends on:** Core 20's quick-sim, Core 24

- [ ] Reference statistical targets (completion %, YPC, sack rates, score distributions by era profile)
- [ ] Batch-sim runner: thousands of headless games producing distribution reports
- [ ] Deviation dashboards: which systems push stats out of range
- [ ] Tuning-loop workflow: parameter adjustments → re-run → convergence tracking
- [ ] Regression gate: calibration suite runs in CI (Epic 112) to catch balance-breaking changes

### Epic 84: Difficulty, Assists & Fairness Systems

**Size/Mode:** S / code
**Goal:** Skill levels and assist options make the game playable from novice to sicko without fake stat-cheating feel.
**Depends on:** 78, 79

- [x] Difficulty tiers built from AI capability dials (recognition speed, adaptation, execution variance) — not stat inflation
  *As built: `Data/difficulty.json` holds four tiers (Rookie, Pro, All-Pro, Legend), the
  `Difficulty` setting's choices. `UPSDifficultySubsystem` gives the CPU's players the tier as
  each play starts, after their style (Epic 79): recognition (how fast defenders react, how far
  the quarterback anticipates and how open he needs a man), the opponent model's adaptation dial
  (Epic 78) and a passer's scatter. Ratings are never touched, and the human's own players play as
  tuned. A world with no player settings has no tier, so every AI test runs the AI as tuned.*
- [x] Assist options: pass-lead help, auto-slide protection, suggested play highlighting
  *As built: three Gameplay settings. Pass lead (on by default): the human's throws lead the
  receiver; off, they go at him and the stick does the leading. Auto-slide (off by default): his
  quarterback slides when a tackler closes on him past the line, by the CPU quarterback's slide
  read. Suggested play (on by default): the coaching AI's top play and its formation are
  highlighted on the play-call screens, in the player's color vision setting.*
- [x] Rubber-band policy: explicitly none, or transparent and off-by-default
  *As built: none. Nothing in the difficulty or the opponent model reads the score; a test pins
  it at every tier, leading or trailing by four scores.*

### Epic 85: AI Observability & Debug Tooling

**Size/Mode:** M / code
**Goal:** Every AI decision is inspectable — the debugging surface that makes Epics 78–84 maintainable by agents.
**Depends on:** Core 14, Core 15, 26

- [x] Decision log: per-agent, per-tick reasoning records (considered options, chosen, why)
  *As built: `UPSAIDecisionLog` (world subsystem) records an `FPSAIDecisionRecord` for every
  decision tick of `UPSSkillPlayerAIComponent` and `UPSDefenderAIComponent`, every rush-move choice
  and every CPU play call. Each record holds the player, play and time, his assignment, the action,
  the target and the reason. Where the AI weighed options, they come with their scores and the one
  chosen: the QB's receivers by separation, the rush plan's moves, the coaching AI's plays by
  weight. It is off by default (`Data/ai_debug.json`; console variable `ps.AI.DecisionLog`) and
  costs nothing while off. Recording never changes a decision.*
- [x] On-field debug overlay: live BT state, target, assignment above any pawn (reuses Track A badge rendering)
  *As built: with `ps.AI.DebugOverlay` on (or `UPSAIDecisionLog::SetOverlayEnabled`), each
  player's latest decision is shown above him (`DescribeForOverlay`: player, assignment, action,
  target and reason), with a line to his target. `UPSAIDebugOverlayWidget` is the always-on debug
  layer, made by `APSHUD` in development builds. Each frame `UPSAIDecisionLog::LayoutOverlay`
  lays out a card for every player with a decision, for the player's camera, by Epic 28's badge
  rules (`PSAIDebugOverlay` over `PSOverlayBadgeLayout`: the projection, the scale by distance,
  the nudging clear of each other). The widget draws each card as the badge widget draws a badge,
  and the target lines with Slate lines. Unlike the badges it shows every player during the play;
  a card with no room is dimmed, not hidden. Cards are sized for the display's DPI, so a phone
  reads them the same. While the layer is up the world's debug text stands down. Look:
  `ai_debug.json`'s `Overlay*` fields. Tests: `PlaySports.AIDebug.OverlayLayout`,
  `OverlayCards`, `OverlayTuning`. Not seen on a screen yet: the PIE check of how 22 cards read,
  `Specs/HUD_Spec.md` (AI Debug Overlay).*
- [x] Play post-mortem dump: one file per play with all 22 decision streams, replay-linked (41)
  *As built: when a play ends (Scoring, or the next snap), `Saved/AIPostMortems/Play_<time>_<play>.json`
  holds the snap's situation, both calls, the bus events from the snap on, and every player's
  decision stream. The bus sequence numbers of the snap and the last event are the join key to the
  event history Epic 41's replay is built from. It is written with `ps.AI.PostMortem` or the
  data's switch, and the newest `MaxPostMortemFiles` are kept.*
- [x] Scriptable scenario runner: place 22 players in a state, run one decision cycle, assert outputs (extends Epic 24's gym)
  *As built: `UPSAIScenarioRunner` places a scenario's players (any number, up to all 22) under
  their AI, each with his ratings, DNA, ball, route or assignment. It snaps, runs the decision
  cycles and checks each expectation (action, target, heading) against the decision log.
  Scenarios are data (`Data/ai_scenarios.json`), and `PlaySports.Gym.AIScenarios` runs them
  headlessly. The gym map's functional test (24.1, editor) can call `RunScenario` on its own
  world.*
