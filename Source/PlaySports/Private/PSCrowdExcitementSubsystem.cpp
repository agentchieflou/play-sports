#include "PSCrowdExcitementSubsystem.h"
#include "PSDataIngestion.h"
#include "PSPerfBudget.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSCrowdExcitementPrivate
{
    const FPSCrowdReactionDef* FindReaction(const FPSCrowdTuning& InTuning, EPSCrowdStimulus Stimulus)
    {
        return InTuning.CrowdReactions.FindByPredicate([Stimulus](const FPSCrowdReactionDef& Def) { return Def.Stimulus == Stimulus; });
    }

    float FindThreshold(const FPSCrowdTuning& InTuning, EPSCrowdLevel InLevel)
    {
        const FPSCrowdLevelThreshold* Found = InTuning.Levels.FindByPredicate([InLevel](const FPSCrowdLevelThreshold& Entry) { return Entry.Level == InLevel; });
        return Found ? Found->MinExcitement : 0.f;
    }
}

void UPSCrowdExcitementSubsystem::Deinitialize()
{
    UnbindFromBus();
    Super::Deinitialize();
}

bool UPSCrowdExcitementSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSCrowdExcitementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    BindToBus(InWorld.GetSubsystem<UPSTelemetryBus>());
}

void UPSCrowdExcitementSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSCrowdExcitementSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSCrowdExcitementSubsystem, STATGROUP_Tickables);
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSCrowdExcitementSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/crowd.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSCrowdTuning& UPSCrowdExcitementSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSCrowdExcitementSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    FPSCrowdTuning Loaded;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!Ingestion->LoadCrowdTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCrowdExcitementSubsystem: could not read %s"), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSCrowdExcitementSubsystem::SetTuning(const FPSCrowdTuning& InTuning)
{
    const TArray<FString> Problems = ValidateTuning(InTuning);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSCrowdExcitementSubsystem: %s"), *Problem);
        }
        return false;
    }
    Tuning = InTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSCrowdExcitementSubsystem::ValidateTuning(const FPSCrowdTuning& InTuning)
{
    TArray<FString> Problems;
    const auto InUnit = [](float Value) { return Value >= 0.f && Value <= 1.f; };
    if (!InUnit(InTuning.RestingExcitement) || !InUnit(InTuning.LateCloseBonus) || !InUnit(InTuning.DefaultHomeShare)
        || !InUnit(InTuning.LevelHysteresis))
    {
        Problems.Add(TEXT("RestingExcitement, LateCloseBonus, DefaultHomeShare and LevelHysteresis must be 0-1"));
    }
    if (!(InTuning.HalfLifeSeconds > 0.f) || !(InTuning.BigHitDamage > 0.f) || !(InTuning.DeepPassCm > 0.f) || InTuning.BigGainYards < 1)
    {
        Problems.Add(TEXT("HalfLifeSeconds, BigHitDamage, DeepPassCm and BigGainYards must be above 0"));
    }
    if (InTuning.LateGameQuarter < 1 || InTuning.CloseGameMargin < 0)
    {
        Problems.Add(TEXT("LateGameQuarter must be 1 or more and CloseGameMargin 0 or more"));
    }

    // Every level once, Hush at 0, each threshold above the quieter level's.
    const UEnum* LevelEnum = StaticEnum<EPSCrowdLevel>();
    float Previous = -1.f;
    for (int32 Index = 0; LevelEnum && Index < LevelEnum->NumEnums() - 1; ++Index)
    {
        const EPSCrowdLevel Each = static_cast<EPSCrowdLevel>(LevelEnum->GetValueByIndex(Index));
        int32 Count = 0;
        float Threshold = 0.f;
        for (const FPSCrowdLevelThreshold& Entry : InTuning.Levels)
        {
            if (Entry.Level == Each)
            {
                ++Count;
                Threshold = Entry.MinExcitement;
            }
        }
        const FString Name = LevelEnum->GetNameStringByIndex(Index);
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("Levels: %s needs exactly one threshold"), *Name));
            continue;
        }
        if (Index == 0 ? Threshold != 0.f : !(Threshold > Previous && Threshold <= 1.f))
        {
            Problems.Add(FString::Printf(TEXT("Levels: %s's MinExcitement must be %s"), *Name, Index == 0 ? TEXT("0") : TEXT("above the quieter level's, at most 1")));
        }
        Previous = Threshold;
    }

    // Every stimulus once.
    const UEnum* StimulusEnum = StaticEnum<EPSCrowdStimulus>();
    for (int32 Index = 0; StimulusEnum && Index < StimulusEnum->NumEnums() - 1; ++Index)
    {
        const EPSCrowdStimulus Each = static_cast<EPSCrowdStimulus>(StimulusEnum->GetValueByIndex(Index));
        int32 Count = 0;
        for (const FPSCrowdReactionDef& Def : InTuning.CrowdReactions)
        {
            Count += Def.Stimulus == Each ? 1 : 0;
        }
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("CrowdReactions: %s needs exactly one entry"), *StimulusEnum->GetNameStringByIndex(Index)));
        }
    }
    for (int32 Index = 0; Index < InTuning.CrowdReactions.Num(); ++Index)
    {
        const FPSCrowdReactionDef& Def = InTuning.CrowdReactions[Index];
        if (FMath::Abs(Def.FansDelta) > 1.f || FMath::Abs(Def.RivalsDelta) > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("CrowdReactions[%d]: FansDelta and RivalsDelta must be -1..1"), Index));
        }
    }
    return Problems;
}

