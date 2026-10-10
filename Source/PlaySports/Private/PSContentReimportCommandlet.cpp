#include "PSContentReimportCommandlet.h"
#include "PSDataIngestion.h"
#include "PSPlaybookIngestion.h"
#include "PSPlayerAttributes.h"
#include "PSPlaybookData.h"
#include "PSLeagueData.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"

namespace PSContentReimportPrivate
{
    const TCHAR* const LeagueConfigFile = TEXT("Data/sample_league_config.json");
    const TCHAR* const DefaultTeamsFile = TEXT("Data/sample_teams.json");
    // The files UPSPlayCallSubsystem loads.
    const TCHAR* const RoutesFile = TEXT("Data/sample_routes.json");
    const TCHAR* const PlaybookFile = TEXT("Data/sample_playbook.json");

    void AddProblems(TArray<FString>& OutErrors, const FString& File, const TArray<FString>& Problems)
    {
        for (const FString& Problem : Problems)
        {
            OutErrors.Add(FString::Printf(TEXT("%s - %s"), *File, *Problem));
        }
    }
}

bool UPSContentReimportCommandlet::ReimportAll(const FString& ProjectDir, TArray<FString>& OutErrors, TArray<FString>& OutLoaded)
{
    using namespace PSContentReimportPrivate;

    OutErrors.Reset();
    OutLoaded.Reset();
    UPSDataIngestion* DataIngestion = NewObject<UPSDataIngestion>();
    UPSPlaybookIngestion* PlaybookIngestion = NewObject<UPSPlaybookIngestion>();

    // League config, which names the teams file.
    FString TeamsFile = DefaultTeamsFile;
    {
        FPSLeagueConfig Config;
        if (!DataIngestion->LoadLeagueConfigFromJson(ProjectDir / LeagueConfigFile, Config))
        {
            AddProblems(OutErrors, LeagueConfigFile, { TEXT("failed to load") });
        }
        else
        {
            OutLoaded.Add(FString::Printf(TEXT("%s - league '%s', %d weeks"), LeagueConfigFile, *Config.LeagueName, Config.NumWeeks));
            if (!Config.TeamsDataTablePath.IsEmpty())
            {
                TeamsFile = Config.TeamsDataTablePath;
            }
        }
    }

    // Teams, which name the rosters.
    TArray<TPair<FName, FString>> Rosters;
    {
        const FString TeamsPath = ProjectDir / TeamsFile;
        TArray<FString> Problems;
        if (!DataIngestion->ValidateTeamsJson(TeamsPath, Problems))
        {
            AddProblems(OutErrors, TeamsFile, Problems);
        }
        else
        {
            UDataTable* TeamsTable = NewObject<UDataTable>();
            TeamsTable->RowStruct = FPSTeamInfo::StaticStruct();
            DataIngestion->LoadTeamsFromJson(TeamsPath, TeamsTable);
            TArray<FPSTeamInfo*> Teams;
            TeamsTable->GetAllRows<FPSTeamInfo>(TEXT("PSContentReimport"), Teams);
            for (const FPSTeamInfo* Team : Teams)
            {
                if (Team)
                {
                    Rosters.Emplace(Team->TeamId, Team->RosterDataTablePath);
                }
            }
            OutLoaded.Add(FString::Printf(TEXT("%s - %d teams"), *TeamsFile, Teams.Num()));
        }
    }

    // Each team's roster. A PlayerId belongs to one team: progression keys on it.
    TMap<FName, FName> PlayerTeams;
    for (const TPair<FName, FString>& Roster : Rosters)
    {
        const FString& RosterFile = Roster.Value;
        const FString RosterPath = ProjectDir / RosterFile;
        TArray<FString> Problems;
        if (!DataIngestion->ValidatePlayersJson(RosterPath, Problems))
        {
            AddProblems(OutErrors, FString::Printf(TEXT("%s (team '%s')"), *RosterFile, *Roster.Key.ToString()), Problems);
            continue;
        }

        UDataTable* PlayersTable = NewObject<UDataTable>();
        PlayersTable->RowStruct = FPlayerAttributes::StaticStruct();
        if (!DataIngestion->LoadPlayerAttributesFromJson(RosterPath, PlayersTable))
        {
            AddProblems(OutErrors, RosterFile, { TEXT("failed to load") });
            continue;
        }
        TArray<FPlayerAttributes*> Players;
        PlayersTable->GetAllRows<FPlayerAttributes>(TEXT("PSContentReimport"), Players);
        for (const FPlayerAttributes* Player : Players)
        {
            if (!Player)
            {
                continue;
            }
            if (const FName* OtherTeam = PlayerTeams.Find(Player->PlayerId))
            {
                AddProblems(OutErrors, RosterFile, { FString::Printf(TEXT("PlayerId '%s' is also on team '%s'"), *Player->PlayerId.ToString(), *OtherTeam->ToString()) });
            }
            else
            {
                PlayerTeams.Add(Player->PlayerId, Roster.Key);
            }
        }
        OutLoaded.Add(FString::Printf(TEXT("%s - %d players (team '%s')"), *RosterFile, Players.Num(), *Roster.Key.ToString()));
    }

    // Routes, then plays: every route a play runs is in the library.
    UDataTable* RoutesTable = NewObject<UDataTable>();
    RoutesTable->RowStruct = FPSRoute::StaticStruct();
    if (!PlaybookIngestion->LoadRoutesFromJson(ProjectDir / RoutesFile, RoutesTable))
    {
        AddProblems(OutErrors, RoutesFile, { TEXT("failed to load") });
    }
    else
    {
        OutLoaded.Add(FString::Printf(TEXT("%s - %d routes"), RoutesFile, RoutesTable->GetRowMap().Num()));
    }

    UDataTable* PlaysTable = NewObject<UDataTable>();
    PlaysTable->RowStruct = FPSPlayDefinition::StaticStruct();
    if (!PlaybookIngestion->LoadPlaysFromJson(ProjectDir / PlaybookFile, PlaysTable))
    {
        AddProblems(OutErrors, PlaybookFile, { TEXT("failed to load") });
    }
    else
    {
        TArray<FPSPlayDefinition*> Plays;
        PlaysTable->GetAllRows<FPSPlayDefinition>(TEXT("PSContentReimport"), Plays);
        for (const FPSPlayDefinition* Play : Plays)
        {
            if (!Play)
            {
                continue;
            }
            for (const FPSPlayAssignment& Assignment : Play->Assignments)
            {
                if (!Assignment.RouteId.IsNone() && !RoutesTable->FindRowUnchecked(Assignment.RouteId))
                {
                    AddProblems(OutErrors, PlaybookFile, { FString::Printf(TEXT("play '%s' runs route '%s', which is not in %s"),
                        *Play->PlayId.ToString(), *Assignment.RouteId.ToString(), RoutesFile) });
                }
            }
        }
        OutLoaded.Add(FString::Printf(TEXT("%s - %d plays"), PlaybookFile, Plays.Num()));
    }

    return OutErrors.Num() == 0;
}

int32 UPSContentReimportCommandlet::Main(const FString& Params)
{
    TArray<FString> Errors;
    TArray<FString> Loaded;
    const bool bClean = ReimportAll(FPaths::ProjectDir(), Errors, Loaded);

    for (const FString& Line : Loaded)
    {
        UE_LOG(LogTemp, Display, TEXT("[ContentReimport] %s"), *Line);
    }
    for (const FString& Error : Errors)
    {
        UE_LOG(LogTemp, Error, TEXT("[ContentReimport] %s"), *Error);
    }
    UE_LOG(LogTemp, Display, TEXT("[ContentReimport] Complete. %s"), bClean ? TEXT("All content valid.") : TEXT("Errors found -- see above."));
    return bClean ? 0 : 1;
}
