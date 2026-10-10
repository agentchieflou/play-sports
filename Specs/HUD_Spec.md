# Specification: Core HUD and Scoreboard UMG

> **Superseded in code by Epic 33 (2026-10-10).** `APSHUD` now shows the broadcast package by
> default: `UPSOverlayScoreBugWidget` and `UPSOverlayChyronWidget`, built in code. Both draw
> `UPSOverlayBroadcastSubsystem`, which follows `UPSPlaySimulation`'s `GameState` bus events;
> the look is `Data/broadcast_overlay.json`. A Widget Blueprint is now only a reskin: assign it
> to `ScoreboardWidgetClass` / `ChyronWidgetClass` and draw from the `OnScoreBugChanged` /
> `OnChyronChanged` events. The binding below, which casts to `APSGameMode`, is retired
> (Architecture rule 5).

This document defines the requirements, design, and binding logic for creating the HUD user interface in the level editor Content Browser.

## HUD Class Setup

1. **Subclass APSHUD**: Create a Blueprint class inheriting from `APSHUD`, named `BP_PSHUD`.
2. **Assign Scoreboard Class**: Assign the Scoreboard Widget blueprint (defined below) to the `ScoreboardWidgetClass` property of `BP_PSHUD`.
3. **Register HUD in GameMode**:
   - Open `BP_PSGameMode` (or project settings -> Maps & Modes).
   - Under `HUD Class`, set it to use `BP_PSHUD`.

---

## Scoreboard Widget (`WBP_Scoreboard`)

Create a User Widget asset named `WBP_Scoreboard` in `/Game/UI/WBP_Scoreboard`.

### 1. UI Elements Layout
- **Game Clock**: Text block displaying `GameTimeSeconds` converted to a `MM:SS` format.
- **Score (Home vs Away)**: Two text blocks displaying current scores.
- **Down & Distance**: A text block formatted as `[Down] & [Distance]` (e.g., "1st & 10", "3rd & 4").
- **Play Phase Indicator**: A smaller debug text block displaying the current active play phase.

### 2. Binding Logic
The widget should retrieve data from the active `APSGameMode`:
- Get the active GameMode: `GetGameMode` -> Cast to `APSGameMode`.
- Bind **Home Score**: Read `APSGameMode::HomeScore`.
- Bind **Away Score**: Read `APSGameMode::AwayScore`.
- Bind **Down & Distance & Clock**:
  - Get `APSGameMode::PlaySimulation` -> `GetPlayState`.
  - Read `FPlayState::Down`, `FPlayState::Distance`, `FPlayState::GameTimeSeconds`, and `FPlayState::Phase`.

---

## Play Result Banner (`WBP_PlayResult`)

Create a User Widget asset named `WBP_PlayResult` in `/Game/UI/WBP_PlayResult`.

### 1. UI Elements Layout
- **Banner Text**: Large text block centered on screen (e.g., "TOUCHDOWN!", "+12 Yards", "Incomplete Pass").
- **Background Panel**: Semi-transparent backing panel with fade-in/fade-out animations.

### 2. Logic & Animation
- When the play transitions to the `Scoring` phase:
  - Get the `FPlayResult` struct from `APSGameMode::PlaySimulation`.
  - If `ResultType` is `Touchdown`: Set banner text to "TOUCHDOWN!" and play the scoring animation.
  - If `ResultType` is `Tackle`: Set banner text to `+ [YardsGained] Yards`.
  - If `ResultType` is `Incomplete`: Set banner text to "INCOMPLETE PASS".
- Display the banner on viewport, play fade-in/fade-out animation, and remove from parent after a delay.

---

## Telestrator Drawing Layer (`UPSTelestratorWidget`, Epic 44)

`APSHUD` makes it on every tier. It is idle, letting every click and finger through, until the
`Telestrator` action turns on analysis mode over a replay or the film view. All of it is code:
the toolbar, the input and the painting. A Widget Blueprint subclass (`TelestratorWidgetClass`
on the HUD) can restyle the toolbar; `OnAnalysisChanged` tells it when the layer comes and
goes.

### 1. What runs in code
- While analysis is on, the layer covers the screen and the player is in UI input mode, so
  nothing reaches the camera, the replay or the pawn. The touch layer stands down too.
- A toolbar sits at the top centre: Draw, Arrow, Circle, Player, Undo, Clear, Save and Done.
  The tool in hand is lit in `MarkColor`.
- A mouse drag or a finger draws with the current tool. A gamepad's left stick, WASD or the
  arrows move a cursor, and A or Space draws at it. X, Y, LB, RB and B (Tab, Backspace, Delete,
  Enter and Esc) are next tool, undo, clear, save and leave, read from the input catalog's
  `Telestrator` context.
- Marks and the stroke in progress are painted with anti-aliased Slate lines. Their sizes are
  shares of the screen's shorter side (`Data/telestrator.json`), so a phone and a monitor show
  the same drawing.

### 2. Editor / PIE check (not done yet)
1. In PIE, start a replay and press Y, D-pad Up or flick the right stick up:
   - the replay holds and the toolbar appears; freehand, arrow, circle and a player tap draw
     where the pointer goes, the tapped player lights up;
   - with a pad, the cursor moves with the left stick and A draws; X cycles the tools;
   - Done or B leaves, the marks go, a held replay plays on, and game input comes back.
2. On an iPhone (or the mobile preview at a phone's DPI): enter with the on-screen D-pad Up in a
   replay, draw with a finger, use the toolbar. Check the toolbar fits the screen's width in
   portrait and landscape, and that the on-screen controls are gone while drawing.
3. Look: line weight (`MarkWidth`, `MinStrokeWidth`), colors, the ring and cursor sizes.
   Restyle the toolbar in a Widget Blueprint if the code-built one reads poorly. Check that a
   saved still (RB / Enter, `Saved/Telestrator`) shows the drawing in its screenshot.

---

## AI Debug Overlay (`UPSAIDebugOverlayWidget`, Epic 85.2)

`APSHUD` makes it in development builds. It is idle until `ps.AI.DebugOverlay 1`, or
`UPSAIDecisionLog::SetOverlayEnabled`, turns the overlay on. Then:

### 1. What runs in code
- Every player on the field with an AI decision this play has a card over his head:
  `DescribeForOverlay` (player, assignment, action, target, then the reason), wrapped at
  `OverlayMaxLineChars`, on his side's color.
- It is laid out the way Epic 28 lays out badges (`PSOverlayBadgeLayout`): the same projection,
  the same scale by distance (the badge style's `ReferenceDistance`, `MinScale`, `MaxScale`), and
  the same nudging clear of each other. A card with no room is dimmed where it was, not hidden.
  Unlike the badges, it doesn't hide players during the play.
- Each player who goes at a place has a line to it. While the layer is up, the decision log's
  debug text in the world stands down.
- The look is the `Overlay*` fields of `Data/ai_debug.json`. Cards are sized in Slate units
  times the display's DPI scale, so a phone shows them at the same size to the eye.

### 2. Editor / PIE check (not done yet)
1. In PIE run `ps.AI.DebugOverlay 1` and play a down. Check that each player's card follows him
   and that the text is readable at the broadcast camera's distances. Check that cards nudge
   apart in the line of scrimmage's crowd, and that the dimmed ones are still useful.
2. Tune `OverlayFontSize`, `OverlayMaxLineChars`, the colors and `OverlayNudgeStep` until 22
   cards read at once, or decide to show fewer (a filter by side or player is not built).
3. On an iPhone, check the cards' size to the eye matches a monitor's.