// --- The model -----------------------------------------------------------------------------

float UPSCrowdExcitementSubsystem::ComputeRestingExcitement(const FPSCrowdTuning& InTuning, int32 InQuarter, int32 InHomeScore, int32 InAwayScore)
{
    const bool bLateAndClose = InQuarter >= InTuning.LateGameQuarter && FMath::Abs(InHomeScore - InAwayScore) <= InTuning.CloseGameMargin;
    return FMath::Clamp(InTuning.RestingExcitement + (bLateAndClose ? InTuning.LateCloseBonus : 0.f), 0.f, 1.f);
}

float UPSCrowdExcitementSubsystem::SettleToward(float InExcitement, float Rest, float HalfLifeSeconds, float Seconds)
{
    if (!(HalfLifeSeconds > 0.f) || Seconds <= 0.f)
    {
        return InExcitement;
    }
    return Rest + (InExcitement - Rest) * FMath::Pow(0.5f, Seconds / HalfLifeSeconds);
}

EPSCrowdLevel UPSCrowdExcitementSubsystem::RateLevel(const FPSCrowdTuning& InTuning, float InExcitement, EPSCrowdLevel Current)
{
    EPSCrowdLevel Raw = EPSCrowdLevel::Hush;
    for (const FPSCrowdLevelThreshold& Entry : InTuning.Levels)
    {
        if (InExcitement >= Entry.MinExcitement && Entry.Level > Raw)
        {
            Raw = Entry.Level;
        }
    }
    if (Raw >= Current)
    {
        return Raw;
    }
    // Down only once clearly under the current level.
    const float CurrentThreshold = PSCrowdExcitementPrivate::FindThreshold(InTuning, Current);
    return InExcitement < CurrentThreshold - InTuning.LevelHysteresis ? Raw : Current;
}

float UPSCrowdExcitementSubsystem::ExcitementDelta(const FPSCrowdReactionDef& Def, float FansShare)
{
    const float Share = FMath::Clamp(FansShare, 0.f, 1.f);
    return Share * Def.FansDelta + (1.f - Share) * Def.RivalsDelta;
}

EPSCrowdReaction UPSCrowdExcitementSubsystem::ChooseReaction(const FPSCrowdReactionDef& Def, float FansShare)
{
    return FansShare >= 0.5f ? Def.FansReaction : Def.RivalsReaction;
}

// --- The match -----------------------------------------------------------------------------

void UPSCrowdExcitementSubsystem::SetMatchContext(FName InHomeTeamId, FName InAwayTeamId, float InHomeShare)
{
    HomeTeamId = InHomeTeamId;
    AwayTeamId = InAwayTeamId;
    HomeShare = InHomeShare >= 0.f ? FMath::Min(InHomeShare, 1.f) : -1.f;
}

float UPSCrowdExcitementSubsystem::GetHomeShare()
{
    return HomeShare >= 0.f ? HomeShare : GetTuning().DefaultHomeShare;
}

// --- The crowd -----------------------------------------------------------------------------

void UPSCrowdExcitementSubsystem::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    ApplyPlatformTier(PSPlatformTiers::GetActiveTier());
    Bus->OnSnapMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleSnap);
    Bus->OnGameStateMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleGameState);
    Bus->OnThrowMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleThrow);
    Bus->OnCatchMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleCatch);
    Bus->OnTackleMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleTackle);
    Bus->OnDamageMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleDamage);
    Bus->OnFumbleMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleFumble);
    Bus->OnBoundaryCrossedMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandleBoundaryCrossed);
    Bus->OnPenaltyMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandlePenalty);
    Bus->OnPlayResultMC.AddUObject(this, &UPSCrowdExcitementSubsystem::HandlePlayResult);
    BoundBus = Bus;

    // The crowd starts at rest; the game's first state announces its level (AnnounceLevel), by
    // when everything that plays the crowd is listening.
    Excitement = GetRestingExcitement();
    Level = RateLevel(GetTuning(), Excitement, EPSCrowdLevel::Hush);
    bLevelAnnounced = false;
}

