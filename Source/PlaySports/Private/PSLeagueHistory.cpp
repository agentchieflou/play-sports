#include "PSLeagueHistory.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSLeagueNarrative.h"
#include "PSStatsData.h"
#include "PSStatsEngine.h"
#include "Misc/Paths.h"

namespace PSLeagueHistoryPrivate
{
    FString CategoryName(EPSStatCategory Category)
    {
        return StaticEnum<EPSStatCategory>()->GetNameStringByValue(static_cast<int64>(Category));
    }

    FString RecordText(const FPSTeamStanding& Standing)
    {
        return FString::Printf(TEXT("%d-%d-%d"), Standing.Wins, Standing.Losses, Standing.Ties);
    }

    FString Ordinal(int32 Place)
    {
        const int32 LastTwo = Place % 100;
        const int32 Last = Place % 10;
        const TCHAR* Suffix = (LastTwo >= 11 && LastTwo <= 13) ? TEXT("th") : Last == 1 ? TEXT("st") : Last == 2 ? TEXT("nd") : Last == 3 ? TEXT("rd") : TEXT("th");
        return FString::Printf(TEXT("%d%s"), Place, Suffix);
    }
}

FString UPSLeagueHistory::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/legacy.json");
}

bool UPSLeagueHistory::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSLegacyTuning Loaded;
    if (!Ingestion->LoadLegacyTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLeagueHistory: Could not load the legacy tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLeagueHistory: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSLeagueHistory::ValidateTuning(const FPSLegacyTuning& InTuning)
{
    using namespace PSLeagueHistoryPrivate;

    TArray<FString> Problems;
    const FPSHallOfFameTuning& Hall = InTuning.HallOfFame;
    if (Hall.WaitSeasons < 0 || Hall.MinSeasons < 0)
    {
        Problems.Add(TEXT("HallOfFame: WaitSeasons and MinSeasons must be 0 or more"));
    }
    if (Hall.InductionScore <= 0.f || Hall.MaxInducteesPerSeason < 1)
    {
        Problems.Add(TEXT("HallOfFame: InductionScore must be above 0 and MaxInducteesPerSeason at least 1"));
    }
    if (Hall.Thresholds.Num() == 0)
    {
        Problems.Add(TEXT("HallOfFame: needs thresholds, or nobody is ever voted in"));
    }
    TSet<EPSStatCategory> Seen;
    for (const FPSHallOfFameThreshold& Threshold : Hall.Thresholds)
    {
        const FString Name = CategoryName(Threshold.Category);
        if (UPSStatsEngine::IsTeamCategory(Threshold.Category) || Seen.Contains(Threshold.Category))
        {
            Problems.Add(FString::Printf(TEXT("HallOfFame: %s is a team category or listed twice"), *Name));
        }
        if (Threshold.CareerValue < 1)
        {
            Problems.Add(FString::Printf(TEXT("HallOfFame: %s's CareerValue must be at least 1"), *Name));
        }
        Seen.Add(Threshold.Category);
    }
    TSet<EPSAwardKind> AwardsSeen;
    for (const FPSHallOfFameAward& Entry : Hall.AwardScores)
    {
        if (Entry.Score < 0.f || AwardsSeen.Contains(Entry.Award))
        {
            Problems.Add(FString::Printf(TEXT("HallOfFame: the %s award's score is negative or listed twice"),
                *StaticEnum<EPSAwardKind>()->GetNameStringByValue(static_cast<int64>(Entry.Award))));
        }
        AwardsSeen.Add(Entry.Award);
    }
    TSet<EPSStatCategory> Leaders;
    for (const EPSStatCategory Category : InTuning.LeaderCategories)
    {
        if (UPSStatsEngine::IsTeamCategory(Category) || Leaders.Contains(Category))
        {
            Problems.Add(FString::Printf(TEXT("LeaderCategories: %s is a team category or listed twice"), *CategoryName(Category)));
        }
        Leaders.Add(Category);
    }

    TSet<EPlayerRole> Roles;
    for (const FPSRoleAgingCurve& RoleCurve : InTuning.RoleCurves)
    {
        const FString Role = StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(RoleCurve.Role));
        const FPSProgressionTuning& Curve = RoleCurve.Curve;
        if (Roles.Contains(RoleCurve.Role))
        {
            Problems.Add(FString::Printf(TEXT("RoleCurves: %s is listed twice"), *Role));
        }
        Roles.Add(RoleCurve.Role);
        if (Curve.PeakAgeStart > Curve.PeakAgeEnd || Curve.GrowthPerYear < 0.f || Curve.DeclinePerYear < 0.f
            || Curve.LowSnapShareThreshold < 0.f || Curve.LowSnapShareThreshold > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("RoleCurves: %s needs PeakAgeStart <= PeakAgeEnd, growth and decline of 0 or more and a 0-1 LowSnapShareThreshold"), *Role));
        }
    }

    const FPSRetirementTuning& Retirement = InTuning.Retirement;
    if (Retirement.MinAge < 0 || Retirement.ForcedAge <= Retirement.MinAge)
    {
        Problems.Add(TEXT("Retirement: MinAge must be 0 or more and ForcedAge above it"));
    }
    for (const float Fraction : { Retirement.BaseChance, Retirement.ChancePerYear, Retirement.LowRatingChance, Retirement.InjuredChance,
        Retirement.LowMorale, Retirement.LowMoraleChance, Retirement.MaxRetirementShare })
    {
        if (Fraction < 0.f || Fraction > 1.f)
        {
            Problems.Add(TEXT("Retirement: the chances, LowMorale and MaxRetirementShare are 0-1"));
            break;
        }
    }
    if (Retirement.LowRating < 0.f || Retirement.LowRating > 100.f)
    {
        Problems.Add(TEXT("Retirement: LowRating is a 0-100 rating"));
    }
    return Problems;
}

