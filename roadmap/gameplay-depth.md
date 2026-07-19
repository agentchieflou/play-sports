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

- [ ] Audible system: swap to a compatible play from the same formation
- [ ] Hot-route individual receivers (route swap from an allowed set per alignment)
- [ ] Pre-snap motion with defensive reaction (man-indicator when a DB travels)
- [ ] Protection adjustments: slide protection, RB/TE block-or-release
- [ ] Crowd-noise interference on road audibles (consumes Epic 49's coupling)

### Epic 67: Defensive Pre-Snap Interaction

**Size/Mode:** M / code
**Goal:** The defense disguises and adjusts: shell rotations, show-blitz, coverage audibles, individual matchups.
**Depends on:** Core 15, Core 16, 66

- [ ] Coverage-shell disguise (show two-high, rotate at snap)
- [ ] Show-blitz/creep mechanics with actual vs. shown assignment separation
- [ ] Defensive audibles and per-player matchup assignment (shadow a receiver)
- [ ] AI usage of disguise driven by `Awareness` and coaching profile (Epic 18)

### Epic 68: Route-Running Nuance Model

**Size/Mode:** L / code
**Goal:** Routes are contested skills, not spline-following — releases, stems, breaks, and double moves resolved by attributes.
**Depends on:** C3, C4, Core 14, Core 16

- [ ] Release contest vs. press coverage at the line (win/delay/reroute outcomes)
- [ ] Stem/break sharpness derived from `Agility` (round vs. sharp cuts, separation math)
- [ ] Double-move system with defender-bite probability (ties to 69's leverage state)
- [ ] Option/sight-adjust routes reading coverage post-snap (hooks Epic 16's schema)
- [ ] Timing windows: QB read progression (14) synchronized to route break timing

### Epic 69: Coverage Matchup Engine

**Size/Mode:** L / code
**Goal:** DB-vs-WR is a continuous contest — press, leverage, cushion, zone handoffs, help rules.
**Depends on:** C3, C4, Core 15, 68

- [ ] Leverage model (inside/outside positioning as persistent state both AIs play against)
- [ ] Press/jam contest at snap paired with 68's release system
- [ ] Zone handoff rules (carry vertical, pass off underneath, communicate — visible via Track A stars)
- [ ] Safety help behavior (over-the-top rules, robber, rotation integrity)
- [ ] Pass-interference emergence: contest physics can draw Epic 11 penalty flags organically

### Epic 70: Pass-Rush Move System

**Size/Mode:** M / code
**Goal:** Rushers win with technique, not just stats — swim, rip, bull, spin, and counters against blocker responses.
**Depends on:** C3, C4, Core 9

- [ ] Move library with attribute-gated success curves (swim/rip/bull/spin/club)
- [ ] Counter-move chains (blocker anchors bull → rusher spins off)
- [ ] Rush-plan AI: pick moves by matchup history within the game
- [ ] Double-team recognition and split responsibilities

### Epic 71: QB Pocket Play & Scramble System

**Size/Mode:** M / code
**Goal:** The QB navigates a live pocket — climbs, slides, escapes, and decides to run.
**Depends on:** Core 14, Core 9

- [ ] Pocket-shape awareness from live line-play state (climb/slide directions)
- [ ] Escape triggers and scramble-drill activation (receivers break off per Epic 17's rules)
- [ ] Run/throw decision layer past the LOS constraint, slide/protect-self endings
- [ ] Sack resolution variety (strip attempts, throwaways under `Awareness` gates, grounding risk)

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

- [ ] Kick/punt block mechanics (edge timing, interior push, block-or-return unit choice)
- [ ] Return schemes: wall/wedge setups from playbook data, lane discipline for coverage
- [ ] Fake punt/FG plays integrated into playbook + AI call logic (Epic 18 risk model)
- [ ] Onside kicks and desperation kick-return laterals

### Epic 76: Situational Football Intelligence

**Size/Mode:** M / code
**Goal:** The game understands its own leverage moments — two-minute drill, four-minute offense, clock-kill, hurry-up.
**Depends on:** Core 12, Core 18

- [ ] Tempo system: huddle/no-huddle/hurry-up pacing controls for user and AI
- [ ] Two-minute logic: sideline throws, spike/kneel, timeout optimization
- [ ] Four-minute (leading) logic: clock-kill runs, stay-inbounds behavior
- [ ] End-of-half decision correctness harness (scripted scenarios asserting sane AI choices, extends Epic 24)

### Epic 77: Momentum & Composure Layer

**Size/Mode:** S / code
**Goal:** Big moments move an invisible psychological needle that subtly modulates performance.
**Depends on:** Core 6, 49

- [ ] Momentum model fed by game events (turnovers, streaks, crowd state)
- [ ] Per-player composure response (attribute-scaled boost/press effects, kept subtle and tunable)
- [ ] Full transparency in telemetry (26) so its influence is inspectable and capped
