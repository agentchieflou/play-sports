#include "PSSpecialTeamsModel.h"
#include "PSDataIngestion.h"
#include "PSPlaySimulation.h"
#include "Misc/Paths.h"

namespace PSSpecialTeamsModelPrivate
{
    FPSReturnSchemeDef MakeScheme(const TCHAR* Formation, float ReturnYardsBonus, float BigReturnChance)
    {
        FPSReturnSchemeDef Scheme;
        Scheme.Formation = Formation;
        Scheme.ReturnYardsBonus = ReturnYardsBonus;
        Scheme.BigReturnChance = BigReturnChance;
        return Scheme;
    }

    FPSFieldGoalRangeDef MakeRange(float MaxYards, float MakeChance)
    {
        FPSFieldGoalRangeDef Range;
        Range.MaxYards = MaxYards;
        Range.MakeChance = MakeChance;
        return Range;
    }

    bool IsLineman(EPlayerRole Role)
    {
        return Role == EPlayerRole::OffensiveLineman || Role == EPlayerRole::DefensiveLineman;
    }
}

FPSSpecialTeamsTuning::FPSSpecialTeamsTuning()
{
    using namespace PSSpecialTeamsModelPrivate;

    // Equal to Data/special_teams.json.
    FieldGoalRanges = { MakeRange(30.f, 0.95f), MakeRange(40.f, 0.85f), MakeRange(50.f, 0.7f), MakeRange(65.f, 0.3f) };
    ReturnSchemes = { MakeScheme(TEXT("Return Wall"), 4.f, 0.06f), MakeScheme(TEXT("Return Wedge"), 2.f, 0.03f) };
}

namespace PSSpecialTeams
{
    EPSSpecialTeamsPlay FromCategory(const FString& Category)
    {
        if (Category.IsEmpty())
        {
            return EPSSpecialTeamsPlay::None;
        }
        const UEnum* PlayEnum = StaticEnum<EPSSpecialTeamsPlay>();
        const int64 Value = PlayEnum ? PlayEnum->GetValueByNameString(Category) : INDEX_NONE;
        return Value == INDEX_NONE ? EPSSpecialTeamsPlay::None : static_cast<EPSSpecialTeamsPlay>(Value);
    }

    bool IsKickoffOnly(EPSSpecialTeamsPlay Play)
    {
        return Play == EPSSpecialTeamsPlay::Kickoff || Play == EPSSpecialTeamsPlay::OnsideKick
            || Play == EPSSpecialTeamsPlay::HandsTeam || Play == EPSSpecialTeamsPlay::ReturnLaterals;
    }

    EPSSpecialTeamsPlay GetShownKick(EPSSpecialTeamsPlay Play)
    {
        switch (Play)
        {
        case EPSSpecialTeamsPlay::Punt:
        case EPSSpecialTeamsPlay::FakePunt:
            return EPSSpecialTeamsPlay::Punt;
        case EPSSpecialTeamsPlay::FieldGoal:
        case EPSSpecialTeamsPlay::FakeFieldGoal:
            return EPSSpecialTeamsPlay::FieldGoal;
        default:
            return EPSSpecialTeamsPlay::None;
        }
    }

    bool IsCallableAt(EPSSpecialTeamsPlay Play, bool bKickoff)
    {
        return bKickoff ? (IsKickoffOnly(Play) || Play == EPSSpecialTeamsPlay::KickReturn) : !IsKickoffOnly(Play);
    }
}

UPSSpecialTeamsModel::UPSSpecialTeamsModel()
{
    Stream.GenerateNewSeed();
}

FString UPSSpecialTeamsModel::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/special_teams.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSSpecialTeamsModel::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSpecialTeamsTuning Loaded;
    if (!Ingestion->LoadSpecialTeamsTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSpecialTeamsModel: Could not load special-teams tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = MoveTemp(Loaded);
    return true;
}

void UPSSpecialTeamsModel::Seed(int32 InSeed)
{
    Stream.Initialize(InSeed);
}

