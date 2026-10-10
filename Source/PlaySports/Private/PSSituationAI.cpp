#include "PSSituationAI.h"
#include "PSDataIngestion.h"
#include "Misc/Paths.h"

namespace PSSituationAIPrivate
{
    FPSTempoDef MakeTempo(EPSTempo Tempo, const TCHAR* Label, float SnapAtPlayClockSeconds, bool bRerunLastCall)
    {
        FPSTempoDef Def;
        Def.Tempo = Tempo;
        Def.Label = Label;
        Def.SnapAtPlayClockSeconds = SnapAtPlayClockSeconds;
        Def.bRerunLastCall = bRerunLastCall;
        return Def;
    }

    FPSSituationTempoDef MakeSituationTempo(EPSGameSituation Situation, EPSTempo ClockRunning, EPSTempo ClockStopped)
    {
        FPSSituationTempoDef Def;
        Def.Situation = Situation;
        Def.ClockRunningTempo = ClockRunning;
        Def.ClockStoppedTempo = ClockStopped;
        return Def;
    }

    FPSSituationCategoryWeight MakeWeight(EPSGameSituation Situation, bool bOffense, const TCHAR* Category, float Delta, const TCHAR* Reason)
    {
        FPSSituationCategoryWeight Weight;
        Weight.Situation = Situation;
        Weight.bOffense = bOffense;
        Weight.Category = Category;
        Weight.Delta = Delta;
        Weight.Reason = Reason;
        return Weight;
    }
}

FPSSituationalTuning::FPSSituationalTuning()
{
    using namespace PSSituationAIPrivate;

    // Equal to Data/situational_tuning.json.
    Tempos = {
        MakeTempo(EPSTempo::Huddle, TEXT("Huddle"), 12.f, false),
        MakeTempo(EPSTempo::NoHuddle, TEXT("No-huddle"), 25.f, false),
        MakeTempo(EPSTempo::HurryUp, TEXT("Hurry-up"), 40.f, true),
        MakeTempo(EPSTempo::MilkClock, TEXT("Milk the clock"), 2.f, false) };
    SituationTempos = {
        MakeSituationTempo(EPSGameSituation::Normal, EPSTempo::Huddle, EPSTempo::Huddle),
        MakeSituationTempo(EPSGameSituation::TwoMinuteDrill, EPSTempo::HurryUp, EPSTempo::NoHuddle),
        MakeSituationTempo(EPSGameSituation::FourMinuteOffense, EPSTempo::MilkClock, EPSTempo::Huddle),
        MakeSituationTempo(EPSGameSituation::VictoryFormation, EPSTempo::MilkClock, EPSTempo::MilkClock) };
    HumanTempoCycle = { EPSTempo::Huddle, EPSTempo::NoHuddle, EPSTempo::HurryUp };
    SidelineRouteIds = { TEXT("Out"), TEXT("Flat") };
    MiddleRouteIds = { TEXT("Slant"), TEXT("Curl"), TEXT("Post") };
    CategoryWeights = {
        MakeWeight(EPSGameSituation::TwoMinuteDrill, true, TEXT("Run"), -1.f, TEXT("Two-minute drill: a run keeps the clock moving")),
        MakeWeight(EPSGameSituation::TwoMinuteDrill, true, TEXT("DeepPass"), 1.f, TEXT("Two-minute drill: need chunks of yardage")),
        MakeWeight(EPSGameSituation::TwoMinuteDrill, true, TEXT("Screen"), 0.5f, TEXT("Two-minute drill: yards after the catch")),
        MakeWeight(EPSGameSituation::TwoMinuteDrill, false, TEXT("Prevent"), 1.5f, TEXT("Two-minute drill: keep everything in front")),
        MakeWeight(EPSGameSituation::TwoMinuteDrill, false, TEXT("Blitz"), -1.f, TEXT("Two-minute drill: don't give up the big play")),
        MakeWeight(EPSGameSituation::FourMinuteOffense, true, TEXT("Run"), 1.5f, TEXT("Four-minute offense: run and keep the clock moving")),
        MakeWeight(EPSGameSituation::FourMinuteOffense, true, TEXT("DeepPass"), -1.f, TEXT("Four-minute offense: an incompletion stops the clock")),
        MakeWeight(EPSGameSituation::FourMinuteOffense, true, TEXT("PlayAction"), -0.5f, TEXT("Four-minute offense: an incompletion stops the clock")),
        MakeWeight(EPSGameSituation::FourMinuteOffense, false, TEXT("Base"), 1.f, TEXT("Need a stop: load up against the run")),
        MakeWeight(EPSGameSituation::FourMinuteOffense, false, TEXT("Prevent"), -1.f, TEXT("Need the ball back: no soft coverage")) };
}

FString UPSSituationAI::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/situational_tuning.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSSituationAI::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSituationalTuning Loaded;
    if (!Ingestion->LoadSituationalTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSituationAI: Could not load situational tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = MoveTemp(Loaded);
    return true;
}

