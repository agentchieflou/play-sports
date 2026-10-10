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

- [x] Prospect generation: draft classes with ratings, DNA (79), and hidden bust/boom variance *(`UPSDraft` over `Data/draft.json` (`FPSDraftTuning`) takes Epic 122's draft-class mode (`UPSLeagueGenerator::GenerateDraftClass`: ratings, entry ages, DNA) through `UPSFranchiseFlow::PrepareDraft` during the season. Each prospect's true grade (the contract market's `RatePlayer`) hides behind a public projection with an error of its own; `BoomBustChance` of a class is off by a further `BoomBustSwing` either way, and the pick reveals the truth. All draws are seeded per prospect and report)*
- [x] Scouting model: reports narrow uncertainty ranges; scouting budget allocation matters *(each team's report budget is `PointsPerSeason` times its scouting funding (Epic 95's `GetFundingIndex(Scouting)`); a report reads the true grade with `ReportNoise`, and now and then misleads by `MisleadSwing`; a team's estimate weighs the projection and its reports by their certainty, so its range narrows with each report (`GetProspectView`, `GetBoard`). A team sees only what it scouted; the CPU spends its points over the best-projected (`AutoScout`, open to the player too). No draft board screen yet (Track I))*
- [x] Draft event flow: rounds, picks, AI team needs-based selection, trade-up/down (88) *(`UPSFranchiseFlow::BeginDraft` after the season: `NumRounds` rounds, the worst record first. The team on the clock takes its board's best: its estimate plus `NeedWeight` times its need at the role (`contracts.json`'s `RosterTarget`). `AdvanceDraft` stops when the player's team is on the clock (`MakePick`, or `AutoPick` as the CPU would). Trading picks up or down is Epic 88's, which depends on this epic; no trades yet)*
- [x] Combine/pro-day data layer feeding scouting accuracy *(`CombineDrills` read a true rating plus noise (40-yard dash, 10-yard split, 3-cone, bench, board interview). Combine results are public and make the projection surer (`CombineCertainty`); the `ProDayShare` who hold pro days instead keep a wider projection, and their numbers are seen only by teams that scout them)*
- [x] Rookie integration into rosters/contracts (87) *(each pick joins his team's roster with his true ratings and signs a rookie-scale contract with `UPSContractManager`: `RookieYears` years, `FirstPickSalary` falling to the minimum salary by pick, guarantees from `FirstPickGuarantee` to `LastPickGuarantee`. A team without the cap room drafts him unsigned. The undrafted join free agency when it is open. The draft persists in `UPSFranchiseSaveGame::Draft`)*

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

- [x] Weekly training allocation (develop players vs. gameplan vs. rest) with tradeoffs *(`UPSWeeklyPreparation` over `Data/training.json` (`FPSTrainingTuning`): a week splits into develop, gameplan and rest shares. Development raises the roster's own ratings (`DevelopRatings`, less near 100, times the coordinator's Development from Epic 89 and the training funding from Epic 95's `GetFundingIndex`); development and gameplanning are practice, whose intensity tires players and risks injury; rest restores freshness. `UPSFranchiseFlow::PrepareWeek` runs every team's week once, before its games (`SimulateWeek` runs it if not yet run). Everything persists in `UPSFranchiseSaveGame::Training`. No training screen yet (Track I))*
- [x] Gameplan boosts: scouted-opponent focus areas granting situational modifiers *(a scouting report reads the opponent's play calling as Epic 78's `FPSTendencyRead`: from the calls the opponent model saw it make (`SetObservedCalls` with `UPSOpponentModel::GetCareerCells`, once there are `MinSamples`), else from its coordinators' schemes (Epic 89). Six focus areas (run, short-pass and deep-pass defense; blitz pickup, attacking base, running against prevent) are worth their categories' share of its calls against an even mix; the gameplan share's bonus, split between up to two focus areas, lifts their roles' weighted ratings in the game against that opponent only. Applied to the quick sim's copies of the players; a live game doesn't read the gameplan yet, and the quick sim has no per-call situations, so the boost lasts the game)*
- [x] Practice injury/fatigue risk coupling (Core 19's stamina and injury models) *(each player's freshness is the stamina ratio Core 19's `UPSInjuryModel` rolls practice injuries with (seeded per team and week, so a loaded save rolls the same): games and practice spend it, less for a higher Stamina rating; a week and rest restore it. The injured sit out simulated games until healed week by week; a tired player plays up to `FatiguePerformanceSwing` below his ratings; the season's end heals everyone. In-game injuries still aren't rolled (the quick sim has none))*
- [x] AI teams run the same system (no user-only advantages) *(there is no user path: every team without its own choice runs `RecommendAllocation` (rest when its healthy players are tired, the gameplan late in the season) and `RecommendFocus` (the most relevant focus area); a team that chooses the same gets exactly the same week. CPU teams scout the human from the calls the opponent model saw him make once the game hands them over (`SetObservedCalls`; nothing does yet outside the tests), the human scouts them from their schemes)*

### Epic 91: Morale, Chemistry & Locker Room

**Size/Mode:** M / code
**Goal:** Players respond to usage, winning, contracts, and each other — a human layer over the roster.
**Depends on:** Core 19, 87

- [x] Morale model: inputs (playing time, team success, contract status, role) → effects (performance variance, FA willingness) *(`UPSLockerRoom::EvaluateTeam` over `Data/morale.json` (`FPSMoraleTuning`), weekly: starter or backup by the roster's depth chart (starters per role from the default personnel packages), a backup rated above a starter, the team's win percentage, pay against his worth (his demand, Epic 87) and a deal's last year, the room's leaders; eased by `MoraleInertia`. Effects: ratings up to `PerformanceSwing` either way (`ApplyEffects`, on the simulation's copies), and his morale goes to free agency (`UPSFreeAgency::SetFreeAgentMorale`) where it prices his old team's offers. `UPSFranchiseFlow` evaluates every week and applies the effects in its quick sims; the state persists in `UPSFranchiseSaveGame::LockerRoom`)*
- [x] Chemistry: unit cohesion from lineup stability (OL continuity bonus) *(`RecordLineup` per game: each unit's starters (`Units` in the data: the offensive line, the secondary) and games together; cohesion = games / `FullCohesionGames`, worth up to `MaxBonus` on the unit's starters; a lineup change starts over)*
- [x] Event system: trade requests, holdouts, leadership emergence *(trade request after `TradeRequestWeeks` miserable weeks (withdrawn when he cheers up); a holdout at a new league year by an underpaid, unhappy star, who sits out the quick sims until paid (`HoldoutEnded`); a leader emerges on a winning team and lifts his teammates; `FPSLockerRoomEvent`s, kept by the flow)*
- [x] Transparency surface so effects are readable, never mysterious *(each player's morale keeps its factors with their effect and a reason (`FPSMoraleFactor`); `DescribePlayer` reads them out with his performance swing and flags, `DescribeTeamChemistry` each unit's games, cohesion and bonus. No locker-room screen yet (Track I))*

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

- [x] Storyline detection rules over the stat/event stream (win streaks, rookie surges, revenge games)
  *As built: `UPSLeagueNarrative::CloseWeek`, run by `UPSFranchiseFlow::AdvanceWeek` (`SetNarrative`),
  tells the week's stories from the authorities: win and losing streaks (`StreakMin`) and next week's
  revenge games from `UPSFranchiseSeason`'s results; a rookie (first season of a league with
  history) among a category's `RookieSurgeTopN`, records `UPSStatsEngine` announced broken
  (`OnRecordBroken`) and a close MVP race from the stats engine (Epic 92). Weights per kind in
  `Data/league_narrative.json`. Tested: `PlaySports.Narrative.Storylines`.*
- [x] Weekly league news digest generation (template-based; LLM-enhanced via 82 when bridged)
  *As built: each week's `FPSNewsDigest`, heaviest storyline first, then the week's honors, written
  from the string table's `Narrative.*` templates (`UPSLocalization`). With Epic 82's bridge online
  (`SetIntelligence`) the digest is offered to a model as a `NewsDigest` request routed as
  `narration` (`UPSGameIntelligenceSubsystem::OpenTextRequest`); its answer is kept as `ModelText`.
  Digests persist in `UPSFranchiseSaveGame::Narrative`. Tested: `PlaySports.Narrative.NewsDigest`,
  `.FranchiseSave`.*
- [x] Awards system: weekly honors, season awards with voting model
  *As built: offensive and defensive player of the week from the week's box scores
  (`OffenseScoring`, `DefenseScoring`); at season end (`UPSFranchiseFlow::EndSeason`, before the stats
  engine archives the season) MVP, offensive and defensive player of the year and rookie of the year
  by a seeded vote (`VoterCount` voters with `VoterNoise`, `BallotPoints`). The award record for Epic
  94: `GetAwards`, `GetAwardsForPlayer`, `CountAwards`, `OnAwardGiven`. Tested:
  `PlaySports.Narrative.Awards`.*
- [x] Broadcast integration: active storylines feed Track A chyrons and Track H commentary
  *As built: `FeedBroadcast` queues a game's `MaxBroadcastStorylines` heaviest storylines about its
  two teams as chyrons through `UPSOverlayBroadcastSubsystem::PushChyron` (no cast); `GetTalkingPoints`
  is the same list for Track H's commentary engine (Epic 96, not built yet) to read. Nothing calls
  `FeedBroadcast` at a played franchise game's kickoff yet: the franchise flow isn't carried into the
  match world at runtime. Tested: `PlaySports.Narrative.Broadcast`.*

### Epic 94: Multi-Season Aging, Retirement & Legacy

**Size/Mode:** M / code
**Goal:** The league regenerates across decades — aging curves, retirements, hall of fame, franchise history.
**Depends on:** 86, 92

- [x] Age-based progression/regression curves per role (extends Core 19's progression) *(`UPSPlayerAging` over `Data/legacy.json`'s `RoleCurves`: each role's own `FPSProgressionTuning` (a running back peaks at 23-26 and falls fast, a quarterback at 27-33) run through Core 19's `UPSPlayerProgression` at each season's end; a role without one ages on `Data/player_progression.json`. A player's snap share is his depth-chart place (1, 1/2, 1/3 ...), so buried backups grow at half. Everyone is a year older (`FPlayerAttributes::Age`, Epic 122; the contract manager's default age for a player without one). Generated leagues and draft classes (Epic 122) walk each player's ratings to his age on the same role curve (`UPSLeagueGenerator` through `UPSPlayerAging::GetCurve`), so a generated veteran has declined as he will go on to; tested by `PlaySports.Content.LeagueGenerator.RoleCareerCurves`)*
- [x] Retirement decisions (age, performance, injuries, morale) and roster churn balance *(from `MinAge` a veteran's chance grows each year, and rises when his rating is under `LowRating`, when he ends the season hurt (Epic 90's `UPSWeeklyPreparation`, read before it heals everyone) or unhappy (Epic 91's morale); at `ForcedAge` he goes. At most `MaxRetirementShare` of a roster retires a season, the likeliest first (the forced always), so the draft (Epic 86) refills it. A retiree leaves his roster, his contract is cut, and the league's history keeps his career. Rolls are seeded per player and season. Free agents don't age yet)*
- [x] Hall of fame induction from career stat thresholds + awards (92) *(`UPSLeagueHistory`: after each season, retirees who have waited `WaitSeasons` and played `MinSeasons` go in when their best career total reaches its threshold (`HallOfFame.Thresholds`, from Epic 92's career lines), the best first, `MaxInducteesPerSeason` a year. Awards join the score with Epic 93 (blocked on 82); no awards yet)*
- [ ] Franchise history archive: past seasons, champions, legends browsable in UI (Track I) *(code half done: `UPSLeagueHistory` archives every season's final standings, champion (the best record: no playoffs are played yet) and leaders, every retiree's career and the hall of fame in `UPSFranchiseSaveGame::History`; `GetFranchiseHistory`, `DescribeSeason` and `DescribeFranchise` give a team's seasons, titles and legends. The browsing screens are Track I's)*

### Epic 95: Owner Mode & League Economics

**Size/Mode:** S / code
**Goal:** A light economic layer above GM play — revenue, pricing, staff budgets, relocation pressure.
**Depends on:** 87, Core 20

- [x] Revenue model (attendance from team success/pricing, media share) *(`UPSOwnerEconomy` over `Data/owner_economics.json` (`FPSEconomyTuning`): each home game draws a crowd from the home team's record coming in, its fans' satisfaction and its ticket price (`SetTicketPrice`, `PredictAttendance` for a pricing preview), paying the gate and concessions; an equal media share at the season's end. `EndSeason` closes each team's books: revenue less payroll (the cap its contracts used, Epic 87) and budget is profit. `UPSFranchiseFlow` records every simulated game and the season's books; the economy persists in `UPSFranchiseSaveGame::Economy`)*
- [x] Budget allocation: scouting (86), training (90), staff (89) funded from revenue *(`SetBudget`: shares of revenue per `EPSBudgetDepartment`, within `MaxBudgetFraction`; `GetDepartmentFunding` (the share of last season's revenue) and `GetFundingIndex` (against the league's average) are what those systems read. Epics 86 and 90 don't exist yet, and Epic 89's carousel doesn't read its funding yet)*
- [x] Fan-satisfaction pressure with long-losing consequences (kept simple; this is a garnish epic) *(wins and losses, prices above the base and winning or losing seasons move a team's 0-1 satisfaction, which fills (or empties) its stadium; `RelocationLosingSeasons` losing seasons in a row with fans under `RelocationSatisfactionThreshold` put it under relocation pressure, flagged in its season report. No owner screen yet (Track I))*
