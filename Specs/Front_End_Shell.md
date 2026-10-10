# Specification: Front-End Shell (Epic 101)

The menu system that frames the game: main menu, mode select, pause menu, and the screen stack
later UI epics (102 play calling, 103 settings, 105 onboarding) push onto. This file is the
editor handoff: what exists in code, and what an editor session adds.

## 1. What runs today (code)

- **Screens are data.** `Data/ui_menus.json` (`FPSMenuCatalog`, loaded through
  `UPSDataIngestion`) lists every screen, its title/body, its options and whether Back may
  leave it. Add or reorder menu entries there, not in code.
- **One component runs every menu.** `UPSMenuComponent` sits on `APSPlayerController`. It
  owns a `UPSMenuStack` (push/pop/replace/clear with change events), applies the Back rules
  (the root screen can't be backed out of; Back on the pause screen resumes) and switches the
  player between UI input (cursor shown) and game input as the stack fills and empties.
- **Default look.** `UPSMenuScreenWidget` builds a plain layout in code when it has no designer
  tree: a dark backdrop, the title, an optional body line and one button per option, fading in
  over `TransitionSeconds`. Slate moves focus between buttons with the D-pad, stick, arrows or
  Tab; A/Enter presses; Back is whatever the input catalog binds to Cancel in the `Menu`
  context (Escape, B), plus the Pause keys on the pause screen.
- **Booting into the front end.** `APSMenuGameMode` (game mode alias `Menu`) shows the root
  screen on any map with no roster or pawn. Packaged builds boot into it through
  `LocalMapOptions=?game=Menu` (`Config/DefaultEngine.ini`). Choosing a mode travels to the
  default map with `?mode=PlayNow|Franchise|Practice` and no game option, so the map's own game
  mode runs the match; "Quit to Main Menu" travels back with `?game=Menu`.
- **Pausing.** The `Pause` action (P, Start) on the field calls `APlayerController::SetPause`
  and opens the pause screen: Resume, Settings, Quit to Main Menu.
- **Team select.** Play Now opens a `TeamSelect` screen: one option per team in
  `Data/sample_teams.json`, tinted with its primary color and labelled with overall, offense and
  defense ratings that `UPSUITeamCatalog` derives from the team's roster (mean of each player's
  average skill attribute). Choosing a team travels with `?mode=PlayNow?team=<TeamId>`.
- **Loading.** Every travel first shows the `Loading` screen with a tip for the mode
  (`Data/loading_tips.json`, `UPSLoadingTips`: shuffled, no repeats until all have shown), then
  travels on the next frame. In standalone and packaged games `UPSLoadingScreenSubsystem` puts
  the same tip on the engine loading screen (MoviePlayer) for at least `MinimumDisplaySeconds`
  while the map loads; the editor has no MoviePlayer, so PIE shows only the in-world screen.

- **Play calling (Epic 102).** Every scrimmage down opens the `PlayCall` screen for a human's
  side: formations, then that formation's plays; choosing one calls it. `UPSPlayCallSubsystem`
  owns the calls and the snap; see `Specs/Play_Call_Interface.md`.
- **Settings (Epic 103).** The `Settings` screen (main menu and pause menu) lists the
  categories of `Data/ui_settings.json`: Video, Audio, Gameplay, Controls, Accessibility.
  - A category opens the `SettingsCategory` screen. It shows each setting as "Label: value"
    with its description under it, then "Reset to defaults".
  - Choosing a setting steps it: a toggle flips, a choice moves to the next (wrapping), a slider
    goes up a step (wrapping to its minimum). The screen redraws with the focus kept.
  - `UPSSettingsSubsystem` (game instance) owns the values, saves them in the profile
    (`UPSProfileSaveGame::Settings`) as they change, and applies video (outside the editor)
    and the master volume to the engine.
  - `UPSSettingsComponent` on each player controller applies vibration, its strength, the
    stick dead zone and input buffering to that player.
  - Settings → Keys and buttons opens the `InputRemap` screen (Epic 103.4): each action a
    player may remap with its key on the active device. Choose one, then press the new key or
    button; Back cancels, and the screen says what happened ("Juke is now B", or why the key
    was refused). See `Specs/Input_Architecture.md` section 6.
  - The other audio volumes are stored for the sound classes, which don't exist yet. They read
    the values with `GetNumber` and hear changes on `OnSettingChanged`.
