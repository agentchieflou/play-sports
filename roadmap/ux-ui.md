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

- [x] Settings framework (persisted user config; video/audio/gameplay/controls categories) *(`Data/ui_settings.json` declares Video, Audio, Gameplay, Controls and Accessibility settings (toggles, choices, sliders). `UPSSettingsSubsystem` (game instance) keeps the values, saves them in `UPSProfileSaveGame::Settings` as they change, and applies video (outside the editor) and the master volume to the engine. `UPSSettingsComponent` on the player controller applies vibration, its strength, the stick dead zone (a scale on the tuned value) and input buffering. The `Settings` menu lists the categories; a category lists its settings with their values, and choosing one steps it. Other volumes wait on sound classes)*
- [ ] Colorblind-safe modes flowing through team-color resolution (37) and overlay palettes *(Partial; waits on 37 and the Track A overlays. The Color vision setting (Standard, protanopia, deuteranopia, tritanopia) and `UPSUIColorLibrary` exist: `ResolveColor` daltonizes a color for the mode, and `ResolveMatchupColors` falls back to a secondary color when home and away still look alike (`MinMatchupColorDistance`, `Data/ui_accessibility.json`). Team select's accents use it. Epic 37's team-color resolution and the overlay palettes don't exist yet; they are to call the library when they land)*
- [x] Subtitle/caption system for commentary (96) and UI narration hooks *(A new `Speech` event on `UPSTelemetryBus` (speaker, text, channel, spoken length) is what the booth (96), the PA or a referee publishes. `UPSUIAccessibilitySubsystem` (world) turns it into a caption while the Captions setting is on, up for its spoken length or its reading length (`Data/ui_accessibility.json`), and `UPSUICaptionWidget` draws the newest lines at the Caption size setting's size. Menus narrate each screen as it opens and each option as it takes focus through `Narrate`, which with Menu narration on fires `OnNarration` for a text-to-speech voice; none is wired yet)*
- [x] Input remapping surface (consumes 104's action mapping) *(A remap replaces an action's keys for one kind of device over the catalog (`UPSInputConfig::ApplyRemaps`), checked by the catalog's own validation first, so a key can't mean two things in one context. Menu actions are fixed (`"bRemappable": false` on `Menu`). Remaps are saved in the profile and applied by `UPSSettingsComponent`, and glyphs follow with no glyph edit. Settings → Keys and buttons (`InputRemap` screen) lists the actions with their current keys; choose one, press the new key, Back cancels)*
- [ ] Motion/flash reduction options (camera shake, pyro intensity)

### Epic 104: Controller Feel & Input Depth

**Size/Mode:** M / code
**Goal:** Moment-to-moment input feels responsive and expressive — the skill-move vocabulary and its buffering.
**Depends on:** Core 3, Core 6, 126, 127

- [x] Extend the Track M Enhanced Input context stack with gameplay-depth contexts (pre-snap, ball-carrier, passing, defense, menus) — consumes 126's controller/`UPSInputConfig`, does not create them *(`PreSnap`, `Passing`, `BallCarrier` and `Defense` are catalog contexts; `UPSPlayContextComponent` keeps one on the controller's stack at a time, from the snap/whistle on the bus and the controlled pawn's possession. Menus already had theirs, Epic 101. See `Specs/Input_Architecture.md` section 3)*
- [x] Ball-carrier move set: juke, spin, truck, stiff-arm, hurdle, slide — attribute-gated, physics-coupled (Core 8) *(`UPSCarrierMoveComponent` on every pawn, `Data/carrier_moves.json`. Each move has a rating gate and a stamina cost. It changes the carrier's velocity as it starts, and for its window scales the tackle chance by `TackleChanceScale` in proportion to the rating. A slide gives the carrier up: down at the next contact, with no hit and no fumble. The tackle formula moved out of `ResolveTackle` into `PSBallResolutionHelpers::ComputeTackleChance` (`FTackleTuningRow`, same numbers). Buttons: `UPSCarrierInputComponent`, BallCarrier context)*
- [x] Passing input model: placement modifiers, touch/bullet, pump fake *(`UPSPassingComponent`: five receiver-slot buttons (X, Y, B, RB, A / 1-5) left to right across the field. A tap throws touch (`TouchSpeedScale` of the arm), a hold throws a bullet, and the Move stick at release places the ball deeper, shorter or to either side. LB/Q pump-fakes, which freezes coverage for `PumpFakeFreezeSeconds × (1 − Awareness/100)`. Tuning: `Data/passing_input.json`)*
- [x] Input buffering/queuing tuned against animation commitment windows (Track D) *(`UPSInputBufferComponent` on the controller sits between the catalog actions and their consumers. A buffered press (`Data/input_buffer.json`) waits up to its `BufferSeconds` while its target is busy: a carrier move during another move's `CommitSeconds` (new in `Data/carrier_moves.json`, standing in for Track D's animation windows) or its own cooldown, a pass button before the passer holds the ball. A key that meant nothing when it went down counts in a depth context that comes on within its window, so a slot pressed the frame before Passing throws; a hike key is never replayed. Newest press wins (`MaxQueued` 1), and holds are timed from the physical press. Passing and the carrier's buttons now listen to the buffer. See `Specs/Input_Architecture.md` section 6)*
- [x] Kick meter and defensive interaction inputs (jump-snap, strip attempts) *(`UPSKickMeterComponent`: a kick phase on the bus (`APSGameMode` now names Kickoff, Punt and FieldGoal) lines the human up when he controls the kicking side, the new `Kicking` context comes on, and `UPSPlaySimulation` waits up to `LineUpSeconds` for him. Hold Kick to fill the power, release to lock it, press to stop the needle; the `Kick` bus event's roll replaces the CPU kicker's random number for the field goal, punt and kickoff (`Data/kick_meter.json`). On defense `UPSDefenseInputComponent` times `JumpSnap` (the new `DefensePreSnap` context, Space/LT) at the snap: a clean jump bursts the defender off the line, an early one is flagged offside through a `JumpSnap` bus event. `Strip` (Defense, R/RB) opens a strip attempt on `UPSDefenderTechniqueComponent` (every pawn) that trades tackle odds for fumble odds in `ResolveTackle`, whose fumble formula moved to `PSBallResolutionHelpers::ComputeFumbleChance` (same numbers). Tuning: `Data/defensive_techniques.json`)*

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

- [ ] `FText`/string-table discipline pass across existing UI + gate for new work (review-verify skill addition) *(Partial; the play-call screens are left. `UPSLocalization` reads two UE string tables, both registered with `LOCTABLE_FROMFILE_GAME` so UE's gather collects them. `Data/ui_text.csv` holds the code's own text and is written by hand. `Data/ui_text_data.csv` is generated from `ui_menus.json`, `ui_settings.json` and `loading_tips.json` by `tools/ui_text.py`. The menus (localized as `GetPresentedScreen` builds them), setting values, captions, narration, the loading screen and the HUD's clock and banners now read them, and names go through `Verbatim`. Gate: `validate_data.py` fails on a stale generated table, on a key the code names that is missing, or on FText built from a raw string in `PSUI*`, `PSMenu*`, `PSHUD*`, `PSLoading*` and `PSSettings*` files. review-verify checklist item 7 covers every other user-facing file. Left: the play-call screens' text, which `UPSPlayCallSubsystem` still builds in English (it waits for Epic 75's open change to that file), and the Track A score bug's text)*
- [x] Locale-aware formatting (numbers, dates, units — metric/imperial display toggle for `WeightKg`/`HeightCm`) *(`UPSLocalization::FormatNumber`/`FormatPercent`/`FormatDate` follow the culture, so German shows 1.234.567,5. `FormatWeight`/`FormatHeight` show `WeightKg`/`HeightCm` in the new `Units` setting's system (Gameplay: feet and pounds, or centimeters and kilograms), with unit patterns in `Data/ui_text.csv`. Team select shows each roster's average height and weight in it. Percent settings use the culture's percentage)*
- [ ] Pseudo-localization test pass proving nothing is hardcoded *(Partial; the play-call screens are left, as in 106.1. `ps.Loc.Pseudo 1` accents, pads and brackets every string from the tables, and `Verbatim` marks names with single angle quotes. `PlaySports.Localization.PseudoLocalizedUI` checks with `IsFullyLocalized` that no plain letter is left on every menu screen, each settings category, the remap prompt, narration, setting values, captions, HUD banners and a loading tip. Play-call screens are excluded until their text moves to the tables)*