bool UPSLeagueHistory::ArchiveSeason(int32 Season, const TArray<FPSTeamStanding>& SortedStandings, const UPSStatsEngine* Stats)
{
    if (Season <= 0 || SortedStandings.Num() == 0 || FindMutableSeason(Season))
    {
        return false;
    }

    FPSSeasonArchive Archive;
    Archive.Season = Season;
    Archive.Standings = SortedStandings;
    Archive.ChampionTeamId = SortedStandings[0].TeamId;
    if (Stats)
    {
        for (const EPSStatCategory Category : Tuning.LeaderCategories)
        {
            if (UPSStatsEngine::IsTeamCategory(Category))
            {
                continue;
            }
            const TArray<FPSLeaderEntry> Best = Stats->GetLeaders(Category, EPSStatScope::Season, Season, 1);
            if (Best.Num() > 0 && Best[0].Value > 0)
            {
                FPSSeasonLeader& Leader = Archive.Leaders.AddDefaulted_GetRef();
                Leader.Category = Category;
                Leader.PlayerId = Best[0].Id;
                Leader.TeamId = Best[0].TeamId;
                Leader.Value = Best[0].Value;
            }
        }
    }
    State.Seasons.Add(Archive);
    State.Seasons.StableSort([](const FPSSeasonArchive& A, const FPSSeasonArchive& B) { return A.Season < B.Season; });
    return true;
}

bool UPSLeagueHistory::RecordRetirement(const FPlayerAttributes& Player, FName TeamId, int32 Season, const FString& Reason, const UPSStatsEngine* Stats)
{
    if (Player.PlayerId.IsNone() || IsRetired(Player.PlayerId))
    {
        return false;
    }

    FPSRetiredPlayer Retiree;
    Retiree.Player = Player;
    Retiree.LastTeamId = TeamId;
    Retiree.PrimaryTeamId = TeamId;
    Retiree.RetiredAfterSeason = Season;
    Retiree.Reason = Reason;
    Retiree.Career.PlayerId = Player.PlayerId;
    Retiree.Career.TeamId = TeamId;
    if (Stats)
    {
        Retiree.Career = Stats->GetPlayerCareer(Player.PlayerId);

        // His seasons, and the team he played the most games for (the later one on a tie).
        TMap<FName, int32> GamesByTeam;
        const auto CountSeason = [&Retiree, &GamesByTeam](const FPSPlayerStatLine& Line)
        {
            if (Line.Games > 0)
            {
                ++Retiree.Seasons;
                GamesByTeam.FindOrAdd(Line.TeamId) += Line.Games;
            }
        };
        bool bCurrentArchived = false;
        for (const FPSSeasonStats& Past : Stats->GetStatBook().History)
        {
            bCurrentArchived |= Past.Season == Stats->GetSeason();
            const FName Id = Player.PlayerId;
            if (const FPSPlayerStatLine* Line = Past.Players.FindByPredicate([Id](const FPSPlayerStatLine& Candidate) { return Candidate.PlayerId == Id; }))
            {
                CountSeason(*Line);
            }
        }
        if (!bCurrentArchived)
        {
            CountSeason(Stats->GetPlayerSeason(Player.PlayerId, Stats->GetSeason()));
        }
        int32 MostGames = 0;
        for (const TPair<FName, int32>& Team : GamesByTeam)
        {
            if (!Team.Key.IsNone() && Team.Value >= MostGames)
            {
                MostGames = Team.Value;
                Retiree.PrimaryTeamId = Team.Key;
            }
        }
    }
    ScoreRetiree(Retiree);
    State.Retired.Add(Retiree);
    if (FPSSeasonArchive* Archive = FindMutableSeason(Season))
    {
        Archive->Retired.AddUnique(Player.PlayerId);
    }
    return true;
}

