#include "PSFranchiseFlow.h"
#include "PSLeagueNarrative.h"
#include "PSContractManager.h"
#include "PSDataIngestion.h"
#include "PSDraft.h"
#include "PSFranchiseSeason.h"
#include "PSFreeAgency.h"
#include "PSLockerRoom.h"
#include "PSLeagueData.h"
#include "PSLeagueGenerator.h"
#include "PSLeagueHistory.h"
#include "PSMatchSetup.h"
#include "PSOwnerEconomy.h"
#include "PSPlayerAging.h"
#include "PSPlayerAttributes.h"
#include "PSQuickSimRunner.h"
#include "PSRoster.h"
#include "PSStaffManager.h"
#include "PSStatsEngine.h"
#include "PSWeeklyPreparation.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"

void UPSFranchiseFlow::Initialize(UPSFranchiseSeason* InSeason, UPSStaffManager* InStaffs, FName InUserTeamId)
{
    Season = InSeason;
    Staffs = InStaffs;
    UserTeamId = InUserTeamId;
    CarouselEvents.Reset();
    EconomyReports.Reset();
    Retirements.Reset();
    LockerRoomEvents.Reset();
    TrainingEvents.Reset();
    FreeAgency = nullptr;
    LastRollover = FPSLeagueYearRollover();
    bSeasonEnded = false;
}

void UPSFranchiseFlow::SetNarrative(UPSLeagueNarrative* InNarrative)
{
    Narrative = InNarrative;
    if (Narrative)
    {
        Narrative->SetStats(Stats);
    }
}

void UPSFranchiseFlow::SetStats(UPSStatsEngine* InStats)
{
    Stats = InStats;
    if (Stats && Stats->GetSeason() <= 0)
    {
        Stats->StartSeason(Contracts && Contracts->GetLeagueYear() > 0 ? Contracts->GetLeagueYear() : 1);
    }
    if (Narrative)
    {
        Narrative->SetStats(Stats);
    }
}

