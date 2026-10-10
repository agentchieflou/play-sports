#include "PSMatchSetup.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSFranchiseSeason.h"
#include "PSLeagueData.h"
#include "PSPlayCallSubsystem.h"
#include "PSStaffManager.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"

namespace PSMatchSetupPrivate
{
    /** "key=value" pairs of a travel options string, split on '?' (a leading one or not). */
    static TMap<FString, FString> ParseOptions(const FString& Options)
    {
        TMap<FString, FString> Parsed;
        TArray<FString> Pairs;
        Options.ParseIntoArray(Pairs, TEXT("?"), true);
        for (const FString& Pair : Pairs)
        {
            FString Key;
            FString Value;
            if (Pair.Split(TEXT("="), &Key, &Value))
            {
                Parsed.Add(Key.TrimStartAndEnd(), Value.TrimStartAndEnd());
            }
        }
        return Parsed;
    }

    /** The first league team after TeamId in league order (wrapping) that isn't TeamId. */
    static FName NextTeamAfter(const TArray<FName>& LeagueTeamIds, FName TeamId)
    {
        const int32 Count = LeagueTeamIds.Num();
        const int32 Start = LeagueTeamIds.IndexOfByKey(TeamId);
        for (int32 Step = 1; Step <= Count; ++Step)
        {
            const FName Candidate = LeagueTeamIds[(FMath::Max(Start, 0) + Step) % Count];
            if (Candidate != TeamId && !Candidate.IsNone())
            {
                return Candidate;
            }
        }
        return NAME_None;
    }
}

FString UPSMatchSetup::ModeToString(EPSMatchMode InMode)
{
    switch (InMode)
    {
    case EPSMatchMode::Franchise:
        return TEXT("Franchise");
    case EPSMatchMode::Practice:
        return TEXT("Practice");
    case EPSMatchMode::Versus:
        return TEXT("Versus");
    default:
        return TEXT("PlayNow");
    }
}

EPSMatchMode UPSMatchSetup::ModeFromString(const FString& Text)
{
    if (Text == TEXT("Franchise"))
    {
        return EPSMatchMode::Franchise;
    }
    if (Text == TEXT("Practice"))
    {
        return EPSMatchMode::Practice;
    }
    if (Text == TEXT("Versus"))
    {
        return EPSMatchMode::Versus;
    }
    return EPSMatchMode::PlayNow;
}

FString UPSMatchSetup::BuildOptions(EPSMatchMode InMode, FName InUserTeamId, FName InHomeTeamId, FName InAwayTeamId, int32 InHomeSeat)
{
    FString Options = FString::Printf(TEXT("mode=%s"), *ModeToString(InMode));
    if (!InHomeTeamId.IsNone())
    {
        Options += FString::Printf(TEXT("?home=%s"), *InHomeTeamId.ToString());
    }
    if (!InAwayTeamId.IsNone())
    {
        Options += FString::Printf(TEXT("?away=%s"), *InAwayTeamId.ToString());
    }
    if (!InUserTeamId.IsNone())
    {
        Options += FString::Printf(TEXT("?team=%s"), *InUserTeamId.ToString());
    }
    if (InHomeSeat >= 0)
    {
        Options += FString::Printf(TEXT("?homeseat=%d"), InHomeSeat);
    }
    return Options;
}

FString UPSMatchSetup::ToOptions() const
{
    FString Options = FString::Printf(TEXT("mode=%s"), *ModeToString(Mode));
    if (!HomeTeamId.IsNone())
    {
        Options += FString::Printf(TEXT("?home=%s"), *HomeTeamId.ToString());
    }
    if (!AwayTeamId.IsNone())
    {
        Options += FString::Printf(TEXT("?away=%s"), *AwayTeamId.ToString());
    }
    if (!UserTeamId.IsNone())
    {
        Options += FString::Printf(TEXT("?team=%s"), *UserTeamId.ToString());
    }
    if (SeasonWeek > 0)
    {
        Options += FString::Printf(TEXT("?week=%d"), SeasonWeek);
    }
    return Options;
}

