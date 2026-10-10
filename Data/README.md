# Data/ Content Contract

All game content (players, teams, playbooks, league config) is authored as JSON here and
loaded through `UPSDataIngestion` / `UPSPlaybookIngestion` (`Source/PlaySports/Public/PSDataIngestion.h`,
`PSPlaybookIngestion.h`) — never through a new ad-hoc parser (Architecture rule 4).

`tools/content.py` is the one command for content (Epic 125):

```
python tools/content.py validate   # every contract below and every reference between files
python tools/content.py report     # rating distributions, name duplication, roster shape
python tools/content.py import     # validate, then the commandlet below (needs UE_ROOT)
python tools/content.py            # validate, then report
```

`validate` is `tools/validate_data.py`. CI runs it on every PR, so generated content is gated
like hand-written content. The per-file contracts for teams, the league config, the playbook
and player rating ranges live in `tools/content_contracts.py`, which also
checks the references between files:

- the league config's `TeamsDataTablePath` names a teams file with at least `NumPlayoffTeams`
  teams;
- every team's `RosterDataTablePath` names a roster, and every file in `rosters/` belongs to a
  team;
- `PlayerId`s are unique across the league;
- every play's `RouteId` is in the route library.

`report` prints its warnings in the CI log; they do not fail the build. `--strict` makes them fail.

The import is the content commandlet (Epic 21):

```
UnrealEditor-Cmd.exe play-sports.uproject -run=PSContentReimport
```

It loads the league config, follows it to the teams and each team's roster, and loads the route
library and the playbook, all through the game's own loaders. It logs actionable
`<file> - <problem>` errors, including a `PlayerId` on two teams and a play's route missing from
the library. The automation test `PlaySports.Content.ImportShippedContent` runs the same import on
every CI build.

## Files

