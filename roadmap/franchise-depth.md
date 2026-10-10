# Track G — Franchise & Meta-Game Depth (Epics 86–95)

Deepens core Epic 20's season loop into a living league: player acquisition, economics,
coaching, human stories, and history. Almost entirely pure code + data — excellent territory for
parallel agent work while gameplay tracks proceed. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** persistence is ready — `UPSSaveSubsystem` (versioned,
CRC, backup, async, 4 tests) and `Specs/Save_Architecture.md`'s Franchise category are the
mandated save path for every epic here. The schedule generator becomes `UPSScheduleEngine`
after C3's rename. Epic 92's stat pipeline is a C1 bus subscriber (events are already
timestamped); drive/clock state from core Epics 10/12 exists in the sim. No epic here invents
its own persistence or event capture.

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

- [x] Contract model (years, base, bonus proration, guarantees, dead money) *(`FPSContract` in `PSContractData.h`: consecutive league years, each a base salary, its guaranteed part and a share of the bonuses paid; cap hit = base + share. `UPSContractManager` holds every contract (`FPSContractLedger`, persisted in `UPSFranchiseSaveGame::ContractLedger`); `MakeContract` turns an offer into flat salaries, a bonus prorated over up to `MaxProrationYears` and front-loaded guarantees. Money is in thousands of dollars)*
- [x] Cap engine with league-year rollover and compliance enforcement *(`Data/contracts.json` (`FPSContractTuning`). Cap space = the year's cap + carryover - cap hits - dead money; `SignContract` refuses a deal over it. `RolloverLeagueYear` grows the cap, carries unused space (up to `MaxCarryoverFraction`), expires finished deals, drops past dead money and lists teams over the new cap; `EnforceCompliance` cuts a CPU team back under it, least rating per cap dollar first, off its roster too. `UPSFranchiseFlow::EndSeason` runs the rollover)*
- [x] Negotiation logic: player-side demands from performance, age, market, morale (91) *(`UPSContractNegotiation`: rating sets the ask between the minimum and the role's top of the market (a cap fraction), age shortens the deal and discounts it past `DeclineAge`, the league's average cap space moves it within `MaxMarketAdjustment`; an offer is valued by guarantees, years and, on the player's own team, his morale, then accepted, countered with the ask, or rejected. Morale is a 0-1 input until Epic 91 supplies it; ratings are the five-skill average, so Epic 92's stats don't weigh in yet)*
- [x] Free-agency period flow (bidding AI teams, user offers, decision timers) *(`UPSFreeAgency`, deterministic, day by day: CPU teams bid for roles short of `RosterTarget` within their cap space, the user's team with `SubmitOffer` (refused over the cap, rejected too low, signed on the spot far above the ask, otherwise weighed); after `DecisionDays` a player signs the best offer he would counter; unsigned asks cool to a floor. Signings join the team's `UPSRoster` (new `AddPlayer`/`RemovePlayer`). `UPSFranchiseFlow` opens it at season end with every expired or cut player. No free-agency screen yet (Track I))*
- [x] Restructure/extension/cut tooling with cap-consequence preview *(`PreviewCut`/`CutPlayer` (dead money now or spread over two years), `PreviewRestructure`/`RestructureContract` (base above the minimum into a prorated bonus), `PreviewExtension`/`ExtendContract` and `ProposeExtension` (the player answers first); each preview gives the player's cap hit, dead money and the team's space this year and next, before and after, without making the move. No cap screen yet (Track I))*

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

- [x] Coach entities: HC/OC/DC with scheme affinities and skill ratings *(`UPSStaffManager` over `Data/coaching_staffs.json` (`FPSCoachingLeague`): seven schemes (West Coast, Air Raid, Power Run, Zone Run; Cover 2 Zone, 3-4 Pressure, Nickel Man) and 21 coaches, free agents included, each with a scheme, play-calling and development ratings, and a head coach's aggression)*
- [x] Scheme-playbook binding: a team's playbook (16) derives from its coordinators *(`BuildTeamPlan`: the plays in the coordinators' scheme formations plus every kick and clock play, and a tendency of the head coach's aggression and the scheme's category lean, scaled by the coordinator's play calling. `UPSPlayCallSubsystem::SetTeamPlan` holds each team's plan: CPU calls, suggestions and human calls stay in the team's book, the call screen and the reasons name the scheme. `ApplyToPlayCall` sets both, called at kickoff through `UPSMatchSetup::ApplyStaffs` for the match's home and away teams)*
- [x] Player-scheme fit modifiers (a zone-scheme lineman misfit in power) *(`GetSchemeFit` / `ApplySchemeFit`: a player's weighted ratings in his coordinator's scheme against his average across the side's schemes, as a multiplier between `WorstFitMultiplier` and `BestFitMultiplier` that a developing coordinator softens; the roster keeps the player's own ratings. Applied at kickoff to the simulation's players and in `UPSFranchiseFlow`'s quick sims)*
- [x] Carousel: firings, hirings, and scheme churn between seasons *(`RunCarousel`, deterministic: losing head coaches past their grace seasons and the worst units' coordinators are fired, winning teams' coordinators are hired away as head coaches, a new head coach's scheme displaces the coordinator running another, and vacancies fill worst team first, the head coach's scheme preferred. Staffs persist in `UPSFranchiseSaveGame`; `UPSFranchiseFlow::EndSeason` runs it on the final standings)*

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

- [x] Stat event pipeline from telemetry (26) → per-play attribution (passer/rusher/receiver/tacklers) *(`UPSPlaySimulation`, the outcome authority, announces every play as a `PlayResult` bus event (`FPSTelemetryPlayResultEvent`: the situation at the snap, the result, the points, and the passer, receiver, rusher, tackler and interceptor by PlayerId, from the Throw/Catch/Tackle events by display name, or the quick sim's rolled players) and through `OnPlayResolved` for world-less quick sims (`UPSQuickSimRunner::OnPlayResolved`). `UPSStatsEngine::RecordPlay` attributes each play once. A played game's tackles don't reach the bus yet (`UPSBallActionComponent` calls `RecordTackle` directly), so live runs and tacklers go unattributed; the quick sim throws every play)*
- [x] Aggregation layers: game box score, season, career, franchise, league *(`FPSBoxScore` per game; season totals summed from this season's box scores and kept as `FPSSeasonStats` totals at `EndSeason`; `GetPlayerCareer`, `GetFranchiseTotals`, `GetLeagueTotals`. `UPSFranchiseFlow` records every simulated game and archives the season at its end; `APSGameMode::MatchStats` keeps a played game's box score from the bus)*
- [x] Leaderboards and record book with broken-record events (feeds 93, Track H commentary) *(`GetLeaders` for any `EPSStatCategory` over single games, a season or careers (team categories over a team's history); the record book sets a first mark quietly, then announces a fall as a `RecordBroken` bus event and `OnRecordBroken`: a single-game record whenever beaten, a season or career record when a new holder passes it, or the season holder sets it again in a later season)*
- [x] Persistence in the save architecture (Epic 116) and query API for UI/overlays *(`FPSStatBook` in `UPSFranchiseSaveGame::StatBook`: this season's box scores, past seasons' totals, the records; queries by game, player season, career, team season, franchise, league, leaders and records. No stats screen or overlay reads it yet (Tracks I and A))*
- [x] Advanced derived metrics (per-attempt efficiencies, situational splits) *(`ComputePlayerMetrics`: completion and touchdown/interception percentages, yards per attempt, the NFL passer rating, yards per carry and catch, catch rate; `ComputeTeamMetrics` from the down and red-zone splits each team line keeps: third-down and red-zone touchdown rates, yards per play, field-goal percentage, turnover margin)*

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
