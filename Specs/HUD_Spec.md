# Specification: Core HUD and Scoreboard UMG

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
