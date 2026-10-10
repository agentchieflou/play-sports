#include "PSFranchiseFlow.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSeason.h"
#include "PSLeagueData.h"
#include "PSMatchSetup.h"
#include "PSPlayerAttributes.h"
#include "PSQuickSimRunner.h"
#include "PSRoster.h"
#include "PSStaffManager.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"

void UPSFranchiseFlow::Initialize(UPSFranchiseSeason* InSeason, UPSStaffManager* InStaffs, FName InUserTeamId)
{
    Season = InSeason;
    Staffs = InStaffs;
    UserTeamId = InUserTeamId;
    CarouselEvents.Reset();
    bSeasonEnded = false;
}

void UPSFranchiseFlow::SetTeamRoster(FName TeamId, UPSRoster* Roster)
{
    if (TeamId.IsNone())
    {
        return;
    }
    if (Roster)
    {
        RostersByTeam.Add(TeamId, Roster);
    }
    else
    {
        RostersByTeam.Remove(TeamId);
    }
}

int32 UPSFranchiseFlow::LoadLeagueRosters(const FString& TeamsJsonPath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    UDataTable* Teams = NewObject<UDataTable>(this);
    Teams->RowStruct = FPSTeamInfo::StaticStruct();
    if (!Ingestion->LoadTeamsFromJson(TeamsJsonPath, Teams))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSFranchiseFlow: Could not load the league's teams from %s."), *TeamsJsonPath);
        return 0;
    }

    int32 Loaded = 0;
    TArray<FPSTeamInfo*> TeamRows;
    Teams->GetAllRows<FPSTeamInfo>(TEXT("UPSFranchiseFlow"), TeamRows);
    for (const FPSTeamInfo* Team : TeamRows)
    {
        if (!Team || Team->TeamId.IsNone())
        {
            continue;
        }

        FString RosterPath = FPaths::ProjectDir() / Team->RosterDataTablePath;
        FPaths::CollapseRelativeDirectories(RosterPath);
        UDataTable* RosterTable = NewObject<UDataTable>(this);
        RosterTable->RowStruct = FPlayerAttributes::StaticStruct();
        if (!Ingestion->LoadPlayerAttributesFromJson(RosterPath, RosterTable))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSFranchiseFlow: Could not load the %s roster from %s."), *Team->TeamId.ToString(), *RosterPath);
            continue;
        }

        TArray<FPlayerAttributes*> Rows;
        RosterTable->GetAllRows<FPlayerAttributes>(TEXT("UPSFranchiseFlow"), Rows);
        TArray<FPlayerAttributes> Players;
        for (const FPlayerAttributes* Row : Rows)
        {
            if (Row)
            {
                Players.Add(*Row);
            }
        }

        UPSRoster* Roster = NewObject<UPSRoster>(this);
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        SetTeamRoster(Team->TeamId, Roster);
        ++Loaded;
    }
    return Loaded;
}

UPSRoster* UPSFranchiseFlow::GetTeamRoster(FName TeamId) const
{
    UPSRoster* const* Found = RostersByTeam.Find(TeamId);
    return Found ? *Found : nullptr;
}

bool UPSFranchiseFlow::BuildUserMatch(UPSMatchSetup* Setup) const
{
    return Setup && Season && Setup->InitializeForSeasonGame(Season, Season->GetCurrentWeek(), UserTeamId);
}

int32 UPSFranchiseFlow::SimulateWeek(bool bIncludeUserGame)
{
    if (!Season)
    {
        return 0;
    }

    int32 Played = 0;
    const int32 Week = Season->GetCurrentWeek();
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>(this);
    UPSMatchSetup* Match = NewObject<UPSMatchSetup>(this);
    for (const FPSWeekMatchup& Matchup : Season->GetMatchupsForWeek(Week))
    {
        const bool bUserGame = !UserTeamId.IsNone() && (Matchup.HomeTeamId == UserTeamId || Matchup.AwayTeamId == UserTeamId);
        if (Matchup.bPlayed || (bUserGame && !bIncludeUserGame))
        {
            continue;
        }

        const UPSRoster* HomeRoster = GetTeamRoster(Matchup.HomeTeamId);
        const UPSRoster* AwayRoster = GetTeamRoster(Matchup.AwayTeamId);
        if (!HomeRoster || !AwayRoster || !Match->SetTeams(Matchup.HomeTeamId, Matchup.AwayTeamId))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSFranchiseFlow: Week %d, %s vs %s has a team without a roster; not played."), Week, *Matchup.HomeTeamId.ToString(), *Matchup.AwayTeamId.ToString());
            continue;
        }

        // Each team plays with its own staff's scheme fit; the rosters keep their ratings.
        TArray<FPlayerAttributes> HomePlayers = HomeRoster->GetFullRoster();
        TArray<FPlayerAttributes> AwayPlayers = AwayRoster->GetFullRoster();
        Match->ApplyStaffs(Staffs, nullptr, HomePlayers, AwayPlayers);

        const FPSQuickSimResult Result = Runner->SimulateGame(HomePlayers, AwayPlayers);
        if (Season->RecordGameResult(Week, Matchup.HomeTeamId, Matchup.AwayTeamId, Result.HomeScore, Result.AwayScore))
        {
            ++Played;
        }
    }
    return Played;
}

int32 UPSFranchiseFlow::GetFinalWeek() const
{
    int32 FinalWeek = 0;
    if (Season)
    {
        for (const FPSWeekMatchup& Matchup : Season->GetAllMatchups())
        {
            FinalWeek = FMath::Max(FinalWeek, Matchup.WeekNumber);
        }
    }
    return FinalWeek;
}

bool UPSFranchiseFlow::IsRegularSeasonComplete() const
{
    if (!Season || Season->GetAllMatchups().Num() == 0)
    {
        return false;
    }
    for (const FPSWeekMatchup& Matchup : Season->GetAllMatchups())
    {
        if (!Matchup.bPlayed)
        {
            return false;
        }
    }
    return true;
}

bool UPSFranchiseFlow::AdvanceWeek()
{
    if (!Season || bSeasonEnded)
    {
        return false;
    }
    Season->AdvanceWeek();
    return Season->GetCurrentWeek() > GetFinalWeek() && EndSeason();
}

bool UPSFranchiseFlow::EndSeason()
{
    if (bSeasonEnded || !IsRegularSeasonComplete())
    {
        return false;
    }

    bSeasonEnded = true;
    if (Staffs)
    {
        CarouselEvents = Staffs->RunCarousel(Season->GetStandings());
        for (const FPSCarouselEvent& Event : CarouselEvents)
        {
            UE_LOG(LogTemp, Display, TEXT("UPSFranchiseFlow: Carousel: %s"), *Event.Description);
        }
    }
    return true;
}
