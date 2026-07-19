# Track J — Multiplayer & Shared Play (Epics 107–111)

Human-vs-human play, from couch to online. Deliberately later-stage: online netcode for a
physics game is a major architectural commitment — Epic 108 is the decision gate the rest of
the track hangs on. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** Epic 108's determinism audit now has concrete inputs:
C1's timestamped event history, C2's single outcome authority, and Epic 115's serialization
format are the replication substrate — the audit reviews those, not abstract principles. The
review flagged tick-order races (ball vs sim vs GameMode writes) as an existing hazard the
audit must resolve; C2 removes the largest one. Epic 107 inherits Track M's device detection
(127) and glyph table (128) for its two-controller session flow.

### Epic 107: Local Head-to-Head

**Size/Mode:** M / code
**Goal:** Two players on one machine — the cheapest multiplayer and the test bed for all competitive rules.
**Depends on:** 104, 102

- [ ] Two-controller session flow (side select, simultaneous play-calling with hidden picks)
- [ ] Split input contexts: user-vs-user control switching rules on defense
- [ ] Competitive-integrity toggles (hide route art (27) and defensive icons (31) from the opponent's view where applicable)
- [ ] Pause/quit/resume etiquette rules for versus play

### Epic 108: Online Architecture Decision & Foundation

**Size/Mode:** XL / code
**Goal:** A researched, tested architectural commitment for online play — deterministic lockstep vs. server-authoritative replication — proven by a networked prototype.
**Depends on:** Core 17 (determinism hooks), 115

- [ ] Feasibility study: physics determinism audit across the engine systems (documented findings, extends Epic 115)
- [ ] Architecture selection with written ADR (lockstep + rollback vs. UE replication vs. hybrid)
- [ ] Networked prototype: one play between two machines with the chosen model
- [ ] Latency strategy: input delay budget, prediction/rollback scope, disconnect handling
- [ ] Session/matchmaking service abstraction (platform-agnostic interface, implementation later)

### Epic 109: Online Head-to-Head

**Size/Mode:** L / code
**Goal:** Full online versus games on the Epic 108 foundation.
**Depends on:** 108, 107

- [ ] Full-game session flow online (connect, play, complete, report)
- [ ] Desync detection/telemetry and recovery policy
- [ ] Quit/disconnect handling with fair outcomes
- [ ] Connection-quality UX (indicators, degradation modes)

### Epic 110: Spectator & Broadcast Mode

**Size/Mode:** S / code
**Goal:** A third participant can watch a game live with full broadcast presentation.
**Depends on:** 109, 38

- [ ] Spectator session join with no-input camera director view (38 does the work)
- [ ] Spectator-side overlay freedom (their own Track A toggles, both playbooks visible)
- [ ] Local recording of spectated games via Epic 41's replay persistence

### Epic 111: Asynchronous League Play

**Size/Mode:** M / code
**Goal:** Multi-human franchise leagues that don't require simultaneous scheduling — play your game, advance together.
**Depends on:** Core 20, 109, 116

- [ ] Multi-user league model: human-controlled franchises inside one season state
- [ ] Week advancement rules (deadlines, auto-sim absentees, commissioner controls)
- [ ] League state sync/transfer format (save-architecture extension, 116)
- [ ] League dashboard surfaces: schedules, results, standings, transactions feed
