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

## League config schema (`FPSLeagueConfig`)

Single JSON object (not an array): `LeagueName`, `NumWeeks`, `ByeWeekNumbers` (int array),
`NumPlayoffTeams`, `TeamsDataTablePath`.

## Playbook schema (`FPSPlayDefinition` / `FPSRoute`)

See `Source/PlaySports/Public/PSPlaybookData.h` for the full assignment/route shape. Every
`Route`-kind assignment's `RouteId` must exist in `sample_routes.json`.

## Adding a new team

1. Add a `rosters/team_<name>.json` roster file following the player schema above (aim for at
   least one player per `EPlayerRole`).
2. Add an entry to `sample_teams.json` pointing `RosterDataTablePath` at it.
3. Run the content commandlet (or `ValidatePlayersJson`/`ValidateTeamsJson` directly) before
   committing -- CI's "Validate data contracts" step does not currently know about this
   commandlet, so validate locally.
