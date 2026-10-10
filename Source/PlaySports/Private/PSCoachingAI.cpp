#include "PSCoachingAI.h"

void UPSCoachingAI::SeedDeterminism(int32 Seed)
{
    DeterminismStream.Initialize(Seed);
}

void UPSCoachingAI::SetSuggestionProvider(TScriptInterface<IPSCoachingSuggestionProvider> InProvider)
{
    SuggestionProvider = InProvider;
}

float UPSCoachingAI::GetSituationalCategoryWeight(const FString& Category, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense, TArray<FString>* OutReasons) const
{
    float Weight = 1.f;

    // Each rule that moves this category's weight also says why, for the play-call screen's
    // suggestions (Epic 102).
    auto Apply = [&Weight, OutReasons](bool bApplies, float Delta, const TCHAR* Reason)
    {
        if (!bApplies || Delta == 0.f)
        {
            return;
        }
        Weight += Delta;
        if (OutReasons)
        {
            OutReasons->Add(FString::Printf(TEXT("%s (%+.1f)"), Reason, Delta));
        }
    };

    if (bOffense)
    {
        // Short yardage / goal line: ground game and safe short throws.
        if (Situation.Distance <= 3)
        {
            Apply(Category == TEXT("Run"), 1.5f, TEXT("Short yardage: run it"));
            Apply(Category == TEXT("ShortPass"), 0.5f, TEXT("Short yardage: a quick throw"));
            Apply(Category == TEXT("DeepPass"), -0.75f, TEXT("Short yardage: no need to go deep"));
        }

        // Long yardage: need a bigger gain.
        if (Situation.Distance >= 8)
        {
            Apply(Category == TEXT("DeepPass"), 1.f + Tendency.AggressionScore, TEXT("Long yardage: take a shot"));
            Apply(Category == TEXT("PlayAction"), 0.5f, TEXT("Long yardage: sell the run"));
            Apply(Category == TEXT("Run"), -0.5f, TEXT("Long yardage: a run rarely gets there"));
        }

        // 3rd down: prioritize the percentage play (ShortPass/Screen), scaled down by aggression.
        if (Situation.Down == 3)
        {
            Apply(Category == TEXT("ShortPass") || Category == TEXT("Screen"), 1.f - (Tendency.AggressionScore * 0.5f), TEXT("3rd down: the percentage play"));
        }

        // Backed up near the own goal line: avoid turnover-risk deep shots.
        if (Situation.YardLine <= 10)
        {
            Apply(Category == TEXT("DeepPass"), -1.f, TEXT("Backed up: avoid the deep throw"));
            Apply(Category == TEXT("Run") || Category == TEXT("ShortPass"), 0.5f, TEXT("Backed up: play it safe"));
        }

        // Trailing late: need explosive plays, de-prioritize the clock-killing run.
        const bool bTrailingLate = Situation.Quarter == 4 && Situation.GameClockSeconds < 120.f && Situation.ScoreDifferential < 0;
        if (bTrailingLate)
        {
            Apply(Category == TEXT("DeepPass") || Category == TEXT("Screen"), 1.f, TEXT("Trailing late: need a big play"));
            Apply(Category == TEXT("Run"), -1.f, TEXT("Trailing late: a run burns clock"));
        }
    }
    else
    {
        // Defense reads the same tells the offense uses to lean pass, and dials up
        // pressure accordingly, scaled by aggression.
        const bool bLikelyPassingDown = Situation.Distance >= 7 || Situation.Down == 3;
        if (bLikelyPassingDown)
        {
            Apply(Category == TEXT("Blitz"), Tendency.AggressionScore * 1.5f, TEXT("Passing down: bring pressure"));
        }
        else
        {
            Apply(Category == TEXT("Base"), 0.5f, TEXT("Running down: stay in base"));
        }

        const bool bProtectingLead = Situation.Quarter == 4 && Situation.GameClockSeconds < 120.f && Situation.ScoreDifferential > 0;
        if (bProtectingLead)
        {
            Apply(Category == TEXT("Prevent"), 1.5f, TEXT("Protecting a late lead: keep it in front"));
            Apply(Category == TEXT("Blitz"), -1.f, TEXT("Protecting a late lead: don't gamble"));
        }
    }

    if (const float* TendencyWeight = Tendency.CategoryWeights.Find(Category))
    {
        Weight *= FMath::Max(*TendencyWeight, 0.01f);
        if (OutReasons && !FMath::IsNearlyEqual(*TendencyWeight, 1.f))
        {
            OutReasons->Add(FString::Printf(TEXT("Team tendency (x%.1f)"), *TendencyWeight));
        }
    }

    return FMath::Max(Weight, 0.01f);
}

TArray<FPSPlaySuggestion> UPSCoachingAI::RankPlays(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, const TArray<FPSPlayDefinition>& Candidates, bool bOffense) const
{
    TArray<FPSPlaySuggestion> Ranked;
    for (const FPSPlayDefinition& Candidate : Candidates)
    {
        FPSPlaySuggestion Suggestion;
        Suggestion.PlayId = Candidate.PlayId;
        Suggestion.DisplayName = Candidate.DisplayName;
        Suggestion.Category = Candidate.PlayCategory;
        Suggestion.Weight = GetSituationalCategoryWeight(Candidate.PlayCategory, Situation, Tendency, bOffense, &Suggestion.Reasons);
        Ranked.Add(MoveTemp(Suggestion));
    }

    // Stable, so equal weights keep playbook order.
    Ranked.StableSort([](const FPSPlaySuggestion& A, const FPSPlaySuggestion& B) { return A.Weight > B.Weight; });
    return Ranked;
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
