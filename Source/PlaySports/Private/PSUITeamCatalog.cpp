#include "PSUITeamCatalog.h"
#include "PSDataIngestion.h"
#include "PSLeagueData.h"
#include "PSPlayerAttributes.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"

namespace PSUITeamCatalog
{
    static float SkillAverage(const FPlayerAttributes& Player)
    {
        return (Player.Speed + Player.Agility + Player.Strength + Player.Acceleration + Player.Awareness) / 5.f;
    }

    static bool IsDefensiveRole(EPlayerRole Role)
    {
        return Role == EPlayerRole::DefensiveLineman || Role == EPlayerRole::Linebacker || Role == EPlayerRole::DefensiveBack;
    }

    static int32 MeanRating(float Sum, int32 Count)
    {
        return Count > 0 ? FMath::RoundToInt(Sum / Count) : 0;
    }
}

FString UPSUITeamCatalog::GetDefaultTeamsPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/sample_teams.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSUITeamCatalog::ParseHexColor(const FString& Hex, FLinearColor& OutColor)
{
    if (Hex.Len() != 7 || !Hex.StartsWith(TEXT("#")))
    {
        return false;
    }
    for (int32 Index = 1; Index < Hex.Len(); ++Index)
    {
        if (!FChar::IsHexDigit(Hex[Index]))
        {
            return false;
        }
    }
    OutColor = FLinearColor(FColor::FromHex(Hex));
    return true;
}

bool UPSUITeamCatalog::BuildSummaries(const FString& TeamsJsonPath, TArray<FPSTeamSummary>& OutSummaries, TArray<FString>& OutErrors)
{
    OutSummaries.Reset();

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    UDataTable* Teams = NewObject<UDataTable>();
    Teams->RowStruct = FPSTeamInfo::StaticStruct();
    if (!Ingestion->LoadTeamsFromJson(TeamsJsonPath, Teams))
    {
        OutErrors.Add(FString::Printf(TEXT("Could not load teams from %s"), *TeamsJsonPath));
        return false;
    }

    TArray<FPSTeamInfo*> TeamRows;
    Teams->GetAllRows<FPSTeamInfo>(TEXT("UPSUITeamCatalog"), TeamRows);
    for (const FPSTeamInfo* Team : TeamRows)
    {
        if (!Team)
        {
            continue;
        }

        FPSTeamSummary Summary;
        Summary.TeamId = Team->TeamId;
        Summary.DisplayName = Team->DisplayName;
        Summary.Abbreviation = Team->Abbreviation;
        Summary.Division = Team->Division;
        Summary.LogoPath = Team->LogoPath;
        if (!ParseHexColor(Team->PrimaryColor, Summary.PrimaryColor))
        {
            OutErrors.Add(FString::Printf(TEXT("Team '%s': PrimaryColor '%s' is not #RRGGBB"), *Team->TeamId.ToString(), *Team->PrimaryColor));
        }
        if (!ParseHexColor(Team->SecondaryColor, Summary.SecondaryColor))
        {
            OutErrors.Add(FString::Printf(TEXT("Team '%s': SecondaryColor '%s' is not #RRGGBB"), *Team->TeamId.ToString(), *Team->SecondaryColor));
        }

        FString RosterPath = FPaths::ProjectDir() / Team->RosterDataTablePath;
        FPaths::CollapseRelativeDirectories(RosterPath);
        UDataTable* Roster = NewObject<UDataTable>();
        Roster->RowStruct = FPlayerAttributes::StaticStruct();
        if (!Ingestion->LoadPlayerAttributesFromJson(RosterPath, Roster))
        {
            OutErrors.Add(FString::Printf(TEXT("Team '%s': could not load roster %s"), *Team->TeamId.ToString(), *Team->RosterDataTablePath));
        }
        else
        {
            TArray<FPlayerAttributes*> Players;
            Roster->GetAllRows<FPlayerAttributes>(TEXT("UPSUITeamCatalog"), Players);

            float AllSum = 0.f;
            float OffenseSum = 0.f;
            float DefenseSum = 0.f;
            int32 OffenseCount = 0;
            int32 DefenseCount = 0;
            for (const FPlayerAttributes* Player : Players)
            {
                if (!Player)
                {
                    continue;
                }
                const float Rating = PSUITeamCatalog::SkillAverage(*Player);
                AllSum += Rating;
                if (PSUITeamCatalog::IsDefensiveRole(Player->Role))
                {
                    DefenseSum += Rating;
                    ++DefenseCount;
                }
                else
                {
                    OffenseSum += Rating;
                    ++OffenseCount;
                }
            }

            Summary.PlayerCount = OffenseCount + DefenseCount;
            Summary.Overall = PSUITeamCatalog::MeanRating(AllSum, Summary.PlayerCount);
            Summary.Offense = PSUITeamCatalog::MeanRating(OffenseSum, OffenseCount);
            Summary.Defense = PSUITeamCatalog::MeanRating(DefenseSum, DefenseCount);
        }

        OutSummaries.Add(Summary);
    }

    return OutSummaries.Num() > 0;
}