float UPSLeagueHistory::GetHallScore(const FPSPlayerStatLine& Career) const
{
    float Score = 0.f;
    for (const FPSHallOfFameThreshold& Threshold : Tuning.HallOfFame.Thresholds)
    {
        if (Threshold.CareerValue > 0 && !UPSStatsEngine::IsTeamCategory(Threshold.Category))
        {
            Score = FMath::Max(Score, static_cast<float>(Career.GetValue(Threshold.Category)) / Threshold.CareerValue);
        }
    }
    return Score;
}

void UPSLeagueHistory::SetAwards(const UPSLeagueNarrative* InAwards)
{
    Awards = InAwards;
}

float UPSLeagueHistory::GetAwardScore(FName PlayerId) const
{
    const UPSLeagueNarrative* Record = Awards.Get();
    float Score = 0.f;
    if (Record && !PlayerId.IsNone())
    {
        for (const FPSHallOfFameAward& Entry : Tuning.HallOfFame.AwardScores)
        {
            Score += Entry.Score * Record->CountAwards(PlayerId, Entry.Award);
        }
    }
    return Score;
}

void UPSLeagueHistory::ScoreRetiree(FPSRetiredPlayer& Retiree) const
{
    Retiree.AwardScore = GetAwardScore(Retiree.Player.PlayerId);
    Retiree.HallScore = GetHallScore(Retiree.Career) + Retiree.AwardScore;
}

TArray<FPSRetiredPlayer> UPSLeagueHistory::RunHallOfFameVote(int32 Season)
{
    const FPSHallOfFameTuning& Hall = Tuning.HallOfFame;
    TArray<int32> Candidates;
    for (int32 Index = 0; Index < State.Retired.Num(); ++Index)
    {
        FPSRetiredPlayer& Retiree = State.Retired[Index];
        ScoreRetiree(Retiree);
        if (!Retiree.bHallOfFame && Season - Retiree.RetiredAfterSeason >= Hall.WaitSeasons && Retiree.Seasons >= Hall.MinSeasons
            && Retiree.HallScore >= Hall.InductionScore)
        {
            Candidates.Add(Index);
        }
    }
    // The best careers first; the order they retired breaks ties.
    Candidates.StableSort([this](int32 A, int32 B) { return State.Retired[A].HallScore > State.Retired[B].HallScore; });

    TArray<FPSRetiredPlayer> Inductees;
    FPSSeasonArchive* Archive = FindMutableSeason(Season);
    for (int32 Rank = 0; Rank < Candidates.Num() && Inductees.Num() < Hall.MaxInducteesPerSeason; ++Rank)
    {
        FPSRetiredPlayer& Inductee = State.Retired[Candidates[Rank]];
        Inductee.bHallOfFame = true;
        Inductee.InductedAfterSeason = Season;
        Inductees.Add(Inductee);
        if (Archive)
        {
            Archive->Inducted.AddUnique(Inductee.Player.PlayerId);
        }
    }
    return Inductees;
}

bool UPSLeagueHistory::FindSeason(int32 Season, FPSSeasonArchive& OutSeason) const
{
    const FPSSeasonArchive* Found = State.Seasons.FindByPredicate([Season](const FPSSeasonArchive& Archive) { return Archive.Season == Season; });
    if (Found)
    {
        OutSeason = *Found;
    }
    return Found != nullptr;
}

bool UPSLeagueHistory::FindRetiredPlayer(FName PlayerId, FPSRetiredPlayer& OutPlayer) const
{
    const FPSRetiredPlayer* Found = State.Retired.FindByPredicate([PlayerId](const FPSRetiredPlayer& Retiree) { return Retiree.Player.PlayerId == PlayerId; });
    if (Found)
    {
        OutPlayer = *Found;
    }
    return Found != nullptr;
}

bool UPSLeagueHistory::IsRetired(FName PlayerId) const
{
    return State.Retired.ContainsByPredicate([PlayerId](const FPSRetiredPlayer& Retiree) { return Retiree.Player.PlayerId == PlayerId; });
}

TArray<FPSRetiredPlayer> UPSLeagueHistory::GetHallOfFame() const
{
    TArray<FPSRetiredPlayer> Members = State.Retired.FilterByPredicate([](const FPSRetiredPlayer& Retiree) { return Retiree.bHallOfFame; });
    Members.StableSort([](const FPSRetiredPlayer& A, const FPSRetiredPlayer& B) { return A.InductedAfterSeason < B.InductedAfterSeason; });
    return Members;
}