- **Accessibility (Epic 103.2/103.3).** `UPSUIAccessibilitySubsystem` (world) owns what the
  player reads and sees:
  - Captions. Whoever speaks -- the commentary booth (Epic 96), the PA, a referee -- publishes
    a `Speech` event on `UPSTelemetryBus` (speaker, text, channel, spoken length). With the
    Captions setting on it becomes a caption, "Speaker: text", up for its spoken length or,
    without one, its reading length (`Data/ui_accessibility.json`). `UPSUICaptionWidget` on
    the local player draws the newest lines at the bottom of the screen at the Caption size
    setting's font size.
  - UI narration hooks. Each screen calls `Narrate` with its title and text as it opens, and
    each option with its label and detail as it takes focus (`UPSMenuComponent::NarrateOption`).
    With Menu narration on, `OnNarration` carries the text to a voice. No text-to-speech voice
    is wired yet; a platform one subscribes there.
  - Color vision. `UPSUIColorLibrary` makes a color safe for the Color vision setting:
    `ResolveColor` shifts what a red, green or blue deficiency loses into channels the player
    still sees, and `ResolveMatchupColors` falls back to a secondary color when home and away
    still look alike. Team select's accents go through it today; Epic 37's team colors and
    the Track A overlays are to call it as they arrive.
  - Motion and flashes (Epic 103.5), all on `UPSUIAccessibilitySubsystem`:
    - With Reduced motion on, every blended change of view is a cut. `APSPlayerController`
      overrides `SetViewTarget` and passes the blend time through `GetTransitionSeconds`.
    - Menus don't fade (`UPSMenuComponent::GetTransitionSeconds`).
    - A camera's follow speed through `GetCameraFollowSpeed` is 0, so `FInterpTo` snaps and
      there is no lag to swing through.
    - Nothing shakes. Every gameplay shake starts through `StartCameraShake`, which plays it at
      the Camera shake setting's strength, or not at all.
    - `GetFlashScale` is the Flashes and pyro setting, for stadium pyro and screen flashes to
      scale by.
- **Localization (Epic 106).** All UI text comes through `UPSLocalization`, from two UE string
  tables: `Data/ui_text.csv` (the code's own text, written by hand) and
  `Data/ui_text_data.csv` (generated from the menu, settings and tip files by
  `tools/ui_text.py`). See `Data/README.md`, "UI text tables".
  - `UPSMenuComponent::GetPresentedScreen` is the boundary. It localizes each screen as it
    builds it, so widgets (`UPSMenuScreenWidget`, `UPSUICaptionWidget`, the loading screen)
    receive localized strings. Names are shown as they are (`Verbatim`).
  - The HUD's clock, phase and banners (`UPSScoreboardWidget`, `UPSPlayResultWidget`) take
    their patterns from the table.
  - Numbers, percentages and dates follow the culture. The `Units` setting (Gameplay) picks
    feet and pounds or centimeters and kilograms. Team select shows each roster's average
    height and weight in it.
  - Pseudo-localization: `ps.Loc.Pseudo 1` in the console accents, pads and brackets every
    string from the tables, and marks names with single angle quotes. Anything still plain
    on screen bypassed the tables. `PlaySports.Localization.PseudoLocalizedUI` checks every
    menu screen except play calling this way.

## 2. Not yet (other epics)

- `?mode=` and `?team=` are read by the match (`UPSMatchSetup`, owned by `APSGameMode`): the
  picked team is at home against the next team in league order, and both teams' coaching staffs
  take over at kickoff (Epic 89). A franchise game travels with explicit `?home=`, `?away=` and
  `?week=` from the schedule (`UPSFranchiseFlow::BuildUserMatch`, `UPSMatchSetup::ToOptions`).
  The field still spawns `RosterJsonPath`'s players for both sides, whatever the teams; the
  Franchise menu option still needs its hub (Track G) and Practice the gym map (Core 24).
- Settings a slider would suit are stepped by choosing them until a designer widget gives
  them a slider (left/right on a focused option).
- Narration has no voice: `OnNarration` fires, but nothing speaks it until a platform
  text-to-speech hook subscribes.
- Color vision covers team select only: the match's team colors (Epic 37) and the broadcast
  overlays (Track A) don't exist yet.
- Motion gaps (Epic 103.5):
  - The broadcast camera's follow (`APSBroadcastCamera::Tick`, `TrackingSpeed`) doesn't read
    `GetCameraFollowSpeed` yet. That file has open camera-lane changes (Epics 38 and 39).
  - Nothing in the game shakes the camera or fires pyro yet; the first shake or flash is to
    use `StartCameraShake`/`GetFlashScale`.
- Localization gaps (Epic 106):
  - The play-call screens' generated text is still built in English in
    `UPSPlayCallSubsystem`, so those screens are left out of the pseudo-localization check.
  - Key-refusal reasons from `UPSInputConfig` are developer English and shown verbatim.
  - There is no language setting yet: the culture is the platform's.
- Logos: `LogoPath` is empty for every team until an editor session imports logo textures; the
  abbreviation stands in.

## 3. Editor session (when one is available)

1. **Try it in PIE.** PIE starts the editor's map with its own game mode, so the front end
   doesn't show by default. Either set World Settings → GameMode Override to `PSMenuGameMode`
   on a menu map, or type `open GameMap?game=Menu` in the PIE console.
2. **Restyle.** Create `WBP_MenuScreen` as a subclass of `UPSMenuScreenWidget`, build the
   layout in the designer, and wire buttons to `ChooseOption(OptionId)` / `GoBack()`.
   Implement `OnScreenSet` to fill it from `GetScreen()`. Then set `ScreenWidgetClass` on the
   player controller's `MenuComp` (a Blueprint subclass of `APSPlayerController`). One class
   restyles every screen; the data stays the same.
3. **A menu map (optional).** A small `Content/Maps/MainMenu` with a camera and backdrop, set
   as `GameDefaultMap` with GameMode Override `PSMenuGameMode`, replaces booting the game map
   in menu mode.