bool UPSSituationAI::ShouldKneelOut(const FPSSituationContext& Situation) const
{
    if (Situation.Down < 1 || Situation.Down > 4)
    {
        return false;
    }

    // End of the 1st half, backed up: nothing to gain, a turnover to lose.
    if (Situation.Quarter == 2)
    {
        return Situation.GameClockSeconds <= Tuning.EndOfHalfKneelSeconds && Situation.YardLine <= Tuning.EndOfHalfKneelMaxYardLine;
    }
    if (Situation.Quarter != 4 || Situation.ScoreDifferential <= 0)
    {
        return false;
    }

    // Kneels left this series, and the gaps between snaps the clock runs through: one before
    // each kneel after the first, plus one before the first while the clock runs now. The
    // defense stops one gap per timeout.
    const int32 Kneels = 5 - Situation.Down;
    const int32 Gaps = (Kneels - 1) + (Situation.bClockRunning ? 1 : 0);
    const int32 Stopped = FMath::Clamp(Situation.OpponentTimeoutsRemaining, 0, Gaps);
    const float Killable = Kneels * Tuning.KneelPlaySeconds + (Gaps - Stopped) * Tuning.KneelPreSnapSeconds;
    return Situation.GameClockSeconds <= Killable;
}

EPSGameSituation UPSSituationAI::ClassifySituation(const FPSSituationContext& Situation) const
{
    if (ShouldKneelOut(Situation))
    {
        return EPSGameSituation::VictoryFormation;
    }

    const float Clock = Situation.GameClockSeconds;
    if (Situation.Quarter == 2)
    {
        // Any score: points before the half.
        return Clock <= Tuning.TwoMinuteWindowSeconds ? EPSGameSituation::TwoMinuteDrill : EPSGameSituation::Normal;
    }
    if (Situation.Quarter != 4)
    {
        return EPSGameSituation::Normal;
    }
    if (Situation.ScoreDifferential > 0)
    {
        return Clock <= Tuning.FourMinuteWindowSeconds ? EPSGameSituation::FourMinuteOffense : EPSGameSituation::Normal;
    }
    if (Clock <= Tuning.TwoMinuteWindowSeconds)
    {
        return EPSGameSituation::TwoMinuteDrill;
    }
    if (Situation.ScoreDifferential < -Tuning.OneScorePoints && Clock <= Tuning.TwoScoreWindowSeconds)
    {
        return EPSGameSituation::TwoMinuteDrill;
    }
    return EPSGameSituation::Normal;
}

EPSTempo UPSSituationAI::ChooseTempo(const FPSSituationContext& Situation) const
{
    const EPSGameSituation Read = ClassifySituation(Situation);
    for (const FPSSituationTempoDef& Def : Tuning.SituationTempos)
    {
        if (Def.Situation == Read)
        {
            return Situation.bClockRunning ? Def.ClockRunningTempo : Def.ClockStoppedTempo;
        }
    }
    return EPSTempo::Huddle;
}

EPSTempo UPSSituationAI::GetTempoForPlay(const FPSPlayDefinition& Play, EPSTempo Tempo) const
{
    switch (PSSituation::ClockPlayFromCategory(Play.PlayCategory))
    {
    case EPSClockPlay::Spike: return Tuning.SpikeTempo;
    case EPSClockPlay::Kneel: return Tuning.KneelTempo;
    default:                  return Tempo;
    }
}

const FPSTempoDef* UPSSituationAI::FindTempo(EPSTempo Tempo) const
{
    return Tuning.Tempos.FindByPredicate([Tempo](const FPSTempoDef& Def) { return Def.Tempo == Tempo; });
}

float UPSSituationAI::GetSnapPlayClock(EPSTempo Tempo) const
{
    const FPSTempoDef* Def = FindTempo(Tempo);
    return Def ? Def->SnapAtPlayClockSeconds : -1.f;
}

EPSTempo UPSSituationAI::GetNextHumanTempo(EPSTempo Current) const
{
    const TArray<EPSTempo>& Cycle = Tuning.HumanTempoCycle;
    if (Cycle.Num() == 0)
    {
        return Current;
    }
    const int32 Index = Cycle.IndexOfByKey(Current);
    return Index == INDEX_NONE ? Cycle[0] : Cycle[(Index + 1) % Cycle.Num()];
}

EPSClockPlay UPSSituationAI::DecideClockPlay(const FPSSituationContext& Situation) const
{
    const EPSGameSituation Read = ClassifySituation(Situation);
    if (Read == EPSGameSituation::VictoryFormation)
    {
        return EPSClockPlay::Kneel;
    }

    // A timeout stops the clock without spending a down, so a spike is for when none is left.
    const bool bSpike = Read == EPSGameSituation::TwoMinuteDrill
        && Situation.bClockRunning
        && Situation.TimeoutsRemaining <= 0
        && Situation.GameClockSeconds <= Tuning.ClockUrgencySeconds
        && Situation.GameClockSeconds >= Tuning.SpikeMinSeconds
        && Situation.Down <= Tuning.MaxSpikeDown;
    return bSpike ? EPSClockPlay::Spike : EPSClockPlay::None;
}