FPSSpecialTeamsUnitRatings UPSSpecialTeamsModel::RateUnit(const TArray<FPlayerAttributes>& Players)
{
    FPSSpecialTeamsUnitRatings Ratings;
    if (Players.Num() == 0)
    {
        return Ratings;
    }

    float FastestSpeed = 0.f;
    float AwarenessSum = 0.f;
    float StrengthSum = 0.f;
    float LinemenStrengthSum = 0.f;
    int32 Linemen = 0;
    for (const FPlayerAttributes& Player : Players)
    {
        FastestSpeed = FMath::Max(FastestSpeed, Player.Speed);
        AwarenessSum += Player.Awareness;
        StrengthSum += Player.Strength;
        if (PSSpecialTeamsModelPrivate::IsLineman(Player.Role))
        {
            LinemenStrengthSum += Player.Strength;
            ++Linemen;
        }
    }
    Ratings.EdgeSpeed = FastestSpeed;
    Ratings.Awareness = AwarenessSum / Players.Num();
    Ratings.InteriorStrength = Linemen > 0 ? LinemenStrengthSum / Linemen : StrengthSum / Players.Num();
    return Ratings;
}

float UPSSpecialTeamsModel::GetFieldGoalDistance(int32 YardLine) const
{
    return 100.f - YardLine + Tuning.FieldGoalSnapYards;
}

float UPSSpecialTeamsModel::GetFieldGoalChance(float DistanceYards) const
{
    for (const FPSFieldGoalRangeDef& Range : Tuning.FieldGoalRanges)
    {
        if (DistanceYards <= Range.MaxYards)
        {
            return Range.MakeChance;
        }
    }
    return 0.f;
}

float UPSSpecialTeamsModel::GetBlockChance(EPSSpecialTeamsPlay Kick, EPSSpecialTeamsPlay Defense, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Defending) const
{
    float Chance = Kick == EPSSpecialTeamsPlay::Punt ? Tuning.PuntBlockChance
        : Kick == EPSSpecialTeamsPlay::FieldGoal ? Tuning.FieldGoalBlockChance : 0.f;
    if (Chance <= 0.f)
    {
        return 0.f;
    }

    // Edge timing and interior push.
    Chance += Tuning.EdgeSpeedFactor * (Defending.EdgeSpeed - Kicking.EdgeSpeed);
    Chance += Tuning.InteriorStrengthFactor * (Defending.InteriorStrength - Kicking.InteriorStrength);
    if (Defense == EPSSpecialTeamsPlay::KickBlock)
    {
        Chance *= Tuning.BlockUnitMultiplier;
    }
    return FMath::Clamp(Chance, 0.f, Tuning.MaxBlockChance);
}

float UPSSpecialTeamsModel::GetCoverageDiscipline(const FPSSpecialTeamsUnitRatings& Coverage) const
{
    return Tuning.CoverageAwarenessSpan > 0.f ? FMath::Clamp((Coverage.Awareness - 50.f) / Tuning.CoverageAwarenessSpan, -1.f, 1.f) : 0.f;
}

const FPSReturnSchemeDef* UPSSpecialTeamsModel::FindReturnScheme(const FString& Formation) const
{
    return Formation.IsEmpty() ? nullptr : Tuning.ReturnSchemes.FindByPredicate([&Formation](const FPSReturnSchemeDef& Scheme) { return Scheme.Formation == Formation; });
}

float UPSSpecialTeamsModel::GetFakeSuccessChance(EPSSpecialTeamsPlay Fake, EPSSpecialTeamsPlay Defense) const
{
    float Chance = Fake == EPSSpecialTeamsPlay::FakePunt ? Tuning.FakePuntSuccessChance
        : Fake == EPSSpecialTeamsPlay::FakeFieldGoal ? Tuning.FakeFieldGoalSuccessChance : 0.f;
    if (Chance > 0.f && Defense == EPSSpecialTeamsPlay::KickBlock)
    {
        Chance += Tuning.FakeVsBlockUnitDelta;
    }
    return FMath::Clamp(Chance, 0.f, 1.f);
}

int32 UPSSpecialTeamsModel::RollReturn(const FPSSpecialTeamsCall& Call, int32 MinYards, int32 MaxYards, const FPSSpecialTeamsUnitRatings& Coverage)
{
    float Yards = static_cast<float>(Stream.RandRange(MinYards, FMath::Max(MinYards, MaxYards)));
    const FPSReturnSchemeDef* Scheme = FindReturnScheme(Call.ReturnFormation);
    float BigChance = Scheme ? Scheme->BigReturnChance : Tuning.DefaultBigReturnChance;
    if (Scheme)
    {
        Yards += Scheme->ReturnYardsBonus;
    }

    // A unit built to block the kick or to catch an onside kick has few blockers for a return.
    if (Call.Receiving == EPSSpecialTeamsPlay::KickBlock || Call.Receiving == EPSSpecialTeamsPlay::HandsTeam)
    {
        Yards -= Call.Receiving == EPSSpecialTeamsPlay::KickBlock ? Tuning.BlockUnitReturnPenaltyYards : Tuning.HandsTeamReturnPenaltyYards;
        BigChance = 0.f;
    }

    // Coverage that stays in its lanes takes yards off and keeps the return from breaking.
    const float Discipline = GetCoverageDiscipline(Coverage);
    Yards -= Discipline * Tuning.LaneDisciplineYards;
    BigChance = FMath::Max(0.f, BigChance * (1.f - Discipline * Tuning.LaneDisciplineBigReturnScale));
    if (Stream.FRand() < BigChance)
    {
        Yards += Tuning.BigReturnYards;
    }
    return FMath::RoundToInt(Yards);
}