void UPSCrowdExcitementSubsystem::AnnounceLevel()
{
    bLevelAnnounced = true;
    FPSTelemetryCrowdEvent Announce;
    Announce.Kind = EPSCrowdEventKind::Level;
    Announce.Level = Level;
    Announce.Excitement = Excitement;
    Publish(Announce);
}

void UPSCrowdExcitementSubsystem::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnGameStateMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
        Bus->OnDamageMC.RemoveAll(this);
        Bus->OnFumbleMC.RemoveAll(this);
        Bus->OnBoundaryCrossedMC.RemoveAll(this);
        Bus->OnPenaltyMC.RemoveAll(this);
        Bus->OnPlayResultMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSCrowdExcitementSubsystem::ApplyPlatformTier(const FPSPlatformTier& Tier)
{
    UpdateIntervalSeconds = Tier.CrowdUpdateHz > 0.f ? 1.f / Tier.CrowdUpdateHz : 0.f;
}

float UPSCrowdExcitementSubsystem::GetRestingExcitement()
{
    return ComputeRestingExcitement(GetTuning(), Quarter, HomeScore, AwayScore);
}

EPSCrowdReaction UPSCrowdExcitementSubsystem::ApplyStimulus(EPSCrowdStimulus Stimulus, bool bHomeBenefits, float Scale)
{
    const FPSCrowdReactionDef* Def = PSCrowdExcitementPrivate::FindReaction(GetTuning(), Stimulus);
    if (!Def)
    {
        return EPSCrowdReaction::None;
    }
    const float FansShare = bHomeBenefits ? GetHomeShare() : 1.f - GetHomeShare();
    Excitement = FMath::Clamp(Excitement + ExcitementDelta(*Def, FansShare) * FMath::Max(Scale, 0.f), 0.f, 1.f);
    const EPSCrowdReaction Reaction = ChooseReaction(*Def, FansShare);
    if (Reaction != EPSCrowdReaction::None)
    {
        LastReaction = Reaction;
        FPSTelemetryCrowdEvent Event;
        Event.Kind = EPSCrowdEventKind::Reaction;
        Event.Level = Level;
        Event.Reaction = Reaction;
        Event.Stimulus = FName(*StaticEnum<EPSCrowdStimulus>()->GetNameStringByValue(static_cast<int64>(Stimulus)));
        Event.Excitement = Excitement;
        Event.bHomeFavoured = bHomeBenefits;
        Publish(Event);
    }
    UpdateLevel();
    return Reaction;
}

void UPSCrowdExcitementSubsystem::AdvanceTime(float DeltaSeconds)
{
    SinceUpdate += DeltaSeconds;
    if (SinceUpdate < UpdateIntervalSeconds)
    {
        return;
    }
    PS_PERF_SCOPE(Crowd);
    Excitement = SettleToward(Excitement, GetRestingExcitement(), GetTuning().HalfLifeSeconds, SinceUpdate);
    SinceUpdate = 0.f;
    UpdateLevel();
}

void UPSCrowdExcitementSubsystem::UpdateLevel()
{
    const EPSCrowdLevel Rated = RateLevel(GetTuning(), Excitement, Level);
    if (Rated == Level)
    {
        return;
    }
    Level = Rated;
    FPSTelemetryCrowdEvent Event;
    Event.Kind = EPSCrowdEventKind::Level;
    Event.Level = Level;
    Event.Excitement = Excitement;
    Publish(Event);
}

void UPSCrowdExcitementSubsystem::Publish(const FPSTelemetryCrowdEvent& Event)
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->PublishCrowd(Event);
    }
}

// --- The bus's moments -----------------------------------------------------------------------

void UPSCrowdExcitementSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bTouchdownThisPlay = false;
    bTurnoverThisPlay = false;
    bSackThisPlay = false;
}

void UPSCrowdExcitementSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    // A new game (the drive count went back): the crowd starts again at rest.
    const bool bNewGame = Event.CompletedDrives < CompletedDrives;
    Quarter = Event.Quarter;
    HomeScore = Event.HomeScore;
    AwayScore = Event.AwayScore;
    bHomeOffense = Event.bHomeHasPossession;
    CompletedDrives = Event.CompletedDrives;
    if (bNewGame)
    {
        Excitement = GetRestingExcitement();
        Level = RateLevel(GetTuning(), Excitement, EPSCrowdLevel::Hush);
    }
    if (bNewGame || !bLevelAnnounced)
    {
        AnnounceLevel();
    }
}

