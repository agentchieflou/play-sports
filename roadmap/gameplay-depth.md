# Track E — Gameplay Depth (Epics 66–77)

Deepens the Phase 1/2 core into real football nuance: the pre-snap chess match, route/coverage/
rush technique, situational play, and the psychological layer. Mostly pure code — the meatiest
track for C++ gameplay agents. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** the systems this track deepens are now real on `main`:
`APSBall` (throw/catch/handoff/pitch/fumble), attribute-scaled movement with
`FMovementTuningRow`, tackle/blocking logic (being componentized in Phase 1.5 C3). Epics 68–70
**extract and extend those components** — growing the pawn/sim further is review-rejected.
Epic 73's penalty detection subscribes to C1 bus events. Catch/contest probability formulas are
currently hardcoded in `APSBall::OnBallOverlap` — C4 extracts them to testable, DataTable-tuned
functions this track then builds on. Tests per epic, tuning in DataTables.

### Epic 66: Offensive Pre-Snap Interaction

**Size/Mode:** L / code
**Goal:** The user (or AI) can audible, hot-route, motion, and adjust protection before the snap.
**Depends on:** Core 14, Core 16

- [x] Audible system: swap to a compatible play from the same formation *(`UPSPreSnapSubsystem::Audible`/`AudibleToNext`: another play of the call's formation, made at `UPSPlayCallSubsystem` as a new call; the old call's changes go with it. A CPU QB with `CpuReadMinAwareness` checks a run out of a blitz or heavy box and a pass out of a light one. Three plays added so formations share plays. Tuning: `Data/presnap_tuning.json`)*
- [x] Hot-route individual receivers (route swap from an allowed set per alignment) *(`HotRoute`/`CycleHotRoute`: wide, slot, tight and backfield sets in `HotRouteSets`; `UPSPlayOrchestrator` applies them at the snap. The CPU beats a blitz look with its slot on `BlitzHotRoute`)*
- [x] Pre-snap motion with defensive reaction (man-indicator when a DB travels) *(`StartMotion`: across the formation; in man coverage the AI defender over him travels with him and the `PreSnap` bus event carries `bManIndicator`; the route starts where the motion ends)*
- [x] Protection adjustments: slide protection, RB/TE block-or-release *(`SetSlide`: the game mode's line pairing now goes through `ComputeBlockingPairs`, slide side first, leaving the backside rusher to a back or tight end kept in (`SetProtection` Block/Release). Human buttons: `UPSPreSnapInputComponent`, PreSnap context)*
- [ ] Crowd-noise interference on road audibles (consumes Epic 49's coupling) *(waits for Epic 49: there is no crowd-noise model to consume yet)*

### Epic 67: Defensive Pre-Snap Interaction

**Size/Mode:** M / code
**Goal:** The defense disguises and adjusts: shell rotations, show-blitz, coverage audibles, individual matchups.
**Depends on:** Core 15, Core 16, 66

- [x] Coverage-shell disguise (show two-high, rotate at snap)
  *As built: `UPSDefenderPreSnapSubsystem` lines the defense up once it has called, on its next
  tick (or the first read of its look). It starts from the formation spots the side stands on,
  so a personnel change's lineup (`UPSPersonnelManager`) comes first. The safeties
  take the shell's structure (`ShellSafeties` in `Data/defensive_presnap.json`), or the other
  one when disguised. At the snap `UPSDefenderAIComponent` plays the real assignment from there,
  so the disguise rotates.*
- [x] Show-blitz/creep mechanics with actual vs. shown assignment separation
  *As built: real blitzers walk up; with a creep they hold a coverage look and walk up late
  (`TickPreSnap`). A shown blitz walks up linebackers who drop at the snap. The offense's read
  (`UPSPreSnapSubsystem::GetDefensiveLook`) now comes from where defenders stand
  (`ReadShownLook`), never the call.*
- [x] Defensive audibles and per-player matchup assignment (shadow a receiver)
  *As built: `Audible`/`AudibleToNext` switch to another play of the call's front.
  `SetShadow` keeps a defender in man on a receiver until cleared; `UPSPlayOrchestrator` applies
  it at the snap. The human makes these calls with the defense's own buttons in the
  `DefensePreSnap` context: `DefenseAudible`, `ShadowSelect`, `Shadow`, `ShowBlitz`,
  `DisguiseShell` and `Creep` (`UPSDefenderPreSnapInputComponent`). Every change goes on the bus
  (`DefensivePreSnap`).*
- [x] AI usage of disguise driven by `Awareness` and coaching profile (Epic 18)
  *As built: a CPU call disguises its shell, shows a blitz or creeps one. Each is as likely as
  the coach's `AggressionScore` allows, times the involved defenders' Awareness; the rolls are
  seeded. A low-Awareness safety leaks his disguise part-way to his real spot. On a man call,
  the CPU's best back shadows the best receiver. The play-call subsystem still uses a default
  profile, so `SetTendency` is where a team's profile plugs in.*

### Epic 68: Route-Running Nuance Model

**Size/Mode:** L / code
**Goal:** Routes are contested skills, not spline-following — releases, stems, breaks, and double moves resolved by attributes.
**Depends on:** C3, C4, Core 14, Core 16

- [x] Release contest vs. press coverage at the line (win/delay/reroute outcomes) *(`UPSRouteRunnerComponent` on `APSOffenseController`: a defender within `PressRadius` in front at the snap contests the release, `(Agility + Strength) / 2` against the presser's, rolled from the play's seed; a delay holds him, a reroute moves his route toward the sideline. Announced as `RouteRunning` on the bus. Defenders don't line up in press yet: that alignment is Epic 69's)*
- [x] Stem/break sharpness derived from `Agility` (round vs. sharp cuts, separation math) *(a stiff receiver turns for the next leg up to `MaxBreakRounding` before the corner, a sharp one cuts on it; `PSRouteRunning::BreakSeparationGain` is the separation a break makes on the defender, which the QB counts on when he throws before the break)*
- [x] Double-move system with defender-bite probability (ties to 69's leverage state) *(`bFake` waypoints (`SlantGo`, `OutAndUp`): the receiver sells the fake, the nearest defender bites by `BiteChance` (receiver Agility against his Awareness) and `UPSDefenderAIComponent` freezes him. The leverage term joins the bite chance when Epic 69's leverage state exists)*
- [x] Option/sight-adjust routes reading coverage post-snap (hooks Epic 16's schema) *(`FPSRoute::OptionReadWaypoint`, `VsManBranch`, `VsZoneBranch`: at the read a man defender sends him on the man branch, away from the defender's leverage; zone sits him down (`Option` route, `Offense_TwinsOption`))*
- [x] Timing windows: QB read progression (14) synchronized to route break timing *(`ChooseReceiver` reads a planned receiver only from his break (up to `MaxAnticipationSeconds` early, by Awareness) to `ReadWindowSeconds` after; out of time, or past every window, he reads the whole field. Tests: `Tests/PSRouteRunningTests.cpp`)*

### Epic 69: Coverage Matchup Engine

**Size/Mode:** L / code
**Goal:** DB-vs-WR is a continuous contest — press, leverage, cushion, zone handoffs, help rules.
**Depends on:** C3, C4, Core 15, 68

- [x] Leverage model (inside/outside positioning as persistent state both AIs play against) *(`UPSCoverageMatchupSubsystem`, the coverage matchup engine: every man matchup has its shell's side (`Shells` in `Data/coverage_matchups.json`), which the defender plays `LeverageShade` over and holds until the receiver crosses his face (`Coverage` Leverage Lost/Regained on the bus). The receiver plays against it: a break (now announced, `RouteRunning` Break) away from it puts the defender out of phase for the separation it makes (Epic 68's `BreakSeparationGain` plus `AwayFromLeverageBonus`, over `SeparationRecoverySpeed`), one into it gains nothing; a double move's fake toward it bites `LeverageBiteBonus` more often; an option route breaks away from it)*
- [x] Press/jam contest at snap paired with 68's release system *(before the snap a man-coverage back over a receiver walks up to press when the shell allows it and his jam chance (one minus Epic 68's release chance) reaches `PressMinJamChance`; the receiver's route runner contests his release against that presser. A won jam trails tight (`PressCushion`); a beaten presser is out of phase `PressBeatenSeconds` and freezes)*
- [x] Zone handoff rules (carry vertical, pass off underneath, communicate — visible via Track A stars) *(a zone defender picks up the receiver in his zone and plays on him; leaving it, he is handed to the zone he runs into, carried on vertically when no deeper zone is free, or passed off underneath. Each is announced (`Coverage` Carry, HandOff, PassOff) for the defensive iconography (Epic 31) to draw; drawing the stars is Epic 31's)*
- [x] Safety help behavior (over-the-top rules, robber, rotation integrity) *(a deep defender stays `OverTopCushion` over the deepest receiver in his area; when one leaves the deep zones (a blown-coverage rotation, Epic 17.4) the rest split the field between them; man defenders left over in a man-free or Cover 1 call take the deep middle, then the robber, who jumps the crosser in his window)*
- [x] Pass-interference emergence: contest physics can draw Epic 11 penalty flags organically *(a defender in contact with the targeted receiver while the ball is in the air, further from where it comes down than the receiver, draws a `PassInterference` flag (`FlagChance`, seeded per snap); `UPSPlaySimulation` enforces it as a spot foul with a first down, declined when the play gained more. Tests: `Tests/PSCoverageMatchupTests.cpp`)*

### Epic 70: Pass-Rush Move System

**Size/Mode:** M / code
**Goal:** Rushers win with technique, not just stats — swim, rip, bull, spin, and counters against blocker responses.
**Depends on:** C3, C4, Core 9

- [x] Move library with attribute-gated success curves (swim/rip/bull/spin/club)
  *As built: `UPSRushMoveComponent` on `APSDefenseController`, the library in
  `Data/pass_rush_moves.json`; each move pits one rusher rating against one blocker rating, gated
  by `MinAttribute` and clamped. A win ends `PairLinemen`'s engagement and bursts the rusher at
  the passer; each resolved move goes on the bus (`PassRushMove`).*
- [x] Counter-move chains (blocker anchors bull → rusher spins off)
  *As built: a stopped move records the blocker's `Response` (Anchor, Punch, Mirror); the moves
  that `Counter` it get `CounterBonus` on the next try.*
- [x] Rush-plan AI: pick moves by matchup history within the game
  *As built: the plan scores each move by its chance pulled toward its record against this
  blocker this game (kept per blocker `PlayerId` across plays).*
- [x] Double-team recognition and split responsibilities
  *As built: a lineman engaged on the rusher, or a free one beside him, makes a double team:
  every move's chance scales down and the `Split` move (double teams only) comes out. No
  protection scheme calls double teams yet; that is offense-side work.*

### Epic 71: QB Pocket Play & Scramble System

**Size/Mode:** M / code
**Goal:** The QB navigates a live pocket — climbs, slides, escapes, and decides to run.
**Depends on:** Core 14, Core 9

- [x] Pocket-shape awareness from live line-play state (climb/slide directions) *(`UPSPocketComponent` on `APSOffenseController` reads the rushers around the QB, a blocked one pressing less: edge pressure makes him climb (never onto the line), one-sided inside pressure slide away from it. Tuning: `Data/pocket_tuning.json`)*
- [x] Escape triggers and scramble-drill activation (receivers break off per Epic 17's rules) *(a collapsed pocket (free rusher within `EscapeRadius`, or too much pressure) with nobody open sends him out to the side with room, announced as a `Pocket` Escape on the bus; the orchestrator that handed out the play hears it and runs `TriggerScrambleDrill`: receivers on routes break upfield of him toward that side, deep ones on further, blockers keep blocking)*
- [x] Run/throw decision layer past the LOS constraint, slide/protect-self endings *(scrambling behind the line he throws on the run to a man who comes open (for `ScrambleMaxSeconds`), tucks it up a clear lane, and once past the line is a runner who can't throw and slides (`UPSCarrierMoveComponent`'s Slide) ahead of a closing tackler after `SlideMinGain`)*
- [x] Sack resolution variety (strip attempts, throwaways under `Awareness` gates, grounding risk) *(a sack about to land: a rusher from the blind side tries a strip (`StripChance`, seeded per snap); one the QB sees gets the ball thrown away from `ThrowawayMinAwareness`, at a receiver's feet or out wide past the line, and a throwaway from the tackle box with nobody near it is announced as `IntentionalGrounding` for the rules (Epic 73) to flag, unless the QB is aware enough (`GroundingAvoidAwareness`) to take the sack. Tests: `Tests/PSPocketTests.cpp`)*

### Epic 72: Play-Action, RPO & Option Football

**Size/Mode:** L / code
**Goal:** Deception plays exist as first-class mechanics with defenders who can genuinely be fooled.
**Depends on:** 66, 68, Core 14, Core 15

- [ ] Play-action: fake handoff mechanics with linebacker-bite model (`Awareness` + tendency history)
- [ ] RPO: post-snap read of a conflict defender gating give/pull/throw
- [ ] Zone-read and triple-option assignments (dive/keep/pitch with defender keys)
- [ ] Defensive integrity rules so option football is stoppable by disciplined AI
- [ ] Playbook schema extensions (Epic 16) for all deception play types

### Epic 73: Full Penalty & Rules Depth

**Size/Mode:** M / code
**Goal:** Expands Epic 11's starter penalties toward rulebook coverage — emergent, not dice-rolled.
**Depends on:** Core 11, 69, 61

- [ ] Holding (offensive/defensive) detected from actual engagement physics states
- [ ] PI/illegal contact/hands-to-face from coverage contest data (69)
- [ ] Procedural penalties: false start, offsides, illegal formation/motion, too many men
- [ ] Enforcement engine: spot fouls, offsetting, accept/decline decision flow (AI + user)

### Epic 74: Ball Security & Turnover Ecosystem

**Size/Mode:** M / code
**Goal:** Turnovers are earned events with full aftermath — strips, tips, interception returns, scoop-and-scores.
**Depends on:** Core 7, Core 8, 61

- [ ] Carry-security model (ball-carrier cover-up vs. defender strip attempts, situational tuck)
- [ ] Tipped/batted ball physics with live-ball scramble behavior
- [ ] Interception returns: instant offense/defense role flip for all 22 (stress-tests Epic 17)
- [ ] Fumble pile determination and review hook (ties to 41's replay)

### Epic 75: Special Teams Depth & Trick Plays

**Size/Mode:** M / code
**Goal:** Beyond Epic 13's basics — blocks, returns as schemes, fakes, and onside kicks.
**Depends on:** Core 13

- [x] Kick/punt block mechanics (edge timing, interior push, block-or-return unit choice) *(`UPSSpecialTeamsModel::GetBlockChance`: a base chance per kick, plus edge timing (the rushers' fastest player over the cover men's) and interior push (linemen's strength over the protection's), times `BlockUnitMultiplier` when the defense calls `KickBlock`; a blocked punt recoils and can be scored. The CPU defense calls the block or the return against the kick the offense's formation shows (`UPSSpecialTeamsAI::DecideReceiving`), re-calling when a human offense lines up to kick; a block unit returns short. All tuning in `Data/special_teams.json`)*
- [x] Return schemes: wall/wedge setups from playbook data, lane discipline for coverage *(`KickReturn` plays in `sample_playbook.json` (`Return Wall`, `Return Wedge`); the receiving play's formation names its `ReturnSchemes` entry (yards and big-return chance); the coverage's awareness sets its lane discipline, which takes yards and big returns away. Kickoffs, punts and field goals all resolve through the model now, replacing the sim's hardcoded rolls)*
- [x] Fake punt/FG plays integrated into playbook + AI call logic (Epic 18 risk model) *(`Offense_FakePunt` / `Offense_FakeFieldGoal` share the real kick's formation; `UPSSpecialTeamsAI::DecideOffense` fakes on 4th and short when the coach's aggression clears `FakeMinAggression` and a roll beats `FakeCallChance` times it, after `ShouldGoForItOnFourthDown`. A played game's 4th down is now a call (punt, field goal, fake or go) instead of the sim's automatic kick; the sim resolves a fake at the snap, better against a block unit)*
- [x] Onside kicks and desperation kick-return laterals *(after a score the scorers kick off through a call window (`FPlayState::bKickoff`; a safety's free kick from the 20), fixing the old double possession flip that gave the scorers the ball back; the kicking team calls `OnsideKick` when it needs the ball late (or rarely by surprise), the receivers answer with the `HandsTeam`, and with seconds left and a touchdown needed they call `ReturnLaterals`: a touchdown or a fumble the kickers recover)*

### Epic 76: Situational Football Intelligence

**Size/Mode:** M / code
**Goal:** The game understands its own leverage moments — two-minute drill, four-minute offense, clock-kill, hurry-up.
**Depends on:** Core 12, Core 18

- [x] Tempo system: huddle/no-huddle/hurry-up pacing controls for user and AI *(every offensive call carries a tempo (`EPSTempo`, `Data/situational_tuning.json`): the CPU's from `UPSSituationAI::ChooseTempo`, a human's from the `Tempo` action (N / Y) through `UPSPlayCallSubsystem::CycleHumanTempo`; hurry-up reruns the human's last real play with no call screen. The call's play-clock mark rides the `PlayCall` bus event and `UPSPlaySimulation` runs a running game clock down to it at the snap (an accelerated clock: the CPU still snaps after `CpuSnapDelaySeconds`), replacing the flat 30-second tackle runoff outside quick sim)*
- [x] Two-minute logic: sideline throws, spike/kneel, timeout optimization *(`UPSSituationAI` reads the two-minute drill; `UPSCoachingAI` weights sideline routes up and middle routes and runs down from data, calls a spike on a running clock with no timeout left, and the CPU side calls its timeout when the window opens (`Timeout` bus event, charged by the sim). Spike and kneel are `Clock` plays in `sample_playbook.json`, resolved by the sim at the snap; a carrier out of bounds stops the clock in the last 2:00 / 5:00 of a half (`DoesOutOfBoundsStopClock`); the carrier AI heads for the sideline when the call says so)*
- [x] Four-minute (leading) logic: clock-kill runs, stay-inbounds behavior *(four-minute offense puts the run first and milks the play clock; the carrier AI turns back inside near the sideline (`FSkillPlayerAITuningRow::SidelineCushion`); victory formation kneels when the kneels and the milked clock the defense can't stop cover the time; a trailing defense spends its timeouts on a running clock)*
- [x] End-of-half decision correctness harness (scripted scenarios asserting sane AI choices, extends Epic 24) *(`PlaySports.Situation.EndOfHalfDecisionHarness`: 16 scripted clock/score/field scenarios, each asserting the situation, clock play, both sides' timeouts, tempo, sideline intent and the play caller's pick)*

### Epic 77: Momentum & Composure Layer

**Size/Mode:** S / code
**Goal:** Big moments move an invisible psychological needle that subtly modulates performance.
**Depends on:** Core 6, 49

- [ ] Momentum model fed by game events (turnovers, streaks, crowd state)
- [ ] Per-player composure response (attribute-scaled boost/press effects, kept subtle and tunable)
- [ ] Full transparency in telemetry (26) so its influence is inspectable and capped
