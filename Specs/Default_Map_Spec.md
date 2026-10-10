# Specification: Default Map

`Config/DefaultEngine.ini` names `/Game/Maps/GameMap` as both `GameDefaultMap` and `EditorStartupMap`.

## How the map is made (Epic 146.2)

The map is not made by hand. The content pipeline's `game_map` step
(`tools/content_pipeline/steps/game_map.py`) builds it headlessly in the editor on the CI runner.
The Content workflow (`.github/workflows/content.yml`) runs that step, and the result is committed
through Git LFS as `Content/Maps/GameMap.umap`. `Specs/ADR_Content_Pipeline.md` describes the loop.

The level holds:

- **Daylight that needs no lighting build:** a movable directional light (the sun, used as the
  atmosphere's sun), a movable sky light, a sky atmosphere and exponential height fog.
- **A player start** on the near goal line, facing upfield (+X).
- **The match as its GameMode Override** (`APSGameMode`). The game boots into the map with
  `?game=Menu` (`LocalMapOptions`), which runs the front end instead. Play Now travels back to it
  without a game option, so the override starts the match.
- **The field grid** (`APSFieldGrid`) at the origin (146.3). At BeginPlay it spawns the field's
  trigger volumes and its surface (`APSFieldSurface`: the ground, grass, end zones, lines and hash
  marks), both built from data (`Specs/Field_Geometry_Spec.md`, `Specs/Field_Markings_Spec.md`).
  So the front end has the field behind it, and the match uses this grid instead of spawning one.

The level holds no field geometry of its own, so changing the field's dimensions or look never
means regenerating the map.

## Changing it

Change the step (or the data it reads), then let the Content workflow regenerate the map and
commit its `generated-content` artifact. Never save `GameMap` from the editor. The drift check
treats that as drift. The ADR's "Hand edits" section says how hand-made work joins a generated
level.

## Verification

- `PlaySports.Content.GameMap` (headless, in CI) loads the map. It checks that the map is a real
  level, not an LFS pointer; that the match is its game mode; and that it has the movable lights
  and the player start.
- The Content workflow's check mode re-runs the step on every push to `main` that touches the
  pipeline or `Content/`, and fails if the map no longer matches its step.
- Still a human check: how it looks in PIE or a packaged build.
