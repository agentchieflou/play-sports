#include "PSSpecialTeamsAI.h"
#include "PSDataIngestion.h"

bool UPSSpecialTeamsAI::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSpecialTeamsTuning Loaded;
    if (!Ingestion->LoadSpecialTeamsTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSpecialTeamsAI: Could not load special-teams tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = MoveTemp(Loaded);
    return true;
}

float UPSSpecialTeamsAI::GetFieldGoalDistance(const FPSSituationContext& Situation) const
{
    return 100.f - Situation.YardLine + Tuning.FieldGoalSnapYards;
}

bool UPSSpecialTeamsAI::IsInFieldGoalRange(const FPSSituationContext& Situation) const
{
    return GetFieldGoalDistance(Situation) <= Tuning.MaxFieldGoalAttemptYards;
}

EPSSpecialTeamsPlay UPSSpecialTeamsAI::DecideOffense(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bGoForIt, float Roll) const
{
    if (Situation.bKickoff)
    {
        return EPSSpecialTeamsPlay::None;
    }

    // The half's last snap: kick it when three points are what the score needs.
    const bool bInRange = IsInFieldGoalRange(Situation);
    const bool bLastPlay = (Situation.Quarter == 2 || Situation.Quarter == 4) && Situation.GameClockSeconds <= Tuning.LastPlaySeconds;
    const bool bFieldGoalHelps = Situation.Quarter != 4 || (Situation.ScoreDifferential >= -3 && Situation.ScoreDifferential <= 0);
    if (bLastPlay && bInRange && bFieldGoalHelps)
    {
        return EPSSpecialTeamsPlay::FieldGoal;
    }
    if (Situation.Down != 4 || bGoForIt)
    {
        return EPSSpecialTeamsPlay::None;
    }

    // A trailing team late can't give the ball away; a field goal only helps within three.
    const bool bTrailingLate = Situation.Quarter == 4 && Situation.ScoreDifferential < 0 && Situation.GameClockSeconds <= Tuning.NoPuntTrailingSeconds;
    const bool bFake = Situation.Distance <= Tuning.FakeMaxDistance && Tendency.AggressionScore >= Tuning.FakeMinAggression
        && Roll < Tuning.FakeCallChance * Tendency.AggressionScore;
    if (bInRange && (!bTrailingLate || Situation.ScoreDifferential >= -3))
    {
        return bFake ? EPSSpecialTeamsPlay::FakeFieldGoal : EPSSpecialTeamsPlay::FieldGoal;
    }
    if (bTrailingLate)
    {
        return EPSSpecialTeamsPlay::None;
    }
    return bFake ? EPSSpecialTeamsPlay::FakePunt : EPSSpecialTeamsPlay::Punt;
}

EPSSpecialTeamsPlay UPSSpecialTeamsAI::DecideKickoff(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, float Roll) const
{
    if (!Situation.bKickoff)
    {
        return EPSSpecialTeamsPlay::None;
    }
    const bool bNeedBall = Situation.Quarter == 4 && Situation.ScoreDifferential < 0 && Situation.ScoreDifferential >= -Tuning.OnsideMaxDeficit
        && Situation.GameClockSeconds <= Tuning.OnsideWindowSeconds;
    const bool bProtectingLate = Situation.Quarter == 4 && Situation.ScoreDifferential > 0;
    const bool bSurprise = !bProtectingLate && Roll < Tuning.SurpriseOnsideChance * Tendency.AggressionScore;
    return bNeedBall || bSurprise ? EPSSpecialTeamsPlay::OnsideKick : EPSSpecialTeamsPlay::Kickoff;
}

