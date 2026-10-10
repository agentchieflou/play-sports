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
| `loose_ball.json` | `FPSLooseBallTuning` (single object) | `UPSDataIngestion::LoadLooseBallTuningFromJson`, via `UPSLooseBallSubsystem` |
| `deception.json` | `FPSDeceptionTuning` (single object) | `UPSDataIngestion::LoadDeceptionTuningFromJson`, via `UPSDeceptionSubsystem` |
| `play_recognition.json` | `FPSPlayRecognitionTuning` (single object: classifier geometry, `FormationClasses`, key and read tuning) | `UPSDataIngestion::LoadPlayRecognitionTuningFromJson`, via `UPSPlayRecognitionSubsystem` |
| `coverage_matchups.json` | `FPSCoverageMatchupTuning` (single object: tuning, `Shells`, `DefaultShell`) | `UPSDataIngestion::LoadCoverageMatchupTuningFromJson`, via `UPSCoverageMatchupSubsystem` |
| `presnap_tuning.json` | `FPreSnapTuningRow` (single object) | `UPSDataIngestion::LoadPreSnapTuningFromJson`, via `UPSPreSnapSubsystem` |
| `input_buffer.json` | `FInputBufferTuningRow` (single object: `MaxQueued`, `Actions`) | `UPSDataIngestion::LoadInputBufferTuningFromJson`, via `UPSInputBufferComponent` |
| `defensive_techniques.json` | `FDefensiveTechniqueTuningRow` (single object) | `UPSDataIngestion::LoadDefensiveTechniquesFromJson`, via `UPSDefenderTechniqueComponent` |
| `kick_meter.json` | `FKickMeterTuningRow` (single object) | `UPSDataIngestion::LoadKickMeterTuningFromJson`, via `UPSKickMeterComponent` |
| `ui_settings.json` | `FPSSettingsCatalog` (single object: `Categories`, `Settings`) | `UPSDataIngestion::LoadSettingsCatalogFromJson`, via `UPSSettingsSubsystem` |
| `ui_accessibility.json` | `FPSUIAccessibilityTuning` (single object) | `UPSDataIngestion::LoadUIAccessibilityTuningFromJson`, via `UPSUIAccessibilitySubsystem` |
| `ui_text.csv` | UE string table `PSUI` (CSV: `Key`, `SourceString`, `Comment`) | `UPSLocalization::RegisterStringTables` (`LOCTABLE_FROMFILE_GAME`) |
| `ui_text_data.csv` | UE string table `PSUIData`, **generated** by `tools/ui_text.py` | same |
| `ui_hints.json` | `FPSHintCatalog` (single object: `Hints`) | `UPSDataIngestion::LoadHintCatalogFromJson`, via `UPSUIHintSubsystem` |
| `pass_rush_moves.json` | `FPSRushMoveCatalog` (single object: `RushMoves` plus the rush plan's tuning) | `UPSDataIngestion::LoadRushMovesFromJson`, via `UPSRushMoveComponent` |
| `session_telemetry.json` | `FPSSessionTelemetryTuning` (single object) | `UPSDataIngestion::LoadSessionTelemetryTuningFromJson`, via `UPSSessionTelemetrySubsystem` |
| `run_fits.json` | `FPSRunFitCatalog` (single object: `Fronts`, `DefaultFront` plus the fit tuning) | `UPSDataIngestion::LoadRunFitsFromJson`, via `UPSDefenderGapSubsystem` |
| `camera_all22.json` | `FPSAll22CameraTuning` (single object: `All22Rigs`, framing tuning) | `UPSDataIngestion::LoadAll22CameraTuningFromJson`, via `UPSCameraAll22Component` |
| `camera_director.json` | `FPSCameraDirectorTuning` (single object: `Shots`, `CutRules`, `Interest`, constraints) | `UPSDataIngestion::LoadCameraDirectorTuningFromJson`, via `UPSCameraDirectorComponent` |
| `camera_skycam.json` | `FPSSkycamTuning` (single object) | `UPSDataIngestion::LoadSkycamTuningFromJson`, via `UPSCameraSkycamComponent` |
| `replay.json` | `FPSReplayTuning` (single object) | `UPSDataIngestion::LoadReplayTuningFromJson`, via `UPSReplaySubsystem` |
| `highlights.json` | `FPSHighlightTuning` (single object) | `UPSDataIngestion::LoadHighlightTuningFromJson`, via `UPSHighlightSubsystem` |
| `telestrator.json` | `FPSTelestratorTuning` (single object) | `UPSDataIngestion::LoadTelestratorTuningFromJson`, via `UPSTelestratorSubsystem` |
| `photo_mode.json` | `FPSPhotoModeTuning` (single object) | `UPSDataIngestion::LoadPhotoModeTuningFromJson`, via `UPSPhotoModeSubsystem` |
| `input_glyphs.json` | `FPSInputGlyphCatalog` (single object: `GlyphSets`) | `UPSDataIngestion::LoadInputGlyphsFromJson`, via `UPSInputGlyphs` (owned by `UPSInputConfig`) |
| `touch_controls.json` | `FPSTouchLayout` (single object: `SafeZone`, `TouchControls`, `TouchContexts`, ...) | `UPSDataIngestion::LoadTouchLayoutFromJson`, via `UPSTouchInputComponent` |
| `situational_tuning.json` | `FPSSituationalTuning` (single object: `Tempos`, `SituationTempos`, `CategoryWeights`, ...) | `UPSDataIngestion::LoadSituationalTuningFromJson`, via `UPSSituationAI` (owned by `UPSCoachingAI`) |
| `special_teams.json` | `FPSSpecialTeamsTuning` (single object: kickoff, punt, field-goal, block, return, fake and AI fields) | `UPSDataIngestion::LoadSpecialTeamsTuningFromJson`, via `UPSSpecialTeamsModel` (owned by `UPSPlaySimulation`) and `UPSSpecialTeamsAI` (owned by `UPSCoachingAI`) |
| `coaching_staffs.json` | `FPSCoachingLeague` (single object: `Schemes`, `Coaches`, `Staffs`, `Tuning`) | `UPSDataIngestion::LoadCoachingLeagueFromJson`, via `UPSStaffManager` |
| `morale.json` | `FPSMoraleTuning` (single object: morale inputs, effects, event thresholds, `Units`) | `UPSDataIngestion::LoadMoraleTuningFromJson`, via `UPSLockerRoom` |
| `draft.json` | `FPSDraftTuning` (single object: prospect uncertainty, `CombineDrills`, scouting, the CPU's board, the rookie scale) | `UPSDataIngestion::LoadDraftTuningFromJson`, via `UPSDraft` |
| `training.json` | `FPSTrainingTuning` (single object: allocation, development, funding, gameplan `FocusAreas`, fatigue, `PracticeInjury`, recommendation fields) | `UPSDataIngestion::LoadTrainingTuningFromJson`, via `UPSWeeklyPreparation` |
| `legacy.json` | `FPSLegacyTuning` (single object: `HallOfFame`, `LeaderCategories`) | `UPSDataIngestion::LoadLegacyTuningFromJson`, via `UPSLeagueHistory` |
| `owner_economics.json` | `FPSEconomyTuning` (single object: gate, media, fan and budget fields, `DefaultBudget`) | `UPSDataIngestion::LoadEconomyTuningFromJson`, via `UPSOwnerEconomy` |
| `contracts.json` | `FPSContractTuning` (single object: cap, contract rules, demand, offer and free-agency fields, `PositionMarkets`) | `UPSDataIngestion::LoadContractTuningFromJson`, via `UPSContractManager` (and `UPSFreeAgency`) |
| `telemetry_sampling.json` | `FPSTelemetrySamplingTuning` (single object) | `UPSDataIngestion::LoadTelemetrySamplingTuningFromJson`, via `UPSTelemetrySamplingSubsystem` |
| `overlay_reticle.json` | `FPSOverlayReticleStyle` (single object: colors, mesh, `ReticleStates`) | `UPSDataIngestion::LoadOverlayReticleStyleFromJson`, via `UPSOverlayReticleComponent` |
| `control_handoff.json` | `FControlHandoffTuningRow` (single object) | `UPSDataIngestion::LoadControlHandoffTuningFromJson`, via `UPSControlHandoffComponent` |
| `broadcast_overlay.json` | `FPSBroadcastOverlayTheme` (single object: colors, sizes, thresholds, `ChyronKinds`) | `UPSDataIngestion::LoadBroadcastOverlayThemeFromJson`, via `UPSOverlayBroadcastSubsystem` |
| `personnel_panel.json` | `FPSPersonnelPanelStyle` (single object: the roles each panel counts, naming rules, colors) | `UPSDataIngestion::LoadPersonnelPanelStyleFromJson`, via `UPSOverlayPersonnelSubsystem` |
| `ball_flight_overlay.json` | `FPSBallFlightStyle` (single object: colors, meshes, arc and ring sizes, goal posts, readout labels) | `UPSDataIngestion::LoadBallFlightStyleFromJson`, via `UPSOverlayBallFlightSubsystem` |
| `overlay_badges.json` | `FPSOverlayBadgeStyle` (single object: `Groups`, `RoleLabels`, sizes and layout rules) | `UPSDataIngestion::LoadOverlayBadgeStyleFromJson`, via `UPSOverlayBadgeComponent` |
| `player_emphasis.json` | `FPSEmphasisStyle` (single object: `Kinds`, `DimStencil`, `MaxEmphasized`) | `UPSDataIngestion::LoadEmphasisStyleFromJson`, via `UPSOverlayEmphasisSubsystem` |
| `player_dna.json` | `FPSPlayerDNACatalog` (single object: `Axes`, `Bindings`, `RushMoveLeans`, `RushStyleWeight`, `TraitThreshold`) | `UPSDataIngestion::LoadPlayerDNACatalogFromJson`, via `UPSPlayerDNASubsystem` |
| `defensive_presnap.json` | `FPSDefensivePreSnapTuning` (single object) | `UPSDataIngestion::LoadDefensivePreSnapTuningFromJson`, via `UPSDefenderPreSnapSubsystem` |
| `opponent_model.json` | `FPSOpponentModelTuning` (single object: distance buckets, read and strength tuning, `Counters`) | `UPSDataIngestion::LoadOpponentModelTuningFromJson`, via `UPSOpponentModel` |
| `versus_rules.json` | `FPSVersusRules` (single object) | `UPSDataIngestion::LoadVersusRulesFromJson`, via `UPSVersusSubsystem` |
| `ai_debug.json` | `FPSAIDebugTuning` (single object: switches, post-mortem folder and limits, overlay placement) | `UPSDataIngestion::LoadAIDebugTuningFromJson`, via `UPSAIDecisionLog` |
| `ai_scenarios.json` | `FPSAIScenarioCatalog` (single object: `Scenarios`) | `UPSDataIngestion::LoadAIScenariosFromJson`, via `UPSAIScenarioRunner` |
| `gap_overlay.json` | `FPSGapOverlayStyle` (single object) | `UPSDataIngestion::LoadGapOverlayStyleFromJson`, via `UPSDefenderGapOverlaySubsystem` |
| `league_generator.json` | `FPSLeagueGeneratorTuning` (single object: league shape, `RoleProfiles`, `NameCultures`, `NameBlocklist`, `DraftClass`) | `UPSDataIngestion::LoadLeagueGeneratorTuningFromJson`, via `UPSLeagueGenerator` |
| `playbook_generator.json` | `FPSPlaybookGeneratorTuning` (single object: `OffenseFormations`, `Concepts`, `DefensiveFronts`, `Coverages`, `Pressures`, `SchemeFlavors`, sizes) | `UPSDataIngestion::LoadPlaybookGeneratorTuningFromJson`, via `UPSPlaybookGenerator` |
| `player_progression.json` | `FPSProgressionTuning` (single object: the age curve) | `UPSDataIngestion::LoadProgressionTuningFromJson`, via `UPSLeagueGenerator` (and `UPSPlayerProgression`'s callers) |
| `difficulty.json` | `FPSDifficultyCatalog` (single object: `DifficultyTiers`, the assists' setting IDs, `SuggestedPlayAccent`) | `UPSDataIngestion::LoadDifficultyCatalogFromJson`, via `UPSDifficultySubsystem` |
| `perf_harness.json` | `FPSPerfHarnessTuning` (single object) | `UPSDataIngestion::LoadPerfHarnessTuningFromJson`, via `UPSPerfHarness`; also read by `tools/perf_budget.py` |
| `play_art.json` | `FPSPlayArtStyle` (single object) | `UPSDataIngestion::LoadPlayArtStyleFromJson`, via `UPSOverlayPlayArtSubsystem` |
| `game_intelligence.json` | `FPSGameIntelligenceTuning` (single object) | `UPSDataIngestion::LoadGameIntelligenceTuningFromJson`, via `UPSGameIntelligenceSubsystem`; its tasks are checked against `tools/orchestrator/routing.json` |
| `league_narrative.json` | `FPSNarrativeTuning` (single object: storyline rules and `StorylineKinds`, award scoring, the vote, the digest's model task) | `UPSDataIngestion::LoadNarrativeTuningFromJson`, via `UPSLeagueNarrative` |
| `audio_cues.json` | `FPSAudioTuning` (single object: `Cues`, `EventCues`, `LayerSettings`, `StartupLoops` and the moments' thresholds) | `UPSDataIngestion::LoadAudioTuningFromJson`, via `UPSAudioSubsystem`; its layers' settings are checked against `ui_settings.json` |
| `crowd.json` | `FPSCrowdTuning` (single object: the excitement model, `Levels`, `CrowdReactions`) | `UPSDataIngestion::LoadCrowdTuningFromJson`, via `UPSCrowdExcitementSubsystem` |
| `commentary_hooks.json` | `FPSCommentaryHookTuning` (single object) | `UPSDataIngestion::LoadCommentaryHookTuningFromJson`, via `UPSCommentaryEventModel`; its task is checked against `tools/orchestrator/routing.json` |
| `commentary_lines.json` | `FPSCommentaryLibrary` (single object: the booth's pacing and its `Lines`) | `UPSDataIngestion::LoadCommentaryLibraryFromJson`, via `UPSCommentaryEngine`; each line's text is `Data/ui_text.csv`'s `Commentary.Line.<LineId>` |

## Player schema (`FPlayerAttributes`)

Field names must match exactly (case-sensitive): `PlayerId`, `DisplayName`, `Role`, `WeightKg`,
`HeightCm`, `Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness`, `Stamina`. Ratings run
0-100; `WeightKg` and `HeightCm` are above 0.

`Age` is optional (Epic 122): a whole number from 18 to 50, or 0 (the same as leaving it out) for
unknown. The contract manager prices a player at his age, and at `contracts.json`'s
`DefaultPlayerAge` when it is unknown. The shipped hand-written rosters give none; generated ones
(`UPSLeagueGenerator`) always do.

`DisplayName` must not be a real person's (the no-real-person policy, Epic 122): no name on
`league_generator.json`'s `NameBlocklist`, in full or initial form ("J. Allen"), ignoring case and
punctuation. `validate_data.py` checks every roster against it.

`DNA` is optional: the player's style (`FPSPlayerDNA`, Epic 79), an object of the style axes
`player_dna.json` lists for his role, each from -1 to 1, e.g. `"DNA": { "Mobility": 0.6,
"Gunslinger": -0.2 }`. A missing axis is 0, and a player with no `DNA` plays neutral. Don't write it
by hand at roster scale: `python tools/player_dna.py --write` gives every player without one a
profile generated from his ratings (see the player DNA schema below).

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

## Player DNA schema (`FPSPlayerDNACatalog`)

Single object (Epic 79; per-athlete style, so two players rated alike play differently):
- `Axes[]`, one per style axis. Each has:
  - `Axis`: an `FPSPlayerDNA` field (`Mobility`, `Gunslinger`, `RunPower`, `RouteStyle`,
    `RushPower`, `BallHawk`), each once.
  - `Roles`: the `EPlayerRole`s it applies to; on anyone else it is ignored.
  - `LowTrait`/`HighTrait` (identifiers, unique), `LowLabel`/`HighLabel` and
    `LowDescription`/`HighDescription`: the scouting trait at each end. Its text goes into
    `ui_text_data.csv` as `Trait.<TraitId>.Label` and `.Description` (`tools/ui_text.py --write`).
  - `Generator`: how `tools/player_dna.py` generates the axis. `HighAttribute` and
    `LowAttribute` (ratings) lean it: `RatingLean` axis points per point of their difference,
    measured from the league's average for the role, plus seeded variation of standard
    deviation `Spread` (0 or more).
- `Bindings[]`: what the axes change in the AI. Each scales one numeric field (`Field`) of a
  tuning (`Target`: `SkillAI` = `skill_ai_tuning.json`, `Pocket` = `pocket_tuning.json`,
  `DefenderAI` = `defense_ai_tuning.json`, `RouteRunning` = `route_running.json`, `Recognition` =
  `play_recognition.json`) by 1 at a
  neutral axis, `AtHigh` at +1 and `AtLow` at -1 (both above 0), linearly between. Each AI
  component applies its player's bindings as a play starts.
- `RushMoveLeans[]`: `Move` (a move in `pass_rush_moves.json`, once) and `Lean` (-1 finesse to +1
  power). The rush plan multiplies a move's score by `1 + RushStyleWeight * RushPower * Lean`.
- `RushStyleWeight` (0 to below 1), `TraitThreshold` (above 0, at most 1): a scout sees an axis's
  trait once the player is at least the threshold from neutral.

`PSPlayerDNA::ValidateCatalog` and `tools/validate_data.py` check it; `validate_data.py` also checks
every player's `DNA` against it.

## Opponent model schema (`FPSOpponentModelTuning`)

Single object (Epic 78; how the CPU learns the human's play-calling and counters it):
- `DistanceBuckets`: rising yards to go, the top of each bucket but the last (`[3, 7]`: short,
  medium, long). The human's calls are counted by his side, the down, the bucket, the offense's
  personnel and the category, once each play is snapped.
- `MinSamples` (1 or more): a read needs this many calls. It comes from the narrowest situation
  with enough: down, distance and personnel; down and distance; down; or everything.
- `PriorGameWeight` (0-1): what a call from an earlier game counts for (the profile save keeps
  them).
- `FirstHalfStrength`, `SecondHalfStrength` (0-1) and `HalftimeQuarter` (2 or more): how hard the
  CPU leans on its read before and after its halftime adjustments, times the adaptation dial
  (`DefaultAdaptationDial`, 0-1, until the difficulty sets one).
- `MinMultiplier` (above 0, at most 1) and `MaxMultiplier` (1 or more): no counter moves a
  category's weight outside them.
- `Counters[]`: `bOffense` (the human's side), `Observed` (a category he calls: `Run`,
  `ShortPass`, `DeepPass`, `PlayAction`, `Screen` on offense; `Base`, `Blitz`, `Prevent` on
  defense), `Counter` (a category of the CPU's side) and `Weight`. The CPU multiplies Counter's
  weight by `1 + strength * (Observed's share - an even share) * Weight`, summed over its
  counters. Only categories some counter observes are tracked.

`PSOpponentModel::ValidateTuning` and `tools/validate_data.py` check it.

## AI debug schema (`FPSAIDebugTuning`)

Single object (Epic 85; the AI's decision log, overlay and post-mortems, `UPSAIDecisionLog`):
- `bLogDecisions`, `bWritePostMortems`: on without the console variables (`ps.AI.DecisionLog`,
  `ps.AI.PostMortem`; `ps.AI.DebugOverlay` draws the overlay). Shipped off: logging costs nothing
  while off.
- `PostMortemDirectory`: a folder under `Saved/` (no `..`) for one `Play_<time>_<play>.json` per
  play; `MaxPostMortemFiles` (above 0) of them are kept, the newest.
- `MaxRecordsPerPlay` (above 0): a play keeps at most this many decisions.
- `OverlayHeightCm`, `OverlayFontScale` (above 0): where the overlay's text sits above a player,
  and its size.

## AI scenario schema (`FPSAIScenarioCatalog`)

Single object (Epic 85; scripted scenarios for `UPSAIScenarioRunner`, run by the automation test
`PlaySports.Gym.AIScenarios`):
- `Scenarios[]`, each with a unique `ScenarioId`, a `Description`, the offense's `OffenseCategory`
  (an offensive play category), `Down`, `Distance`, and the decision cycle: `Steps` (1 or more) of
  `StepSeconds` (above 0) each.
- `Players[]`: a unique `PlayerId`, an `EPlayerRole` `Role`, a `Location` (`X`/`Y`/`Z`; the line of
  scrimmage is X = 0, the offense going +X), a `Rating` (0-100, every rating; default 70), an
  optional `DNA` (as a roster's), `bHasBall`; on offense a `Route` of offsets from his spot (none:
  he blocks, or reads as QB); on defense an `Assignment` (`PassRush`, `Contain`, `ManCoverage`,
  `ZoneCoverage`, `RunFit` or `Block`), the `CoverTarget` he covers (a player of the scenario) and a
  `ZoneOffset` from his spot.
- `Expectations[]`: a player of the scenario (`PlayerId`) and the `Action` his latest decision
  must be (`FPSAIDecisionRecord::Action`: his state, or the act -- `Throw`, `Scramble`, `HandOff`,
  ...). Optionally its `Target`, and a `Heading` (`X`/`Y`/`Z`) he must be steered within
  `MaxAngleDegrees` (default 45) of.

`UPSAIScenarioRunner::ValidateScenario` and `tools/validate_data.py` check them.

## Difficulty schema (`FPSDifficultyCatalog`)

Single object (Epic 84; how hard the CPU plays and how much the game helps, `UPSDifficultySubsystem`):
- `DifficultyTiers[]`, easiest first: a unique `TierId`, a `Label` (the tiers' labels are the
  `Difficulty` setting's `Choices` in `ui_settings.json`, in the same order), and the CPU's
  capability dials, never a rating:
  - `AdaptationDial` (0-1): how far the CPU counters the human's play-calling (the opponent
    model's dial; 0 never adapts).
  - `ThrowScatterScale` (above 0): execution variance, how many times as far a CPU passer's throws
    scatter as his Awareness alone makes them.
  - `Scales[]`: recognition and execution dials. `Dial` names the capability, `Target` the AI
    tuning (`SkillAI`, `Pocket`, `DefenderAI`, `RouteRunning` or `Recognition`, as in `player_dna.json`), `Field`
    one of its numbers and `Scale` (above 0) what it is multiplied by for the CPU's players, after
    their style. Each field once per tier.
- `DifficultySetting`, `PassLeadSetting`, `AutoSlideSetting`, `SuggestedPlaySetting`: the settings
  that pick the tier and switch each assist (a `Choice`, then three `Toggle`s, in
  `ui_settings.json`'s Gameplay category).
- `SuggestedPlayAccent` (`#RRGGBB`): the suggested play's highlight on the play-call screens,
  drawn in the player's color vision setting.

Only the CPU's players get a tier: those on a side no human controls. A world without player
settings (a headless test, the scenario gym) has no tier and plays the AI as tuned. There is no
rubber band: nothing reads the score. `PSDifficulty::ValidateCatalog` and
`tools/validate_data.py` check it.

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
- A route may carry a `ReadOrder` (Epic 27): 1 for the quarterback's primary read, then 2, 3,
  ... down to the check-down. Leave it out for a route the play doesn't rank. Only a `Route`
  with a `RouteId` has one, and a play's ranks run 1, 2, 3, ... with no gap or repeat. The
  pre-snap route art colors routes by it (`play_art.json`); the AI doesn't read it. Several
  players repeating one role's slot share its rank.
- Any assignment may carry an `Art` block (Epic 35), the play art's annotation layer, which the
  AI ignores: `Color` (`#RRGGBB`, the assignment's art in this color instead of its read's or its
  icon's), `bEmphasis` (drawn `EmphasisScale` larger: the key route, the blitzer) and
  `BadgeLetter` (one or two capitals or digits the player wears on his position badge this play,
  where he wears no pass button). Leave out what the play doesn't set. A letter on a slot that
  several players repeat labels them all.
- `PlayCategory` may also be a clock play, `Spike` or `Kneel` (Epic 76), which the simulation
  resolves at the snap.
- The route library's own rules are under "Route schema extras" below.
- An offensive play may carry a `Deception` object (`FPSDeceptionDef`, Epic 72; omitted means
  `None`):
  - `Type`: `None`, `PlayAction`, `RPO`, `ZoneRead` or `TripleOption`.
  - `PlaySide`: `1` (right of the ball) or `-1` (left); the option's keys are on that side.
  - `PassRole`: the RPO's pass option, the first player of this role on a route.
  - `PitchRole`: the triple option's pitch man, not the QB or the RB.

  `PlayAction` goes on a pass play, not a `Run`, `Screen` or special-teams one. The run options
  are `Run` plays with a `RunningBack` on a `Route` (the QB meets him at the mesh). An RPO's
  `PassRole` must run a route, and a triple option's `PitchRole` must be in the play. Tuning is
  `deception.json` below; `tools/content_contracts.py` checks the rules.

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

## League generator schema (`FPSLeagueGeneratorTuning`, Epic 122)

`league_generator.json` is what `UPSLeagueGenerator` makes fictional leagues and draft classes
from. The same seed and tuning always make the same league. Every field is required:

- The league: `LeagueName`, `NumTeams`, `NumWeeks`, `ByeWeekNumbers`, `NumPlayoffTeams` (2 to
  `NumTeams`), `Divisions` (a team given none joins the smallest). Teams the caller doesn't supply
  get stand-ins until Epic 123 generates identities: `PlaceholderTeamName` ("League Team 05",
  TeamId and abbreviation `T05`) and `PlaceholderColors` (`#RRGGBB`, taken in turn).
- Talent: `TeamTalentSpread` (a team's talent, added to all its players'),
  `TalentPerExperienceYear` and `MaxExperienceTalent` (veterans who lasted are better).
- Ages: `EntryAgeMin`/`EntryAgeMax` (18 or more), the ages players enter the league at.
- `RoleProfiles[]`, one per `EPlayerRole`: `Role`, `RosterCount` (the shipped profiles sum to 53),
  `IdCode` (PlayerIds are `<TeamId>_<IdCode>_<NNN>`, best first), `Attrition` (above 0, below 1:
  how fast the role's age pyramid thins), `MaxAge` (`EntryAgeMax` to 50), and `Attributes[]`: one
  curve for every float field of `FPlayerAttributes` at a player's prime: `Attribute`, `Mean`,
  `StdDev`, `Min`, `Max` (0-100 for a rating, above 0 for `WeightKg`/`HeightCm`) and
  `TalentWeight` (0-1: how much of the spread is his talent, shared by his ratings; 0 for his
  body). The age curve (`player_progression.json`) then takes a younger player below his prime and
  an older one past his decline, so ratings and ages agree.
- Names: `NameCultures[]` (`Culture`, `Weight`, `FirstNames`, `LastNames`: a player's first and
  last names come from one culture), `NameBlocklist` (real people no player is named after) and
  `MaxNameAttempts` (draws for a new, unblocked name before a middle initial separates the last).
- `DraftClass`: `ProspectsPerTeam` (a class has this many per team in the league), `TalentShift`
  and `TalentSpread` (a class's talent against the league's).

DNA is generated by Epic 79's rule from `player_dna.json` (`PSPlayerDNA::GenerateProfile`),
centered on the league's own players. `validate_data.py` checks this file
(`PSLeagueGenerator::ValidateTuning` is the same check in C++).

The automation test `PlaySports.Content.LeagueGenerator.WritesValidContent` writes a generated
32-team league to `Saved/GeneratedLeague/`, laid out like `Data/`, and imports it through the
game's loaders. CI then runs `python tools/content.py check --root Saved/GeneratedLeague --strict`
on it, so a generated league passes every contract here and the content report finds nothing to
warn about.

## Playbook generator schema (`FPSPlaybookGeneratorTuning`, Epic 121)

`playbook_generator.json` is the concept grammar `UPSPlaybookGenerator` makes playbooks from,
instead of hand-authoring each play. Every field is required:

- `OffenseFormations`: the formations concepts line up in, each in a personnel package (Epic
  19.5), which says how many of each role it puts on the field. `PlayActionDrop` is the
  quarterback's spot on a play-action pass, in cm (below 0).
- `Concepts[]`: `ConceptId` (letters and digits), `Label`, `Family` (flood, mesh, dagger, zone,
  ...), `Category` (`Run`, `ShortPass`, `DeepPass` or `Screen`), `Formations` (a subset of
  `OffenseFormations`; empty for all), `QBDrop` (the quarterback's spot), `BackSpot` (a run's
  carrier's spot), `LineKind` (`PassBlock` or `RunBlock`: the line, and a tight end or back no slot
  claims) and `BacksideRoute` (what a wide receiver no slot claims runs; empty: he blocks).
  - `Slots[]`, in the quarterback's read order: `Roles` (receivers, in preference) and `Routes` (route
    library IDs). Each slot goes to the first receiver of its roles the formation still has, and its
    route gets the slot's place as its `ReadOrder` (1, 2, ...), so the play art ranks the
    progression; a backside route is unread. A concept makes a play for every combination of its
    slots' routes (at most 64) in every formation its slots fit.
  - `Deceptions`: the Epic 72 variants it is made with: `None`, `PlayAction` on a pass (a
    `PlayAction` play from `PlayActionDrop`), `ZoneRead` or `RPO` on a run.
- `DefensiveFronts[]`: `Formation` (a defensive personnel package's), `Front` (in `run_fits.json`)
  and `LineKind` (`PassRush`, or `RunFit` on the goal line).
- `Coverages[]`: `Shell` (with rules in `coverage_matchups.json`), `Label`, `Category` (`Base` or
  `Prevent`), `MaxBlitzers` (the most it can send and still cover) and `Slots[]`: `Role`, `Kind`
  (`ZoneCoverage` or `ManCoverage`) and `Zone` (a zone landmark from the ball, cm, played on the
  defender's own side). Each role's jobs are in priority order, deep help first, so a blitzer takes
  the last one.
- `Pressures[]`: `PressureId`, `Label` and `Blitzers` (`EPlayerRole` name to count). One must send
  nobody. The call sheet is every front x coverage x pressure the coverage can afford; a call that
  sends anyone is a `Blitz`.
- `SchemeFlavors[]`: per coaching identity (a `SchemeId` in `coaching_staffs.json`), how much it likes
  each concept (`ConceptWeights`, offense) or each shell and pressure (`ShellWeights` and
  `PressureWeights`, defense); 1 when unlisted.
- `OffensePlaybookSize`, `DefensePlaybookSize` and `CategoryEmphasis`: a scheme's generated book has
  this many plays, shared out by its `CategoryWeights` raised to `CategoryEmphasis` (every category
  it weighs gets one), then drawn by its flavor.

Generated plays are ordinary `FPSPlayDefinition`s with PlayIds `<SchemeId>_<ConceptId>_<Formation>_<n>`
(or `<SchemeId>_Def_<Formation>_<Shell>_<PressureId>`), so the play loader, the AI and the playbook
contract treat them like the hand-written book, and `UPSPlaybookGenerator::ValidatePlayArt` holds them
to Epic 35's art/AI consistency check (`PSPlayArt::ValidatePlayArt`). The automation test
`PlaySports.Content.PlaybookGenerator.WritesValidContent` writes every scheme's book to
`Saved/GeneratedPlaybooks/Data/playbooks/`. CI checks those books with
`python tools/content.py check --root Saved/GeneratedPlaybooks --strict`. `validate_data.py` checks
this file (`PSPlaybookGenerator::ValidateTuning` is the same check in C++).

## Age curve schema (`FPSProgressionTuning`)

`player_progression.json`: `PeakAgeStart` and `PeakAgeEnd` (the prime window, 18 to 50),
`GrowthPerYear` (rating points a younger player gains each offseason; awareness half as much),
`DeclinePerYear` (lost each offseason past the window; strength half, awareness a quarter as
much), `LowSnapShareThreshold` (a backup under this snap share grows half as fast).
`UPSPlayerProgression::ApplyOffseasonProgression` applies it.

## Adding a new team

1. Add a `rosters/team_<name>.json` roster file following the player schema above. A live game
   fields each side from its team's own roster, so the roster must fill every package in
   `personnel_packages.json`: at least 1 QB, 2 RB, 2 TE, 4 WR, 5 OL, 6 DL, 4 LB and 6 DB, plus
   depth for fatigue substitutions (the Falcons carry 31, the other shipped teams 39).
   `python tools/content.py report` warns about a package a team can't field, and a unit test
   fails the build on one for shipped teams. `python tools/player_dna.py --write` gives new
   players their DNA.
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
  the `EInputActionValueType` names), `Description`, `Contexts` (IDs above), `Bindings[]`,
  optional `bTriggerWhenPaused` (default false; `UInputAction::bTriggerWhenPaused`). Enhanced
  Input drops every other action while the game is paused, so the actions used over a paused
  game set it: `Move`, `Pause` and the `Replay` buttons (a replay pauses the game under it).
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
- `PassDropDepth`: a run-fit defender who reads pass (`play_recognition.json`'s keys, Epic 80)
  drops to this depth past the line.
- `MaxReactionSeconds`: how long a defender with 0 Awareness takes to react (no delay at 100): to
  the ball coming out as it is, to his keys and a throw as `play_recognition.json`'s read scales
  stretch it.
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
  - `ReplayPoseRateHz` (0 or more): how often a replay (Epic 41) re-poses the players and the
    ball, per second; 0 is every frame.
  - `TargetFrameRate` (above 0): the frame rate the tier is budgeted for (Epic 114).
  - `SystemBudgets[]`: `System` (`Simulation`, `AI`, `Telemetry`, `Overlays`, `UI`, `Animation`,
    `Crowd`, `Audio`) and `BudgetMs` (0 or more), the game-thread ms per frame that system may use.
    Exactly one per system; together they fit in a frame (1000 / `TargetFrameRate`), and the
    `Telemetry` budget covers `TelemetrySampleBudgetMs`. The profiling harness and CI hold the
    measured times to them (`Specs/Platform_Audit.md` section 7).
  - `PlayArtRefreshHz` (0 or more): how often a second the pre-snap route art (Epic 27) resolves
    the call again to follow the players as they shift and go in motion; 0 rebuilds it only on
    events (a call, a hot route, a new spot).
  - `AudioUpdateHz` (0 or more): how often a second the audio (Epic 23, `UPSAudioSubsystem`)
    releases finished voices and follows the volume settings; 0 is every frame.
  - `AudioMaxVoices` (1 or more): one-shot sounds that may play at once; past it a cue takes a
    lower-priority one's voice or gives way.
  - `CrowdUpdateHz` (0 or more): how often a second the crowd's excitement (Epic 23.2,
    `UPSCrowdExcitementSubsystem`) settles and is re-rated; 0 is every frame.
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

## Hints schema (`FPSHintCatalog`)

Single object (Epic 105.4; first-time hints on the play-call screen, `Specs/Front_End_Shell.md`):
- `Hints[]`, each with:
  - `HintId` (unique): the profile remembers it once shown (`UPSProfileSaveGame::SeenHints`);
  - `Trigger`: an `EPSHintTrigger`:
    - `OffenseCall` and `DefenseCall`: the human's first call on that side;
    - `FourthDown`: the human's offense on 4th down;
    - `TwoMinuteDrill`: the human's offense in a two-minute drill, as `UPSSituationAI` reads it;
    - `Kickoff`: the human's side kicking off;
  - `Text`: what the hint says. Keep it free of button names, so it reads the same on every
    device. It is translated through `ui_text_data.csv` (`Hint.<HintId>`).

Hints are tried in order and the first that applies and hasn't been seen comes up, so the
specific ones go first. The `Hints` setting (Gameplay, `ui_settings.json`) turns them off.
`UPSUIHintSubsystem::ValidateCatalog` and `tools/validate_data.py` check it.

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
  `HUD.Phase.<Phase>`, `HUD.Score.<ScoreType>`, `PlayCall.Category.<PlayCategory>` (a
  category without a row shows its ID split into words), `Broadcast.DriveResult.<Result>` (a
  drive result without a row is shown as the simulation wrote it). The broadcast overlays'
  own words are `Broadcast.*` (the score bug and its chyrons) and `Personnel.Count*`.
- `ui_text_data.csv` (table `PSUIData`) is **generated** from the user-facing strings of
  `ui_menus.json`, `ui_settings.json`, `loading_tips.json`, `defensive_adjustments.json`,
  `ui_hints.json` and the broadcast overlays' data (`broadcast_overlay.json`,
  `ball_flight_overlay.json`, `overlay_badges.json`, `personnel_panel.json`,
  `personnel_packages.json`). Don't edit it. After changing one of those files, run `python tools/ui_text.py --write`.
  Keys: `Menu.<ScreenId>.Title|Body`, `Menu.<ScreenId>.<OptionId>.Label|Detail`,
  `Setting.Category.<CategoryId>`, `Setting.<SettingId>.Label|Description|Unit|Choice<Index>`,
  `Tip.<TipId>`, `Adjustment.<AdjustmentId>.Label|Description`, `Hint.<HintId>`,
  `Broadcast.HomeLabel|AwayLabel`, `BallFlight.<Verdict>Label`, `Badge.Role.<Role>`,
  `Personnel.Role.<Role>`, `Personnel.OffenseNameFormat`, `Personnel.DefenseName.<Backs>`,
  `Personnel.DefenseNameFallback`, `Personnel.Package.<PackageId>`. A name pattern's
  `{Label}` placeholders are the style's own labels; a translation keeps them.
- Not translated, shown through `UPSLocalization::Verbatim`:
  - names: team, player, play, formation, front, coverage and route names;
  - button glyph labels (`input_glyphs.json`) and the engine's key names;
  - for now, text other systems write in English: the coaching AI's suggestion reasons, the
    situation AI's moments and the tempo labels (`situational_tuning.json`).
- UI strings may not contain backslashes, because the string table import reads them as
  escapes. A real newline is fine.

`tools/validate_data.py` (through `tools/ui_text.py`) fails on any of these:
- a stale `ui_text_data.csv`;
- a key the code names that isn't in `ui_text.csv`;
- a remappable action, or one of its contexts, without a name row;
- duplicate or empty keys, or unbalanced placeholders;
- FText built from a raw string in UI code (`Private/PSUI*`, `PSMenu*`, `PSHUD*`, `PSLoading*`,
  `PSSettings*`, `PSPlayCall*`, and the broadcast overlays' `PSOverlay*` and
  `PSGameStateEvents*`).

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

## Contract tuning schema (`FPSContractTuning`)

Single object (Epic 87), read by `UPSContractManager`. Money is in thousands of dollars (whole
numbers): `255000` is a $255 million cap. The contracts themselves live in the franchise save
(`UPSFranchiseSaveGame::ContractLedger`), not here.
- The cap: `FirstLeagueYear`, `SalaryCap` (the first year's), `CapGrowthRate` (per rollover),
  `MinimumSalary`, `MaxContractYears`, `MaxProrationYears` (a bonus spreads over at most this many
  years), `MaxCarryoverFraction` (unused space carried into the next year, as a fraction of the cap).
- Demands (`UPSContractNegotiation`): `ReplacementRating` asks the minimum and `EliteRating` his
  role's top of the market along `DemandCurveExponent`; `PrimeAge` and `YearsLostPerYearPastPrime`
  (deal length), `DeclineAge`, `AgeDiscountPerYear`, `MinAgeMultiplier` (value with age);
  `MinGuaranteeFraction`/`MaxGuaranteeFraction`; the market's `MarketSpaceWeight`,
  `NeutralCapSpaceFraction`, `MaxMarketAdjustment`.
- Offers: `MoraleLoyaltyWeight` (morale on his own team's offers), `GuaranteeValueWeight`,
  `YearsMismatchPenalty`, `AcceptRatio`, `WalkAwayRatio` (counter at or above, reject below).
- Free agency (`UPSFreeAgency`): `FreeAgencyDays`, `DecisionDays`, `InstantAcceptRatio`,
  `DemandDecayPerDay`, `DemandFloorFraction`, the CPU's `AIBidRatio`, `AINeedPremium`,
  `AICapCushionFraction`, `AIOffersPerDay`, and `DefaultPlayerAge` (the age of a player whose
  roster gives none; `UPSContractManager::GetPlayerAge`).
- `PositionMarkets[]`: one per `EPlayerRole`: `Role`, `TopCapFraction` (an elite player's ask as
  a fraction of the cap), `RosterTarget` (how many a team wants; fewer is a free-agency need).

`tools/validate_data.py` checks it: every role has one market, the bounds are ordered and the
offer ratios run walk-away <= accept <= instant.

## Morale schema (`FPSMoraleTuning`)

Single object (Epic 91), read by `UPSLockerRoom`. Morale runs 0-1 (0.5 neutral): 0.5 plus each input,
eased from last week's by `MoraleInertia`. Each player's morale and flags and each unit's lineup live
in the franchise save (`UPSFranchiseSaveGame::LockerRoom`). How many start at a role comes from the
default packages in `personnel_packages.json`, not from here.
- Inputs: `StarterBonus`, `BackupPenalty`, `BetterThanStarterPenalty` (playing time);
  `TeamSuccessWeight` x (win% - 0.5); `UnderpaidRatio`, `UnderpaidPenalty`, `WellPaidBonus`,
  `ContractYearPenalty` (pay against his worth, his demand from `contracts.json`); `LeaderBoost` per
  leader in the room, up to `MaxLeaders`.
- Effects: `PerformanceSwing` (ratings up or down by this much at morale 1 or 0).
- Events: a trade request after `TradeRequestWeeks` weeks under `TradeRequestMorale`; a holdout at a
  new league year by a player rated `StarRating`+, paid under `HoldoutPayRatio` of his worth, with
  morale under `HoldoutMorale`; a leader: a starter with `LeaderAwareness`+ on a team winning
  `LeaderWinPercentage`+, with morale `LeaderMorale`+.
- `Units[]` (chemistry): `Unit`, `Role` (its starters), `FullCohesionGames`, `MaxBonus`.

`tools/validate_data.py` checks it: fractions at most 1, inertia and swing below 1, each unit's role,
games and bonus.

## Training schema (`FPSTrainingTuning`)

Single object (Epic 90), read by `UPSWeeklyPreparation`. Each player's freshness and injury and each
team's practice and gameplan live in the franchise save (`UPSFranchiseSaveGame::Training`), not here.
- Allocation: `DefaultAllocation` (`Develop`, `Gameplan`, `Rest` shares; only the proportions
  matter); a week's intensity is `Develop` x `DevelopIntensity` + `Gameplan` x `GameplanIntensity`.
- Development: `DevelopPointsPerWeek` rating points a full week adds at weight 1, split by
  `DevelopRatings` (`Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness`), in proportion
  for a rating within `DevelopHeadroom` of 100; times the coordinator's Development
  (`MinCoachDevelopment` at 0 to `MaxCoachDevelopment` at 100, `coaching_staffs.json`).
- Funding (the training budget, `owner_economics.json`): x `FundingFloor` + (1 - `FundingFloor`) x
  the team's funding index, at most `MaxFundingMultiplier`.
- Gameplan: `GameplanBonusPerShare` x the gameplan share x funding, split between up to
  `MaxFocusAreas` focus areas, each times its relevance (the opponent's share of its categories
  against an even mix, at most `MaxRelevance`; `UnscoutedRelevance` with no read), at most
  `MaxGameplanBonus`.
- `FocusAreas[]`: `FocusId`, `Label`, `bVersusOffense` (prepares for the opponent's offense or
  defense), `Categories` (the opponent model's play categories on that side: `Run`, `ShortPass`,
  `DeepPass`, `PlayAction`, `Screen`; `Base`, `Blitz`, `Prevent`), `Roles` it lifts, `Ratings`
  (how much of the bonus each rating takes).
- Fatigue (freshness 0-1, Core 19's stamina ratio): `GameFatigue` a game, `PracticeFatigue` a
  full-intensity week, each less `StaminaFatigueRelief` x Stamina / 100 of it; `WeeklyRecovery`
  every week plus `RestRecovery` x the rest share; `FatiguePerformanceSwing` below his ratings at 0.
- `PracticeInjury` (`FPSInjuryTuning`, Core 19's injury model): `BaseInjuryChance` a player's chance
  in a full-intensity week (times the week's intensity), `MaxFatigueMultiplier` at freshness 0,
  `MinRecoveryWeeks`..`MaxRecoveryWeeks` out; `RandomSeed` seeds each team's week.
- The recommendation (every team without its own choice, CPU or not): `AIRestShift` from development
  to rest under `AIRestFreshness` average freshness, `AILateGameplanShift` to the gameplan from
  `AILateSeasonProgress` of the season, the `AIFocusAreas` most relevant focus areas.

`tools/validate_data.py` checks it: fractions at most 1, the bonus and swing below 1, the injury
tuning's ranges, each focus area's roles, rating weights and categories (each one
`opponent_model.json` tracks on its side).

## Draft schema (`FPSDraftTuning`)

Single object (Epic 86), read by `UPSDraft`. The class itself comes from `league_generator.json`'s
draft-class mode (Epic 122); the class, every team's scouting and the picks live in the franchise
save (`UPSFranchiseSaveGame::Draft`). Grades are the contract market's overall rating (0-100);
money is in thousands of dollars.
- `NumRounds`.
- Prospects: the public projection is the true grade plus a hidden error drawn with
  `PublicUncertainty` (`CombineCertainty` times it for a combine attendee); `BoomBustChance` of a
  class is off by a further `BoomBustSwing` either way. `ProDayShare` skip the combine: their
  measurables are seen only by teams that scout them.
- `CombineDrills[]`: `DrillId`, `Label`, `Attribute` (the rating it reads), result `Base` +
  `PerPoint` x the true rating + a draw of `Noise`.
- Scouting: `PointsPerSeason` at average scouting funding (x `FundingFloor` + (1 - `FundingFloor`) x
  the owner economy's index, at most `MaxFundingMultiplier`); a report costs `ReportCost` and reads
  the true grade within `ReportNoise`, or `MisleadChance` of the time a further `MisleadSwing` off; a
  range is the estimate +/- `RangeSigmas` of its uncertainty.
- The CPU: scouts the `AIScoutTargets` best-projected; a pick's value is its estimate plus
  `NeedWeight` x its need at his role (`contracts.json`'s `RosterTarget`).
- Rookies: `RookieYears`; `FirstPickSalary` falling to the minimum salary as
  (1 - t)^`RookieScaleExponent`; guarantees from `FirstPickGuarantee` to `LastPickGuarantee`.

`tools/validate_data.py` checks it: positive uncertainties and costs, 0-1 shares and guarantees,
each drill reading a rating, `RookieYears` within `contracts.json`'s `MaxContractYears`.

## Legacy schema (`FPSLegacyTuning`)

Single object (Epic 94), read by `UPSLeagueHistory`. The archive itself (every finished season, every
retired player, the hall of fame) lives in the franchise save (`UPSFranchiseSaveGame::History`), not
here.
- `HallOfFame`: a retired player is voted in once `WaitSeasons` seasons have passed since he retired,
  if he played `MinSeasons` seasons and his hall score reaches `InductionScore`; at most
  `MaxInducteesPerSeason` a season, the best first. His hall score is his best category: the highest
  of his career totals over their `Thresholds[]` (`Category`, a player `EPSStatCategory`;
  `CareerValue`).
- `LeaderCategories`: the player categories whose season leader each season's archive keeps.
- `RoleCurves[]` (read by `UPSPlayerAging`): `Role` and its `Curve`, Core 19's `FPSProgressionTuning`
  (`PeakAgeStart`, `PeakAgeEnd`, `GrowthPerYear`, `DeclinePerYear`, `LowSnapShareThreshold`); a role
  not listed ages on `player_progression.json`'s curve.
- `Retirement` (`UPSPlayerAging`): from `MinAge`, `BaseChance` plus `ChancePerYear` a year past it,
  plus `LowRatingChance` under `LowRating`, `InjuredChance` when hurt at the season's end,
  `LowMoraleChance` under `LowMorale`; always at `ForcedAge`; at most `MaxRetirementShare` of a
  roster a season (the forced always); `RandomSeed`.

`tools/validate_data.py` checks it: whole-number waits, a positive score, each threshold and leader
a player category listed once, one curve per role with its peak in order, 0-1 chances, `ForcedAge`
above `MinAge`.

## Owner economics schema (`FPSEconomyTuning`)

Single object (Epic 95), read by `UPSOwnerEconomy`. Team money is in thousands of dollars, as the
salary cap's; ticket prices and concessions are in dollars. Each team's price, fans, budget and
revenue live in the franchise save (`UPSFranchiseSaveGame::Economy`), not here.
- The gate: `StadiumCapacity`; `BaseTicketPrice` and the owner's range `MinTicketPrice`..`MaxTicketPrice`;
  the crowd fills `BaseFillRate` of the stadium, plus `WinFillWeight` x (win% - 0.5), plus
  `SatisfactionFillWeight` x (satisfaction - 0.5), less `PriceElasticity` x (price / base - 1), at
  least `MinFillRate`; `ConcessionsPerFan`.
- `MediaRevenuePerTeam`: each team's media share a season.
- Fans (0-1): `StartingSatisfaction`, `WinSatisfactionGain`, `LossSatisfactionLoss`,
  `PriceSatisfactionLoss` (a home game at a price above the base), `WinningSeasonSatisfactionGain`,
  `LosingSeasonSatisfactionLoss`; relocation pressure after `RelocationLosingSeasons` losing
  seasons in a row with fans below `RelocationSatisfactionThreshold`.
- Budget: `MaxBudgetFraction` of revenue at most, all departments; `DefaultBudget`
  (`ScoutingFraction`, `TrainingFraction`, `StaffFraction`).

`tools/validate_data.py` checks it: ordered prices and fill rates, fractions at most 1, the default
budget within `MaxBudgetFraction`.

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

## Coverage matchup schema (`FPSCoverageMatchupTuning`)

Single object (Epic 69; the coverage matchup engine, `UPSCoverageMatchupSubsystem`). Every number
is 0 or more; distances are cm, shares and chances 0-1:
- Press: `PressDepth`, `PressShade`: a pressing back lines up this far in front of his receiver
  and this far to his leverage side (the spot must be inside `route_running.json`'s
  `PressRadius`, or the release contest would not find him). `PressAlignWidth`: the receiver he is
  over is the nearest within this distance across the field. `PressMinJamChance`: the CPU presses
  only when the back's chance to win the jam (one minus the receiver's release chance) is at least
  this. `PreSnapArrivalRadius`: walking up, this close is there. `PressCushion`: a back who won his
  jam trails this close (in place of `ManCushion`); `PressBeatenSeconds`: one who lost it is out of
  phase this long.
- Leverage: `LeverageShade`: a man defender plays this far to his leverage side.
  `LeverageLostMargin` / `LeverageRegainMargin`: the receiver this far across his face takes it;
  the defender this far back on his side has it again. `LeverageBiteBonus`: added to a double
  move's bite chance for a fake toward the leverage (taken off for one away). `BreakMinLateral`:
  a break (or fake) whose leg is at least this much across the field counts as toward or away.
  `IntoLeverageSeparationScale`: the share of a break's separation (Epic 68) kept when it goes into
  the leverage; `AwayFromLeverageBonus`: added when it goes away. `SeparationRecoverySpeed` (above
  0): the defender is out of phase for the separation over this, at most `MaxOutOfPhaseSeconds`.
- Zones: `CarryMargin`: a zone defender keeps his receiver until he is this far outside the zone
  (`defense_ai_tuning.json`'s `ZoneRadius`), playing `ZoneCarryCushion` downfield of him;
  `VerticalCarryDepth`: one leaving this far past the defender's spot is carried on (vertical)
  when no deeper zone is free to take him; any other is passed off.
- Safety help: `DeepZoneDepth`: a zone this far past the line is deep. `OverTopCushion`,
  `DeepHelpWidth`, `DeepShadeWeight`: a deep defender stays this far deeper than the deepest
  receiver within `DeepHelpWidth` across of his spot, shaded this share toward him.
  `FieldWidth` (above 0): when a deep defender leaves the deep zones, the rest split this evenly.
  `FreeDeepDepth`: a free deep-middle defender's depth; `RobberDepth`, `RobberRadius`,
  `RobberJumpWeight`: a robber's depth, the window he reads, and how far he jumps the nearest
  receiver in it.
- Pass interference: `ContactRadius`, `TrailMargin`: a defender this close to the targeted
  receiver while the ball is in the air, and this much further from where it comes down than the
  receiver, is playing through him; `FlagChance`: how often the officials flag it.
- `Shells`: one rule per coverage shell (`FPSPlayDefinition::CoverageShell`) -- `Shell`,
  `Leverage` (`Inside` toward the ball, `Outside` toward the sideline), `bPress`, and `FreeRoles`
  (`DeepMiddle`, `Robber`), the jobs its left-over man defenders take, deepest first. Every shell a
  `Base`, `Blitz` or `Prevent` play in `sample_playbook.json` calls needs one; names are unique.
  `DefaultShell` is the rule for any other.

`tools/validate_data.py` checks it.

## Loose-ball schema (`FPSLooseBallTuning`)

Single object (Epic 17.4; the players play a blocked kick's loose ball, `UPSLooseBallSubsystem`).
Every number is 0 or more; distances are cm:
- `BlockedFieldGoalYards` (whole yards): a blocked field goal comes loose this far behind the line,
  at the hold (a blocked punt's recoil is `special_teams.json`'s `BlockedPuntRecoilYards`).
- `ChaseRadius`: the players this close to the loose ball go for it, whatever their call; after a
  scoop, the kicking team's players this close to the returner run him down.
- `RecoverRadius` (at most `ChaseRadius`): the nearest player this close tries to take it, with his
  fumble-recovery chance (`catch_tuning.json`'s `FumbleRecovery*`).
- `ScoopClearRadius`: a defender with no opponent this close scoops it up and returns it; one with an
  opponent on him, or anyone on the kicking team, falls on it.
- `SquirtDistance`, `RetrySeconds`: a muffed ball squirts this far, and the player who muffed it
  waits this long before trying again.
- `MaxLooseSeconds`, `MaxReturnSeconds` (above 0): the officials blow it dead where it lies (the
  defense's ball) when nobody has it by then, and a return still going after its time where the
  returner is.

`tools/validate_data.py` checks it.

## Deception schema (`FPSDeceptionTuning`)

Single object (Epic 72; play-action, RPO and option football, `UPSDeceptionSubsystem`). Every
number is 0 or more; distances are cm, chances 0-1, ratings 0-100:
- `FakeSeconds`: the QB carries out a play-action fake hand-off this long before his drop. A
  run-fit defender who hasn't seen through it by then bites: `play_recognition.json` (Epic 80)
  says who and for how long.
- `TendencyWindow` (whole, 1 or more): the offense's run share is the share of runs in its last
  this-many scrimmage calls; the recognition model expects the run from it.
- `MeshRideSeconds`: on a run option the QB rides the mesh with the back this long after the snap
  before he reads his key.
- `ReadMinSpeed` (cm/s): a key moving at least this fast is read by whom he is heading for (the
  back or the QB, the run or the pass option); slower, by whom he is nearer.
- `KeyLineDepth`: a defender this close to the line is on it. The end man on the line, on the
  play side, is the zone read's key and the triple option's dive key.
- `PitchReadRadius`, `PitchWindowDepth`: the triple option's QB, keeping it, pitches once the pitch
  key is this close to him (and nearer him than the pitch man), until he is `PitchWindowDepth`
  past the line.
- `MeshRecognizeRadius`: the QB with the ball this close to the back, behind the line, is a mesh.
  The defense sees the option and hands out option jobs.
- `DisciplineAwareness` (0-100): a defender this aware plays his option job (the read key takes the
  QB, the pitch key the pitch man). One less aware chases the ball.
- `ScrapeRadius`: when the read key crashes on the dive, an aware linebacker this close to him
  scrapes over to the QB.

`tools/validate_data.py` checks it.

## Play recognition schema (`FPSPlayRecognitionTuning`)

Single object (Epic 80; how defenders read the offense, `UPSPlayRecognitionSubsystem`). Distances
are cm, speeds cm/s; every number is 0 or more:
- The formation, read from the alignment at the snap (the offense attacks +X):
  - `UnderCenterMaxDepth`, `PistolMaxDepth` (not shallower): a QB this close behind the line is
    under center, then in the pistol; deeper, in the shotgun.
  - `BackfieldMinDepth`, `BoxHalfWidth`: anyone but the QB and the line this deep and this close to
    the ball across the field is a back; everyone else is a receiver on his side of the ball.
  - `StackWidth`: two backs this close across are an I, further apart split. `OffsetWidth`: a lone
    back further across than this is offset. Three backs are a full house.
  - `InlineWidth`: a receiver this close to the ball is in tight (a tight end here is inline);
    further out he is split. The strong side has more receivers, then more inline tight ends, then
    is the right.
  - `FormationClasses[]`, most specific first: the read is named after the first whose every
    condition holds. `ClassId` (an identifier, once, not `Unknown`); optional `QBAlignment`
    (`UnderCenter`, `Pistol`, `Shotgun`), `Backfield` (`Empty`, `Single`, `Offset`, `I`, `Split`,
    `Full`), `MinStrongSide`, `MaxWeakSide` (-1 for any), `MinTightEnds`, `MinSplitReceivers`
    (whole numbers); `RunLean` (0-1), how likely the defense thinks a run is from the look.
  - `DefaultRunLean` (0-1): the lean of a look no class matches (`Unknown`).
- The keys, read after the snap:
  - `DropKeyDepth`, `DropKeyRetreat`: the QB with the ball this far behind the line and this much
    deeper than he lined up is a drop (pass).
  - `FlowMinSpeed` (above 0): a back in the box behind the line heading downhill (more than across)
    this fast is backfield flow (run, but a fake can show it).
  - `LineKeyDistance` (above 0): the linemen on average this far forward of their stance is a run,
    this far back a pass set. A hand-off is a run.
- The reads: a defender reads a pass key in his reaction (`defense_ai_tuning.json`'s
  `MaxReactionSeconds` at his Awareness) times `PassReadScale`, a run key times `RunReadScale`,
  breaks on a throw times `ThrowReadScale` and sees through a play-action fake times
  `FakeReadScale`; `player_dna.json` binds these by style (`Target` `Recognition`). A true key beats
  backfield flow; between keys alike, the latest shown wins.
  - `TendencyWeight` (0-1): what he expects is the formation's lean pulled this far toward the
    offense's recent run share (`deception.json`'s `TendencyWindow`).
  - `ExpectationWeight`: expecting the run outright, a pass read takes `1 + ExpectationWeight`
    times as long (a run read likewise expecting the pass); the fake read between
    `1 - ExpectationWeight` and `1 + ExpectationWeight` times.
  - `LatencyJitter` (0 to below 1): each defender's fake read varies by up to this fraction,
    seeded per snap.
  - `MaxBiteSeconds`: one who hasn't seen through the fake by `deception.json`'s `FakeSeconds`
    bites, holding for the rest of his fake read, at most this long.

`PSPlayRecognition::ValidateTuning` and `tools/validate_data.py` check it.

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

## Personnel panel schema (`FPSPersonnelPanelStyle`)

Single object (Epic 29; the offense and defense personnel panels, `UPSOverlayPersonnelSubsystem`,
drawn by `UPSOverlayPersonnelWidget`). The counts are read live from the players on the field; a
package `personnel_packages.json` lists with exactly those counts goes by its `DisplayName`, any
other by the rules below.
- `OffenseRoles[]`, `DefenseRoles[]`: the roles each panel counts, in order, each a `Role` of its
  side with a `Label` ("RB 1 | TE 3 | WR 1").
- `OffenseNameFormat`: an unlisted offensive package's name; `{Label}` stands for that role's
  count (`"{RB}{TE} Personnel"` reads "13 Personnel").
- `DefenseNames[]`: an unlisted defensive package's name by its `DefensiveBacks` (each count once),
  and `DefenseNameFallback` for any other count (`"{DL}-{LB}-{DB}"`).
- `PanelColor`, `TextColor`, `FlashColor` (`#RRGGBB`; a substitution flashes the panel and the
  counts it changed toward `FlashColor` over `ChangeFlashSeconds`, 0 or more, on a Full tier),
  `FontSize`, `TitleFontSize` (1 or more), `bShowInPlay` (false: before the snap only).

`UPSOverlayPersonnelSubsystem::ValidateStyle` and `tools/validate_data.py` check it.

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

## Player emphasis schema (`FPSEmphasisStyle`)

Single object (Epic 36; `UPSOverlayEmphasisSubsystem`, which commentary, replay and coaching tips
ask to emphasize a player). It marks each emphasized player's meshes for the custom-depth pass with
a stencil value; the emphasis post-process material draws the outline, glow or dimming for that
value (`Specs/Player_Emphasis_Spec.md`).
- `Kinds[]`: exactly one each for `Highlight` (a key-player callout), `Mismatch` (a mismatch alert)
  and `Focus` (a replay's focus), with its `Stencil` (1-255) and `Priority` (of several requests on
  one player the highest wins; under the budget the highest players are drawn first).
- `DimStencil` (1-255): players dimmed by another's spotlight. All four stencils must differ.
- `MaxEmphasized` (1 or more): players emphasized at once, since each costs custom-depth draws.
  Dimmed players don't count.
- `bSpotlightDimsEmphasized`: in a spotlight, dim the other emphasized players too.

`UPSOverlayEmphasisSubsystem::ValidateStyle` and `tools/validate_data.py` check it.

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

## Replay schema (`FPSReplayTuning`)

Single object (Epic 41; how `UPSReplaySubsystem` cuts, plays, shows and saves replays). How often a
replay re-poses the field is per platform tier (`ReplayPoseRateHz` in `platform_tiers.json`):
- `PreRollSeconds`, `PostRollSeconds` (0 or more): how long before a clip's first event (a play's
  snap) and after its last (the whistle) the clip runs.
- `PlaybackRates` (each above 0 and at most 1, the first exactly 1, no repeats): the speeds the
  slow-motion control steps through; a replay starts at the first.
- `ScrubSecondsPerSecond` (above 0): how fast a held scrub button moves the playhead.
- `SaveFrameRateHz` (above 0): a saved replay keeps a scheduled frame at most this often;
  keyframes and the clip's first and last frames are always kept.
- `Cameras` (no repeats): the cameras the camera button steps through, the first being the one a
  replay opens on. `Director` (the camera director, Epic 38), `Skycam` (Epic 39), `Free` (circles
  the ball on the Move stick), or a `RigId` of `camera_all22.json` (Epic 40).
- `FreeCamMinDistanceCm` <= `FreeCamDistanceCm` (the start) <= `FreeCamMaxDistanceCm`,
  `FreeCamPitchDegrees` (above 0, below 90), `FreeCamOrbitDegreesPerSecond`,
  `FreeCamZoomCmPerSecond` (above 0): the free camera.
- `bAutoReplay`; `AutoReplayDelaySeconds`, `AutoReplayHoldSeconds` (0 or more): a play with a score
  or a turnover replays itself this long after it ends, and gives the game back this long after
  the replay's end (unless the viewer took the controls).
- `AutoReplays[]`: one rule per `Trigger` (`Score`, `Turnover`): the `Shot` (an `EPSDirectorShot`)
  the replay opens on and its `PlaybackRate` (above 0, at most 1).
- `ReducedMotionCamera`: a rig in `Cameras`. With Reduced motion on (Epic 103.5) every replay opens
  on it, the automatic ones included.

`UPSReplaySubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Highlights schema (`FPSHighlightTuning`)

Single object (Epic 42; how `UPSHighlightSubsystem` scores plays and plays the reel):
- `YardWeight`, `PointsWeight`, `TurnoverWeight`, `BrokenTackleWeight`, `WinProbabilityWeight` (0 or
  more): a play's importance is its yards (either way), points, turnover, broken tackles and swing
  in the home team's chance of winning (0 to 1), each by its weight. `MinImportance`: less is never
  a highlight.
- `ReelSize` (1 or more): the game's reel keeps its most important plays. `SeasonHighlightsKept`
  (1 or more): a franchise season keeps its most important.
- `KindShots[]`: exactly one `Shot` (an `EPSDirectorShot`) for each `Kind` (`Score`, `Turnover`,
  `BigPlay`): the angle a highlight of that kind opens on.
- `BeatLeadSeconds`, `BeatSeconds` (0 or more), `BeatPlaybackRate` (above 0, at most 1): the slow-
  motion beat, starting before the play's key moment; the rest plays in real time.
- `ClipGapSeconds`: each clip holds its end this long. `SettleAfterWhistleSeconds` (above 0): a play
  counts as over this long after its whistle when no game state has said so.
- `bPlayReelAtGameEnd`, `GameEndReelDelaySeconds`: the reel plays by itself after the final whistle.
- `WinProbability`: `MarginScale`, `TimeFloor`, `GameSeconds`, `QuarterSeconds` (above 0),
  `PossessionPoints` (0 or more): the home team's chance of winning is a logistic of the margin
  plus what the ball is worth where it is (`PossessionPoints` on the opponent's goal line, scaled
  by the yard line), divided by the square root of the share of the game left (at least
  `TimeFloor`).

`UPSHighlightSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Telestrator schema (`FPSTelestratorTuning`)

Single object (Epic 44; how `UPSTelestratorSubsystem` keeps drawings on a paused replay or the film
view):
- `FieldHeightCm`: the field's height in the world; drawings are pinned to this plane.
- `MinPointSpacing` (0 or more, a fraction of the screen): a freehand point closer than this to
  the last one kept is dropped. `MaxStrokePoints` (2 or more): the most one stroke keeps.
- `PlayerPickRadius` (above 0, a fraction of the screen): a player tap picks the player within
  this of it.
- `MaxMarks` (1 or more): the most marks on one frame.
- The drawing layer (`UPSTelestratorWidget`, `PSTelestratorLayer`). Sizes are shares of the
  screen's shorter side, so a phone and a monitor show the same drawing:
  - `MarkColor`, `AutoMarkColor` (`#RRGGBB`): hand-drawn marks and the auto-annotation's.
  - `MarkWidth` (above 0) and `MinStrokeWidth` (above 0, Slate units): a mark's width, and the
    thinnest any line is drawn.
  - `ArrowheadLength` (above 0) and `ArrowheadAngleDegrees` (above 0, below 90): an arrow's head.
  - `PlayerRingRadius` (above 0): the ring round a highlighted player. `CircleSegments` (8 or
    more): segments in a circle or ring.
  - `CursorSpeed` (above 0, shorter sides a second at full tilt), `CursorDeadZone` (0 to below
    1) and `CursorRadius` (above 0): the cursor a gamepad or the keys draw with.

`UPSTelestratorSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Photo mode schema (`FPSPhotoModeTuning`)

Single object (Epic 45; `UPSPhotoModeSubsystem`'s free camera, filters and photos):
- The camera: `MoveCmPerSecond`, `RiseCmPerSecond`, `TurnDegreesPerSecond` (all above 0) and
  `TurnStepDegrees` (0 or more: one press or swipe turns this much at once). `MaxPitchDegrees`
  (above 0, below 90). `MaxDistanceCm` (above 0): how far it flies from where photo mode began.
  `MinHeightCm`: the lowest world height it goes to.
- Zoom, roll and focus: `MinFieldOfView` < `MaxFieldOfView` (degrees, within 0 to 180) and
  `ZoomDegreesPerSecond`; `MaxRollDegrees` and `RollDegreesPerSecond`; `MinFocusCm` <
  `MaxFocusCm`, `FocusDoublingsPerSecond`, and `DefaultFocusCm` between them (the focus when the
  camera follows nobody).
- `Apertures`: f-stops, ascending, stepped through by the aperture button; a leading 0 is depth
  of field off. Photo mode starts on the first.
- `Filters[]`: `FilterId` (unique) and what it changes, each defaulting to no change:
  `Saturation` (0 is black and white), `Contrast` (both 0 or more, 1 unchanged), `Tint`
  (`#RRGGBB`, multiplied in), `WhiteTemp` (K, 6500 unchanged), `Vignette` (0 to 1).
- `Presets[]` (at least one): `PresetId` (unique) and `Filters`, a stack of filter ids. The
  filter button steps through them; photo mode starts on the first.
- `CaptureResolutionMultiplier` (1 or more) and `MaxCaptureDimension` (1 or more): a photo is the
  viewport's size times the multiplier, its longer side at most the maximum.

`UPSPhotoModeSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

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

## Defensive pre-snap schema (`FPSDefensivePreSnapTuning`)

Single object (Epic 67; how the defense lines up, disguises and adjusts before the snap,
`UPSDefenderPreSnapSubsystem`). Depths are cm past the front of the offensive line, widths cm
across the field from its centre:
- `ShellSafeties[]`: each coverage shell (a play's `CoverageShell`) and its `DeepSafeties`
  (0, 1 or 2). A shell not listed plays one.
- `TwoHighDepth`, `TwoHighWidth`: two-high safeties' spots. `SingleHighDepth`: the single-high
  safety's, in the middle. `RobberDepth`, `RobberWidth`: a safety rolled down into the box.
- `DeepSafetyDepth`: the offense counts a defender this deep as a deep safety. It must lie past
  `RobberDepth` and no deeper than the deep spots.
- `ShowBlitzDepth`: a linebacker or back showing blitz walks up to here. The offense reads a
  blitz from one within `BlitzLookDepth` of the line and `BlitzLookWidth` of its centre
  (`ShowBlitzDepth` must be within `BlitzLookDepth`). `ShowBlitzCount`: how many linebackers,
  nearest the ball, show a blitz that isn't coming.
- `CreepDelaySeconds`, `CreepSpeedScale` (0-1): when creeping blitzers start walking up, and how
  fast.
- `MaxDisguiseLeak` (0-1): at Awareness 0 a disguising safety lines up this fraction of the way
  to his real spot (none at 100).
- `DisguiseChance*`, `ShowBlitzChance*`, `CreepChance*` (0-1, `Conservative` and `Aggressive`):
  how often a CPU defense uses each disguise, from AggressionScore 0 to 1, times the involved
  defenders' average Awareness / 100.
- `bCpuShadowsTopReceiver`: on a CPU man call, its best defensive back shadows the best
  receiver.
- `AudibleAction`, `SelectAction`, `ShadowAction`, `ShowBlitzAction`, `DisguiseAction`,
  `CreepAction`: the human defense's buttons, each a different Boolean action in the input
  catalog's `DefensePreSnap` context (`DefenseAudible`, `ShadowSelect`, `Shadow`, `ShowBlitz`,
  `DisguiseShell`, `Creep`), on while he controls a defender before the snap.

`UPSDefenderPreSnapSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

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

## Gap integrity overlay schema (`FPSGapOverlayStyle`)

Single object (Epic 81; the run defense's gap integrity shown live,
`UPSDefenderGapOverlaySubsystem`):
- `bEnabledByDefault`: shown from the start. Otherwise `ps.Overlay.GapIntegrity 1` at the console
  (or `SetEnabled`) shows it.
- `RefreshSeconds` (above 0): how often the markers follow the line as it moves. Their states
  change with each `GapIntegrity` bus event.
- `MarkerHeight` (cm, 0 or more) above the gap's spot, `MarkerRadius` (cm, above 0).
- `FilledColor`, `BlockedColor`, `OpenColor`, `UnownedColor` (`#RRGGBB`): a gap whose owner is in
  it; in it but engaged with a blocker; somewhere else; and one nobody owns.
- `bEmphasizeOpenOwners`, `OpenOwnerEmphasis` (`Highlight`, `Mismatch` or `Focus`): the owner of
  an open gap is emphasized with this look (`UPSOverlayEmphasisSubsystem`).
- `bDrawDebug`: development builds draw the markers as debug rings until the editor-made marker
  exists (`Specs/Gap_Integrity_Overlay_Spec.md`).

`UPSDefenderGapOverlaySubsystem::ValidateStyle` and `tools/validate_data.py` check it.

## Profiling harness schema (`FPSPerfHarnessTuning`)

Single object (Epic 114; `UPSPerfHarness`, and `tools/perf_budget.py` in CI). The per-system
budgets themselves are per tier, in `platform_tiers.json`.
- `FrameSeconds` (above 0): seconds per simulated frame of the standard play.
- `WarmupFrames` (0 or more): pre-snap frames run before the capture starts.
- `PassFrames`, `PursuitFrames` (1 or more), `PreSnapFrames` (0 or more): the captured play, from
  the snap to the catch, from the catch to the tackle, and the next down's pre-snap.
- `HistogramBucketMs` (above 0), `HistogramBucketCount` (1 or more): each system's frame-time
  histogram. Times are reported to the bucket width.
- `MaxBusEventsPerPlay` (1 or more): the most bus events the standard play may record.
- `HardFailMultiplier` (1 or more): CI fails when a system's 95th-percentile frame time is over
  its budget times this, and warns between the budget and this.
- `RegressionTolerance`, `MinRegressionMs` (0 or more), `TrendWindow` (1 or more): CI warns of a
  regression when a system's 95th percentile is this fraction, and at least this many ms, over
  the median of its last `TrendWindow` runs recorded on main.

`UPSPerfHarness::ValidateTuning` and `tools/validate_data.py` check it.

## Play art schema (`FPSPlayArtStyle`)

Single object (Epics 27 and 31; both sides' calls drawn before the snap,
`UPSOverlayPlayArtSubsystem`, `Specs/Route_Ribbons_Spec.md`, `Specs/Defensive_Icons_Spec.md`). Sizes
are cm, colors `#RRGGBB`:
- `RibbonWidth` (above 0): a route ribbon's width. `PrimaryWidthScale` (above 0): the primary
  read's ribbon is this many times as wide.
- `GroundOffset` (0 or more): the art lies this far above the turf.
- `RingRadius` (above 0): the ring where a route ends. `BreakMarkerRadius` (0 or more): the debug
  draw's mark at a cut.
- `EmphasisScale` (above 0): an assignment the play emphasizes (its `Art.bEmphasis`) is drawn this
  many times as large.
- `ReadColors` (at least one): route colors by the play's `ReadOrder`, the first for the primary
  read; a read past the list takes the last. `UnrankedColor`: a route the play doesn't rank.
- `BranchOpacity` (0 to 1): an option route's branches, each run on one read only, are drawn this
  opaque.
- `SnapFadeSeconds` (0 or more): at the snap the art fades out over this long on a `Full` tier;
  on other tiers it goes at once, and a `Minimal` tier draws none (`platform_tiers.json`).
- `NoRouteArtCategories`: offensive `PlayCategory` values that draw no route art (kicks and clock
  plays).
- The defense's icons (Epic 31): `ZoneStarRadius` (above 0) and `ZoneStarColor`, the star at a zone
  landmark; `ManLineWidth` (above 0) and `ManLineColor`, the line from a man defender to his
  receiver; `RushArrowWidth` (above 0), `RushArrowDepth` (0 or more: how far behind the line a
  rusher's arrow reaches), `BlitzArrowColor` (the call's blitzers) and `RushArrowColor` (the other
  rushers).
- `NoDefenseArtCategories`: defensive `PlayCategory` values that draw no icons (the kicking game's).
- `bDrawDebug`: development builds draw the art as debug lines until the editor-made renderer
  exists.
- `Diagram` (`FPSPlayDiagramStyle`, Epic 102.1): the same art drawn flat as the play-call
  screen's previews (`PSPlayDiagram`, `UPSPlayDiagramWidget`; `Specs/Play_Call_Interface.md`).
  Sizes are field cm unless noted:
  - `MinFieldWidth`, `MinFieldDepth` (above 0): the diagram shows at least this much field
    across (centred on the ball) and deep; `FieldMargin` (0 or more) is kept clear round the
    drawing.
  - `WidthScale` (above 0): the art's line widths are drawn this many times as wide;
    `MinStrokeWidth` (above 0, Slate units): no line is drawn thinner on screen.
  - `PlayerRadius` (above 0): a player's ring (offense) or X (defense); `MarkWidth` (above 0):
    the width of players, blocks, guides and the line of scrimmage.
  - `OffenseColor`, `DefenseColor`: each side's players; `OpponentOpacity` (0 to 1): the other
    side's players are drawn this opaque (0 leaves them out); `GuideOpacity` (0 to 1): a zone
    defender's drop and a "go to your spot" path, in the side's color.
  - `ArrowheadLength` (above 0) and `ArrowheadAngleDegrees` (above 0, below 90): the head on
    every route, guide and rush.
  - `RunBlockStemLength`, `PassBlockStemLength` (0 or more), `BlockBarWidth` (above 0),
    `BlockColor`: a blocker's T, its stem upfield for a run block and back for a pass block.
  - `LineOfScrimmageColor`; `CircleSegments` (6 or more): segments in a ring.
  - `BackgroundColor`, `BackgroundOpacity` (0 to 1; 0 draws none): the diagram's backdrop.
  - `PreviewWidth`, `PreviewHeight` (above 0, Slate units, which scale with the display): the
    preview's size beside each play.

What a cut is comes from the route-running tuning (`BreakMinAngleDegrees` in
`route_running.json`). The `RouteArt` and `DefenseIcons` settings (Gameplay, `ui_settings.json`)
turn each side's art off; `StudyMode` shows the defense's icons to the offense too, outside
head-to-head games, where `versus_rules.json` decides.
`UPSOverlayPlayArtSubsystem::ValidateStyle` and `tools/validate_data.py` check it.

## Game intelligence schema (`FPSGameIntelligenceTuning`)

Single object (Epic 82; `UPSGameIntelligenceSubsystem`, the game's hooks for outside models through
the Epic 25 bridge):
- `ContextBudgetChars` (at least 1024, `PSGameStateSerializer::MinBudgetChars`): the most characters
  a request's game state (or post-game analysis) may take; a model reads about four to a token.
- `PlayCallTimeoutSeconds` (above 0): how long a CPU side's call waits for an outside model's play
  before calling its own.
- `MaxOpenRequests`, `MaxAnswerChars`, `MaxKeyPlays` (1 or more), `LeadersPerCategory` (0 or more):
  open requests at once, the longest answer taken, the key plays in a post-game analysis and the
  game's leaders per stat category in the game state.
- `bPostGameRequests`: ask for the drive summary and game analysis at the final whistle.
- `PlayCallTask`, `DriveSummaryTask`, `GameAnalysisTask`: the model router's task each request
  names, each one of `tools/orchestrator/routing.json`'s `tasks` (Epic 119); the play call's needs
  at least the `min_capability` of the summary's.
- `PlayCallInstructions`, `DriveSummaryInstructions`, `GameAnalysisInstructions` (not empty): what
  each request asks of the model.

`UPSGameIntelligenceSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## League narrative schema (`FPSNarrativeTuning`)

Single object (Epic 93; `UPSLeagueNarrative`, driven by `UPSFranchiseFlow`):
- `StreakMin` (2 or more): wins or losses in a row that make a streak. `RookieSurgeTopN` (1 or more):
  a rookie in a category's top this many is a story. `AwardRaceFromWeek` (1 or more) and
  `AwardRaceMargin` (0 to 1): from that week, an MVP race whose second is within that share of the
  leader is news.
- `StorylineKinds[]`: exactly one `Weight` (0 or more) for each `Kind` (`WinStreak`, `LosingStreak`,
  `RookieSurge`, `RevengeGame`, `RecordBroken`, `AwardRace`). The news and the broadcast lead with the
  heaviest.
- `MaxDigestItems`, `DigestsKept` (1 or more), `MaxBroadcastStorylines` (0 or more): a week's news
  items, the digests kept in the save, a game's storyline chyrons.
- `OffenseScoring[]`, `DefenseScoring[]` (`Category`, an `EPSStatCategory`, and `Weight`): award
  scores. `MvpWinWeight` (0 or more): the MVP's score adds his team's winning share times it.
- The season's vote: `VoterCount` (1 or more) voters, each seeing every score off by up to
  `VoterNoise` (0 or more, under 1), rank `BallotPoints.Num()` players for those points (above 0,
  never more for a lower place); `VotingSeed` with the season makes it repeatable.
- `DigestTask` (a task in `tools/orchestrator/routing.json`), `DigestInstructions`,
  `DigestContextChars` (512 or more): with Epic 82's bridge online, what a model is asked to write
  each week, and the most characters of facts it gets.

The news text itself is the string table's `Narrative.*` rows (`Data/ui_text.csv`).
`UPSLeagueNarrative::ValidateTuning` and `tools/validate_data.py` check it.

## Audio cue schema (`FPSAudioTuning`)

Single object (Epic 23.1; `UPSAudioSubsystem`, which turns gameplay events on the telemetry bus
into sound):
- `Cues[]`, each with:
  - `CueId` (unique, not `None`), `Layer` (an `EPSAudioLayer`: `Field`, `Crowd`, `Stinger`,
    `Commentary`, `Music`, `Ambience`);
  - `SoundPath`: the sound asset's object path (`/Game/Audio/Field/Whistle.Whistle`), or empty
    until an editor session imports it. An empty cue is still requested and logged; it makes no
    sound.
  - `Volume` (0-1), `Priority` (0-100: with every voice busy a cue takes the lowest-priority voice
    below it), `CooldownSeconds` (0 or more: it doesn't repeat sooner), `DurationSeconds` (above 0:
    how long it holds a voice when its sound's length isn't known);
  - `bLoop` and `LoopGroup` (a loop's group, `None` otherwise: one loop of a group plays at a time,
    outside the voice count), `bSpatial` (placed where the moment happened),
    `bScaleByIntensity` (its volume scales with the moment's force).
- `EventCues[]`: `Trigger` (an `EPSAudioTrigger` other than `Manual`: `Snap`, `Cadence`,
  `Whistle`, `Tackle`, `Hit`, `Contact`, `Throw`, `Catch`, `Fumble`, `Kick`, `Score`,
  `PlayResult`, `Flag`, `Timeout`, `GoalLine`, `CrowdLevel`, `CrowdReaction`, `QuarterEnd`, `Speech`),
  `Detail` (`None` for any, or what narrows the trigger: `Sack`, `Big`, `Deep`, `Interception`,
  `Turnover`, a score's kind, a crowd level or reaction, a spoken commentary line's `LineId`) and
  `CueId` (in `Cues`). Every rule that matches plays. A recorded commentary line is a `Speech` rule
  with its `LineId` (Epic 96); none is recorded yet.
- `LayerSettings[]`: a `Layer` (once each) and the `SettingId` of a 0-100 slider in
  `ui_settings.json` that sets its volume. A layer without one plays at full volume.
- `StartupLoops[]`: loops (in `Cues`) started when the match's world begins play: the stadium's
  ambience.
- `BigHitDamage`, `FullIntensityDamage` (above 0): a hit of `BigHitDamage` or more (Epic 139's
  damage) is `Big`; a hit's force is its damage over `FullIntensityDamage`.
- `DeepPassCm` (above 0): a pass thrown this far or farther is `Deep`.
- `MaxRequestsKept` (1 or more): requests kept in the log.

The voices and the update rate are the platform tier's (`AudioMaxVoices`, `AudioUpdateHz`).
`UPSAudioSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Crowd schema (`FPSCrowdTuning`)

Single object (Epic 23.2; `UPSCrowdExcitementSubsystem`, the one authority on the crowd's
excitement):
- `RestingExcitement` (0-1): where the excitement settles between moments; from
  `LateGameQuarter` (1 or more) on, while the margin is `CloseGameMargin` (0 or more) points or
  fewer, it rests `LateCloseBonus` (0-1) higher. `HalfLifeSeconds` (above 0): how fast it settles.
- `DefaultHomeShare` (0-1): the home team's fans' share of the stadium, unless the match sets one.
- `Levels[]`: every `EPSCrowdLevel` (`Hush`, `Murmur`, `Buzz`, `Roar`, `Eruption`) once, with its
  `MinExcitement`: `Hush` at 0, each above the quieter one's, at most 1. `LevelHysteresis` (0-1):
  how far under its threshold the excitement must fall to leave a level.
- `CrowdReactions[]`: every `EPSCrowdStimulus` (`DeepPass`, `BigGain`, `FirstDown`,
  `Incompletion`, `Touchdown`, `FieldGoalGood`, `FieldGoalMissed`, `Safety`, `Sack`,
  `Interception`, `FumbleLost`, `BigHit`, `TurnoverOnDowns`, `Flag`) once, with `FansDelta` and
  `RivalsDelta` (-1..1): the excitement the benefiting team's fans and the other team's add, each by
  its share; and `FansReaction`, `RivalsReaction` (an `EPSCrowdReaction`: `None`, `Cheer`, `Roar`,
  `Eruption`, `Gasp`, `Groan`, `Boo`, `Stunned`): what the crowd does when those fans are the
  majority.
- `BigGainYards` (1 or more), `BigHitDamage`, `DeepPassCm` (above 0): what makes a big gain, a big
  hit and a deep ball.

The update rate is the platform tier's (`CrowdUpdateHz`).
`UPSCrowdExcitementSubsystem::ValidateTuning` and `tools/validate_data.py` check it.

## Commentary hooks schema (`FPSCommentaryHookTuning`)

Single object (Epic 23.5; `UPSCommentaryEventModel`, which publishes the game's moments as
structured Commentary events):
- `BigHitDamage`, `DeepPassCm`, `TwoMinuteWarningSeconds` (above 0): a big hit, a deep pass, and
  the two-minute warning's clock.
- `MaxMomentsKept` (1 or more): moments (and model lines) kept.
- Stakes (Epic 96.1), each 0-1, added and capped at 1: `LateQuarterStakes` from `LateGameQuarter`
  (1 or more) on, `CloseGameStakes` within `CloseGameMargin` (0 or more) points,
  `CriticalDownStakes` on third or fourth down, `RedZoneStakes` at or past `RedZoneYardLine`
  (1-99), `ScoreStakes` for points, `TurnoverStakes` for a turnover.
- Novelty (Epic 96.1): the first moment of its kind this game is 1, falling to 0 by its
  `NoveltyHorizon`-th (1 or more); a play of `BigPlayYards` (1 or more) adds `BigPlayNovelty` (0-1);
  a record is 1.
- `bOfferToModels`, `ModelMoments[]` (each an `EPSCommentaryMoment` once): while Epic 82's bridge
  is online, these moments are offered to outside models as Commentary requests.
- `ModelTask` (a task in `tools/orchestrator/routing.json`), `ModelInstructions` (not empty),
  `ModelContextChars` (512 or more): what a model is asked, and the most characters of a moment's
  facts it gets.

`UPSCommentaryEventModel::ValidateTuning` and `tools/validate_data.py` check it.

## Commentary booth schema (`FPSCommentaryLibrary`)

Single object (Epic 96; `UPSCommentaryEngine`, the booth that speaks the commentary hooks' moments
through the bus's caption event):
- Pacing: a line takes its words over `WordsPerSecond` (above 0), within `MinLineSeconds` (above 0)
  and `MaxLineSeconds` (at least that). A play-by-play line still waiting after `MaxDelaySeconds`,
  or an analyst's after `ColorMaxDelaySeconds` (above 0), is dropped. A line cuts off the one being
  said when its priority is at least `InterruptMargin` (0-100) higher; otherwise it waits in its
  voice's queue of `QueueLength` (1 or more).
- `ColorWindowDelaySeconds` (0 or more): the analyst speaks from this long after the play is over
  until the snap, never over the play-by-play.
- Selection: a line's score is its `Priority`, plus `StakesWeight` x the moment's stakes and
  `NoveltyWeight` x its novelty, less `RepeatPenalty` x its uses this game (all 0 or more); lines
  within `VarietyBand` (0 or more) of the best are picked among by a stream seeded with `Seed`.
- Storylines (Epic 93): at most `MaxTalkingPointsPerGame` (0 or more) a game, at
  `TalkingPointPriority` (0-100), `TalkingPointGapSeconds` (0 or more) apart, through the string
  table's `Commentary.Storyline` (`{Headline}`, `{Body}`).
- `bUseModelLines`, `ModelLinePriority` (0-100): an outside model's line for the play (Epic 82's
  bridge) replaces the analyst's template line.
- `MaxSpokenKept` (1 or more): spoken lines kept for the log.
- `Lines[]`, each with a unique `LineId`, a `Voice` (`PlayByPlay` or `Color`), a `Moment` (an
  `EPSCommentaryMoment`), a `Priority` (0-100) and, optionally:
  - `CooldownSeconds`, `MaxPerGame`, `MaxPerSeason` (0 or more; 0 is no cap);
  - conditions: `Detail` (the moment's; `None` or missing for any), `MinDown`/`MaxDown` (0-4),
    `MinYards`/`MaxYards`, `MinStakes` (0-1), `bRequireFirstDown`, `bRequireTurnover`;
  - `bNeedsPrimary`, `bNeedsSecondary`, `bNeedsTotal`: required when its text names `{Player}`,
    `{Other}`, or `{Total}`/`{Stat}`, so a line is never said without them.

Every moment has at least one `PlayByPlay` line. A line's text is `Data/ui_text.csv`'s
`Commentary.Line.<LineId>`, naming only a moment's facts: `{Player}`, `{Other}`, `{Yards}`,
`{Points}`, `{Down}`, `{Distance}`, `{Quarter}`, `{Clock}`, `{HomeScore}`, `{AwayScore}`, `{Total}`,
`{Stat}` (a `Narrative.Stat.*` name) and `{Penalty}` (a `Commentary.Penalty.*` name). The voices'
names are `Commentary.Voice.PlayByPlay` and `Commentary.Voice.Color`. The update rate is the
platform tier's `AudioUpdateHz`. `UPSCommentaryEngine::ValidateLibrary` and `tools/validate_data.py`
check it.
