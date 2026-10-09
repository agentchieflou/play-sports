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

## 2. Not yet (other epics)

- `?mode=` is passed but nothing reads it yet: Franchise needs its hub (Track G), Practice the
  gym map (Core 24). Both start an ordinary game today.
- The Settings screen is a placeholder until Epic 103.
- Team select and loading screens with tips are Epic 101's remaining stories.

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
