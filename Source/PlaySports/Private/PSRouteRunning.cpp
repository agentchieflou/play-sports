#include "PSRouteRunning.h"

float PSRouteRunning::ReleaseRating(const FPlayerAttributes& Player)
{
    return (Player.Agility + Player.Strength) * 0.5f;
}

float PSRouteRunning::ReleaseWinChance(const FPlayerAttributes& Receiver, const FPlayerAttributes& Presser, const FRouteRunningTuningRow& Tuning)
{
    const float Edge = ReleaseRating(Receiver) - ReleaseRating(Presser);
    const float Chance = Tuning.ReleaseBaseWinChance + Edge * Tuning.ReleaseRatingWeight;
    return FMath::Clamp(Chance, Tuning.ReleaseMinWinChance, FMath::Max(Tuning.ReleaseMinWinChance, Tuning.ReleaseMaxWinChance));
}

EPSReleaseOutcome PSRouteRunning::ResolveRelease(const FPlayerAttributes& Receiver, const FPlayerAttributes& Presser, float Roll, const FRouteRunningTuningRow& Tuning)
{
    const float WinChance = ReleaseWinChance(Receiver, Presser, Tuning);
    if (Roll < WinChance)
    {
        return EPSReleaseOutcome::Win;
    }
    const float DelayCut = WinChance + (1.f - WinChance) * FMath::Clamp(Tuning.DelayShare, 0.f, 1.f);
    return Roll < DelayCut ? EPSReleaseOutcome::Delay : EPSReleaseOutcome::Reroute;
}

float PSRouteRunning::BreakRounding(float Agility, const FRouteRunningTuningRow& Tuning)
{
    return Tuning.MaxBreakRounding * (1.f - FMath::Clamp(Agility, 0.f, 100.f) / 100.f);
}

float PSRouteRunning::TurnAngleDegrees(const FVector& From, const FVector& Corner, const FVector& To)
{
    FVector In = Corner - From;
    FVector Out = To - Corner;
    In.Z = 0.f;
    Out.Z = 0.f;
    if (In.IsNearlyZero() || Out.IsNearlyZero())
    {
        return 0.f;
    }
    const float Cosine = FMath::Clamp(FVector::DotProduct(In.GetSafeNormal(), Out.GetSafeNormal()), -1.f, 1.f);
    return FMath::RadiansToDegrees(FMath::Acos(Cosine));
}

float PSRouteRunning::BreakSeparationGain(float ReceiverAgility, float DefenderAgility, const FRouteRunningTuningRow& Tuning)
{
    return FMath::Max(0.f, Tuning.BreakSeparationBase + Tuning.BreakSeparationPerAgility * (ReceiverAgility - DefenderAgility));
}

float PSRouteRunning::BiteChance(float ReceiverAgility, float DefenderAwareness, const FRouteRunningTuningRow& Tuning)
{
    const float Chance = Tuning.BiteBaseChance
        + Tuning.BiteAgilityWeight * FMath::Clamp(ReceiverAgility, 0.f, 100.f) / 100.f
        - Tuning.BiteAwarenessWeight * FMath::Clamp(DefenderAwareness, 0.f, 100.f) / 100.f;
    return FMath::Clamp(Chance, Tuning.BiteMinChance, FMath::Max(Tuning.BiteMinChance, Tuning.BiteMaxChance));
}

int32 PSRouteRunning::FindBreakWaypoint(const FPSRoute& Route, const FRouteRunningTuningRow& Tuning)
{
    // A double move's real break comes after its fake; offsets are from the player's spot, so
    // the route starts at zero.
    int32 First = 0;
    for (int32 Index = 0; Index < Route.Waypoints.Num(); ++Index)
    {
        if (Route.Waypoints[Index].bFake)
        {
            First = Index + 1;
        }
    }
    for (int32 Index = First; Index + 1 < Route.Waypoints.Num(); ++Index)
    {
        const FVector From = Index > 0 ? Route.Waypoints[Index - 1].Offset : FVector::ZeroVector;
        if (TurnAngleDegrees(From, Route.Waypoints[Index].Offset, Route.Waypoints[Index + 1].Offset) >= Tuning.BreakMinAngleDegrees)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

float PSRouteRunning::ReadTime(const FPSRoute& Route, const UDataTable* RouteLibrary, const FRouteRunningTuningRow& Tuning)
{
    if (Route.Waypoints.Num() == 0)
    {
        return 0.f;
    }
    if (Route.Waypoints.IsValidIndex(Route.OptionReadWaypoint))
    {
        // The read, then the quicker branch's first leg.
        float BranchSeconds = TNumericLimits<float>::Max();
        for (const FName Branch : { Route.VsManBranch, Route.VsZoneBranch })
        {
            const FPSRoute* BranchRoute = RouteLibrary && !Branch.IsNone() ? RouteLibrary->FindRow<FPSRoute>(Branch, TEXT("PSRouteRunning"), false) : nullptr;
            if (BranchRoute && BranchRoute->Waypoints.Num() > 0)
            {
                BranchSeconds = FMath::Min(BranchSeconds, BranchRoute->Waypoints[0].TimingSeconds);
            }
        }
        const float ReadSeconds = Route.Waypoints[Route.OptionReadWaypoint].TimingSeconds;
        return BranchSeconds < TNumericLimits<float>::Max() ? ReadSeconds + BranchSeconds : ReadSeconds;
    }
    const int32 Break = FindBreakWaypoint(Route, Tuning);
    return Break != INDEX_NONE ? Route.Waypoints[Break].TimingSeconds : Route.Waypoints.Last().TimingSeconds;
}
