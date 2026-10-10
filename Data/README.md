# Data/ Content Contract

All game content (players, teams, playbooks, league config) is authored as JSON here and
loaded through `UPSDataIngestion` / `UPSPlaybookIngestion` (`Source/PlaySports/Public/PSDataIngestion.h`,
`PSPlaybookIngestion.h`) — never through a new ad-hoc parser (Architecture rule 4).

Re-import and validate everything in one action with the content commandlet (Epic 21):

```
UnrealEditor-Cmd.exe play-sports.uproject -run=PSContentReimport
```

This validates every file below and logs actionable `Row N: <field> <problem>` errors before
loading anything, so a bad row never silently produces a half-populated DataTable.

## Files

| File | Schema struct | Loader |
| --- | --- | --- |
| `sample_players.json` | `FPlayerAttributes` (array field `Players`) | `UPSDataIngestion::LoadPlayerAttributesFromJson` |
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
| `presnap_tuning.json` | `FPreSnapTuningRow` (single object) | `UPSDataIngestion::LoadPreSnapTuningFromJson`, via `UPSPreSnapSubsystem` |
| `input_buffer.json` | `FInputBufferTuningRow` (single object: `MaxQueued`, `Actions`) | `UPSDataIngestion::LoadInputBufferTuningFromJson`, via `UPSInputBufferComponent` |
| `defensive_techniques.json` | `FDefensiveTechniqueTuningRow` (single object) | `UPSDataIngestion::LoadDefensiveTechniquesFromJson`, via `UPSDefenderTechniqueComponent` |
| `kick_meter.json` | `FKickMeterTuningRow` (single object) | `UPSDataIngestion::LoadKickMeterTuningFromJson`, via `UPSKickMeterComponent` |
| `ui_settings.json` | `FPSSettingsCatalog` (single object: `Categories`, `Settings`) | `UPSDataIngestion::LoadSettingsCatalogFromJson`, via `UPSSettingsSubsystem` |
| `pass_rush_moves.json` | `FPSRushMoveCatalog` (single object: `RushMoves` plus the rush plan's tuning) | `UPSDataIngestion::LoadRushMovesFromJson`, via `UPSRushMoveComponent` |
| `input_glyphs.json` | `FPSInputGlyphCatalog` (single object: `GlyphSets`) | `UPSDataIngestion::LoadInputGlyphsFromJson`, via `UPSInputGlyphs` (owned by `UPSInputConfig`) |
| `situational_tuning.json` | `FPSSituationalTuning` (single object: `Tempos`, `SituationTempos`, `CategoryWeights`, ...) | `UPSDataIngestion::LoadSituationalTuningFromJson`, via `UPSSituationAI` (owned by `UPSCoachingAI`) |

## Player schema (`FPlayerAttributes`)

Field names must match exactly (case-sensitive): `PlayerId`, `DisplayName`, `Role`, `WeightKg`,
`HeightCm`, `Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness`, `Stamina`.

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

## Team schema (`FPSTeamInfo`)

`TeamId` (unique), `DisplayName`, `Division`, `RosterDataTablePath` (relative path to that
team's player roster JSON, loaded separately via `LoadPlayerAttributesFromJson`).
Identity for team select (Epic 101): `Abbreviation` (2-4 letters or digits), `PrimaryColor` and
`SecondaryColor` (`#RRGGBB`), `LogoPath` (soft object path; empty until logos are imported).
Team ratings are not stored: `UPSUITeamCatalog` derives them from the roster.

## League config schema (`FPSLeagueConfig`)

Single JSON object (not an array): `LeagueName`, `NumWeeks`, `ByeWeekNumbers` (int array),
`NumPlayoffTeams`, `TeamsDataTablePath`.

## Playbook schema (`FPSPlayDefinition` / `FPSRoute`)

See `Source/PlaySports/Public/PSPlaybookData.h` for the full assignment/route shape. Every
`Route`-kind assignment's `RouteId` must exist in `sample_routes.json`.

Two `PlayCategory` values are clock plays (Epic 76): `Spike` and `Kneel` (the `Clock` formation).
The CPU calls them only when the clock does (`UPSSituationAI::DecideClockPlay`), and
`UPSPlaySimulation` resolves them at the snap: a spike is an incompletion, a kneel is down for
`UPSRulesConfig::KneelYardage` with the clock running.

## Adding a new team

1. Add a `rosters/team_<name>.json` roster file following the player schema above (aim for at
   least one player per `EPlayerRole`).
2. Add an entry to `sample_teams.json` pointing `RosterDataTablePath` at it.
3. Run the content commandlet (or `ValidatePlayersJson`/`ValidateTeamsJson` directly) before
   committing -- CI's "Validate data contracts" step does not currently know about this
   commandlet, so validate locally.

## Input catalog schema (`FPSInputCatalog`)

The single source of input actions and mapping contexts (`Specs/Input_Architecture.md`).
`UPSInputConfig` builds one `UInputAction` per action and one `UInputMappingContext` per
context from it at runtime; `APSPlayerController` applies the `OnField` context when it
possesses a pawn.

- `Contexts[]`: `ContextId` (unique), `Priority` (int; higher wins on a shared key),
  `Description`.
- `Actions[]`: `ActionId` (unique), `ValueType` (`Boolean`, `Axis1D`, `Axis2D`, `Axis3D` --
  the `EInputActionValueType` names), `Description`, `Contexts` (IDs above), `Bindings[]`.
- `Bindings[]`: `Key` (an engine `EKeys` name such as `W`, `Mouse2D`, `Gamepad_Left2D`),
  optional `bSwizzleYX` (route a 1D key onto a 2D action's Y axis) and `bNegate`.

Rules enforced by `tools/validate_data.py` and `UPSInputConfig::Validate()`: every action has
at least one keyboard/mouse key and one `Gamepad_*` key in every context it is declared for,
and no key is bound to two actions in the same context.

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

`GlyphSets[]`, each: `GlyphSetId` (unique, e.g. `Xbox`), `Device` (`KeyboardMouse` or
`Gamepad`), `bDefaultForDevice` (exactly one default set per device), `bFallbackToKeyName`
(unlisted keys get a keycap with the key's name -- for keyboards), `Keys[]` (`Key` an engine
`EKeys` name of that device, `GlyphId` the icon an imported texture is registered under, `Label`
the text shown until then) and `Actions[]` (`ActionId`, `GlyphId`, `Label`: one glyph for a
whole action, such as `WASD` for Move). Which key an action uses comes from `input_actions.json`,
so a rebinding never needs a glyph edit; every key the input catalog binds must be drawable by
its device's default set, which `UPSInputGlyphs::Validate` and `tools/validate_data.py` check.

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
