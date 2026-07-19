# Track I — UX, UI & Input (Epics 101–106)

Everything between the player and the field: menus, play calling, accessibility, controller
feel, and onboarding. UMG-heavy but largely code-driveable. Sizing/mode legend: see
`ROADMAP.md`.

**Reality note (2026-07-19 review):** `APSHUD` exists as a bare widget host and Epic 5's
scoreboard stories are still open (UMG assets are editor-mode work per the Specs/ pattern);
Epic 101's shell absorbs and replaces it. Input mapping exists on `APSPlayerPawn` (Epic 3);
Epic 104 extends via Enhanced Input contexts, not new pawn code. UI reads game state from C1
bus subscriptions and the C2 single authority — never direct sim/GameMode reads.

### Epic 101: Front-End Shell

**Size/Mode:** L / code
**Goal:** A real menu system frames the game — main menu, mode select, team select, loading flow.
**Depends on:** Core 5

- [ ] Screen-stack framework (navigation, back-handling, transitions) all other UI epics build on
- [ ] Main menu + mode select (Play Now, Franchise, Practice/Gym)
- [ ] Team select with identity display (colors, logos, ratings from team data)
- [ ] Loading/transition screens with tips pipeline
- [ ] Pause menu in-game with settings access

### Epic 102: Play-Call Interface

**Size/Mode:** L / code
**Goal:** Browsing and calling plays is fast, informative, and readable — the most-used screen in the game.
**Depends on:** Core 16, 101

- [ ] Play-call screen: formation → concept browsing with play-art previews (reuses Track A art pipeline, 35)
- [ ] Suggestion surfaces: situation-aware recommendations (Core 18) with reasoning shown
- [ ] Recent/favorite plays and tendency self-awareness readout (what you've been calling — ties to 78)
- [ ] Defensive call flow (front + coverage + adjustments) with the same speed bar
- [ ] Time pressure: play-clock integration, quick-call fallback

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
**Depends on:** Core 3, Core 6

- [ ] Enhanced Input action mapping across contexts (pre-snap, ball-carrier, passing, defense, menus)
- [ ] Ball-carrier move set: juke, spin, truck, stiff-arm, hurdle, slide — attribute-gated, physics-coupled (Core 8)
- [ ] Passing input model: placement modifiers, touch/bullet, pump fake
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