FPSSpecialTeamsOutcome UPSSpecialTeamsModel::ResolveKickoff(const FPSSpecialTeamsCall& Call, int32 KickYardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Receiving)
{
    FPSSpecialTeamsOutcome Outcome;
    Outcome.bPossessionChanges = true;

    if (Call.Kicking == EPSSpecialTeamsPlay::OnsideKick)
    {
        const float Recovery = Call.Receiving == EPSSpecialTeamsPlay::HandsTeam ? Tuning.OnsideRecoveryVsHandsTeamChance : Tuning.OnsideRecoveryChance;
        const int32 Spot = FMath::Clamp(KickYardLine + Tuning.OnsideKickYards, 1, 99);
        Outcome.Yards = Tuning.OnsideKickYards;
        if (Stream.FRand() < Recovery)
        {
            Outcome.Result = EPSSpecialTeamsResult::OnsideRecovered;
            Outcome.bPossessionChanges = false;
            Outcome.NextYardLine = Spot;
        }
        else
        {
            Outcome.Result = EPSSpecialTeamsResult::OnsideLost;
            Outcome.NextYardLine = 100 - Spot;
        }
        return Outcome;
    }

    // A team lateraling for its life runs everything back.
    if (Call.Receiving != EPSSpecialTeamsPlay::ReturnLaterals && Stream.FRand() < Tuning.KickoffTouchbackChance)
    {
        Outcome.Result = EPSSpecialTeamsResult::Touchback;
        Outcome.NextYardLine = Tuning.TouchbackYardLine;
        return Outcome;
    }

    const int32 ReturnLine = RollReturn(Call, Tuning.KickoffReturnMinYardLine, Tuning.KickoffReturnMaxYardLine, Kicking);
    Outcome.Yards = ReturnLine;
    Outcome.Result = EPSSpecialTeamsResult::Returned;
    Outcome.NextYardLine = FMath::Clamp(ReturnLine, 1, 99);
    if (Call.Receiving == EPSSpecialTeamsPlay::ReturnLaterals)
    {
        const float Roll = Stream.FRand();
        if (Roll < Tuning.LateralTouchdownChance)
        {
            Outcome.Result = EPSSpecialTeamsResult::LateralTouchdown;
            Outcome.bTouchdown = true;
        }
        else if (Roll < Tuning.LateralTouchdownChance + Tuning.LateralFumbleLostChance)
        {
            // The kickers fall on it where the return was stopped.
            Outcome.Result = EPSSpecialTeamsResult::LateralFumbleLost;
            Outcome.bPossessionChanges = false;
            Outcome.NextYardLine = 100 - Outcome.NextYardLine;
        }
        return Outcome;
    }
    if (ReturnLine >= 100)
    {
        Outcome.Result = EPSSpecialTeamsResult::ReturnTouchdown;
        Outcome.bTouchdown = true;
    }
    return Outcome;
}

