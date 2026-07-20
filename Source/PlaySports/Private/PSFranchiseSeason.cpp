#include "PSFranchiseSeason.h"

void UPSFranchiseSeason::InitializeSeason(const TArray<FName>& TeamIds, const TArray<FSeasonWeek>& Weeks)
{
    Standings.Empty();
    Matchups.Empty();
    CurrentWeek = 1;

    for (const FName& TeamId : TeamIds)
    {
        FPSTeamStanding Standing;
        Standing.TeamId = TeamId;
        Standings.Add(Standing);
    }

    if (TeamIds.Num() < 2)
    {
        return;
    }

    // Circle method round-robin: fix team 0, rotate the rest each round.
    TArray<FName> Rotation = TeamIds;
    const bool bOddTeamCount = (Rotation.Num() % 2) != 0;
    if (bOddTeamCount)
    {
        Rotation.Add(NAME_None); // bye slot
    }

    const int32 NumTeams = Rotation.Num();
    const int32 RoundsPerCycle = NumTeams - 1;

    TArray<const FSeasonWeek*> PlayableWeeks;
    for (const FSeasonWeek& Week : Weeks)
    {
        if (!Week.bByeWeek)
        {
            PlayableWeeks.Add(&Week);
        }
    }

    int32 WeekCursor = 0;
    while (WeekCursor < PlayableWeeks.Num())
    {
        const int32 Round = WeekCursor % RoundsPerCycle;
        const int32 WeekNumber = PlayableWeeks[WeekCursor]->WeekNumber;

        for (int32 i = 0; i < NumTeams / 2; ++i)
        {
            const int32 HomeIdx = (Round + i) % RoundsPerCycle;
            const int32 AwayIdx = (Round + NumTeams - 1 - i) % RoundsPerCycle;
            const int32 FixedIdx = NumTeams - 1;

            const FName TeamA = (i == 0) ? Rotation[FixedIdx] : Rotation[HomeIdx];
            const FName TeamB = Rotation[AwayIdx];

            if (TeamA.IsNone() || TeamB.IsNone())
            {
                continue; // bye
            }

            FPSWeekMatchup Matchup;
            Matchup.WeekNumber = WeekNumber;
            Matchup.HomeTeamId = TeamA;
            Matchup.AwayTeamId = TeamB;
            Matchups.Add(Matchup);
        }

        ++WeekCursor;
    }
}

TArray<FPSWeekMatchup> UPSFranchiseSeason::GetMatchupsForWeek(int32 WeekNumber) const
{
    TArray<FPSWeekMatchup> Result;
    for (const FPSWeekMatchup& Matchup : Matchups)
    {
        if (Matchup.WeekNumber == WeekNumber)
        {
            Result.Add(Matchup);
        }
    }
    return Result;
}

bool UPSFranchiseSeason::RecordGameResult(int32 WeekNumber, FName HomeTeamId, FName AwayTeamId, int32 HomeScore, int32 AwayScore)
{
    FPSWeekMatchup* Found = nullptr;
    for (FPSWeekMatchup& Matchup : Matchups)
    {
        if (Matchup.WeekNumber == WeekNumber && Matchup.HomeTeamId == HomeTeamId && Matchup.AwayTeamId == AwayTeamId)
        {
            Found = &Matchup;
            break;
        }
    }

    if (!Found || Found->bPlayed)
    {
        return false;
    }

    Found->bPlayed = true;
    Found->HomeScore = HomeScore;
    Found->AwayScore = AwayScore;

    FPSTeamStanding* HomeStanding = Standings.FindByPredicate([HomeTeamId](const FPSTeamStanding& S) { return S.TeamId == HomeTeamId; });
    FPSTeamStanding* AwayStanding = Standings.FindByPredicate([AwayTeamId](const FPSTeamStanding& S) { return S.TeamId == AwayTeamId; });

    if (HomeStanding)
    {
        HomeStanding->PointsFor += HomeScore;
        HomeStanding->PointsAgainst += AwayScore;
        if (HomeScore > AwayScore) ++HomeStanding->Wins;
        else if (HomeScore < AwayScore) ++HomeStanding->Losses;
        else ++HomeStanding->Ties;
    }

    if (AwayStanding)
    {
        AwayStanding->PointsFor += AwayScore;
        AwayStanding->PointsAgainst += HomeScore;
        if (AwayScore > HomeScore) ++AwayStanding->Wins;
        else if (AwayScore < HomeScore) ++AwayStanding->Losses;
        else ++AwayStanding->Ties;
    }

    return true;
}

TArray<FPSTeamStanding> UPSFranchiseSeason::GetSortedStandings() const
{
    TArray<FPSTeamStanding> Sorted = Standings;
    Sorted.Sort([](const FPSTeamStanding& A, const FPSTeamStanding& B)
    {
        if (!FMath::IsNearlyEqual(A.GetWinPercentage(), B.GetWinPercentage()))
        {
            return A.GetWinPercentage() > B.GetWinPercentage();
        }
        return (A.PointsFor - A.PointsAgainst) > (B.PointsFor - B.PointsAgainst);
    });
    return Sorted;
}