EPSSpecialTeamsPlay UPSSpecialTeamsAI::DecideReceiving(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, float Roll) const
{
    // The receiving team's own score is the other side of the kicking team's.
    const int32 OwnDifferential = -Situation.ScoreDifferential;
    const bool bFourthQuarter = Situation.Quarter == 4;
    if (Situation.bKickoff)
    {
        if (bFourthQuarter && Situation.GameClockSeconds <= Tuning.LateralsWindowSeconds && OwnDifferential < 0 && OwnDifferential >= -Tuning.LateralsMaxDeficit)
        {
            return EPSSpecialTeamsPlay::ReturnLaterals;
        }
        const bool bExpectOnside = bFourthQuarter && Situation.ScoreDifferential < 0 && Situation.ScoreDifferential >= -Tuning.OnsideMaxDeficit
            && Situation.GameClockSeconds <= Tuning.OnsideWindowSeconds;
        return bExpectOnside ? EPSSpecialTeamsPlay::HandsTeam : EPSSpecialTeamsPlay::KickReturn;
    }
    if (Situation.OffenseKick == EPSSpecialTeamsPlay::None)
    {
        return EPSSpecialTeamsPlay::None;
    }

    // Block the kick that decides the game, or the punt when a big play is the only chance left.
    const bool bLate = bFourthQuarter && Situation.GameClockSeconds <= Tuning.BlockWindowSeconds;
    const bool bDecidingKick = Situation.OffenseKick == EPSSpecialTeamsPlay::FieldGoal && bLate && Situation.ScoreDifferential >= -3 && Situation.ScoreDifferential <= 0;
    const bool bLastChance = Situation.OffenseKick == EPSSpecialTeamsPlay::Punt && bLate && OwnDifferential < 0;
    return bDecidingKick || bLastChance || Roll < Tuning.BaseBlockCallChance ? EPSSpecialTeamsPlay::KickBlock : EPSSpecialTeamsPlay::KickReturn;
}

EPSSpecialTeamsPlay UPSSpecialTeamsAI::DecideCall(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense, bool bGoForIt, float Roll) const
{
    if (!bOffense)
    {
        return DecideReceiving(Situation, Tendency, Roll);
    }
    return Situation.bKickoff ? DecideKickoff(Situation, Tendency, Roll) : DecideOffense(Situation, Tendency, bGoForIt, Roll);
}

float UPSSpecialTeamsAI::GetPlayAdjustment(const FPSPlayDefinition& Play, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense, bool bGoForIt, float Roll,
    TArray<FString>* OutReasons, bool& bOutExcluded) const
{
    const EPSSpecialTeamsPlay Call = PSSpecialTeams::FromCategory(Play.PlayCategory);
    if (Call == EPSSpecialTeamsPlay::None)
    {
        // A kickoff runs special-teams plays only.
        bOutExcluded = Situation.bKickoff;
        return 0.f;
    }
    if (DecideCall(Situation, Tendency, bOffense, bGoForIt, Roll) != Call)
    {
        bOutExcluded = true;
        return 0.f;
    }
    bOutExcluded = false;
    if (OutReasons)
    {
        OutReasons->Add(FString::Printf(TEXT("%s (%+.1f)"), *DescribeCall(Call), Tuning.SpecialTeamsPlayWeight));
    }
    return Tuning.SpecialTeamsPlayWeight;
}

FString UPSSpecialTeamsAI::DescribeCall(EPSSpecialTeamsPlay Play)
{
    switch (Play)
    {
    case EPSSpecialTeamsPlay::Punt:           return TEXT("4th down: punt it away");
    case EPSSpecialTeamsPlay::FieldGoal:      return TEXT("In range: take the points");
    case EPSSpecialTeamsPlay::FakePunt:       return TEXT("4th and short: fake the punt");
    case EPSSpecialTeamsPlay::FakeFieldGoal:  return TEXT("4th and short: fake the field goal");
    case EPSSpecialTeamsPlay::Kickoff:        return TEXT("Kick it deep");
    case EPSSpecialTeamsPlay::OnsideKick:     return TEXT("Need the ball back: onside kick");
    case EPSSpecialTeamsPlay::KickReturn:     return TEXT("Set up the return");
    case EPSSpecialTeamsPlay::KickBlock:      return TEXT("Everybody rushes: block the kick");
    case EPSSpecialTeamsPlay::HandsTeam:      return TEXT("Expect the onside kick: hands team");
    case EPSSpecialTeamsPlay::ReturnLaterals: return TEXT("Last play: lateral until somebody scores");
    default:                                  return FString();
    }
}
