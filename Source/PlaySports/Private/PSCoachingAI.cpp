#include "PSCoachingAI.h"

void UPSCoachingAI::SeedDeterminism(int32 Seed)
{
    DeterminismStream.Initialize(Seed);
}

void UPSCoachingAI::SetSuggestionProvider(TScriptInterface<IPSCoachingSuggestionProvider> InProvider)
{
    SuggestionProvider = InProvider;
}

float UPSCoachingAI::GetSituationalCategoryWeight(const FString& Category, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense) const
{
    float Weight = 1.f;

    if (bOffense)
    {
        // Short yardage / goal line: ground game and safe short throws.
        if (Situation.Distance <= 3)
        {
            if (Category == TEXT("Run")) Weight += 1.5f;
            if (Category == TEXT("ShortPass")) Weight += 0.5f;
            if (Category == TEXT("DeepPass")) Weight -= 0.75f;
        }

        // Long yardage: need a bigger gain.
        if (Situation.Distance >= 8)
        {
            if (Category == TEXT("DeepPass")) Weight += 1.f + Tendency.AggressionScore;
            if (Category == TEXT("PlayAction")) Weight += 0.5f;
            if (Category == TEXT("Run")) Weight -= 0.5f;
        }

        // 3rd down: prioritize the percentage play (ShortPass/Screen), scaled down by aggression.
        if (Situation.Down == 3)
        {
            if (Category == TEXT("ShortPass") || Category == TEXT("Screen"))
            {
                Weight += 1.f - (Tendency.AggressionScore * 0.5f);
            }
        }

        // Backed up near the own goal line: avoid turnover-risk deep shots.
        if (Situation.YardLine <= 10)
        {
            if (Category == TEXT("DeepPass")) Weight -= 1.f;
            if (Category == TEXT("Run") || Category == TEXT("ShortPass")) Weight += 0.5f;
        }

        // Trailing late: need explosive plays, de-prioritize the clock-killing run.
        const bool bTrailingLate = Situation.Quarter == 4 && Situation.GameClockSeconds < 120.f && Situation.ScoreDifferential < 0;
        if (bTrailingLate)
        {
            if (Category == TEXT("DeepPass") || Category == TEXT("Screen")) Weight += 1.f;
            if (Category == TEXT("Run")) Weight -= 1.f;
        }
    }
    else
    {
        // Defense reads the same tells the offense uses to lean pass, and dials up
        // pressure accordingly, scaled by aggression.
        const bool bLikelyPassingDown = Situation.Distance >= 7 || Situation.Down == 3;
        if (bLikelyPassingDown)
        {
            if (Category == TEXT("Blitz")) Weight += Tendency.AggressionScore * 1.5f;
        }
        else
        {
            if (Category == TEXT("Base")) Weight += 0.5f;
        }

        const bool bProtectingLead = Situation.Quarter == 4 && Situation.GameClockSeconds < 120.f && Situation.ScoreDifferential > 0;
        if (bProtectingLead)
        {
            if (Category == TEXT("Prevent")) Weight += 1.5f;
            if (Category == TEXT("Blitz")) Weight -= 1.f;
        }
    }

    if (const float* TendencyWeight = Tendency.CategoryWeights.Find(Category))
    {
        Weight *= FMath::Max(*TendencyWeight, 0.01f);
    }

    return FMath::Max(Weight, 0.01f);
}

FName UPSCoachingAI::SelectWeightedPlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates, bool bOffense)
{
    if (Candidates.Num() == 0)
    {
        return NAME_None;
    }

    if (SuggestionProvider)
    {
        const FName Suggested = IPSCoachingSuggestionProvider::Execute_SuggestPlay(SuggestionProvider.GetObject(), Situation, bOffense);
        if (!Suggested.IsNone())
        {
            for (const FPSPlayDefinition& Candidate : Candidates)
            {
                if (Candidate.PlayId == Suggested)
                {
                    return Suggested;
                }
            }
        }
    }

    float TotalWeight = 0.f;
    TArray<float> Weights;
    Weights.Reserve(Candidates.Num());

    for (const FPSPlayDefinition& Candidate : Candidates)
    {
        const float Weight = GetSituationalCategoryWeight(Candidate.PlayCategory, Situation, Tendency, bOffense);
        Weights.Add(Weight);
        TotalWeight += Weight;
    }

    float Roll = DeterminismStream.FRandRange(0.f, TotalWeight);
    for (int32 i = 0; i < Candidates.Num(); ++i)
    {
        Roll -= Weights[i];
        if (Roll <= 0.f)
        {
            return Candidates[i].PlayId;
        }
    }

    return Candidates.Last().PlayId;
}

FName UPSCoachingAI::SelectOffensivePlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates)
{
    return SelectWeightedPlay(Situation, Tendency, Candidates, true);
}

FName UPSCoachingAI::SelectDefensivePlay(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates)
{
    return SelectWeightedPlay(Situation, Tendency, Candidates, false);
}

bool UPSCoachingAI::ShouldGoForItOnFourthDown(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency) const
{
    if (Situation.Down != 4)
    {
        return false;
    }

    // Short yardage past midfield: aggressive coaches always go; conservative ones
    // need it very short (Distance <= 1).
    const int32 GoForItDistanceThreshold = 1 + FMath::RoundToInt(Tendency.AggressionScore * 2.f);
    if (Situation.YardLine >= 50 && Situation.Distance <= GoForItDistanceThreshold)
    {
        return true;
    }

    // Trailing late in the 4th quarter: must keep possession regardless of aggression.
    const bool bMustScore = Situation.Quarter == 4 && Situation.GameClockSeconds < 300.f && Situation.ScoreDifferential < 0;
    if (bMustScore && Situation.Distance <= 4)
    {
        return true;
    }

    return false;
}

bool UPSCoachingAI::ShouldAttemptTwoPointConversion(int32 ScoreDifferentialAfterTD, int32 Quarter, const FPSTendencyProfile& Tendency) const
{
    // Standard NFL 2-point decision chart deficits/leads where 2 is strictly better
    // than 1 regardless of aggression (down 5, 10, 15 -> the "coach's chart" spots).
    static const TSet<int32> ChartMandatedDeficits = { -5, -10, -15, -2 };
    if (ChartMandatedDeficits.Contains(ScoreDifferentialAfterTD))
    {
        return true;
    }

    // Late-game aggressive coaches take the 2 to try to seal/extend a one-score game.
    if (Quarter == 4 && Tendency.AggressionScore > 0.75f && FMath::Abs(ScoreDifferentialAfterTD) <= 8)
    {
        return true;
    }

    return false;
}

bool UPSCoachingAI::ShouldCallTimeoutForClockManagement(const FPSSituationContext& Situation, bool bIsTrailing) const
{
    const bool bTwoMinuteWarningWindow = (Situation.Quarter == 2 || Situation.Quarter == 4) && Situation.GameClockSeconds < 120.f;
    return bTwoMinuteWarningWindow && bIsTrailing && Situation.TimeoutsRemaining > 0;
}
