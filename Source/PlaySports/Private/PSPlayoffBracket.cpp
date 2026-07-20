#include "PSPlayoffBracket.h"

TArray<FPSPlayoffMatchup> UPSPlayoffBracket::GenerateBracket(const TArray<FPSTeamStanding>& FinalStandings, int32 NumPlayoffTeams)
{
    TArray<FPSPlayoffMatchup> Bracket;

    TArray<FPSTeamStanding> Sorted = FinalStandings;
    Sorted.Sort([](const FPSTeamStanding& A, const FPSTeamStanding& B)
    {
        if (!FMath::IsNearlyEqual(A.GetWinPercentage(), B.GetWinPercentage()))
        {
            return A.GetWinPercentage() > B.GetWinPercentage();
        }
        return (A.PointsFor - A.PointsAgainst) > (B.PointsFor - B.PointsAgainst);
    });

    const int32 SeedCount = FMath::Min(NumPlayoffTeams, Sorted.Num());
    if (SeedCount < 2)
    {
        return Bracket;
    }

    for (int32 i = 0; i < SeedCount / 2; ++i)
    {
        const int32 HigherSeedIndex = i;
        const int32 LowerSeedIndex = SeedCount - 1 - i;

        FPSPlayoffMatchup Matchup;
        Matchup.Round = 1;
        Matchup.HigherSeed = HigherSeedIndex + 1;
        Matchup.LowerSeed = LowerSeedIndex + 1;
        Matchup.HigherSeedTeamId = Sorted[HigherSeedIndex].TeamId;
        Matchup.LowerSeedTeamId = Sorted[LowerSeedIndex].TeamId;
        Bracket.Add(Matchup);
    }

    return Bracket;
}
