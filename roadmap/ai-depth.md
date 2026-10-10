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

- [ ] Tendency tracker: user play-calling distributions by situation (down/distance/personnel)
- [ ] Counter-selection: defensive call weighting shifts against observed tendencies
- [ ] In-game adjustment moments (halftime adaptation step-change)
- [ ] Guardrails: adaptation strength as a difficulty dial, never psychic (only observed data)

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

- [ ] Formation classifier from offensive alignment data (personnel + splits + backfield set)
- [ ] Key-reading: run/pass diagnosis from line behavior and backfield flow post-snap
- [ ] Recognition latency scaled by `Awareness` + DNA (79) — elite defenders jump routes, poor ones bite on fakes
- [ ] Feeds the deception-resistance rules in Epic 72 (replaces its interim bite model)

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
  `GapIntegrity` bus event fires whenever the open gaps change or an exchange happens. The live
  visualization is still to do: it needs Track A's iconography and an editor session to
  verify.*

### Epic 82: LLM Game-Intelligence Hooks

**Size/Mode:** M / code
**Goal:** Structured game-state surfaces that external models consume for analysis, play suggestion, and narration — all bridge-gated.
**Depends on:** Core 25, 26, Core 18

- [ ] Game-state serialization contract (situation, personnel, tendencies) sized for model context windows
- [ ] Play-call consultation endpoint (Epic 18's hook made real once the bridge exists)
- [ ] Post-game analysis generation (drive summaries, key-play identification from Epic 42's scoring)
- [ ] Model-slot routing per `AGENTS.md` free-tier contract (cheap models for narration, better for strategy)

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

- [ ] Difficulty tiers built from AI capability dials (recognition speed, adaptation, execution variance) — not stat inflation
- [ ] Assist options: pass-lead help, auto-slide protection, suggested play highlighting
- [ ] Rubber-band policy: explicitly none, or transparent and off-by-default

### Epic 85: AI Observability & Debug Tooling

**Size/Mode:** M / code
**Goal:** Every AI decision is inspectable — the debugging surface that makes Epics 78–84 maintainable by agents.
**Depends on:** Core 14, Core 15, 26

- [ ] Decision log: per-agent, per-tick reasoning records (considered options, chosen, why)
- [ ] On-field debug overlay: live BT state, target, assignment above any pawn (reuses Track A badge rendering)
- [ ] Play post-mortem dump: one file per play with all 22 decision streams, replay-linked (41)
- [ ] Scriptable scenario runner: place 22 players in a state, run one decision cycle, assert outputs (extends Epic 24's gym)