bool UPSMatchSetup::InitializeFromOptions(const FString& Options, const TArray<FName>& LeagueTeamIds)
{
    using namespace PSMatchSetupPrivate;

    if (LeagueTeamIds.Num() < 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMatchSetup: The league has %d team(s); a match needs two."), LeagueTeamIds.Num());
        return false;
    }

    const TMap<FString, FString> Parsed = ParseOptions(Options);
    const auto LeagueTeam = [&Parsed, &LeagueTeamIds](const TCHAR* Key) -> FName
    {
        const FString* Value = Parsed.Find(Key);
        if (!Value || Value->IsEmpty())
        {
            return NAME_None;
        }
        const FName TeamId(**Value);
        if (!LeagueTeamIds.Contains(TeamId))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSMatchSetup: '%s' (%s=) is not a league team; ignored."), **Value, Key);
            return NAME_None;
        }
        return TeamId;
    };

    const FString* ModeText = Parsed.Find(TEXT("mode"));
    const FName PickedTeam = LeagueTeam(TEXT("team"));
    FName Home = LeagueTeam(TEXT("home"));
    FName Away = LeagueTeam(TEXT("away"));

    // The player's team is at home unless it was named away; else the league's first team.
    if (Home.IsNone())
    {
        Home = (!PickedTeam.IsNone() && PickedTeam != Away) ? PickedTeam : LeagueTeamIds[0];
        if (Home == Away)
        {
            Home = NextTeamAfter(LeagueTeamIds, Away);
        }
    }
    if (Away.IsNone() || Away == Home)
    {
        Away = NextTeamAfter(LeagueTeamIds, Home);
    }

    Mode = ModeText ? ModeFromString(*ModeText) : EPSMatchMode::PlayNow;
    HomeTeamId = Home;
    AwayTeamId = Away;
    UserTeamId = (PickedTeam == Home || PickedTeam == Away) ? PickedTeam : NAME_None;
    if (!PickedTeam.IsNone() && UserTeamId.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMatchSetup: The player's team %s is not in %s vs %s; the CPU plays both."), *PickedTeam.ToString(), *Home.ToString(), *Away.ToString());
    }
    const FString* WeekText = Parsed.Find(TEXT("week"));
    SeasonWeek = WeekText ? FMath::Max(0, FCString::Atoi(**WeekText)) : 0;

    UE_LOG(LogTemp, Display, TEXT("UPSMatchSetup: %s, %s (home) vs %s (away), player's team %s."), *ModeToString(Mode), *HomeTeamId.ToString(), *AwayTeamId.ToString(), *UserTeamId.ToString());
    return true;
}

bool UPSMatchSetup::InitializeForSeasonGame(const UPSFranchiseSeason* Season, int32 Week, FName InUserTeamId)
{
    if (!Season)
    {
        return false;
    }

    for (const FPSWeekMatchup& Matchup : Season->GetMatchupsForWeek(Week))
    {
        if (InUserTeamId.IsNone() || Matchup.HomeTeamId == InUserTeamId || Matchup.AwayTeamId == InUserTeamId)
        {
            Mode = EPSMatchMode::Franchise;
            HomeTeamId = Matchup.HomeTeamId;
            AwayTeamId = Matchup.AwayTeamId;
            UserTeamId = InUserTeamId;
            SeasonWeek = Week;
            return true;
        }
    }
    return false;
}

bool UPSMatchSetup::SetTeams(FName InHomeTeamId, FName InAwayTeamId)
{
    if (InHomeTeamId.IsNone() || InAwayTeamId.IsNone() || InHomeTeamId == InAwayTeamId)
    {
        return false;
    }
    HomeTeamId = InHomeTeamId;
    AwayTeamId = InAwayTeamId;
    if (UserTeamId != HomeTeamId && UserTeamId != AwayTeamId)
    {
        UserTeamId = NAME_None;
    }
    return true;
}

bool UPSMatchSetup::ApplyStaffs(const UPSStaffManager* Staffs, UPSPlayCallSubsystem* PlayCall, TArray<FPlayerAttributes>& HomePlayers, TArray<FPlayerAttributes>& AwayPlayers) const
{
    if (!Staffs)
    {
        return false;
    }

    if (PlayCall)
    {
        Staffs->ApplyToPlayCall(PlayCall, HomeTeamId, AwayTeamId);
    }
    for (FPlayerAttributes& Player : HomePlayers)
    {
        Player = Staffs->ApplySchemeFit(HomeTeamId, Player);
    }
    for (FPlayerAttributes& Player : AwayPlayers)
    {
        Player = Staffs->ApplySchemeFit(AwayTeamId, Player);
    }
    return Staffs->FindStaff(HomeTeamId) != nullptr && Staffs->FindStaff(AwayTeamId) != nullptr;
}

