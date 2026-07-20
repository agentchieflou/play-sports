#include "PSContentReimportCommandlet.h"
#include "PSDataIngestion.h"
#include "PSPlaybookIngestion.h"
#include "PSPlayerAttributes.h"
#include "PSPlaybookData.h"
#include "PSLeagueData.h"
#include "Misc/Paths.h"

int32 UPSContentReimportCommandlet::Main(const FString& Params)
{
    const FString DataDir = FPaths::ProjectDir() / TEXT("Data");
    bool bAnyErrors = false;

    UPSDataIngestion* DataIngestion = NewObject<UPSDataIngestion>();
    UPSPlaybookIngestion* PlaybookIngestion = NewObject<UPSPlaybookIngestion>();

    // Players
    {
        const FString PlayersPath = DataDir / TEXT("sample_players.json");
        TArray<FString> Errors;
        if (!DataIngestion->ValidatePlayersJson(PlayersPath, Errors))
        {
            bAnyErrors = true;
            for (const FString& Error : Errors)
            {
                UE_LOG(LogTemp, Error, TEXT("[ContentReimport] sample_players.json - %s"), *Error);
            }
        }
        else
        {
            UDataTable* PlayersTable = NewObject<UDataTable>();
            PlayersTable->RowStruct = FPlayerAttributes::StaticStruct();
            DataIngestion->LoadPlayerAttributesFromJson(PlayersPath, PlayersTable);
            UE_LOG(LogTemp, Display, TEXT("[ContentReimport] sample_players.json - loaded %d rows"), PlayersTable->GetRowMap().Num());
        }
    }

    // Teams
    {
        const FString TeamsPath = DataDir / TEXT("sample_teams.json");
        TArray<FString> Errors;
        if (!DataIngestion->ValidateTeamsJson(TeamsPath, Errors))
        {
            bAnyErrors = true;
            for (const FString& Error : Errors)
            {
                UE_LOG(LogTemp, Error, TEXT("[ContentReimport] sample_teams.json - %s"), *Error);
            }
        }
        else
        {
            UDataTable* TeamsTable = NewObject<UDataTable>();
            TeamsTable->RowStruct = FPSTeamInfo::StaticStruct();
            DataIngestion->LoadTeamsFromJson(TeamsPath, TeamsTable);
            UE_LOG(LogTemp, Display, TEXT("[ContentReimport] sample_teams.json - loaded %d rows"), TeamsTable->GetRowMap().Num());
        }
    }

    // League config
    {
        const FString ConfigPath = DataDir / TEXT("sample_league_config.json");
        FPSLeagueConfig Config;
        if (!DataIngestion->LoadLeagueConfigFromJson(ConfigPath, Config))
        {
            bAnyErrors = true;
            UE_LOG(LogTemp, Error, TEXT("[ContentReimport] sample_league_config.json - failed to load"));
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("[ContentReimport] sample_league_config.json - loaded league '%s', %d weeks"), *Config.LeagueName, Config.NumWeeks);
        }
    }

    // Playbook + routes
    {
        UDataTable* PlaysTable = NewObject<UDataTable>();
        PlaysTable->RowStruct = FPSPlayDefinition::StaticStruct();
        if (!PlaybookIngestion->LoadPlaysFromJson(DataDir / TEXT("sample_playbook.json"), PlaysTable))
        {
            bAnyErrors = true;
            UE_LOG(LogTemp, Error, TEXT("[ContentReimport] sample_playbook.json - failed to load"));
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("[ContentReimport] sample_playbook.json - loaded %d plays"), PlaysTable->GetRowMap().Num());
        }

        UDataTable* RoutesTable = NewObject<UDataTable>();
        RoutesTable->RowStruct = FPSRoute::StaticStruct();
        if (!PlaybookIngestion->LoadRoutesFromJson(DataDir / TEXT("sample_routes.json"), RoutesTable))
        {
            bAnyErrors = true;
            UE_LOG(LogTemp, Error, TEXT("[ContentReimport] sample_routes.json - failed to load"));
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("[ContentReimport] sample_routes.json - loaded %d routes"), RoutesTable->GetRowMap().Num());
        }
    }

    // Per-team rosters referenced from sample_teams.json
    {
        const TArray<FString> RosterFiles = { TEXT("rosters/team_hawks.json"), TEXT("rosters/team_wolves.json"), TEXT("rosters/team_bears.json") };
        for (const FString& RosterFile : RosterFiles)
        {
            const FString RosterPath = DataDir / RosterFile;
            TArray<FString> Errors;
            if (!DataIngestion->ValidatePlayersJson(RosterPath, Errors))
            {
                bAnyErrors = true;
                for (const FString& Error : Errors)
                {
                    UE_LOG(LogTemp, Error, TEXT("[ContentReimport] %s - %s"), *RosterFile, *Error);
                }
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("[ContentReimport] %s - schema valid"), *RosterFile);
            }
        }
    }

    UE_LOG(LogTemp, Display, TEXT("[ContentReimport] Complete. %s"), bAnyErrors ? TEXT("Errors found -- see above.") : TEXT("All content valid."));
    return bAnyErrors ? 1 : 0;
}
