# Track I — UX, UI & Input (Epics 101–106)

Everything between the player and the field: menus, play calling, accessibility, controller
feel, and onboarding. UMG-heavy but largely code-driveable. Sizing/mode legend: see
`ROADMAP.md`.

**Reality note (2026-07-19 review):** `APSHUD` exists as a bare widget host and Epic 5's
scoreboard stories are still open (UMG assets are editor-mode work per the Specs/ pattern);
Epic 101's shell absorbs and replaces it. (2026-10-09: Epic 101's screen stack, main menu,
mode select and pause menu landed as `UPSMenuComponent` on `APSPlayerController`, driven by
`Data/ui_menus.json`, with a code-built default layout until Widget Blueprints exist; team
select reads `Data/sample_teams.json` and rates teams from their rosters, and loading screens
draw tips from `Data/loading_tips.json` — see `Specs/Front_End_Shell.md`. 2026-10-10: Epic 102
part 1 made calls real — `UPSPlayCallSubsystem` owns each side's call, the CPU calls sides no
human controls, a human calls from formation → play screens with text play descriptions, the
offense snaps on the human's hike or the CPU's delay, and the snap distributes both calls through
`UPSPlayOrchestrator`; see `Specs/Play_Call_Interface.md`. Part 2 added the coaching AI's
ranked suggestion with its reasons, recent plays and a tendency readout, and the live play clock
with a quick-call; part 3 added favourite plays (kept in the profile save) and pre-snap
defensive adjustments. Only 102.1 stays unticked: its play art waits on Epic 35, which depends on
Track A's overlays (27, 31).) Input bring-up (Enhanced Input, the player
controller, gamepad support) lives in Track M (`roadmap/controller-connectivity.md`); Epic
104 builds feel on top of that substrate per the `Specs/Input_Architecture.md` contract, not
new pawn code. UI reads game state from C1 bus subscriptions and the C2 single authority —
never direct sim/GameMode reads.

### Epic 101: Front-End Shell

**Size/Mode:** L / code
**Goal:** A real menu system frames the game — main menu, mode select, team select, loading flow.
**Depends on:** Core 5

- [x] Screen-stack framework (navigation, back-handling, transitions) all other UI epics build on
- [x] Main menu + mode select (Play Now, Franchise, Practice/Gym)
- [x] Team select with identity display (colors, logos, ratings from team data)
- [x] Loading/transition screens with tips pipeline
- [x] Pause menu in-game with settings access

### Epic 102: Play-Call Interface

**Size/Mode:** L / code
**Goal:** Browsing and calling plays is fast, informative, and readable — the most-used screen in the game.
**Depends on:** Core 16, 101

- [ ] Play-call screen: formation → concept browsing with play-art previews (reuses Track A art pipeline, 35)
- [x] Suggestion surfaces: situation-aware recommendations (Core 18) with reasoning shown
- [x] Recent/favorite plays and tendency self-awareness readout (what you've been calling — ties to 78)
- [x] Defensive call flow (front + coverage + adjustments) with the same speed bar
- [x] Time pressure: play-clock integration, quick-call fallback

### Epic 103: Settings & Accessibility

**Size/Mode:** M / code
**Goal:** The game is configurable and playable by more people — visual, audio, input, and difficulty accessibility.
**Depends on:** 101

- [ ] Settings framework (persisted user config; video/audio/gameplay/controls categories)
- [ ] Colorblind-safe modes flowing through team-color resolution (37) and overlay palettes
- [ ] Subtitle/caption system for commentary (96) and UI narration hooks
- [ ] Input remapping surface (consumes 104's action mapping)
- [ ] Motion/flash reduction options (camera shake, pyro intensity)

### Epic 104: Controller Feel & Input Depth

**Size/Mode:** M / code
**Goal:** Moment-to-moment input feels responsive and expressive — the skill-move vocabulary and its buffering.
**Depends on:** Core 3, Core 6, 126, 127

- [x] Extend the Track M Enhanced Input context stack with gameplay-depth contexts (pre-snap, ball-carrier, passing, defense, menus) — consumes 126's controller/`UPSInputConfig`, does not create them *(`PreSnap`, `Passing`, `BallCarrier` and `Defense` are catalog contexts; `UPSPlayContextComponent` keeps one on the controller's stack at a time, from the snap/whistle on the bus and the controlled pawn's possession. Menus already had theirs, Epic 101. See `Specs/Input_Architecture.md` section 3)*
- [x] Ball-carrier move set: juke, spin, truck, stiff-arm, hurdle, slide — attribute-gated, physics-coupled (Core 8) *(`UPSCarrierMoveComponent` on every pawn, `Data/carrier_moves.json`. Each move has a rating gate and a stamina cost. It changes the carrier's velocity as it starts, and for its window scales the tackle chance by `TackleChanceScale` in proportion to the rating. A slide gives the carrier up: down at the next contact, with no hit and no fumble. The tackle formula moved out of `ResolveTackle` into `PSBallResolutionHelpers::ComputeTackleChance` (`FTackleTuningRow`, same numbers). Buttons: `UPSCarrierInputComponent`, BallCarrier context)*
- [x] Passing input model: placement modifiers, touch/bullet, pump fake *(`UPSPassingComponent`: five receiver-slot buttons (X, Y, B, RB, A / 1-5) left to right across the field. A tap throws touch (`TouchSpeedScale` of the arm), a hold throws a bullet, and the Move stick at release places the ball deeper, shorter or to either side. LB/Q pump-fakes, which freezes coverage for `PumpFakeFreezeSeconds × (1 − Awareness/100)`. Tuning: `Data/passing_input.json`)*
- [ ] Input buffering/queuing tuned against animation commitment windows (Track D)
- [ ] Kick meter and defensive interaction inputs (jump-snap, strip attempts)

### Epic 105: Onboarding & Practice Modes

**Size/Mode:** M / code
**Goal:** New players learn football and the controls inside purpose-built practice spaces.
**Depends on:** 101, 104, Core 24's gym map

- [ ] Free-practice mode: any play vs. configurable defense, no clock (extends the functional gym map)
- [ ] Tutorial sequence: movement → passing → defense → play calling, with completion tracking
- [ ] Skill drills with scoring (route timing, pocket navigation, open-field tackling)
- [ ] Contextual hint system for first-time situations (first 4th down, first two-minute drill)

### Epic 106: Localization & Text Infrastructure

**Size/Mode:** S / code
**Goal:** All user-facing text flows through UE's localization system from day one of UI work.
**Depends on:** 101

- [ ] `FText`/string-table discipline pass across existing UI + gate for new work (review-verify skill addition)
- [ ] Locale-aware formatting (numbers, dates, units — metric/imperial display toggle for `WeightKg`/`HeightCm`)
- [ ] Pseudo-localization test pass proving nothing is hardcoded
