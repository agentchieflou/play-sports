#include "PSContractNegotiation.h"

float UPSContractNegotiation::RatePlayer(const FPlayerAttributes& Player)
{
    return (Player.Speed + Player.Agility + Player.Strength + Player.Acceleration + Player.Awareness) / 5.f;
}

float UPSContractNegotiation::GetMarketMultiplier(const FPSContractTuning& Tuning, float MarketCapSpaceFraction)
{
    const float Limit = FMath::Max(0.f, Tuning.MaxMarketAdjustment);
    const float Adjustment = Tuning.MarketSpaceWeight * (MarketCapSpaceFraction - Tuning.NeutralCapSpaceFraction);
    return 1.f + FMath::Clamp(Adjustment, -Limit, Limit);
}

FPSContractDemand UPSContractNegotiation::ComputeDemand(const FPSContractTuning& Tuning, int32 SalaryCap, const FPSNegotiationContext& Context)
{
    FPSContractDemand Demand;

    // Performance: from the minimum (a replacement player) to his role's top of the market.
    const float MinSalary = static_cast<float>(FMath::Max(0, Tuning.MinimumSalary));
    const FPSPositionMarket* Market = Tuning.FindMarket(Context.Role);
    const float Top = FMath::Max(MinSalary, (Market ? Market->TopCapFraction : 0.f) * static_cast<float>(SalaryCap));
    const float Span = Tuning.EliteRating - Tuning.ReplacementRating;
    const float Quality = Span > 0.f ? FMath::Clamp((Context.Rating - Tuning.ReplacementRating) / Span, 0.f, 1.f) : (Context.Rating >= Tuning.EliteRating ? 1.f : 0.f);
    float Value = MinSalary + (Top - MinSalary) * FMath::Pow(Quality, FMath::Max(0.01f, Tuning.DemandCurveExponent));

    // Age: the market pays less past DeclineAge. The market itself: more cap space, higher asks.
    const int32 YearsPastDecline = FMath::Max(0, Context.Age - Tuning.DeclineAge);
    Value *= FMath::Max(Tuning.MinAgeMultiplier, 1.f - Tuning.AgeDiscountPerYear * YearsPastDecline);
    Value *= GetMarketMultiplier(Tuning, Context.MarketCapSpaceFraction);
    Demand.AnnualValue = FMath::Max(FMath::RoundToInt(MinSalary), FMath::RoundToInt(Value));

    // Young players want long deals, older ones shorter; the better the player, the more guaranteed.
    const int32 MaxYears = FMath::Max(1, Tuning.MaxContractYears);
    const int32 YearsPastPrime = FMath::Max(0, Context.Age - Tuning.PrimeAge);
    Demand.Years = FMath::Clamp(FMath::RoundToInt(MaxYears - Tuning.YearsLostPerYearPastPrime * YearsPastPrime), 1, MaxYears);
    Demand.GuaranteedFraction = FMath::Lerp(Tuning.MinGuaranteeFraction, Tuning.MaxGuaranteeFraction, Quality);
    Demand.WalkAwayValue = FMath::RoundToInt(Demand.AnnualValue * Tuning.WalkAwayRatio);
    return Demand;
}

float UPSContractNegotiation::ValueOffer(const FPSContractTuning& Tuning, const FPSContractDemand& Demand, const FPSNegotiationContext& Context, const FPSContractOffer& Offer)
{
    const int32 OfferYears = FMath::Max(1, Offer.Years);
    const float Total = static_cast<float>(Offer.AnnualValue) * OfferYears;
    const float BonusShare = Total > 0.f ? FMath::Clamp(Offer.SigningBonus / Total, 0.f, 1.f) : 0.f;
    const float GuaranteeShare = FMath::Max(FMath::Clamp(Offer.GuaranteedFraction, 0.f, 1.f), BonusShare);

    float Value = static_cast<float>(Offer.AnnualValue) * (1.f + Tuning.GuaranteeValueWeight * (GuaranteeShare - Demand.GuaranteedFraction));
    Value *= FMath::Max(0.f, 1.f - Tuning.YearsMismatchPenalty * FMath::Abs(OfferYears - Demand.Years));
    if (!Context.CurrentTeamId.IsNone() && Offer.TeamId == Context.CurrentTeamId)
    {
        Value *= 1.f + Tuning.MoraleLoyaltyWeight * (FMath::Clamp(Context.Morale, 0.f, 1.f) - 0.5f) * 2.f;
    }
    return Value / static_cast<float>(FMath::Max(1, Demand.AnnualValue));
}

FPSNegotiationResult UPSContractNegotiation::EvaluateOffer(const FPSContractTuning& Tuning, const FPSContractDemand& Demand, const FPSNegotiationContext& Context, const FPSContractOffer& Offer)
{
    FPSNegotiationResult Result;
    Result.Demand = Demand;
    Result.ValueRatio = ValueOffer(Tuning, Demand, Context, Offer);
    if (Result.ValueRatio >= Tuning.AcceptRatio)
    {
        Result.Response = EPSNegotiationResponse::Accept;
        Result.Reason = TEXT("Accepts");
    }
    else if (Result.ValueRatio >= Tuning.WalkAwayRatio)
    {
        Result.Response = EPSNegotiationResponse::Counter;
        Result.Reason = FString::Printf(TEXT("Wants %d years at %d a year, %d%% guaranteed"),
            Demand.Years, Demand.AnnualValue, FMath::RoundToInt(Demand.GuaranteedFraction * 100.f));
    }
    else
    {
        Result.Response = EPSNegotiationResponse::Reject;
        Result.Reason = TEXT("Far below his ask");
    }
    return Result;
}