| File | Schema struct | Loader |
| --- | --- | --- |
| `sample_players.json` | `FPlayerAttributes` (array field `Players`) | `UPSDataIngestion::LoadPlayerAttributesFromJson` |
| `personnel_packages.json` | `FPSPersonnelCatalog` (single object: `DefaultOffensePackage`, `DefaultDefensePackage`, `FatigueSubstitutionThreshold`, `Packages`) | `UPSDataIngestion::LoadPersonnelCatalogFromJson`, via `UPSPersonnelManager` |
| `rosters/team_*.json` | `FPlayerAttributes` (array field `Players`) | same, one file per non-Falcons team |
| `sample_teams.json` | `FPSTeamInfo` (array field `Teams`) | `UPSDataIngestion::LoadTeamsFromJson` |
| `sample_league_config.json` | `FPSLeagueConfig` (single object) | `UPSDataIngestion::LoadLeagueConfigFromJson` |
| `sample_playbook.json` | `FPSPlayDefinition` (array field `Plays`) | `UPSPlaybookIngestion::LoadPlaysFromJson` |
| `sample_routes.json` | `FPSRoute` (array field `Routes`) | `UPSPlaybookIngestion::LoadRoutesFromJson` |
| `input_actions.json` | `FPSInputCatalog` (single object: `Contexts`, `Actions`) | `UPSDataIngestion::LoadInputCatalogFromJson`, via `UPSInputConfig::LoadFromJson` |
| `input_tuning.json` | `FInputTuningRow` (single object) | `UPSDataIngestion::LoadInputTuningFromJson`, via `UPSInputConfig::LoadDefaults` |
| `loading_tips.json` | `FPSLoadingTipCatalog` (single object: `MinimumDisplaySeconds`, `Tips`) | `UPSDataIngestion::LoadLoadingTipsFromJson`, via `UPSLoadingTips` |
| `ui_menus.json` | `FPSMenuCatalog` (single object: `RootScreen`, `PauseScreen`, `TransitionSeconds`, `Screens`) | `UPSDataIngestion::LoadMenuCatalogFromJson`, via `UPSMenuComponent` |
| `force_feedback.json` | `FPSForceFeedbackTuning` (single object: `MasterIntensity`, `Cues`) | `UPSDataIngestion::LoadForceFeedbackTuningFromJson`, via `UPSForceFeedbackComponent` |
| `play_call.json` | `FPlayCallTuningRow` (single object) | `UPSDataIngestion::LoadPlayCallTuningFromJson`, via `UPSPlayCallSubsystem` |
| `defensive_adjustments.json` | `FPSDefensiveAdjustmentCatalog` (single object: `Adjustments`) | `UPSDataIngestion::LoadDefensiveAdjustmentsFromJson`, via `UPSPlayCallSubsystem` |
| `skill_ai_tuning.json` | `FSkillPlayerAITuningRow` (single object) | `UPSDataIngestion::LoadSkillPlayerAITuningFromJson`, via `UPSSkillPlayerAIComponent` |
| `defense_ai_tuning.json` | `FDefenderAITuningRow` (single object) | `UPSDataIngestion::LoadDefenderAITuningFromJson`, via `UPSDefenderAIComponent` |
| `passing_input.json` | `FPassingInputTuningRow` (single object) | `UPSDataIngestion::LoadPassingInputTuningFromJson`, via `UPSPassingComponent` |
| `platform_tiers.json` | `FPSPlatformTierCatalog` (single object: `DefaultTier`, `Platforms`, `Tiers`) | `UPSDataIngestion::LoadPlatformTiersFromJson`, via `PSPlatformTiers::GetActiveTier` |
| `carrier_moves.json` | `FPSCarrierMoveCatalog` (single object: `Moves`) | `UPSDataIngestion::LoadCarrierMovesFromJson`, via `UPSCarrierMoveComponent` |
| `pocket_tuning.json` | `FPocketTuningRow` (single object) | `UPSDataIngestion::LoadPocketTuningFromJson`, via `UPSPocketComponent` and `UPSPlayOrchestrator` |
| `route_running.json` | `FRouteRunningTuningRow` (single object) | `UPSDataIngestion::LoadRouteRunningTuningFromJson`, via `UPSRouteRunnerComponent` |
| `blown_coverage.json` | `FBlownCoverageTuningRow` (single object) | `UPSDataIngestion::LoadBlownCoverageTuningFromJson`, via `UPSBlownCoverageSubsystem` |
| `presnap_tuning.json` | `FPreSnapTuningRow` (single object) | `UPSDataIngestion::LoadPreSnapTuningFromJson`, via `UPSPreSnapSubsystem` |
| `input_buffer.json` | `FInputBufferTuningRow` (single object: `MaxQueued`, `Actions`) | `UPSDataIngestion::LoadInputBufferTuningFromJson`, via `UPSInputBufferComponent` |
| `defensive_techniques.json` | `FDefensiveTechniqueTuningRow` (single object) | `UPSDataIngestion::LoadDefensiveTechniquesFromJson`, via `UPSDefenderTechniqueComponent` |
| `kick_meter.json` | `FKickMeterTuningRow` (single object) | `UPSDataIngestion::LoadKickMeterTuningFromJson`, via `UPSKickMeterComponent` |
| `ui_settings.json` | `FPSSettingsCatalog` (single object: `Categories`, `Settings`) | `UPSDataIngestion::LoadSettingsCatalogFromJson`, via `UPSSettingsSubsystem` |
| `ui_accessibility.json` | `FPSUIAccessibilityTuning` (single object) | `UPSDataIngestion::LoadUIAccessibilityTuningFromJson`, via `UPSUIAccessibilitySubsystem` |
| `ui_text.csv` | UE string table `PSUI` (CSV: `Key`, `SourceString`, `Comment`) | `UPSLocalization::RegisterStringTables` (`LOCTABLE_FROMFILE_GAME`) |
| `ui_text_data.csv` | UE string table `PSUIData`, **generated** by `tools/ui_text.py` | same |
| `pass_rush_moves.json` | `FPSRushMoveCatalog` (single object: `RushMoves` plus the rush plan's tuning) | `UPSDataIngestion::LoadRushMovesFromJson`, via `UPSRushMoveComponent` |
| `session_telemetry.json` | `FPSSessionTelemetryTuning` (single object) | `UPSDataIngestion::LoadSessionTelemetryTuningFromJson`, via `UPSSessionTelemetrySubsystem` |
| `run_fits.json` | `FPSRunFitCatalog` (single object: `Fronts`, `DefaultFront` plus the fit tuning) | `UPSDataIngestion::LoadRunFitsFromJson`, via `UPSDefenderGapSubsystem` |
| `camera_all22.json` | `FPSAll22CameraTuning` (single object: `All22Rigs`, framing tuning) | `UPSDataIngestion::LoadAll22CameraTuningFromJson`, via `UPSCameraAll22Component` |
| `camera_director.json` | `FPSCameraDirectorTuning` (single object: `Shots`, `CutRules`, `Interest`, constraints) | `UPSDataIngestion::LoadCameraDirectorTuningFromJson`, via `UPSCameraDirectorComponent` |
| `camera_skycam.json` | `FPSSkycamTuning` (single object) | `UPSDataIngestion::LoadSkycamTuningFromJson`, via `UPSCameraSkycamComponent` |
| `input_glyphs.json` | `FPSInputGlyphCatalog` (single object: `GlyphSets`) | `UPSDataIngestion::LoadInputGlyphsFromJson`, via `UPSInputGlyphs` (owned by `UPSInputConfig`) |
| `touch_controls.json` | `FPSTouchLayout` (single object: `SafeZone`, `TouchControls`, `TouchContexts`, ...) | `UPSDataIngestion::LoadTouchLayoutFromJson`, via `UPSTouchInputComponent` |
| `situational_tuning.json` | `FPSSituationalTuning` (single object: `Tempos`, `SituationTempos`, `CategoryWeights`, ...) | `UPSDataIngestion::LoadSituationalTuningFromJson`, via `UPSSituationAI` (owned by `UPSCoachingAI`) |
| `special_teams.json` | `FPSSpecialTeamsTuning` (single object: kickoff, punt, field-goal, block, return, fake and AI fields) | `UPSDataIngestion::LoadSpecialTeamsTuningFromJson`, via `UPSSpecialTeamsModel` (owned by `UPSPlaySimulation`) and `UPSSpecialTeamsAI` (owned by `UPSCoachingAI`) |
| `coaching_staffs.json` | `FPSCoachingLeague` (single object: `Schemes`, `Coaches`, `Staffs`, `Tuning`) | `UPSDataIngestion::LoadCoachingLeagueFromJson`, via `UPSStaffManager` |
| `telemetry_sampling.json` | `FPSTelemetrySamplingTuning` (single object) | `UPSDataIngestion::LoadTelemetrySamplingTuningFromJson`, via `UPSTelemetrySamplingSubsystem` |
| `overlay_reticle.json` | `FPSOverlayReticleStyle` (single object: colors, mesh, `ReticleStates`) | `UPSDataIngestion::LoadOverlayReticleStyleFromJson`, via `UPSOverlayReticleComponent` |
| `control_handoff.json` | `FControlHandoffTuningRow` (single object) | `UPSDataIngestion::LoadControlHandoffTuningFromJson`, via `UPSControlHandoffComponent` |
| `broadcast_overlay.json` | `FPSBroadcastOverlayTheme` (single object: colors, sizes, thresholds, `ChyronKinds`) | `UPSDataIngestion::LoadBroadcastOverlayThemeFromJson`, via `UPSOverlayBroadcastSubsystem` |
| `ball_flight_overlay.json` | `FPSBallFlightStyle` (single object: colors, meshes, arc and ring sizes, goal posts, readout labels) | `UPSDataIngestion::LoadBallFlightStyleFromJson`, via `UPSOverlayBallFlightSubsystem` |
| `overlay_badges.json` | `FPSOverlayBadgeStyle` (single object: `Groups`, `RoleLabels`, sizes and layout rules) | `UPSDataIngestion::LoadOverlayBadgeStyleFromJson`, via `UPSOverlayBadgeComponent` |
| `versus_rules.json` | `FPSVersusRules` (single object) | `UPSDataIngestion::LoadVersusRulesFromJson`, via `UPSVersusSubsystem` |

## Player schema (`FPlayerAttributes`)

Field names must match exactly (case-sensitive): `PlayerId`, `DisplayName`, `Role`, `WeightKg`,
`HeightCm`, `Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness`, `Stamina`. Ratings run
0-100; `WeightKg` and `HeightCm` are above 0.

`Role` must be one of the `EPlayerRole` enum names: `Quarterback`, `RunningBack`,
`WideReceiver`, `TightEnd`, `OffensiveLineman`, `DefensiveLineman`, `Linebacker`,
`DefensiveBack`.

Validate before wiring a new roster into the game with:

```cpp
TArray<FString> Errors;
UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
if (!Ingestion->ValidatePlayersJson(JsonPath, Errors))
{
    // Errors[i] is "Row N: <what's wrong>" -- points straight at the bad row.
}
```

`sample_players.json` is the in-game roster (`APSGameMode::RosterJsonPath`): 22 starters followed
by 9 backups (`QB_002`, `RB_002`, `WR_004`, `TE_002`, `DL_005`, `DL_006`, `LB_004`, `DB_005`,
`DB_006`). The depth chart is roster order, so a backup goes after the starters at his role.

## Personnel package schema (`FPSPersonnelCatalog`)

Who takes the field (Epic 19.5). `UPSPersonnelManager` picks each side's players from the roster's
depth chart by package:
- `Packages[]`, each: `PackageId` (unique), `DisplayName`, `bOffense`, `RoleCounts` (an object of
  `EPlayerRole` name to count; the roles all on the package's side, 11 players in all, and an
  offense needs at least one `Quarterback` and one `OffensiveLineman`), and `Formations` (the
  play formations, `FPSPlayDefinition::Formation`, that bring the package on; a formation belongs
  to at most one package per side).
- `DefaultOffensePackage`, `DefaultDefensePackage`: each side's package at kickoff and for a
  formation no package lists.
- `FatigueSubstitutionThreshold` (0-1): a player whose stamina falls below this fraction of his
  maximum rests the next play when someone is behind him on the depth chart.

When a side calls a play, its formation's package comes on: per role, the first players on the
depth chart who can play (a ball carrier sitting out and a resting player are skipped). Only the
players who change come off. A roster that can't fill a package gets the side's default instead.
Every playbook formation has a package: the clock plays (kneel, spike) use 11 personnel, and the
special-teams formations (Epic 75) bring on the `KickingUnit` (punt, field goal, kickoff), the
`ReturnUnit` (kick returns and desperation laterals), the `BlockUnit` and the `HandsTeam`.
`UPSPersonnelManager::ValidateCatalog` and `tools/validate_data.py` check it.

## Team schema (`FPSTeamInfo`)

`TeamId`, `DisplayName`, `Division` and `RosterDataTablePath` are required. `TeamId`,
`DisplayName` and `Abbreviation` are each unique in the league. `RosterDataTablePath` is the
project-relative path to that team's player roster JSON, loaded separately via
`LoadPlayerAttributesFromJson`.
Identity for team select (Epic 101): `Abbreviation` (2-4 letters or digits), `PrimaryColor` and
`SecondaryColor` (`#RRGGBB`), `LogoPath` (soft object path; empty until logos are imported).
Team ratings are not stored: `UPSUITeamCatalog` derives them from the roster.

## League config schema (`FPSLeagueConfig`)

Single JSON object (not an array), every field required: `LeagueName`, `NumWeeks` (1 or more),
`ByeWeekNumbers` (distinct weeks within the season), `NumPlayoffTeams` (2 or more, at most the
league's teams), `TeamsDataTablePath` (the teams file, project-relative).

## Playbook schema (`FPSPlayDefinition` / `FPSRoute`)

See `Source/PlaySports/Public/PSPlaybookData.h` for the full assignment/route shape. The rules
are:

- Each play has a unique `PlayId`, a `Formation`, `bIsOffensivePlay` and a `PlayCategory` for its
  side:
  - offense: `Run`, `ShortPass`, `DeepPass`, `PlayAction`, `Screen`;
  - defense: `Base`, `Blitz`, `Prevent`.
- `Front` and `CoverageShell` are for defensive plays only.
- Assignments use their side's roles and kinds:
  - offense: `Route`, `PassBlock`, `RunBlock`;
  - defense: `ManCoverage`, `ZoneCoverage`, `PassRush`, `RunFit`, `Blitz`.
- Only a `Route` assignment names a `RouteId`, and it must exist in the route library. A `Route`
  assignment without one is "go to your spot", such as the QB's drop.
- `PlayCategory` may also be a clock play, `Spike` or `Kneel` (Epic 76), which the simulation
  resolves at the snap.
- The route library's own rules are under "Route schema extras" below.

Two `PlayCategory` values are clock plays (Epic 76): `Spike` and `Kneel` (the `Clock` formation).
The CPU calls them only when the clock does (`UPSSituationAI::DecideClockPlay`), and
`UPSPlaySimulation` resolves them at the snap: a spike is an incompletion, a kneel is down for
`UPSRulesConfig::KneelYardage` with the clock running.

Special-teams `PlayCategory` values (Epic 75) name `EPSSpecialTeamsPlay` calls: `Punt`, `FieldGoal`,
`FakePunt`, `FakeFieldGoal` (the offense on a scrimmage down), `Kickoff`, `OnsideKick` (the kicking
team), `KickReturn` (a return; its `Formation` names the scheme in `special_teams.json`),
`KickBlock`, `HandsTeam` and `ReturnLaterals` (the receiving team). A kickoff down offers kickoff
calls and returns only; a scrimmage down everything else. The CPU calls one only when
`UPSSpecialTeamsAI` says it's due, and `UPSPlaySimulation` resolves it through
`UPSSpecialTeamsModel`.

## Adding a new team

1. Add a `rosters/team_<name>.json` roster file following the player schema above (aim for at
   least one player per `EPlayerRole`).
2. Add an entry to `sample_teams.json` pointing `RosterDataTablePath` at it.
3. Run `python tools/content.py` before committing. CI runs the same validation and imports
   the roster through the game's loaders (`PlaySports.Content.ImportShippedContent`).

## Input catalog schema (`FPSInputCatalog`)

The single source of input actions and mapping contexts (`Specs/Input_Architecture.md`).
`UPSInputConfig` builds one `UInputAction` per action and one `UInputMappingContext` per
context from it at runtime; `APSPlayerController` applies the `OnField` context when it
possesses a pawn.

- `Contexts[]`: `ContextId` (unique), `Priority` (int; higher wins on a shared key),
  `Description`, optional `bRemappable` (default true). An action in a context with
  `bRemappable` false keeps its keys: the menus read `Menu`'s through Slate (Epic 103.4).
- `Actions[]`: `ActionId` (unique), `ValueType` (`Boolean`, `Axis1D`, `Axis2D`, `Axis3D` --
  the `EInputActionValueType` names), `Description`, `Contexts` (IDs above), `Bindings[]`.
- `Bindings[]`: `Key` (an engine `EKeys` name such as `W`, `Mouse2D`, `Gamepad_Left2D`),
  optional `bSwizzleYX` (route a 1D key onto a 2D action's Y axis) and `bNegate`.

Rules enforced by `tools/validate_data.py` and `UPSInputConfig::Validate()`: every action has
at least one keyboard/mouse key and one `Gamepad_*` key in every context it is declared for,
and no key is bound to two actions in the same context. A player's remap (saved in the profile,
never written here) passes the same checks before it applies.

## Input tuning schema (`FInputTuningRow`)

Single object: `StickDeadZoneLower` and `StickDeadZoneUpper` (radial dead zone, 0 ≤ lower <
upper ≤ 1), `StickResponseExponent` (> 0; 1 is linear), `DeviceSwitchAnalogThreshold` (0–1; how
far a stick must move before the game decides the player picked up the gamepad). `UPSInputConfig`
applies the stick values as Enhanced Input modifiers on every gamepad stick binding;
`tools/validate_data.py` checks the ranges.

## Menu catalog schema (`FPSMenuCatalog`)

`RootScreen` (the front end's first screen; must set `bAllowBack` to false), `PauseScreen` (what
the in-game Pause action opens), `TransitionSeconds` (fade-in per screen), `Screens[]`.
Each screen: `ScreenId` (unique), `Title`, optional `Body`, `bAllowBack` (default true), and
`Options[]`. Each option: `OptionId` (unique on its screen), `Label`, and a `TargetScreen` to
open, a `Command`, or both. `Command` is one of `EPSMenuCommand`: `None`, `Resume`,
`StartPlayNow`, `StartFranchise`, `StartPractice`, `QuitToMainMenu`, `QuitGame`. A screen's
`Content` is `Static` (its authored options), `TeamSelect` (one option per team, generated) or
`Loading` (its body is the loading tip; `LoadingScreen` names it and it is left only by travel),
`PlayCallFormations` (the player's formations; `PlayCallScreen` names it), `PlayCallPlays` (a
formation's plays, each with the `CallPlay` command), `PlayCallRecent` (the player's recent
calls), `PlayCallFavorites` (their starred plays) or `PlayCallAdjustments` (after a human defense
calls; options carry the `ApplyAdjustment` command). Options may carry a `Detail` line.
`UPSMenuComponent::ValidateCatalog` and `tools/validate_data.py` reject dangling targets,
options that do nothing, a root screen Back could close, and screens that can never be left.

## Loading tips schema (`FPSLoadingTipCatalog`)

`MinimumDisplaySeconds` (how long the engine loading screen stays up), `Tips[]`: `TipId`
(unique), `Text`, `Contexts` (any of `Any`, `PlayNow`, `Franchise`, `Practice`). A mode shows its
own tips and the `Any` tips, shuffled, with no repeat until all have been shown.

## Force feedback schema (`FPSForceFeedbackTuning`)

`MasterIntensity` (0–1, scales every cue) and `Cues[]`, exactly one row per
`EPSForceFeedbackCue`: `Hit`, `Tackle`, `Sack`, `Catch`, `Interception`, `Fumble`, `Score`. Each
row (`FForceFeedbackTuningRow`): `Intensity` (0–1; 0 turns the cue off), `Duration` (seconds,
above 0 and at most 3), the four motors `bLeftLarge`, `bLeftSmall`, `bRightLarge`, `bRightSmall`
(at least one on), and `bOnlyWhenInvolved` (true: only when the event names the pawn the player
controls; false: everyone feels it). `UPSForceFeedbackComponent::ValidateTuning` and
`tools/validate_data.py` check all of it.

## Button glyph schema (`FPSInputGlyphCatalog`)

`GlyphSets[]`, each: `GlyphSetId` (unique, e.g. `Xbox`), `Device` (`KeyboardMouse`, `Gamepad`
or `Touch`), `bDefaultForDevice` (exactly one default set per device), `bFallbackToKeyName`
(unlisted keys get a keycap with the key's name -- for keyboards), `Keys[]` (`Key` an engine
`EKeys` name of that device, `GlyphId` the icon an imported texture is registered under, `Label`
the text shown until then) and `Actions[]` (`ActionId`, `GlyphId`, `Label`: one glyph for a
whole action, such as `WASD` for Move). Which key an action uses comes from `input_actions.json`,
so a rebinding never needs a glyph edit; every key the input catalog binds must be drawable by
its device's default set, which `UPSInputGlyphs::Validate` and `tools/validate_data.py` check.
The `Touch` set (Epic 130) lists no keys, because touch controls name actions rather than keys:
it has one `Actions[]` glyph per action a touch control drives, and `touch_controls.json`'s
validation checks that each one is there.

## Play-call tuning schema (`FPlayCallTuningRow`)

Single object (Epic 102; `Specs/Play_Call_Interface.md`):
- `CpuSnapDelaySeconds` (0 or more): how long a CPU (or quick-called) offense waits after both
  calls are in before it snaps. A human offense snaps when its player hikes.
- `QuickCallPlayClockSeconds` (0 or more): the play clock at which a human's uncalled side gets
  the top suggestion called for them.
- `RecentPlaysShown` (1 or more): how many recent calls the Recent plays screen lists.

## Defensive adjustments schema (`FPSDefensiveAdjustmentCatalog`)

`Adjustments[]`, each: `AdjustmentId` (unique), `Label`, `Description`, `Role` (a defender:
`DefensiveLineman`, `Linebacker` or `DefensiveBack`) and `Kind` (a defensive assignment:
`PassRush`, `Blitz`, `RunFit`, `ManCoverage` or `ZoneCoverage`). Choosing one on the Adjust
screen makes every defender of `Role` play `Kind` over the called play for that snap (Epic 102).
`UPSPlayCallSubsystem::ValidateAdjustments` and `tools/validate_data.py` check it.

## Offensive AI tuning schema (`FSkillPlayerAITuningRow`)

Single object (Epic 14; how CPU offensive players play the call). Every field is a number, 0 or
more; distances are cm, times seconds:
- `WaypointArrivalRadius`: how close counts as reaching a route waypoint (or the throw's spot).
- `OpenSeparation`, `AwarenessMisreadSeparation`: a receiver is open to the QB at
  `OpenSeparation + AwarenessMisreadSeparation * (1 - Awareness/100)` from the nearest defender.
- `MinReadSeconds`, `MaxReadSeconds` (min not above max): the QB reads no sooner than the first;
  by the second he throws to his best receiver or scrambles.
- `PressureRadius`: a defender this close forces the QB's decision now.
- `PressuredThrowSeparation`: forced to decide, the QB still throws to a receiver this open.
- `HandoffRadius` (at most 200, the hand-off's own reach), `HandoffTimeoutSeconds`: on a run the
  QB hands off when the back is this close, or keeps it and runs after the timeout.
- `CarrierAvoidRadius`, `CarrierAvoidWeight`: the ball carrier veers from defenders inside the
  radius, by up to the weight (1 = as much as upfield).
- `ThrowLeadSpeed` (above 0): ball speed for leading a receiver.
- `BlockSetDistance`, `BlockEngageRadius`: a blocker sets up this far in front of the QB and
  takes on rushers within the radius of him.
- `FieldHalfWidth`, `SidelineCushion`, `SidelineSteerWeight` (Epic 76): the sidelines are
  `FieldHalfWidth` either side of the middle. When the call says stay in bounds the carrier turns
  back inside within `SidelineCushion` of a sideline; when it says get out of bounds he heads for
  the nearer one once past the line. The weight is how hard (1 = as much as upfield).
- `ReadWindowSeconds`, `MaxAnticipationSeconds` (Epic 68): a receiver on a planned route is read
  from his break (as much as `MaxAnticipationSeconds` before it at Awareness 100) until
  `ReadWindowSeconds` after it.
- `BlownCoverageSeparation` (Epic 17's broken-play reactions): a receiver this far from every
  defender is read whatever his route's timing.

## Defensive AI tuning schema (`FDefenderAITuningRow`)

Single object (how CPU defenders play their assignment). Every field is a number, 0 or more;
distances are cm, times seconds:
- `ArrivalRadius`: how close counts as reaching a spot (a zone, the drop, the ball).
- `ManCushion`, `ManAnticipationSeconds`: a cover defender stays this far downfield of his man
  and, at Awareness 100, reads this far ahead of the man's movement (not at all at 0).
- `ZoneRadius`, `ZoneShadeWeight` (at most 1): a zone defender plays receivers this close to his
  spot, moving this fraction of the way toward the nearest (0 holds the spot).
- `ContainWidth`: a contain rusher aims this far outside the passer.
- `PassReadDepth`, `PassDropDepth`: a passer this far behind the line is a pass read; a run-fit
  defender then drops to `PassDropDepth` past the line.
- `MaxReactionSeconds`: how long a defender with 0 Awareness takes to react to a read or a
  throw (no delay at 100).
- `BallHawkRadius`: coverage defenders this close to where a pass comes down break on it.
- `PumpFakeFreezeSeconds`: how long a coverage defender with 0 Awareness freezes on a pump fake
  (no freeze at 100).

## Passing input schema (`FPassingInputTuningRow`)

Single object (Epic 104; how the human QB's buttons throw, `Specs/Input_Architecture.md`):
- `SlotActions`: the catalog actions that throw to receiver slots 1..N, the receivers ordered left
  to right across the field. Each must be a Boolean action in the catalog's `Passing` context;
  so must `PumpFakeAction`.
- `BulletHoldSeconds`: a slot button held at least this long throws a bullet; a quicker tap
  throws a touch pass.
- `TouchSpeedScale` (above 0, at most 1): a touch pass leaves at this fraction of the passer's
  full arm. A target out of reach that softly is thrown at full speed.
- `PlacementDepth`, `PlacementWidth` (cm): the Move stick at release moves the throw this far
  deeper/shorter and to either side of the receiver's lead point.
- `LeadSpeed` (above 0): ball speed used to lead a moving receiver, as the AI passer does.

## Platform tier schema (`FPSPlatformTierCatalog`)

Single object (Epic 129; `Specs/Platform_Audit.md`):
- `Tiers[]`, each with:
  - `TierId` (unique) and a `Description`;
  - `DeviceProfile`: the profile carrying the tier's rendering settings, either an engine
    profile (`Windows`, `IOS`, ...) or one declared in `Config/DefaultDeviceProfiles.ini`;
  - `AIDecisionInterval`: seconds between each AI player's decisions, 0 for every frame. The AI
    steers every frame in between.
  - `OverlayDetail`: `Full` (everything, animated), `Simplified` (no animated transitions or
    pulses) or `Minimal` (the score bug and the control reticle, static). Track A's overlays
    read it.
  - `TelemetrySampleRateHz`, `TelemetrySampleBudgetMs` (above 0): how often the telemetry
    sampler (Epic 26) records every pawn, and what one recording may cost in ms before the
    sampler halves its rate.
- `Platforms[]`: `Platform` (as `UGameplayStatics::GetPlatformName` reports it: `Windows`,
  `Mac`, `IOS`, `Android`) to `Tier`.
- `DefaultTier`: the tier for a platform with no mapping.

A run can be forced onto a tier with `-PSTier=<TierId>`. A system with a per-tier cost adds its
budget as a field here; it never hardcodes a mobile case.

## Carrier move schema (`FPSCarrierMoveCatalog`)

`Moves[]` (Epic 104.2), one per move. Each has:
- `Move`: `Juke`, `Spin`, `Truck`, `StiffArm`, `Hurdle` or `Slide`, each once.
- `ActionId`: a Boolean action in the input catalog's `BallCarrier` context.
- `Attribute` (`Agility`, `Strength` or `Speed`) and `MinAttribute` (0-100): the rating the move
  runs on, and the least that can do it.
- `WindowSeconds`, `CooldownSeconds`, `StaminaCost`.
- `CommitSeconds`: the move's commitment window (Epic 104.4). Once the move starts, no other move
  starts for this long; a press that arrives meanwhile waits in the input buffer
  (`input_buffer.json`). Track D's animations will own this number once they exist.
- `TackleChanceScale`: what a tackle's chance is multiplied by during the window for a carrier
  rated 100; a lower rating gets proportionally less help.
- `SpeedRetained` (0-1), `LateralSpeed`, `ForwardSpeed` (cm/s): the velocity change as the move
  starts. A juke cuts toward the Move stick's side.
- `bGivesUp`: the slide. The next contact downs the carrier with no hit and no fumble.

`UPSCarrierMoveComponent::ValidateCatalog` and `tools/validate_data.py` check it.

## Input buffer schema (`FInputBufferTuningRow`)

Single object (Epic 104.4; how long a press waits for a busy target, `Specs/Input_Architecture.md`
section 6):
- `MaxQueued` (1 or more): the most presses waiting at once. A newer press pushes out the oldest.
- `Actions[]`, each a Boolean catalog action (once) with its `BufferSeconds`: a press waits this
  long for its target (a carrier committed to a move or cooling down, a passer without the ball
  yet), then is dropped. A press that meant nothing because its context was off counts in that
  context if the context comes on within this long. Actions not listed pass straight through.

`UPSInputBufferComponent::ValidateTuning` and `tools/validate_data.py` check it.

## Pass-rush move schema (`FPSRushMoveCatalog`)

Single object (Epic 70; how a CPU pass rusher beats the man blocking him):
- `RushMoves[]`, one per move. Each has:
  - `Move`: `Bull`, `Swim`, `Rip`, `Spin`, `Club` or `Split`, each once.
  - `Attribute` and `BlockerAttribute` (`Strength`, `Agility`, `Speed` or `Awareness`): the
    rusher's rating the move runs on and the blocker's rating that resists it.
  - `MinAttribute` (0-100): below this rating the rusher doesn't have the move.
  - `BaseWinChance` (0-1) and `RatingScalar`: the success curve, `BaseWinChance + (rusher rating -
    blocker rating) * RatingScalar`, clamped to `WinChanceMin`..`WinChanceMax`.
  - `MoveSeconds` (above 0), `StaminaCost`, and `WinBurstSpeed` (cm/s toward the passer on a win).
  - `Response`: the blocker response that stops the move (`Anchor`, `Punch` or `Mirror`).
    `Counters`: the response the move beats (or `None`), never its own `Response`.
  - `DoubleTeamWinScale` (0-1): what the chance is multiplied by against two blockers.
    `bDoubleTeamOnly`: the move is only used against a double team (the split).
- `FirstMoveSeconds`, `RecoverySeconds`: from the engagement to the first move, and from a
  stopped move to the next.
- `CounterBonus` (0-1): added to a move's chance right after the blocker used the response it
  counters (a blocker who anchored on a bull is spun off).
- `WinChanceMin` <= `WinChanceMax` <= 1.
- `HistoryPriorWeight` (above 0): how many tries the plan's estimate of a move is worth before
  this blocker has seen it. The plan scores a move `(wins + HistoryPriorWeight * chance) /
  (tries + HistoryPriorWeight)` over this game's tries against this blocker.
- `DoubleTeamRadius` (cm): a free offensive lineman this close to the rusher is the second man
  of a double team; a lineman engaged on him counts from anywhere.

`PSRushMoves::ValidateCatalog` and `tools/validate_data.py` check it.

## Defensive technique schema (`FDefensiveTechniqueTuningRow`)

Single object (Epic 104.5; the human defender's buttons, `Specs/Input_Architecture.md` section 6):
- `JumpSnapAction`: a Boolean action in the catalog's `DefensePreSnap` context. `StripAction`: one
  in its `Defense` context.
- `JumpWindowSeconds`: a jump pressed at most this long before the snap is clean; an earlier one
  is offside.
- `GetOffSpeed` (cm/s): what a clean jump adds toward the line of scrimmage at the snap.
- `StripWindowSeconds`, `StripCooldownSeconds`: how long a strip attempt lasts, and from one to
  the next.
- `StripTackleScale` (0-1): a stripping defender's tackles succeed this many times as often.
- `StripFumbleChance` (0-1): what his tackles add to the fumble chance at Strength 100, in
  proportion below.

`UPSDefenderTechniqueComponent::ValidateTuning` and `tools/validate_data.py` check it.

## Kick meter schema (`FKickMeterTuningRow`)

Single object (Epic 104.5; the human kicker, `Specs/Input_Architecture.md` section 6):
- `KickAction`: a Boolean action in the catalog's `Kicking` context.
- `LineUpSeconds` (above 0): how long into a kick phase the play waits for the human's kick.
- `PowerFillSeconds` (above 0): held, the power bar fills from empty to full in this long, then
  drains back.
- `AccuracySweepSeconds` (above 0): with power locked, the needle runs from -1 to 1 in this long.
- `PowerWeight`, `AccuracyWeight` (0 or more, not both 0): the kick's roll is
  `PowerWeight x (1 - power) + AccuracyWeight x |needle|`, clamped to 0-1, where 0 is perfect.

`UPSKickMeterComponent::ValidateTuning` and `tools/validate_data.py` check it.

## Pre-snap tuning schema (`FPreSnapTuningRow`)

Single object (Epic 66; the offense's audibles, hot routes, motion and protection,
`UPSPreSnapSubsystem`). Distances are cm:
- `HotRouteSets[]`, one per `Alignment` (`Wide`, `Slot`, `Tight`, `Backfield`, each once):
  `Routes`, the route IDs a player lined up there may be hot-routed to, in the order the
  hot-route button cycles them; and `ReleaseRoute`, the route a blocker there runs when
  released. Every route must exist in `sample_routes.json`.
- `SlotMaxSplit`: a wide receiver no further than this from the ball is in the slot.
- `MotionEndSplit`, `MotionArrivalRadius`: motion runs across to this far on the other side of
  the ball; arriving is being this close.
- `ManTravelLateralRadius`: in man coverage, the defender within this distance across the field
  of the man in motion travels with him (the man indicator).
- `SlideAimOffset`: a sliding lineman looks for his man this far toward the slide.
- `BoxWidth`, `BoxDepth`: the box, either side of the ball and off the line.
- `CpuReadMinAwareness` (0-100): the CPU quarterback reads the defense before the snap only
  with at least this Awareness.
- `HeavyBoxCount`, `LightBoxCount` (whole numbers, light below heavy): a run into a heavy box
  or a blitz look is checked out of, to a pass; a pass against a light box and no blitz, to a
  run.
- `BlitzHotRoute`, `bCpuKeepsBackInVsBlitz`: against a blitz look the CPU sends its slot
  receiver on this route and keeps a back with a route in to block.
- `bCpuMotionOnPass`: on a pass the CPU motions its slot receiver.
- `AudibleAction`, `SelectAction`, `HotRouteAction`, `MotionAction`, `SlideAction`,
  `ProtectionAction`: the human's pre-snap buttons, each a different Boolean action in the input
  catalog's `PreSnap` context.

`tools/validate_data.py` checks it, including the routes and the actions.

## Settings schema (`FPSSettingsCatalog`)

Single object (Epic 103; the settings menu, `Specs/Front_End_Shell.md`):
- `Categories[]`: `CategoryId` (unique) and `Label`. Each is one screen in the settings menu.
- `Settings[]`, each with:
  - `SettingId` (unique), `Category` (one of the categories), `Label` and `Description`;
  - `Kind`: `Toggle` (value 0 or 1), `Choice` (value is the index into `Choices`, at least two)
    or `Slider` (`Min` < `Max`, a positive `Step`, an optional `Unit` shown after the value);
  - `Values` (choices only, optional): the number each choice stands for, one per choice. A
    frame-rate cap's 60, a dead-zone scale's 1.5.
  - `Default`: the value before the player changes it.

The player's values live in the profile save, by `SettingId`. A setting removed from this file
is dropped from the profile on load; a stored value outside today's range is snapped into it.
Code refers to settings by ID (`UPSSettingsSubsystem` and `UPSSettingsComponent` name the
ones they apply). `UPSSettingsSubsystem::ValidateCatalog` and `tools/validate_data.py` check it.

## Accessibility tuning schema (`FPSUIAccessibilityTuning`)

Single object (Epic 103.2/103.3; captions and color vision, `Specs/Front_End_Shell.md`):
- `CaptionMinSeconds` (positive) and `CaptionMaxSeconds` (no less): how long a caption stays up
  when the speech doesn't say how long it is spoken.
- `CaptionWordsPerSecond` (positive): the reading pace that times such a caption, between those
  bounds.
- `CaptionMaxLines` (a whole number, 1 or more): the most captions on screen; the oldest goes.
- `MinMatchupColorDistance` (0 or more): how different (CIE76 delta E, as the player sees them)
  home and away colors must look before one side falls back to its secondary color.

The settings that switch these on and size them (`Captions`, `CaptionSize`, `Narration`,
`ColorblindMode`) are in `ui_settings.json`. `UPSUIAccessibilitySubsystem::ValidateTuning` and
`tools/validate_data.py` check it.

## UI text tables (`ui_text.csv`, `ui_text_data.csv`)

Epic 106: everything the UI shows comes from one of two UE string tables, so a translation is
UE's gather, translate and compile, and needs no code change. `UPSLocalization`
(`Source/PlaySports/Public/PSLocalization.h`) registers both tables and is the only way UI code
reads them.

- `ui_text.csv` (table `PSUI`) is written by hand: the code's own text. Columns are `Key`,
  `SourceString` and `Comment` (for translators). Patterns use FText's `{Name}` placeholders.
  Code names keys literally: `UPSLocalization::GetText(TEXT("Menu.ResetToDefaults"))`,
  `UPSLocalization::Format(TEXT("Menu.Option"), Arguments)`. Families the code builds:
  `Input.Action.<ActionId>` (one per remappable action), `Input.Context.<ContextId>`,
  `HUD.Phase.<Phase>`, `HUD.Score.<ScoreType>`.
- `ui_text_data.csv` (table `PSUIData`) is **generated** from the user-facing strings of
  `ui_menus.json`, `ui_settings.json` and `loading_tips.json`. Don't edit it. After changing one
  of those files, run `python tools/ui_text.py --write`. Keys: `Menu.<ScreenId>.Title|Body`,
  `Menu.<ScreenId>.<OptionId>.Label|Detail`, `Setting.Category.<CategoryId>`,
  `Setting.<SettingId>.Label|Description|Unit|Choice<Index>`, `Tip.<TipId>`.
- Not translated, shown through `UPSLocalization::Verbatim`: team, player and formation names,
  button glyph labels (`input_glyphs.json`), and the engine's key names.
- UI strings may not contain backslashes, because the string table import reads them as
  escapes. A real newline is fine.

`tools/validate_data.py` (through `tools/ui_text.py`) fails on any of these:
- a stale `ui_text_data.csv`;
- a key the code names that isn't in `ui_text.csv`;
- a remappable action, or one of its contexts, without a name row;
- duplicate or empty keys, or unbalanced placeholders;
- FText built from a raw string in UI code (`Private/PSUI*`, `PSMenu*`, `PSHUD*`, `PSLoading*`,
  `PSSettings*`).

The `Units` setting (`ui_settings.json`, Gameplay) picks feet and pounds or centimeters and
kilograms for `WeightKg`/`HeightCm` wherever they are shown (`UPSLocalization::FormatWeight`,
`FormatHeight`). Team select shows each roster's average size in those units.

## Situational tuning schema (`FPSSituationalTuning`)

Single object (Epic 76; how the coaching AI reads the end of a half). Times are game-clock
seconds left in the quarter; yard lines count from the offense's goal line (0) to the opponent's.
- `Tempos[]`: `Tempo` (`Huddle`, `NoHuddle`, `HurryUp`, `MilkClock`, each once), `Label`,
  `SnapAtPlayClockSeconds` (0-40: the play-clock reading the snap comes at; a running game clock
  runs down to it at the snap) and `bRerunLastCall` (a human offense gets its last play again,
  with no call screen).
- `SituationTempos[]`: per `Situation` (`Normal`, `TwoMinuteDrill`, `FourMinuteOffense`,
  `VictoryFormation`), the CPU offense's `ClockRunningTempo` and `ClockStoppedTempo`.
- `HumanTempoCycle`: the tempos the `Tempo` action cycles through. `SpikeTempo`, `KneelTempo`:
  what a spike or kneel snaps at, whatever the offense's tempo.
- Two-minute drill: `TwoMinuteWindowSeconds` (the 2nd quarter, or a trailing or tied 4th),
  `TwoScoreWindowSeconds` (a 4th-quarter offense down more than `OneScorePoints`),
  `ClockUrgencySeconds` (under it a running clock is stopped: timeout if one is left, else a
  spike on down `MaxSpikeDown` or earlier, with at least `SpikeMinSeconds` left).
- Four-minute offense: `FourMinuteWindowSeconds` (a leading 4th-quarter offense); the defense
  calls timeouts on a running clock under `DefenseTimeoutWindowSeconds` when down by no more than
  `MaxDeficitToChase`.
- Victory formation: the offense kneels when `KneelPlaySeconds` per kneel plus
  `KneelPreSnapSeconds` per gap the defense has no timeout to stop covers the time left; at the
  end of the 1st half, inside its own `EndOfHalfKneelMaxYardLine` with `EndOfHalfKneelSeconds`
  or less.
- Play calling: `ClockPlayWeight` (a spike or kneel the clock calls for), `SidelineRouteIds` and
  `MiddleRouteIds` (route library IDs), `SidelinePlayDelta` and `MiddlePlayDelta` (two-minute
  drill pass plays), and `CategoryWeights[]`: `Situation`, `bOffense` (whose call), `Category`,
  `Delta` and the `Reason` the play-call screen shows.

`tools/validate_data.py` checks it.

## Special-teams tuning schema (`FPSSpecialTeamsTuning`)

Single object (Epic 75). Yard lines (1-99) count from the team's own goal line; chances are 0-1.
- Kickoffs: `KickoffYardLine` (after a touchdown or field goal), `SafetyKickYardLine`,
  `KickoffTouchbackChance`, `TouchbackYardLine`, a return's `KickoffReturnMinYardLine` to
  `KickoffReturnMaxYardLine`, `OnsideKickYards`, `OnsideRecoveryChance` (by surprise) and
  `OnsideRecoveryVsHandsTeamChance`, `HandsTeamReturnPenaltyYards`, and laterals'
  `LateralTouchdownChance` and `LateralFumbleLostChance`.
- Punts: `PuntGrossYardsMin`/`Max`, `PuntTouchbackYardLine`, `PuntReturnYardsMin`/`Max`.
- Field goals: `FieldGoalSnapYards` (added to the line of scrimmage's distance from the goal line),
  `FieldGoalRanges[]` (`MaxYards`, `MakeChance`, shortest first; no chance beyond the last), and the
  missed kick's spot clamp `MissedFieldGoalMinYardLine`/`MaxYardLine`.
- Blocks: `PuntBlockChance` and `FieldGoalBlockChance` against a return unit, times
  `BlockUnitMultiplier` when the defense calls the block, plus `EdgeSpeedFactor` per point of edge
  speed and `InteriorStrengthFactor` per point of interior strength the rushers have over the
  protection, at most `MaxBlockChance`. `BlockedPuntRecoilYards`, `BlockedKickTouchdownChance`,
  `BlockUnitReturnPenaltyYards`.
- Returns: `ReturnSchemes[]` (`Formation` of a `KickReturn` play, `ReturnYardsBonus`,
  `BigReturnChance`), `DefaultBigReturnChance`, `BigReturnYards`; lane discipline from the
  coverage's awareness over `CoverageAwarenessSpan` takes up to `LaneDisciplineYards` and
  `LaneDisciplineBigReturnScale` of the big-return chance.
- Fakes: `FakePuntSuccessChance`, `FakeFieldGoalSuccessChance`, `FakeVsBlockUnitDelta`,
  `FakeExtraYardsMax`.
- The CPU's calls: `MaxFieldGoalAttemptYards`, `LastPlaySeconds`, `NoPuntTrailingSeconds`; the fake
  risk model `FakeMaxDistance`, `FakeMinAggression`, `FakeCallChance`; onside kicks
  `OnsideMaxDeficit`, `OnsideWindowSeconds`, `SurpriseOnsideChance`; laterals `LateralsMaxDeficit`,
  `LateralsWindowSeconds`; blocks `BlockWindowSeconds`, `BaseBlockCallChance`; and
  `SpecialTeamsPlayWeight`.

`tools/validate_data.py` checks it, including that each return scheme is a `KickReturn` play's
formation.

## Coaching staffs schema (`FPSCoachingLeague`)

Single object (Epic 89), read by `UPSStaffManager`; the franchise save keeps the coaches and
staffs after each carousel, while the schemes and tuning always come from here.
- `Schemes[]`: `SchemeId`, `Label` (shown on the call screen and in the play-call reasons),
  `bOffense`, `Formations` (the playbook formations the scheme runs on its side: they make the
  team's playbook, plus every special-teams and clock play), `CategoryWeights` (`PlayCategory` ->
  weight, 1 neutral: the CPU's lean), `FitWeights[]` (`Role`, `Attribute` -- `Speed`, `Agility`,
  `Strength`, `Acceleration`, `Awareness` or `Stamina` -- and `Weight`: what the scheme asks of a
  position), `Description`.
- `Coaches[]`: `CoachId`, `DisplayName`, `Role` (`HeadCoach`, `OffensiveCoordinator`,
  `DefensiveCoordinator`), `SchemeId` (a coordinator's on his side; a head coach's either side, the
  scheme he brings), `PlayCalling` and `Development` (0-100), `Aggression` (0-1, a head coach's).
  A coach on no staff is a free agent the carousel can hire.
- `Staffs[]`: `TeamId` (a `sample_teams.json` team), `HeadCoachId`, `OffensiveCoordinatorId`,
  `DefensiveCoordinatorId`, `HeadCoachSeasons`.
- `Tuning`: scheme adherence `MinSchemeAdherence`/`MaxSchemeAdherence` (at play calling 0/100);
  player fit `FitSpan`, `BestFitMultiplier`, `WorstFitMultiplier`, `DevelopmentMisfitRelief`,
  `FitLabelThreshold`; the carousel's `FireWinPercentage`, `GraceSeasons`,
  `CoordinatorFiresPerSide`, `CoordinatorSafeWinPercentage`, `PromoteWinPercentage`,
  `PromotionBonus`, `SchemeMatchBonus`.

`tools/validate_data.py` checks it, including that each scheme's formations are in the playbook on
its side (an offense keeping a run and a pass, a defense a `Base` call).

## Telemetry sampling schema (`FPSTelemetrySamplingTuning`)

Single object (Epic 26; how `UPSTelemetrySamplingSubsystem` records every pawn's position,
velocity, acceleration and facing for overlays, trails and replay). The sampling rate and the
per-frame budget are per platform tier (`TelemetrySampleRateHz`, `TelemetrySampleBudgetMs` in
`platform_tiers.json`), never in this file:
- `HistorySeconds` (above 0): how much the ring of scheduled frames covers at full rate. The
  fastest tier's rate x `HistorySeconds` is at most 10000 frames.
- `KeyframeEvents`: `EPSTelemetryEventType` names (`Snap`, `Catch`, ...), each once. Each such
  bus event captures every pawn the instant it is published; the keyframe lives as long as its
  event stays in the bus's history.
- `DegradeAfterSamples` frames in a row over the tier's budget halve the rate, at most
  `MaxDegradeLevel` times (0 to 8); `RecoverAfterSamples` frames in a row under
  `RecoverBelowFraction` (above 0, at most 1) of it double the rate back.

`UPSTelemetrySamplingSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Session telemetry schema (`FPSSessionTelemetryTuning`)

Single object (Epic 117). It sets what an opted-in player's sessions record and how much a crash
report says (`Specs/Privacy_Telemetry.md`):
- `FrameTimeBucketMs` (above 0), `FrameTimeBucketCount` (1 or more): the frame-time histogram's
  bucket width and count. Percentiles are reported to the bucket width, rounded up. A frame slower
  than width × count lands in the overflow bucket, which reports the slowest frame.
- `Percentiles`: the frame-time percentiles each session records, each above 0 and at most 100.
- `MinSessionSeconds` (0 or more): a session that ends cleanly with less play than this is not
  kept. One that never ends cleanly is always kept, because it is a crash.
- `MaxStoredSessions` (1 or more): how many sessions the local store keeps; the oldest go first.
- `CheckpointEveryPlays` (0 or more): save the open session every this many plays (0: only at
  the start and the end).
- `CrashBreadcrumbCount` (0 or more): how many recent telemetry-bus events a crash report carries.

`UPSSessionTelemetrySubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Run-fit schema (`FPSRunFitCatalog`)

Single object (Epic 81; how the run defense accounts for every gap). Gaps are `EPSRunGap` names,
left (-Y) and right (+Y) of the ball: `ALeft`/`ARight` beside the center, then `B`, `C` (outside
the tackle, inside an inline tight end) and `D`.
- `Fronts[]`, each with:
  - `Front`: the play's `Front` (`4-3`, `3-4`, `Nickel`, ...), each listed once.
  - `Fits[]`: a `Role` (an `EPlayerRole`, each once per front) and its `Gaps`, given to that
    role's defenders left to right across the field. A gap appears at most once per front;
    defenders beyond the list have none.
- `DefaultFront`: the listed front a call with an unlisted front (or no call) plays.
- `GapWidth` (above 0): a gap outside the last lineman is this wide (cm).
- `InlineTightEndWidth`: a tight end this close outside the end lineman extends the line.
- `FitDepth`, `SecondLevelDepth`: how far past the line of scrimmage a defensive lineman, and
  everyone else, fits his gap.
- `LeverageOffset`: a spill fitter plays this far inside his gap's center, the force player (the
  outermost fitter on each side) this far outside it.
- `FlowWeight` (0-1): a second-level fitter moves this fraction of the way from his gap toward
  the carrier, across the field.
- `AttackRadius`: a carrier this close to a fitter's gap, across the field, is coming through it,
  and the fitter attacks.
- `FillRadius`: a fitter this close to his gap's spot, across the field, fills it (gap
  integrity).

`PSDefenderGaps::ValidateCatalog` and `tools/validate_data.py` check it.

## Route-running tuning schema (`FRouteRunningTuningRow`)

Single object (Epic 68; how receivers run routes as contested skills, `UPSRouteRunnerComponent`
and `PSRouteRunning`). Every field is a number, 0 or more; distances are cm, chances 0-1:
- `PressRadius`: a defender this close in front of a receiver at the snap presses him.
- `ReleaseBaseWinChance`, `ReleaseRatingWeight`, `ReleaseMinWinChance`, `ReleaseMaxWinChance`
  (min not above max): the receiver's chance to win his release is the base plus the weight per
  point his release rating ((Agility + Strength) / 2) beats the presser's, clamped.
- `DelayShare` (at most 1), `DelaySeconds`, `RerouteOffset`, `RerouteDelaySeconds`: of the
  releases he loses this share are a delay (held `DelaySeconds`); the rest a reroute (his route
  moved `RerouteOffset` toward his sideline, held `RerouteDelaySeconds`).
- `BreakMinAngleDegrees` (at most 180): a waypoint turning the route this much is a break.
- `MaxBreakRounding`: at Agility 0 a receiver turns for the next leg this far before the corner;
  at 100 he cuts on the spot.
- `BreakSeparationBase`, `BreakSeparationPerAgility`: the separation a break makes, plus this per
  point of Agility on the defender (never below zero). The QB counts on it throwing early.
- `FakeSellSeconds`: a double move's receiver sells the fake this long.
- `BiteRadius`, `BiteBaseChance`, `BiteAgilityWeight`, `BiteAwarenessWeight`, `BiteMinChance`,
  `BiteMaxChance` (min not above max), `BiteFreezeSeconds`: the nearest defender within the
  radius bites with the base chance plus the receiver's Agility / 100 times its weight minus his
  own Awareness / 100 times its weight, clamped; one who bites freezes `BiteFreezeSeconds`.
- `ManReadRadius`: an option route's receiver reads man when a defender is this close at the
  read point.

## Blown-coverage tuning schema (`FBlownCoverageTuningRow`)

Single object (Epic 17.4; the defense's reaction to a receiver running free,
`UPSBlownCoverageSubsystem`). Every field is a number, 0 or more; distances are cm:
- `CheckIntervalSeconds` (above 0): how often the defense looks, while the quarterback holds the
  ball behind the line.
- `UncoveredSeparation`, `MinDepthPastLine`: a receiver at least `MinDepthPastLine` past the
  line with every defender this far from him is running free.
- `HelpRadius`: the nearest defender playing a zone (or in man with nobody to cover) within this
  distance of him leaves his zone to cover him. Each receiver and each helper once per play.

`tools/validate_data.py` checks it.

## Route schema extras (`FPSRoute`, Epic 68)

On top of `RouteId` and `Waypoints` (`Offset`, `TimingSeconds`) in `sample_routes.json`:
- `Waypoints[].bFake`: a double move's fake break (not the last waypoint).
- `OptionReadWaypoint` (-1 for none): an option route reads the coverage at this waypoint and
  runs `VsManBranch` or `VsZoneBranch` from there. A branch is a route whose offsets start at the
  read point and whose timings count from the read; it is authored breaking outside, and turns
  inside against a man defender with outside leverage. Branches must exist and not be options
  themselves.

## All-22 camera schema (`FPSAll22CameraTuning`)

Single object (Epic 40; the coaches film view, `UPSCameraAll22Component` on the broadcast camera):
- `All22Rigs[]`, in the order the film view toggles through them. Each has:
  - `RigId` (unique) and `Placement`: `Sideline` (high on the -Y sideline, the broadcast camera's
    side) or `EndZone` (high behind the end zone the offense defends).
  - `HeightCm` and `StandoffCm` (both positive): the rig's fixed height, and its distance from the
    field's centre (across the field for the sideline rig, along it for the end-zone rig).
  - `bTrackPlay` and `RailHalfLengthCm`: whether the rig slides along its rail (X for the sideline
    rig, Y for the end-zone rig) to stay square to the players, and how far the rail runs either
    side of its centre.
  - `MinFieldOfView`, `MaxFieldOfView` (degrees, 0 < min <= max < 170): the zoom range. Players
    too spread for the widest zoom make the rig back away along its line of sight.
- `FramingMarginCm`: padding kept around the players on the ground.
- `PlayerHeightCm`: a player's height, centred on the pawn, so heads and feet stay in frame.
- `AspectRatio`: the frame's width over height when no game viewport says otherwise.
- `ReframeSpeed`: how fast the frame closes in once play bunches up (0 closes in at once).
  Widening is always immediate.

`UPSCameraFraming::ValidateTuning` and `tools/validate_data.py` check it.

## Camera director schema (`FPSCameraDirectorTuning`)

Single object (Epic 38; `UPSCameraDirectorComponent` on the broadcast camera cuts the game by itself):
- `bDirectorEnabled`: off leaves the broadcast camera to its plain follow.
- `Shots[]`, the vocabulary, each `Shot` once: `LosWide`, `All22High`, `TightFollow`, `EndZone`,
  `SidelineReaction`, `Skycam`.
  - `All22High` and `EndZone` are taken by the Epic 40 rig named by `RigId`, which must be in
    `camera_all22.json`.
  - `Skycam` is Epic 39's cable rig (`camera_skycam.json`), which flies itself; its numbers here
    are only a fallback for a camera without one.
  - The others stand `DistanceCm` from their target toward the camera side, at `HeightCm`, with a
    `FieldOfView` (0-170 degrees), aiming `AimHeightCm` above the field. The target is the ball
    for `LosWide` and the live subject for the rest. `TightFollow` aims `LeadSeconds` ahead of
    him along his run.
- `CutRules[]`: `Trigger` (`PreSnap`, `Snap`, `Throw`, `Catch`, `Tackle`, `Fumble`, `Score`,
  `PlayEnd`, `Breakaway`; each once, and `PreSnap` is required: it is the opening shot) to the
  `Shot` it asks for. `Breakaway` fires when the live subject breaks into the clear (see
  `Interest`).
- `Interest`: how a player's interest is scored to pick the live subject. It is the sum of:
  - `BallWeight` for holding the ball;
  - `ProximityWeight` falling off to nothing at `ProximityRadiusCm` from the ball;
  - `BreakawayWeight` for a breakaway: at least `BreakawaySpeedCms` with no opponent within
    `BreakawayClearanceCm`;
  - `BigHitWeight`, fading over `BigHitSeconds`, after a tackle, or a hit of at least
    `BigHitDamage`.

  `SwitchMargin` is how much a new subject must out-score the current one by.
- `MinShotSeconds`: the shortest a shot runs before the director cuts away; an earlier ask waits,
  and the latest one wins.
- `FollowInterpSpeed`: how fast the camera eases after its target within a shot.
- `CameraSide` (-1 or 1) and `NeutralBandCm`: the side of the line of action every shot stays on
  (the 180-degree rule), and how close to the line a shot counts as on it.

`UPSCameraDirectorComponent::ValidateTuning` and `tools/validate_data.py` check it, including the
rigs.

## Selected-player reticle schema (`FPSOverlayReticleStyle`)

Single object (Epic 30; the ring under the player the human controls, drawn by
`APSOverlayReticle`):
- `OffenseColor`, `DefenseColor` (`#RRGGBB`): the ring's color by side, used when the human's
  team isn't known or `bUseTeamColor` is false. With `bUseTeamColor`, the team picked at team
  select gives its `PrimaryColor` (`sample_teams.json`).
- `MeshPath`, `MaterialPath`, `ColorParameter`: the ring's mesh, its material and the material's
  vector parameter the color goes into. Engine basic shapes until an editor session authors the
  broadcast hexagon (`Specs/Overlay_Reticle_Spec.md`).
- `MeshDiameter` (above 0, cm across at scale 1), `Thickness`, `GroundClearance` (cm, 0 or more).
- `ReticleStates[]`: one look each for `PreSnap`, `InPlay` and `BallCarrier`: `Radius` (cm, above
  0), `Brightness` (multiplies the color), `PulseHz` and `PulseAmount` (0-1, how far the radius
  swells; pulses only on a tier whose `OverlayDetail` is `Full`).

`UPSOverlayReticleComponent::ValidateStyle` and `tools/validate_data.py` check it.

## Player-switch schema (`FControlHandoffTuningRow`)

Single object (Epic 30; `UPSControlHandoffComponent`):
- `CycleWindowSeconds` (0 or more): a switch press this soon after the last one moves on to the
  next player in the same nearest-to-the-ball order instead of ranking again.
- `PickLeftAction`, `PickRightAction`: the pre-snap direct-pick actions, each a Boolean action in
  the input catalog's `PreSnap` context.

## Broadcast package schema (`FPSBroadcastOverlayTheme`)

Single object (Epic 33; the score bug and the lower-third chyrons, `UPSOverlayBroadcastSubsystem`).
Track C's branding reskins the broadcast by swapping this file:
- `HomeLabel`, `AwayLabel`, `HomeColor`, `AwayColor`: a side whose team isn't known. With
  `bUseTeamColors`, a known team (`sample_teams.json`) shows its own abbreviation and primary color.
- Colors (`#RRGGBB`): `BarColor`, `TextColor`, `RedZoneColor` (the down-and-distance box in the
  red zone), `TwoMinuteColor` (the clock in the two-minute state), `TimeoutColor` and
  `TimeoutUsedColor` (the timeout pips), `ChyronColor`.
- `Anchor` (`BottomCenter`, `TopCenter`, `TopLeft`), `ScoreFontSize`, `TextFontSize` (1 or more).
- `RedZoneYardLine` (1-99, from the offense's goal line: 80 is the opponent's 20) and
  `TwoMinuteSeconds` (the last this-many seconds of the 2nd and 4th quarters).
- Chyron rules: `ChyronMaxQueued` (1 or more waiting; past it the lowest priority goes, oldest
  first), `ChyronMinShowSeconds` (a higher priority chyron cuts in only after this),
  `ChyronGapSeconds`, and `ChyronKinds[]`: one `Kind` each (`ScoreAlert`, `DriveSummary`,
  `PlayStat`, `StatLine`, `Custom`) with its `Priority` (higher first) and `Seconds` on screen
  (above 0).

`UPSOverlayBroadcastSubsystem::ValidateTheme` and `tools/validate_data.py` check it.

## Position badge schema (`FPSOverlayBadgeStyle`)

Single object (Epic 28; the letters floating over players' heads, laid out by
`UPSOverlayBadgeComponent` on the player controller and drawn by `UPSOverlayBadgeWidget`). While the
human's quarterback can throw, his receiver slots wear the button that throws to them (the glyph of
the slot's `PassTarget` action on the device in use, so a remapped key shows); everyone else wears
his role's label.
- `Groups[]`: exactly one each for `Receiver` (wide receivers, tight ends), `Back` (running backs),
  `Quarterback`, `Line` (offensive line) and `Defense` (anyone on defense):
  - `Color`, `TextColor` (`#RRGGBB`);
  - `bPreSnap` (shown before the snap and after the whistle);
  - `InPlay`: `Hidden`, `WhilePassing` (while the human's QB can still throw) or `Always`;
  - `bEssential`: kept on a tier whose `OverlayDetail` is `Minimal` (the pass buttons).
- `RoleLabels[]`: a `Label` for every `EPlayerRole` (`Role`), worn by players without a button.
- `HeadClearance` (cm above the top of the capsule, 0 or more); `BadgeWidth`, `BadgeHeight` (pixels
  at scale 1, above 0); `FontSize` (1 or more).
- `ReferenceDistance` (cm from the camera drawn at scale 1, above 0), `MinScale` and `MaxScale`
  (above 0, `MinScale` at most `MaxScale`).
- Overlap rules: `BallClearance` (pixels kept clear around the ball each way, 0 or more),
  `NudgeStep` (pixels a badge moves up per try, times its scale, above 0) and `MaxNudges` (tries
  before a badge with no room isn't drawn, 0 or more). Pass buttons are placed first, then nearer
  badges before farther.
- `FadeInSeconds` (0 or more; a Full tier only) and `bBadgeControlledPlayer` (badge the human's own
  player too; he already has the reticle).

`UPSOverlayBadgeComponent::ValidateStyle` and `tools/validate_data.py` check it.

## Skycam schema (`FPSSkycamTuning`)

Single object (Epic 39; `UPSCameraSkycamComponent`, a camera hung from four cables over the field):
- `AnchorHalfLengthCm`, `AnchorHalfWidthCm`, `AnchorHeightCm`: the cable towers stand at
  (±half length, ±half width) and the cables leave them at this height.
- `CatenaryParameterCm` (tension over weight per length): the cables hang in catenaries, so the
  camera's ceiling at a point is the anchor height less both cable families' sag there. It is
  highest by the towers and lowest over midfield.
- `EdgeMarginCm`: how far inside the towers' rectangle the camera keeps. `MinHeightCm`: the lowest
  it flies (it must be below the ceiling over midfield).
- `StiffnessPerSecSq`, `DampingPerSec`: the rig's mass, a damped spring toward where it wants to
  be (2·√stiffness damps it critically). `MaxSpeedCms`, `MaxAccelerationCms2`: the winches.
- `BehindQuarterbackDistanceCm`, `BehindQuarterbackHeightCm`: where it parks before the snap.
- `ChaseDistanceCm`, `ChaseHeightCm`: how far behind the ball carrier, along his run, it chases
  from the snap.
- `LookAheadCm`: how far ahead of whoever it follows it looks. `FieldOfView` (0-170 degrees).

`UPSCameraSkycamComponent::ValidateTuning` and `tools/validate_data.py` check it.

## Ball-flight overlay schema (`FPSBallFlightStyle`)

Single object (Epic 32; the pass and kick indicators, `UPSOverlayBallFlightSubsystem`, drawn by
`APSOverlayBallFlight`). Lengths are cm in the game mode's field frame: 100 units a yard along the
field, the offense attacking +X from its own goal line at X = 0.
- Colors (`#RRGGBB`): `ArcColor`, `LandingColor`, `LeadOnTargetColor` and `LeadOffTargetColor`
  (the receiver's lead ring, by whether he gets to the ball), `GoodColor` and `NoGoodColor` (the
  kick readout).
- `DotMeshPath`, `RingMeshPath`, `MaterialPath`, `ColorParameter`: the arc's dots, the rings, their
  material and its color parameter. Engine basic shapes until an editor session authors a ribbon
  (`Specs/Ball_Flight_Overlay_Spec.md`). `MeshDiameter` (above 0): both meshes' size at scale 1.
- `ArcPoints` (2 or more, release to landing, evenly in time), `ArcDotDiameter` (above 0),
  `RingThickness`, `GroundClearance` (0 or more), `GroundZ` (the field's surface; kicks come down to
  it).
- `LandingRadiusFallback` (above 0): the landing ring when no receiver gives one (a pass's ring is
  the receiver's catch radius, his capsule plus the ball). `LeadRadius` (above 0).
- `DeviationTolerance` (above 0): the ball this far off its predicted path has been touched or has
  bounced, and the flight is over. `MaxFlightSeconds` (above 0): the longest flight drawn.
- `LingerSeconds`, `ReadoutSeconds` (0 or more): how long a pass's marks and a kick's readout stay
  up after the flight.
- Goal posts: `GoalPostX` (each end line's X; a kick is judged at the first ahead of it),
  `GoalPostY`, `UprightWidth` (above 0, inside width), `CrossbarHeight`, `ReadoutHeight` (above the
  bar) and `ReadoutTextSize` (above 0).
- `GoodLabel`, `WideLeftLabel`, `WideRightLabel`, `ShortLabel`: the readout's words.

`UPSOverlayBallFlightSubsystem::ValidateStyle` and `tools/validate_data.py` check it.

## Pocket tuning schema (`FPocketTuningRow`)

Single object (Epic 71; the quarterback's pocket and scramble, `UPSPocketComponent`, and the
scramble drill, `UPSPlayOrchestrator::TriggerScrambleDrill`). Every field is a number, 0 or
more; distances are cm, chances 0-1, ratings 0-100:
- `PocketRadius`, `EngagedPressureWeight` (at most 1), `EdgeWidth`: rushers within the radius
  press on the pocket, a blocked one at the weight of a free one, more the closer; one further
  than `EdgeWidth` across the field from the quarterback comes off the edge.
- `MinPressure`, `CollapsePressure` (min below collapse), `EscapeRadius`: below `MinPressure`
  he holds; edge pressure makes him climb (never within `ClimbStopDistance` of the line),
  inside pressure slide away from it. The pocket has collapsed at `CollapsePressure`, or with a
  free rusher within `EscapeRadius`: time to escape.
- `SackImminentRadius`: a free rusher this close is about to sack him.
- `StripBaseChance`, `StripStrengthWeight`: a rusher sacking him from behind (his blind side)
  strips the ball with the base chance plus the weight per point of Strength he has on him.
- `ThrowawayMinAwareness`, `GroundingAvoidAwareness` (min not above avoid): a quarterback who
  sees the sack coming throws the ball away from the first Awareness on; from the second he
  takes the sack rather than ground it.
- `TackleBoxHalfWidth`: a throwaway from inside the tackle box with no receiver near it is a
  grounding risk (announced for the rules to flag).
- `ThrowawayReceiverRange`, `ThrowawayShort`, `ThrowawayDepth`, `ThrowawayWidth`: he throws it
  away at the feet of a receiver within the range (`ThrowawayShort` short of him), else past the
  line and out toward his sideline.
- `ScrambleForwardBias`, `ScrambleMaxSeconds`, `RunLaneClearance`, `RunLaneWidth`: scrambling,
  he runs across the field this much upfield, looks to throw on the run for this long, and tucks
  it and runs when no defender is in his lane this far ahead.
- `SlideTriggerRadius`, `SlideMinGain`: past the line he slides ahead of a defender this close
  once he has gained `SlideMinGain`.
- `ScrambleDrillDepth`, `ScrambleDrillWidth`, `ScrambleDrillJitter`, `ScrambleDeepDepth`,
  `ScrambleDeepRunOn`: in the scramble drill a receiver breaks to `ScrambleDrillDepth` upfield of
  the quarterback, `ScrambleDrillWidth` toward his side, give or take the jitter; one already
  `ScrambleDeepDepth` downfield of him runs `ScrambleDeepRunOn` further, toward that side.

## Touch layout schema (`FPSTouchLayout`)

The on-screen controls of Epic 130 (`Specs/Touch_Controls_Spec.md`). Positions are in the
HUD-safe area, 0 to 1 across its width and height from the top left. Sizes and distances are
fractions of the safe area's height, so a round button stays round on any screen.

- `SafeZone` (`Left`, `Top`, `Right`, `Bottom`): margins, as fractions of the viewport, that no
  control enters: the Dynamic Island, the corners and the home indicator.
- `LayoutAspect`: the safe area's width over its height that the layout is checked against
  (buttons must fit and must not overlap at it).
- `bFloatingStick`: the stick centres where the finger lands in `StickZone`.
- `StickZone`, `GestureZone` (`Min`, `Max`): where a touch that misses every button takes the
  stick, or may swipe.
- `SwipeMinDistance` (safe-area heights) and `SwipeMaxSeconds`: what counts as a swipe.
- `TouchControls[]`: `ControlId`, `Kind` (`Stick`, `Button` or `Swipe`), and `Position` plus
  `Radius` (a button's hit radius, or the stick's full-push distance) or a swipe's `Direction`
  (`Left`, `Right`, `Up`, `Down`, one control each). One stick at most.
- `TouchContexts[]`: per input-catalog context, `Bindings[]` of `ControlId` and `ActionId`. The
  action must live in that context, as a 2D-axis action for a stick or a Boolean one for a
  button or swipe, and must have a gamepad binding: touch values go through it. Every action of a
  listed context needs a touch control there, and a glyph in the default `Touch` glyph set.
- `ContextsWithoutTouch[]`: catalog contexts deliberately left without touch (`World`, `Menu`).
  Every catalog context must be in exactly one of `TouchContexts` and this list.

Contexts stack as their mapping contexts do: the highest-priority active context that binds a
control decides what it does. `PSTouchControls::ValidateLayout` and `tools/validate_data.py`
check all of this. **Adding an action means adding its touch control and its Touch glyph in
the same change; adding a context means adding its touch button set (or listing it in
`ContextsWithoutTouch`).**

## Head-to-head rules schema (`FPSVersusRules`)

Single object (Epic 107; local head-to-head, `UPSVersusSubsystem`). House rules for two players on
one machine:
- `OffenseControlRole` (an offensive role), `DefenseControlRole` (a defensive one): the player each
  seat controls at the start of a down on that side.
- `bResetControlEachDown`: every new down puts both players back on those roles; off, a player
  keeps whoever they had while their side is unchanged.
- `bDefenseSwitchDuringPlay`: the defending player may switch after the snap; off, they keep the
  defender they had at the snap (a takeaway's ball carrier is still theirs to take).
- `bDefensePreSnapPicks`: the defending player may use the pre-snap pick buttons.
- `Screen` (`Shared` or `Split`): one view both players watch, or one each.
- `RouteArtAudience`, `DefensiveIconsAudience` (`Everyone`, `OwnerOnly`, `Nobody`): who may see the
  offense's route art (Epic 27) and the defense's assignment icons (Epic 31) before the snap.
  `OwnerOnly` is that side's own view on a split screen and nobody on a shared one.
- `PausesPerHalf` (-1 for no limit, else 0 or more): pauses each player may call per half.
- `bPauseOnlyBetweenPlays`: no pausing while the ball is live (a disconnect still pauses).
- `bResumeNeedsBoth`: play resumes once both players are ready; off, the player who paused
  resumes alone. `ResumeCountdownSeconds` (0 or more) then counts down, still paused.
- `bPauseOnDisconnect`: a seat's controller disconnecting pauses the game, which can't resume
  until it is back. `bQuitForfeits`: quitting forfeits the game to the other player.

`UPSVersusSubsystem::ValidateRules` and `tools/validate_data.py` check it.