int32 UPSFranchiseFlow::SignLeagueContracts()
{
    if (!Contracts)
    {
        return 0;
    }
    int32 Signed = 0;
    for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
    {
        if (Team.Value)
        {
            Signed += Contracts->SignRosterAtDemand(Team.Key, Team.Value->GetFullRoster());
        }
    }
    return Signed;
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

    // The week's practice comes before its games (Epic 90).
    PrepareWeek();

    int32 Played = 0;
    const int32 Week = Season->GetCurrentWeek();
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>(this);
    UPSMatchSetup* Match = NewObject<UPSMatchSetup>(this);
    if (Stats)
    {
        // Every simulated play goes into the game's box score (Epic 92), and each flag's ruling
        // just before its play: a penalty is a team stat.
        Runner->OnPlayResolved.AddUObject(Stats, &UPSStatsEngine::RecordPlay);
        Runner->OnPenaltyRuled.AddUObject(Stats, &UPSStatsEngine::RecordPenalty);
    }
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
        if (LockerRoom)
        {
            // Epic 91: the starters' units gel, holdouts sit, everyone plays at his morale.
            LockerRoom->RecordLineup(Matchup.HomeTeamId, HomeRoster);
            LockerRoom->RecordLineup(Matchup.AwayTeamId, AwayRoster);
            for (TPair<FName, TArray<FPlayerAttributes>*> Side : { TPair<FName, TArray<FPlayerAttributes>*>(Matchup.HomeTeamId, &HomePlayers),
                TPair<FName, TArray<FPlayerAttributes>*>(Matchup.AwayTeamId, &AwayPlayers) })
            {
                Side.Value->RemoveAll([this](const FPlayerAttributes& Player) { return LockerRoom->IsHoldingOut(Player.PlayerId); });
                for (FPlayerAttributes& Player : *Side.Value)
                {
                    Player = LockerRoom->ApplyEffects(Side.Key, Player);
                }
            }
        }
        if (Preparation)
        {
            // Epic 90: the injured sit; everyone plays at his freshness, with his gameplan for this opponent.
            HomePlayers.RemoveAll([this](const FPlayerAttributes& Player) { return Preparation->IsInjured(Player.PlayerId); });
            AwayPlayers.RemoveAll([this](const FPlayerAttributes& Player) { return Preparation->IsInjured(Player.PlayerId); });
            for (FPlayerAttributes& Player : HomePlayers)
            {
                Player = Preparation->ApplyPreparation(Matchup.HomeTeamId, Matchup.AwayTeamId, Player);
            }
            for (FPlayerAttributes& Player : AwayPlayers)
            {
                Player = Preparation->ApplyPreparation(Matchup.AwayTeamId, Matchup.HomeTeamId, Player);
            }
        }

        if (Stats)
        {
            Stats->BeginGame(Week, Matchup.HomeTeamId, Matchup.AwayTeamId);
        }
        const FPSQuickSimResult Result = Runner->SimulateGame(HomePlayers, AwayPlayers);
        if (Stats)
        {
            Stats->FinishGame();
        }
        if (Preparation)
        {
            Preparation->RecordGame(Matchup.HomeTeamId, HomePlayers);
            Preparation->RecordGame(Matchup.AwayTeamId, AwayPlayers);
        }
        // The home crowd comes for the record the team brought into the game (Epic 95).
        const TArray<FPSTeamStanding> Standings = Season->GetStandings();
        const FPSTeamStanding* HomeRecord = Standings.FindByPredicate([&Matchup](const FPSTeamStanding& Standing) { return Standing.TeamId == Matchup.HomeTeamId; });
        const bool bHomePlayed = HomeRecord && HomeRecord->Wins + HomeRecord->Losses + HomeRecord->Ties > 0;
        const float HomeWinPercentage = bHomePlayed ? HomeRecord->GetWinPercentage() : 0.5f;
        if (Season->RecordGameResult(Week, Matchup.HomeTeamId, Matchup.AwayTeamId, Result.HomeScore, Result.AwayScore))
        {
            ++Played;
            if (Economy)
            {
                Economy->RecordGame(Matchup.HomeTeamId, Matchup.AwayTeamId, Result.HomeScore, Result.AwayScore, HomeWinPercentage);
            }
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

TArray<FPSLockerRoomEvent> UPSFranchiseFlow::EvaluateLockerRooms(bool bNewLeagueYear)
{
    TArray<FPSLockerRoomEvent> Events;
    if (!LockerRoom || !Season)
    {
        return Events;
    }
    const TArray<FPSTeamStanding> Standings = Season->GetStandings();
    for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
    {
        const FPSTeamStanding* Record = Standings.FindByPredicate([&Team](const FPSTeamStanding& Standing) { return Standing.TeamId == Team.Key; });
        const bool bPlayed = Record && Record->Wins + Record->Losses + Record->Ties > 0;
        Events.Append(LockerRoom->EvaluateTeam(Team.Key, Team.Value, bPlayed ? Record->GetWinPercentage() : 0.5f, Contracts, bNewLeagueYear));
    }
    LockerRoomEvents.Append(Events);
    return Events;
}

TArray<FPSTrainingEvent> UPSFranchiseFlow::PrepareWeek()
{
    TArray<FPSTrainingEvent> Events;
    if (!Preparation || !Season || bSeasonEnded)
    {
        return Events;
    }
    const int32 Week = Season->GetCurrentWeek();
    const int32 FinalWeek = GetFinalWeek();
    if (Week < 1 || Week > FinalWeek)
    {
        return Events;
    }
    const TArray<FPSWeekMatchup> Matchups = Season->GetMatchupsForWeek(Week);
    const float SeasonProgress = FMath::Clamp(static_cast<float>(Week - 1) / FinalWeek, 0.f, 1.f);
    for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
    {
        // Its opponent this week; None on a bye.
        FName OpponentId;
        for (const FPSWeekMatchup& Matchup : Matchups)
        {
            if (Matchup.HomeTeamId == Team.Key || Matchup.AwayTeamId == Team.Key)
            {
                OpponentId = Matchup.HomeTeamId == Team.Key ? Matchup.AwayTeamId : Matchup.HomeTeamId;
            }
        }
        Events.Append(Preparation->PrepareTeam(Team.Key, Team.Value, Week, OpponentId, SeasonProgress, Economy, Staffs));
    }
    TrainingEvents.Append(Events);
    return Events;
}

bool UPSFranchiseFlow::PrepareDraft(UPSLeagueGenerator* Generator, int32 Seed)
{
    if (!Draft || !Generator || RostersByTeam.Num() == 0)
    {
        return false;
    }
    // The coming draft: next league year's, or this one's once the season is over (the contracts,
    // else the statistics, have moved on then).
    const int32 Year = Contracts && Contracts->GetLeagueYear() > 0 ? Contracts->GetLeagueYear() : Stats && Stats->GetSeason() > 0 ? Stats->GetSeason() : 1;
    TArray<FPlayerAttributes> LeaguePlayers;
    for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
    {
        if (Team.Value)
        {
            LeaguePlayers.Append(Team.Value->GetFullRoster());
        }
    }
    if (!Draft->PrepareClass(Generator->GenerateDraftClass(Seed, bSeasonEnded ? Year : Year + 1, RostersByTeam.Num(), LeaguePlayers), Seed))
    {
        return false;
    }
    for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
    {
        Draft->RegisterTeam(Team.Key, Team.Value, Team.Key == UserTeamId, Economy ? Economy->GetFundingIndex(Team.Key, EPSBudgetDepartment::Scouting) : 1.f);
    }
    return true;
}

bool UPSFranchiseFlow::BeginDraft()
{
    if (!Draft || !Season || !bSeasonEnded)
    {
        return false;
    }
    // The final standings turned around: the worst team picks first.
    const TArray<FPSTeamStanding> Final = Season->GetSortedStandings();
    TArray<FName> Order;
    for (int32 Index = Final.Num() - 1; Index >= 0; --Index)
    {
        if (RostersByTeam.Contains(Final[Index].TeamId))
        {
            Order.Add(Final[Index].TeamId);
        }
    }
    // Every team's picks go onto its roster (a loaded draft keeps its scouting).
    for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
    {
        Draft->RegisterTeam(Team.Key, Team.Value, Team.Key == UserTeamId, Economy ? Economy->GetFundingIndex(Team.Key, EPSBudgetDepartment::Scouting) : 1.f);
    }
    return Draft->BeginDraft(Order, Contracts, FreeAgency);
}

bool UPSFranchiseFlow::AdvanceWeek()
{
    if (!Season || bSeasonEnded)
    {
        return false;
    }
    // The week just played becomes news (Epic 93).
    if (Narrative)
    {
        Narrative->CloseWeek(Season, Season->GetCurrentWeek());
    }
    EvaluateLockerRooms(false);
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

    // The season being finished, numbered as the statistics (else the contracts) number it,
    // before either moves on.
    const int32 FinishedSeason = Stats && Stats->GetSeason() > 0 ? Stats->GetSeason()
        : Contracts && Contracts->GetLeagueYear() > 0 ? Contracts->GetLeagueYear()
        : LeagueHistory && LeagueHistory->GetSeasons().Num() > 0 ? LeagueHistory->GetSeasons().Last().Season + 1 : 1;
    // The season's awards are voted on its box scores, before the stats engine archives them.
    if (Narrative)
    {
        Narrative->AwardSeason(Season);
    }
    if (Stats)
    {
        Stats->EndSeason();
    }
    if (LeagueHistory)
    {
        // Epic 94: the season goes into the archive.
        LeagueHistory->ArchiveSeason(FinishedSeason, Season->GetSortedStandings(), Stats);
    }
    Retirements.Reset();
    if (PlayerAging)
    {
        // Epic 94: veterans retire (before the injured heal), everyone else ages a year.
        for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
        {
            Retirements.Append(PlayerAging->RunOffseason(Team.Key, Team.Value, FinishedSeason, Contracts, Stats, Preparation, LockerRoom, LeagueHistory));
        }
    }
    if (Preparation)
    {
        // The off-season heals everyone (Epic 90).
        Preparation->EndSeason();
    }
    if (LeagueHistory)
    {
        // The hall of fame votes on the retired (Epic 94).
        LeagueHistory->RunHallOfFameVote(FinishedSeason);
    }
    if (Economy)
    {
        // The books close on the league year just played, before the rollover below.
        EconomyReports = Economy->EndSeason(Season->GetStandings(), Contracts);
    }

    // The league year turns over (Epic 87): deals run out, CPU teams get under the new cap, and
    // free agency opens with everyone unsigned.
    if (Contracts)
    {
        LastRollover = Contracts->RolloverLeagueYear();
        TArray<FPlayerAttributes> Released;
        TArray<FName> ReleasedBy;
        for (const FName& TeamId : LastRollover.TeamsOverCap)
        {
            if (TeamId != UserTeamId)
            {
                const int32 Before = Released.Num();
                Contracts->EnforceCompliance(TeamId, GetTeamRoster(TeamId), Released);
                for (int32 Index = Before; Index < Released.Num(); ++Index)
                {
                    ReleasedBy.Add(TeamId);
                }
            }
        }

        FreeAgency = NewObject<UPSFreeAgency>(this);
        FreeAgency->Initialize(Contracts);
        for (const TPair<FName, UPSRoster*>& Team : RostersByTeam)
        {
            FreeAgency->RegisterTeam(Team.Key, Team.Value, Team.Key == UserTeamId);
        }
        FreeAgency->ReleaseUnsignedToPool(TMap<FName, int32>());
        for (int32 Index = 0; Index < Released.Num(); ++Index)
        {
            FreeAgency->AddFreeAgent(Released[Index], Contracts->GetPlayerAge(Released[Index]), 0.5f, ReleasedBy[Index]);
        }
        if (LockerRoom)
        {
            // Each free agent brings his morale to his old team's offers (Epic 91).
            for (const FPSFreeAgent& FreeAgent : TArray<FPSFreeAgent>(FreeAgency->GetPool()))
            {
                FreeAgency->SetFreeAgentMorale(FreeAgent.Player.PlayerId, LockerRoom->GetMorale(FreeAgent.Player.PlayerId));
            }
        }
        FreeAgency->BeginPeriod();
    }

    // The new league year in the locker rooms: an underpaid, unhappy star may hold out (Epic 91).
    if (LockerRoom)
    {
        EvaluateLockerRooms(true);
    }
    return true;
}