FPSFranchiseHistory UPSLeagueHistory::GetFranchiseHistory(FName TeamId) const
{
    FPSFranchiseHistory History;
    History.TeamId = TeamId;
    for (const FPSSeasonArchive& Archive : State.Seasons)
    {
        const int32 Place = Archive.Standings.IndexOfByPredicate([TeamId](const FPSTeamStanding& Standing) { return Standing.TeamId == TeamId; });
        if (Place == INDEX_NONE)
        {
            continue;
        }
        FPSFranchiseSeasonRecord& Record = History.Seasons.AddDefaulted_GetRef();
        Record.Season = Archive.Season;
        Record.Record = Archive.Standings[Place];
        Record.Finish = Place + 1;
        Record.bChampion = Archive.ChampionTeamId == TeamId;
        History.Championships += Record.bChampion ? 1 : 0;
        History.Wins += Record.Record.Wins;
        History.Losses += Record.Record.Losses;
        History.Ties += Record.Record.Ties;
    }
    History.Legends = State.Retired.FilterByPredicate([TeamId](const FPSRetiredPlayer& Retiree) { return Retiree.bHallOfFame && Retiree.PrimaryTeamId == TeamId; });
    History.Legends.StableSort([](const FPSRetiredPlayer& A, const FPSRetiredPlayer& B) { return A.HallScore > B.HallScore; });
    return History;
}

TArray<FString> UPSLeagueHistory::DescribeSeason(int32 Season) const
{
    using namespace PSLeagueHistoryPrivate;

    TArray<FString> Lines;
    FPSSeasonArchive Archive;
    if (!FindSeason(Season, Archive))
    {
        Lines.Add(FString::Printf(TEXT("Season %d: not in the archive"), Season));
        return Lines;
    }
    Lines.Add(FString::Printf(TEXT("Season %d: champion %s (%s)"), Season, *Archive.ChampionTeamId.ToString(), *RecordText(Archive.Standings[0])));
    for (int32 Place = 0; Place < Archive.Standings.Num(); ++Place)
    {
        const FPSTeamStanding& Standing = Archive.Standings[Place];
        Lines.Add(FString::Printf(TEXT("%s: %s %s (%d for, %d against)"), *Ordinal(Place + 1), *Standing.TeamId.ToString(), *RecordText(Standing),
            Standing.PointsFor, Standing.PointsAgainst));
    }
    for (const FPSSeasonLeader& Leader : Archive.Leaders)
    {
        Lines.Add(FString::Printf(TEXT("Leader in %s: %s (%s), %d"), *CategoryName(Leader.Category), *Leader.PlayerId.ToString(), *Leader.TeamId.ToString(), Leader.Value));
    }
    for (const FName& PlayerId : Archive.Retired)
    {
        FPSRetiredPlayer Retiree;
        FindRetiredPlayer(PlayerId, Retiree);
        Lines.Add(FString::Printf(TEXT("Retired: %s (%s): %s"), *PlayerId.ToString(), *Retiree.LastTeamId.ToString(), *Retiree.Reason));
    }
    for (const FName& PlayerId : Archive.Inducted)
    {
        Lines.Add(FString::Printf(TEXT("Hall of fame: %s"), *PlayerId.ToString()));
    }
    return Lines;
}

TArray<FString> UPSLeagueHistory::DescribeFranchise(FName TeamId) const
{
    using namespace PSLeagueHistoryPrivate;

    const FPSFranchiseHistory History = GetFranchiseHistory(TeamId);
    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("%s: %d season(s), %d championship(s), %d-%d-%d all time"), *TeamId.ToString(), History.Seasons.Num(), History.Championships,
        History.Wins, History.Losses, History.Ties));
    for (const FPSFranchiseSeasonRecord& Record : History.Seasons)
    {
        Lines.Add(FString::Printf(TEXT("Season %d: %s, %s%s"), Record.Season, *RecordText(Record.Record), *Ordinal(Record.Finish), Record.bChampion ? TEXT(", champion") : TEXT("")));
    }
    for (const FPSRetiredPlayer& Legend : History.Legends)
    {
        Lines.Add(FString::Printf(TEXT("Legend: %s (hall of fame after season %d)"), *Legend.Player.PlayerId.ToString(), Legend.InductedAfterSeason));
    }
    return Lines;
}

void UPSLeagueHistory::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->History = State;
    }
}

bool UPSLeagueHistory::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->History.Seasons.Num() == 0 && Save->History.Retired.Num() == 0))
    {
        return false;
    }
    State = Save->History;
    return true;
}

FPSSeasonArchive* UPSLeagueHistory::FindMutableSeason(int32 Season)
{
    return State.Seasons.FindByPredicate([Season](const FPSSeasonArchive& Archive) { return Archive.Season == Season; });
}
