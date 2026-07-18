# Track G — Franchise & Meta-Game Depth (Epics 86–95)

Deepens core Epic 20's season loop into a living league: player acquisition, economics,
coaching, human stories, and history. Almost entirely pure code + data — excellent territory for
parallel agent work while gameplay tracks proceed. Sizing/mode legend: see `ROADMAP.md`.

### Epic 86: Draft & Scouting System

**Size/Mode:** L / code
**Goal:** An annual draft with imperfect information — scouting reveals (and sometimes misleads on) prospect quality.
**Depends on:** Core 19, Core 20, 121–122 (Track L generators)

- [ ] Prospect generation: draft classes with ratings, DNA (79), and hidden bust/boom variance
- [ ] Scouting model: reports narrow uncertainty ranges; scouting budget allocation matters
- [ ] Draft event flow: rounds, picks, AI team needs-based selection, trade-up/down (88)
- [ ] Combine/pro-day data layer feeding scouting accuracy
- [ ] Rookie integration into rosters/contracts (87)

### Epic 87: Contracts & Salary Cap

**Size/Mode:** L / code
**Goal:** Rosters live under economic constraint — contracts, cap math, extensions, cuts with consequences.
**Depends on:** Core 19, Core 20

- [ ] Contract model (years, base, bonus proration, guarantees, dead money)
- [ ] Cap engine with league-year rollover and compliance enforcement
- [ ] Negotiation logic: player-side demands from performance, age, market, morale (91)
- [ ] Free-agency period flow (bidding AI teams, user offers, decision timers)
- [ ] Restructure/extension/cut tooling with cap-consequence preview

### Epic 88: Trade Logic & League Market

**Size/Mode:** M / code
**Goal:** AI teams trade rationally — players and picks move for defensible value.
**Depends on:** 86, 87

- [ ] Value model: players (rating/age/contract) and picks (chart + team context)
- [ ] AI trade proposal generation and evaluation (accept/counter/reject with reasons)
- [ ] Deadline dynamics: contenders buy, rebuilders sell
- [ ] Sanity guardrails: no lopsided fleecing, league-wide trade telemetry for tuning

### Epic 89: Coaching Staffs & Scheme Identity

**Size/Mode:** M / code
**Goal:** Teams have coaches whose schemes visibly shape how they play and what players fit.
**Depends on:** Core 18, Core 16

- [ ] Coach entities: HC/OC/DC with scheme affinities and skill ratings
- [ ] Scheme-playbook binding: a team's playbook (16) derives from its coordinators
- [ ] Player-scheme fit modifiers (a zone-scheme lineman misfit in power)
- [ ] Carousel: firings, hirings, and scheme churn between seasons

### Epic 90: Training, Gameplan & Weekly Preparation

**Size/Mode:** M / code
**Goal:** The week between games matters — practice allocation, opponent-specific gameplans, injury management.
**Depends on:** Core 19, 78

- [ ] Weekly training allocation (develop players vs. gameplan vs. rest) with tradeoffs
- [ ] Gameplan boosts: scouted-opponent focus areas granting situational modifiers
- [ ] Practice injury/fatigue risk coupling (Core 19's stamina and injury models)
- [ ] AI teams run the same system (no user-only advantages)

### Epic 91: Morale, Chemistry & Locker Room

**Size/Mode:** M / code
**Goal:** Players respond to usage, winning, contracts, and each other — a human layer over the roster.
**Depends on:** Core 19, 87

- [ ] Morale model: inputs (playing time, team success, contract status, role) → effects (performance variance, FA willingness)
- [ ] Chemistry: unit cohesion from lineup stability (OL continuity bonus)
- [ ] Event system: trade requests, holdouts, leadership emergence
- [ ] Transparency surface so effects are readable, never mysterious

### Epic 92: Statistics Engine & Record Book

**Size/Mode:** L / code
**Goal:** Every play feeds a queryable statistical universe — box scores, season leaders, career totals, records.
**Depends on:** 26, Core 20

- [ ] Stat event pipeline from telemetry (26) → per-play attribution (passer/rusher/receiver/tacklers)
- [ ] Aggregation layers: game box score, season, career, franchise, league
- [ ] Leaderboards and record book with broken-record events (feeds 93, Track H commentary)
- [ ] Persistence in the save architecture (Epic 116) and query API for UI/overlays
- [ ] Advanced derived metrics (per-attempt efficiencies, situational splits)

### Epic 93: League Narrative & Storyline Generator

**Size/Mode:** M / code
**Goal:** Seasons tell stories — streaks, rivalries, awards races, comeback arcs — surfaced as news and broadcast talking points.
**Depends on:** 92, 82

- [ ] Storyline detection rules over the stat/event stream (win streaks, rookie surges, revenge games)
- [ ] Weekly league news digest generation (template-based; LLM-enhanced via 82 when bridged)
- [ ] Awards system: weekly honors, season awards with voting model
- [ ] Broadcast integration: active storylines feed Track A chyrons and Track H commentary

### Epic 94: Multi-Season Aging, Retirement & Legacy

**Size/Mode:** M / code
**Goal:** The league regenerates across decades — aging curves, retirements, hall of fame, franchise history.
**Depends on:** 86, 92

- [ ] Age-based progression/regression curves per role (extends Core 19's progression)
- [ ] Retirement decisions (age, performance, injuries, morale) and roster churn balance
- [ ] Hall of fame induction from career stat thresholds + awards (92)
- [ ] Franchise history archive: past seasons, champions, legends browsable in UI (Track I)

### Epic 95: Owner Mode & League Economics

**Size/Mode:** S / code
**Goal:** A light economic layer above GM play — revenue, pricing, staff budgets, relocation pressure.
**Depends on:** 87, Core 20

- [ ] Revenue model (attendance from team success/pricing, media share)
- [ ] Budget allocation: scouting (86), training (90), staff (89) funded from revenue
- [ ] Fan-satisfaction pressure with long-losing consequences (kept simple; this is a garnish epic)