bool UPSMatchSetup::LoadFieldPlayers(const FString& TeamsJsonPath, TArray<FPlayerAttributes>& OutHomePlayers, TArray<FPlayerAttributes>& OutAwayPlayers) const
{
    TArray<FPlayerAttributes> HomePlayers;
    TArray<FPlayerAttributes> AwayPlayers;
    if (!HasTeams() || !LoadTeamPlayers(TeamsJsonPath, HomeTeamId, HomePlayers) || !LoadTeamPlayers(TeamsJsonPath, AwayTeamId, AwayPlayers))
    {
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("UPSMatchSetup: On the field, the %s (%d players) against the %s (%d players)."),
        *HomeTeamId.ToString(), HomePlayers.Num(), *AwayTeamId.ToString(), AwayPlayers.Num());
    OutHomePlayers = MoveTemp(HomePlayers);
    OutAwayPlayers = MoveTemp(AwayPlayers);
    return true;
}

bool UPSMatchSetup::ApplyStaffsToField(const UPSStaffManager* Staffs, UPSPlayCallSubsystem* PlayCall, TArray<FPlayerAttributes>& Players) const
{
    // One roster on both sides: its offense at the home team's fit, its defense at the away team's.
    TArray<FPlayerAttributes> Offense;
    TArray<FPlayerAttributes> Defense;
    for (const FPlayerAttributes& Player : Players)
    {
        (APSFieldGrid::GetSideForRole(Player.Role) == EPSTeamSide::Offense ? Offense : Defense).Add(Player);
    }
    const bool bBothStaffs = ApplyStaffs(Staffs, PlayCall, Offense, Defense);
    int32 OffenseIndex = 0;
    int32 DefenseIndex = 0;
    for (FPlayerAttributes& Player : Players)
    {
        Player = APSFieldGrid::GetSideForRole(Player.Role) == EPSTeamSide::Offense ? Offense[OffenseIndex++] : Defense[DefenseIndex++];
    }
    return bBothStaffs;
}

bool UPSMatchSetup::LoadTeamPlayers(const FString& TeamsJsonPath, FName TeamId, TArray<FPlayerAttributes>& OutPlayers)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    UDataTable* Teams = NewObject<UDataTable>();
    Teams->RowStruct = FPSTeamInfo::StaticStruct();
    if (TeamId.IsNone() || !Ingestion->LoadTeamsFromJson(TeamsJsonPath, Teams))
    {
        return false;
    }

    TArray<FPSTeamInfo*> TeamRows;
    Teams->GetAllRows<FPSTeamInfo>(TEXT("UPSMatchSetup"), TeamRows);
    FPSTeamInfo* const* Team = TeamRows.FindByPredicate([TeamId](const FPSTeamInfo* Row) { return Row && Row->TeamId == TeamId; });
    if (!Team)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMatchSetup: %s is not in %s; it has no roster."), *TeamId.ToString(), *TeamsJsonPath);
        return false;
    }

    FString RosterPath = FPaths::ProjectDir() / (*Team)->RosterDataTablePath;
    FPaths::CollapseRelativeDirectories(RosterPath);
    UDataTable* RosterTable = NewObject<UDataTable>();
    RosterTable->RowStruct = FPlayerAttributes::StaticStruct();
    if (!Ingestion->LoadPlayerAttributesFromJson(RosterPath, RosterTable))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMatchSetup: Could not load the %s roster from %s."), *TeamId.ToString(), *RosterPath);
        return false;
    }

    TArray<FPlayerAttributes*> Rows;
    RosterTable->GetAllRows<FPlayerAttributes>(TEXT("UPSMatchSetup"), Rows);
    OutPlayers.Reset();
    for (const FPlayerAttributes* Row : Rows)
    {
        if (Row)
        {
            OutPlayers.Add(*Row);
        }
    }
    return true;
}

TArray<FName> UPSMatchSetup::LoadLeagueTeamIds(const FString& TeamsJsonPath)
{
    TArray<FName> TeamIds;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    UDataTable* Teams = NewObject<UDataTable>();
    Teams->RowStruct = FPSTeamInfo::StaticStruct();
    if (!Ingestion->LoadTeamsFromJson(TeamsJsonPath, Teams))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMatchSetup: Could not load the league's teams from %s."), *TeamsJsonPath);
        return TeamIds;
    }

    TArray<FPSTeamInfo*> Rows;
    Teams->GetAllRows<FPSTeamInfo>(TEXT("UPSMatchSetup"), Rows);
    for (const FPSTeamInfo* Row : Rows)
    {
        if (Row && !Row->TeamId.IsNone())
        {
            TeamIds.AddUnique(Row->TeamId);
        }
    }
    return TeamIds;
}