void UPSCrowdExcitementSubsystem::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (FVector::Dist2D(Event.StartLocation, Event.TargetLocation) >= GetTuning().DeepPassCm)
    {
        ApplyStimulus(EPSCrowdStimulus::DeepPass, bHomeOffense);
    }
}

void UPSCrowdExcitementSubsystem::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (Event.bIsInterception && !bTurnoverThisPlay)
    {
        bTurnoverThisPlay = true;
        ApplyStimulus(EPSCrowdStimulus::Interception, !bHomeOffense);
    }
}

void UPSCrowdExcitementSubsystem::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (Event.bIsSack && !bSackThisPlay)
    {
        bSackThisPlay = true;
        ApplyStimulus(EPSCrowdStimulus::Sack, !bHomeOffense);
    }
}

void UPSCrowdExcitementSubsystem::HandleDamage(const FPSTelemetryDamageEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (Event.Amount >= GetTuning().BigHitDamage)
    {
        ApplyStimulus(EPSCrowdStimulus::BigHit, !bHomeOffense);
    }
}

void UPSCrowdExcitementSubsystem::HandleFumble(const FPSTelemetryFumbleEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (Event.bIsTurnover && !bTurnoverThisPlay)
    {
        bTurnoverThisPlay = true;
        ApplyStimulus(EPSCrowdStimulus::FumbleLost, !bHomeOffense);
    }
}

void UPSCrowdExcitementSubsystem::HandleBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (!Event.bEndZone || Event.CarrierName.IsEmpty() || bTouchdownThisPlay)
    {
        return;
    }
    // The offense's carrier into the end zone it attacks, or a defender returning a turnover into
    // the offense's own.
    if (Event.YardLine >= 100 && !bTurnoverThisPlay)
    {
        bTouchdownThisPlay = true;
        ApplyStimulus(EPSCrowdStimulus::Touchdown, bHomeOffense);
    }
    else if (Event.YardLine <= 0 && bTurnoverThisPlay)
    {
        bTouchdownThisPlay = true;
        ApplyStimulus(EPSCrowdStimulus::Touchdown, !bHomeOffense);
    }
}

void UPSCrowdExcitementSubsystem::HandlePenalty(const FPSTelemetryPenaltyEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    if (Event.Kind == EPSPenaltyEventKind::Flag)
    {
        ApplyStimulus(EPSCrowdStimulus::Flag, !Event.bHomeTeam);
    }
}

void UPSCrowdExcitementSubsystem::HandlePlayResult(const FPSTelemetryPlayResultEvent& Event)
{
    PS_PERF_SCOPE(Crowd);
    // The authority's word on the play: what the crowd saw live isn't repeated.
    const bool bHomeOff = Event.bHomeOffense;
    const int32 Points = Event.HomePoints + Event.AwayPoints;
    if (Points > 0)
    {
        const bool bHomeScored = Event.HomePoints > Event.AwayPoints;
        if (Event.Result == TEXT("FieldGoalGood"))
        {
            ApplyStimulus(EPSCrowdStimulus::FieldGoalGood, bHomeScored);
        }
        else if (Event.Result == TEXT("Safety"))
        {
            ApplyStimulus(EPSCrowdStimulus::Safety, bHomeScored);
        }
        else if (!bTouchdownThisPlay)
        {
            ApplyStimulus(EPSCrowdStimulus::Touchdown, bHomeScored);
        }
    }
    else if (Event.Result == TEXT("FieldGoalMissed"))
    {
        ApplyStimulus(EPSCrowdStimulus::FieldGoalMissed, !bHomeOff);
    }
    else if (Event.bInterception)
    {
        if (!bTurnoverThisPlay)
        {
            ApplyStimulus(EPSCrowdStimulus::Interception, !bHomeOff);
        }
    }
    else if (Event.bTurnoverOnDowns)
    {
        ApplyStimulus(EPSCrowdStimulus::TurnoverOnDowns, !bHomeOff);
    }
    else if (Event.bSack)
    {
        if (!bSackThisPlay)
        {
            ApplyStimulus(EPSCrowdStimulus::Sack, !bHomeOff);
        }
    }
    else if (Event.YardsGained >= GetTuning().BigGainYards)
    {
        ApplyStimulus(EPSCrowdStimulus::BigGain, bHomeOff);
    }
    else if (Event.bFirstDown)
    {
        ApplyStimulus(EPSCrowdStimulus::FirstDown, bHomeOff);
    }
    else if (Event.bPass && !Event.bComplete)
    {
        ApplyStimulus(EPSCrowdStimulus::Incompletion, !bHomeOff);
    }
    bTouchdownThisPlay = false;
    bTurnoverThisPlay = false;
    bSackThisPlay = false;
}