FPSSpecialTeamsOutcome UPSSpecialTeamsModel::ResolvePunt(const FPSSpecialTeamsCall& Call, int32 YardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Receiving)
{
    FPSSpecialTeamsOutcome Outcome;
    Outcome.bPossessionChanges = true;

    if (Stream.FRand() < GetBlockChance(EPSSpecialTeamsPlay::Punt, Call.Receiving, Kicking, Receiving))
    {
        // The ball goes backwards; the defense falls on it, or picks it up and scores.
        const int32 Spot = YardLine - Tuning.BlockedPuntRecoilYards;
        const bool bTouchdown = Spot <= 0 || Stream.FRand() < Tuning.BlockedKickTouchdownChance;
        Outcome.Result = bTouchdown ? EPSSpecialTeamsResult::BlockedTouchdown : EPSSpecialTeamsResult::Blocked;
        Outcome.bTouchdown = bTouchdown;
        Outcome.NextYardLine = FMath::Clamp(100 - Spot, 1, 99);
        Outcome.Yards = -Tuning.BlockedPuntRecoilYards;
        return Outcome;
    }

    const int32 Gross = Stream.RandRange(Tuning.PuntGrossYardsMin, FMath::Max(Tuning.PuntGrossYardsMin, Tuning.PuntGrossYardsMax));
    const int32 Landing = YardLine + Gross;
    Outcome.Yards = Gross;
    Outcome.Result = EPSSpecialTeamsResult::Punted;
    if (Landing >= 100)
    {
        Outcome.Result = EPSSpecialTeamsResult::Touchback;
        Outcome.NextYardLine = Tuning.PuntTouchbackYardLine;
        return Outcome;
    }

    const int32 ReceiveLine = 100 - Landing + FMath::Max(0, RollReturn(Call, Tuning.PuntReturnYardsMin, Tuning.PuntReturnYardsMax, Kicking));
    if (ReceiveLine >= 100)
    {
        Outcome.Result = EPSSpecialTeamsResult::ReturnTouchdown;
        Outcome.bTouchdown = true;
    }
    Outcome.NextYardLine = FMath::Clamp(ReceiveLine, 1, 99);
    return Outcome;
}

FPSSpecialTeamsOutcome UPSSpecialTeamsModel::ResolveFieldGoal(const FPSSpecialTeamsCall& Call, int32 YardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Defending)
{
    FPSSpecialTeamsOutcome Outcome;
    const float Distance = GetFieldGoalDistance(YardLine);
    Outcome.Yards = FMath::RoundToInt(Distance);

    if (Stream.FRand() < GetBlockChance(EPSSpecialTeamsPlay::FieldGoal, Call.Receiving, Kicking, Defending))
    {
        const bool bTouchdown = Stream.FRand() < Tuning.BlockedKickTouchdownChance;
        Outcome.Result = bTouchdown ? EPSSpecialTeamsResult::BlockedTouchdown : EPSSpecialTeamsResult::Blocked;
        Outcome.bTouchdown = bTouchdown;
        Outcome.bPossessionChanges = true;
        Outcome.NextYardLine = FMath::Clamp(100 - YardLine, 1, 99);
        return Outcome;
    }

    if (Stream.FRand() < GetFieldGoalChance(Distance))
    {
        Outcome.Result = EPSSpecialTeamsResult::FieldGoalGood;
        return Outcome;
    }
    Outcome.Result = EPSSpecialTeamsResult::FieldGoalMissed;
    Outcome.bPossessionChanges = true;
    Outcome.NextYardLine = FMath::Clamp(100 - YardLine, Tuning.MissedFieldGoalMinYardLine, Tuning.MissedFieldGoalMaxYardLine);
    return Outcome;
}

int32 UPSSpecialTeamsModel::ResolveFake(const FPSSpecialTeamsCall& Call, int32 Distance)
{
    const int32 Needed = FMath::Max(1, Distance);
    if (Stream.FRand() < GetFakeSuccessChance(Call.Kicking, Call.Receiving))
    {
        return Needed + Stream.RandRange(0, FMath::Max(0, Tuning.FakeExtraYardsMax));
    }
    return Stream.RandRange(0, Needed - 1);
}

bool UPSSpecialTeamsModel::ApplyOutcome(FPlayState& State, const FPSSpecialTeamsOutcome& Outcome, int32 TouchdownPoints, int32 FieldGoalPoints, float ExtraPointChance)
{
    int32& KickingScore = State.bHomeHasPossession ? State.HomeScore : State.AwayScore;
    int32& ReceivingScore = State.bHomeHasPossession ? State.AwayScore : State.HomeScore;
    State.bKickoff = false;
    State.Down = 1;

    if (Outcome.Result == EPSSpecialTeamsResult::FieldGoalGood || Outcome.bTouchdown)
    {
        // The team that scored kicks off next.
        if (Outcome.bTouchdown)
        {
            int32& ScorerScore = Outcome.bPossessionChanges ? ReceivingScore : KickingScore;
            ScorerScore += TouchdownPoints + (Stream.FRand() < ExtraPointChance ? 1 : 0);
        }
        else
        {
            KickingScore += FieldGoalPoints;
        }
        State.bKickoff = true;
        State.YardLine = Tuning.KickoffYardLine;
    }
    else
    {
        State.YardLine = Outcome.NextYardLine;
    }
    State.YardLineToGain = FMath::Min(State.YardLine + 10, 100);
    State.Distance = State.YardLineToGain - State.YardLine;
    return Outcome.bPossessionChanges;
}