bool UPSSituationAI::ShouldCallTimeout(const FPSSituationContext& Situation, bool bForOffense) const
{
    if (!Situation.bClockRunning)
    {
        return false;
    }

    const EPSGameSituation Read = ClassifySituation(Situation);
    if (bForOffense)
    {
        return Situation.TimeoutsRemaining > 0
            && Read == EPSGameSituation::TwoMinuteDrill
            && Situation.GameClockSeconds <= Tuning.ClockUrgencySeconds;
    }

    // The defense trails the four-minute offense and needs the ball back with time to use it.
    return Situation.OpponentTimeoutsRemaining > 0
        && Read == EPSGameSituation::FourMinuteOffense
        && Situation.GameClockSeconds <= Tuning.DefenseTimeoutWindowSeconds
        && Situation.ScoreDifferential <= Tuning.MaxDeficitToChase;
}

EPSBoundaryIntent UPSSituationAI::GetBoundaryIntent(const FPSSituationContext& Situation) const
{
    switch (ClassifySituation(Situation))
    {
    case EPSGameSituation::TwoMinuteDrill:    return EPSBoundaryIntent::GetOutOfBounds;
    case EPSGameSituation::FourMinuteOffense: return EPSBoundaryIntent::StayInbounds;
    case EPSGameSituation::VictoryFormation:  return EPSBoundaryIntent::StayInbounds;
    default:                                  return EPSBoundaryIntent::None;
    }
}

bool UPSSituationAI::IsSidelinePlay(const FPSPlayDefinition& Play) const
{
    return Play.Assignments.ContainsByPredicate([this](const FPSPlayAssignment& Assignment)
    {
        return Assignment.Kind == EPSAssignmentKind::Route && Tuning.SidelineRouteIds.Contains(Assignment.RouteId);
    });
}

bool UPSSituationAI::IsMiddlePlay(const FPSPlayDefinition& Play) const
{
    return !IsSidelinePlay(Play) && Play.Assignments.ContainsByPredicate([this](const FPSPlayAssignment& Assignment)
    {
        return Assignment.Kind == EPSAssignmentKind::Route && Tuning.MiddleRouteIds.Contains(Assignment.RouteId);
    });
}

float UPSSituationAI::GetPlayAdjustment(const FPSPlayDefinition& Play, const FPSSituationContext& Situation, bool bOffense, TArray<FString>* OutReasons, bool& bOutExcluded) const
{
    bOutExcluded = false;
    float Delta = 0.f;
    auto Apply = [&Delta, OutReasons](float Amount, const FString& Reason)
    {
        if (Amount == 0.f)
        {
            return;
        }
        Delta += Amount;
        if (OutReasons)
        {
            OutReasons->Add(FString::Printf(TEXT("%s (%+.1f)"), *Reason, Amount));
        }
    };

    // A spike or a kneel is only ever the clock's call.
    const EPSClockPlay ClockPlay = PSSituation::ClockPlayFromCategory(Play.PlayCategory);
    if (ClockPlay != EPSClockPlay::None)
    {
        if (!bOffense || DecideClockPlay(Situation) != ClockPlay)
        {
            bOutExcluded = true;
            return 0.f;
        }
        Apply(Tuning.ClockPlayWeight, ClockPlay == EPSClockPlay::Kneel
            ? FString(TEXT("Victory formation: kneel out the clock"))
            : FString(TEXT("Clock running, no timeouts: spike it")));
        return Delta;
    }

    const EPSGameSituation Read = ClassifySituation(Situation);
    for (const FPSSituationCategoryWeight& Weight : Tuning.CategoryWeights)
    {
        if (Weight.Situation == Read && Weight.bOffense == bOffense && Weight.Category == Play.PlayCategory)
        {
            Apply(Weight.Delta, Weight.Reason);
        }
    }

    // A run's routes are decoys; a pass's routes decide where the catch is made.
    if (bOffense && Read == EPSGameSituation::TwoMinuteDrill && Play.PlayCategory != TEXT("Run"))
    {
        if (IsSidelinePlay(Play))
        {
            Apply(Tuning.SidelinePlayDelta, TEXT("Two-minute drill: a sideline throw can stop the clock"));
        }
        else if (IsMiddlePlay(Play))
        {
            Apply(Tuning.MiddlePlayDelta, TEXT("Two-minute drill: a catch in the middle keeps the clock running"));
        }
    }
    return Delta;
}

FString UPSSituationAI::DescribeSituation(EPSGameSituation Situation)
{
    switch (Situation)
    {
    case EPSGameSituation::TwoMinuteDrill:    return TEXT("Two-minute drill");
    case EPSGameSituation::FourMinuteOffense: return TEXT("Four-minute offense");
    case EPSGameSituation::VictoryFormation:  return TEXT("Victory formation");
    default:                                  return FString();
    }
}
