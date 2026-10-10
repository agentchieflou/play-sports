#include "PSAudioSubsystem.h"
#include "PSDataIngestion.h"
#include "PSPerfBudget.h"
#include "PSSettingsSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"

namespace PSAudioSubsystemPrivate
{
    template <typename EnumType>
    FName EnumName(EnumType Value)
    {
        return FName(*StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value)));
    }

    /** The phases a play is live in: the snap until the whistle, and the kicks. */
    bool IsLivePhase(const FString& Phase)
    {
        return Phase == TEXT("Snap") || Phase == TEXT("PassRush") || Phase == TEXT("BallCarrierMovement")
            || Phase == TEXT("Kickoff") || Phase == TEXT("Punt") || Phase == TEXT("FieldGoal");
    }

    bool IsDeadPhase(const FString& Phase)
    {
        return Phase == TEXT("Scoring") || Phase == TEXT("PreSnap");
    }

    FPSAudioMoment MakeMoment(EPSAudioTrigger Trigger, FName Detail, float Intensity = 1.f, const FVector& Location = FVector::ZeroVector)
    {
        FPSAudioMoment Moment;
        Moment.Trigger = Trigger;
        Moment.Detail = Detail;
        Moment.Intensity = FMath::Clamp(Intensity, 0.f, 1.f);
        Moment.Location = Location;
        return Moment;
    }
}

void UPSAudioSubsystem::Deinitialize()
{
    UnbindFromBus();
    for (TPair<FName, FVoice>& Loop : Loops)
    {
        if (UAudioComponent* Component = Loop.Value.Component.Get())
        {
            Component->Stop();
        }
    }
    Loops.Reset();
    Voices.Reset();
    Super::Deinitialize();
}

bool UPSAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    BindToBus(InWorld.GetSubsystem<UPSTelemetryBus>());
    PreloadSounds();
    // The stadium's ambience runs from the start (Epic 23.4's zones shape it in the level).
    for (const FName& CueId : GetTuning().StartupLoops)
    {
        RequestCue(CueId, FVector::ZeroVector, 1.f);
    }
}

void UPSAudioSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSAudioSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSAudioSubsystem, STATGROUP_Tickables);
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSAudioSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/audio_cues.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSAudioTuning& UPSAudioSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSAudioSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    FPSAudioTuning Loaded;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!Ingestion->LoadAudioTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSAudioSubsystem: could not read %s"), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSAudioSubsystem::SetTuning(const FPSAudioTuning& InTuning)
{
    const TArray<FString> Problems = ValidateTuning(InTuning);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSAudioSubsystem: %s"), *Problem);
        }
        return false;
    }
    Tuning = InTuning;
    bTuningLoaded = true;
    LoadedSounds.Reset();
    return true;
}

TArray<FString> UPSAudioSubsystem::ValidateTuning(const FPSAudioTuning& InTuning)
{
    TArray<FString> Problems;
    TMap<FName, const FPSAudioCueDef*> ById;
    for (int32 Index = 0; Index < InTuning.Cues.Num(); ++Index)
    {
        const FPSAudioCueDef& Cue = InTuning.Cues[Index];
        if (Cue.CueId.IsNone() || ById.Contains(Cue.CueId))
        {
            Problems.Add(FString::Printf(TEXT("Cues[%d]: CueId is empty or used twice"), Index));
            continue;
        }
        ById.Add(Cue.CueId, &Cue);
        if (Cue.Volume < 0.f || Cue.Volume > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("Cues[%d] %s: Volume must be 0-1"), Index, *Cue.CueId.ToString()));
        }
        if (Cue.Priority < 0 || Cue.Priority > 100)
        {
            Problems.Add(FString::Printf(TEXT("Cues[%d] %s: Priority must be 0-100"), Index, *Cue.CueId.ToString()));
        }
        if (Cue.CooldownSeconds < 0.f || !(Cue.DurationSeconds > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("Cues[%d] %s: CooldownSeconds must be 0 or more and DurationSeconds above 0"), Index, *Cue.CueId.ToString()));
        }
        if (Cue.bLoop && Cue.LoopGroup.IsNone())
        {
            Problems.Add(FString::Printf(TEXT("Cues[%d] %s: a loop needs a LoopGroup"), Index, *Cue.CueId.ToString()));
        }
    }
    for (int32 Index = 0; Index < InTuning.EventCues.Num(); ++Index)
    {
        if (!ById.Contains(InTuning.EventCues[Index].CueId))
        {
            Problems.Add(FString::Printf(TEXT("EventCues[%d]: cue '%s' is not in Cues"), Index, *InTuning.EventCues[Index].CueId.ToString()));
        }
    }
    for (int32 Index = 0; Index < InTuning.StartupLoops.Num(); ++Index)
    {
        const FPSAudioCueDef* const* Cue = ById.Find(InTuning.StartupLoops[Index]);
        if (!Cue || !(*Cue)->bLoop)
        {
            Problems.Add(FString::Printf(TEXT("StartupLoops[%d]: '%s' is not a loop in Cues"), Index, *InTuning.StartupLoops[Index].ToString()));
        }
    }
    TSet<EPSAudioLayer> Layers;
    for (int32 Index = 0; Index < InTuning.LayerSettings.Num(); ++Index)
    {
        const FPSAudioLayerSetting& Setting = InTuning.LayerSettings[Index];
        if (Setting.SettingId.IsNone() || Layers.Contains(Setting.Layer))
        {
            Problems.Add(FString::Printf(TEXT("LayerSettings[%d]: SettingId is empty, or the layer has two"), Index));
        }
        Layers.Add(Setting.Layer);
    }
    if (!(InTuning.BigHitDamage > 0.f) || !(InTuning.FullIntensityDamage > 0.f) || !(InTuning.DeepPassCm > 0.f))
    {
        Problems.Add(TEXT("BigHitDamage, FullIntensityDamage and DeepPassCm must be above 0"));
    }
    if (InTuning.MaxRequestsKept < 1)
    {
        Problems.Add(TEXT("MaxRequestsKept must be 1 or more"));
    }
    return Problems;
}

const FPSAudioCueDef* UPSAudioSubsystem::FindCue(FName CueId)
{
    return GetTuning().Cues.FindByPredicate([CueId](const FPSAudioCueDef& Cue) { return Cue.CueId == CueId; });
}

// --- The platform tier -------------------------------------------------------------------

void UPSAudioSubsystem::ApplyPlatformTier(const FPSPlatformTier& Tier)
{
    UpdateIntervalSeconds = Tier.AudioUpdateHz > 0.f ? 1.f / Tier.AudioUpdateHz : 0.f;
    MaxVoices = FMath::Max(1, Tier.AudioMaxVoices);
}

// --- The bus -------------------------------------------------------------------------------

void UPSAudioSubsystem::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    ApplyPlatformTier(PSPlatformTiers::GetActiveTier());
    Bus->OnSnapMC.AddUObject(this, &UPSAudioSubsystem::HandleSnap);
    Bus->OnGameStateMC.AddUObject(this, &UPSAudioSubsystem::HandleGameState);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSAudioSubsystem::HandlePhaseChange);
    Bus->OnTackleMC.AddUObject(this, &UPSAudioSubsystem::HandleTackle);
    Bus->OnDamageMC.AddUObject(this, &UPSAudioSubsystem::HandleDamage);
    Bus->OnPassRushMoveMC.AddUObject(this, &UPSAudioSubsystem::HandlePassRush);
    Bus->OnThrowMC.AddUObject(this, &UPSAudioSubsystem::HandleThrow);
    Bus->OnCatchMC.AddUObject(this, &UPSAudioSubsystem::HandleCatch);
    Bus->OnFumbleMC.AddUObject(this, &UPSAudioSubsystem::HandleFumble);
    Bus->OnKickMC.AddUObject(this, &UPSAudioSubsystem::HandleKick);
    Bus->OnPlayResultMC.AddUObject(this, &UPSAudioSubsystem::HandlePlayResult);
    Bus->OnPenaltyMC.AddUObject(this, &UPSAudioSubsystem::HandlePenalty);
    Bus->OnTimeoutMC.AddUObject(this, &UPSAudioSubsystem::HandleTimeout);
    Bus->OnBoundaryCrossedMC.AddUObject(this, &UPSAudioSubsystem::HandleBoundaryCrossed);
    Bus->OnCrowdMC.AddUObject(this, &UPSAudioSubsystem::HandleCrowd);
    Bus->OnPreSnapMC.AddUObject(this, &UPSAudioSubsystem::HandlePreSnap);
    BoundBus = Bus;
}

void UPSAudioSubsystem::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnGameStateMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
        Bus->OnDamageMC.RemoveAll(this);
        Bus->OnPassRushMoveMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnFumbleMC.RemoveAll(this);
        Bus->OnKickMC.RemoveAll(this);
        Bus->OnPlayResultMC.RemoveAll(this);
        Bus->OnPenaltyMC.RemoveAll(this);
        Bus->OnTimeoutMC.RemoveAll(this);
        Bus->OnBoundaryCrossedMC.RemoveAll(this);
        Bus->OnCrowdMC.RemoveAll(this);
        Bus->OnPreSnapMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSAudioSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    bBallLive = true;
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Snap, NAME_None, 1.f, Event.LineOfScrimmage));
}

void UPSAudioSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    if (bHaveQuarter && Event.Quarter > LastQuarter)
    {
        // The horn: the half's at the end of the second quarter, the game's after the fourth.
        const FName Detail = Event.Quarter > 4 ? FName(TEXT("Final")) : LastQuarter == 2 ? FName(TEXT("Halftime")) : NAME_None;
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::QuarterEnd, Detail));
    }
    // A new game (the clock back in the first quarter) starts the quarters over.
    LastQuarter = Event.Quarter;
    bHaveQuarter = true;
    FollowPhase(Event.Phase);
}

void UPSAudioSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    FollowPhase(Event.NewPhase);
}

void UPSAudioSubsystem::FollowPhase(const FString& Phase)
{
    if (PSAudioSubsystemPrivate::IsLivePhase(Phase))
    {
        bBallLive = true;
    }
    else if (bBallLive && PSAudioSubsystemPrivate::IsDeadPhase(Phase))
    {
        // Once a play: the game state and the phase change both say the ball is dead.
        bBallLive = false;
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Whistle, NAME_None));
    }
}

void UPSAudioSubsystem::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Tackle, Event.bIsSack ? FName(TEXT("Sack")) : NAME_None));
}

void UPSAudioSubsystem::HandleDamage(const FPSTelemetryDamageEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    const FPSAudioTuning& Active = GetTuning();
    const FName Detail = Event.Amount >= Active.BigHitDamage ? FName(TEXT("Big")) : NAME_None;
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Hit, Detail, Event.Amount / Active.FullIntensityDamage));
}

void UPSAudioSubsystem::HandlePassRush(const FPSTelemetryPassRushEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Contact, Event.bWon ? FName(TEXT("Won")) : FName(TEXT("Stopped"))));
}

void UPSAudioSubsystem::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    const bool bDeep = FVector::Dist2D(Event.StartLocation, Event.TargetLocation) >= GetTuning().DeepPassCm;
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Throw, bDeep ? FName(TEXT("Deep")) : NAME_None, 1.f, Event.StartLocation));
}

void UPSAudioSubsystem::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Catch, Event.bIsInterception ? FName(TEXT("Interception")) : NAME_None, 1.f, Event.CatchLocation));
}

void UPSAudioSubsystem::HandleFumble(const FPSTelemetryFumbleEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Fumble, Event.bIsTurnover ? FName(TEXT("Turnover")) : NAME_None));
}

void UPSAudioSubsystem::HandleKick(const FPSTelemetryKickEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    if (!Event.bLiningUp)
    {
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Kick, FName(*Event.KickType)));
    }
}

void UPSAudioSubsystem::HandlePlayResult(const FPSTelemetryPlayResultEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    const int32 Points = Event.HomePoints + Event.AwayPoints;
    if (Points > 0)
    {
        // A touchdown (with its try) scores 6 or more, whoever carried it in: a return, a pick-six.
        const FName Detail = Points >= 6 ? FName(TEXT("Touchdown"))
            : Event.Result == TEXT("FieldGoalGood") ? FName(TEXT("FieldGoalGood"))
            : Event.Result == TEXT("Safety") ? FName(TEXT("Safety"))
            : FName(*Event.Result);
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Score, Detail));
    }
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::PlayResult, FName(*Event.Result)));
}

void UPSAudioSubsystem::HandlePenalty(const FPSTelemetryPenaltyEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    if (Event.Kind == EPSPenaltyEventKind::Flag)
    {
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Flag, FName(*Event.Penalty)));
    }
}

void UPSAudioSubsystem::HandleTimeout(const FPSTelemetryTimeoutEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Timeout, NAME_None));
}

void UPSAudioSubsystem::HandleBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    if (Event.bEndZone && Event.YardLine >= 100 && !Event.CarrierName.IsEmpty())
    {
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::GoalLine, NAME_None));
    }
}

void UPSAudioSubsystem::HandleCrowd(const FPSTelemetryCrowdEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    if (Event.Kind == EPSCrowdEventKind::Level)
    {
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::CrowdLevel, PSAudioSubsystemPrivate::EnumName(Event.Level), Event.Excitement));
    }
    else
    {
        HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::CrowdReaction, PSAudioSubsystemPrivate::EnumName(Event.Reaction), Event.Excitement));
    }
}

void UPSAudioSubsystem::HandlePreSnap(const FPSTelemetryPreSnapEvent& Event)
{
    PS_PERF_SCOPE(Audio);
    HandleMoment(PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Cadence, PSAudioSubsystemPrivate::EnumName(Event.Action)));
}

// --- Cues --------------------------------------------------------------------------------

int32 UPSAudioSubsystem::HandleMoment(const FPSAudioMoment& Moment)
{
    int32 Played = 0;
    const FPSAudioTuning& Active = GetTuning();
    for (const FPSAudioEventCue& Rule : Active.EventCues)
    {
        if (Rule.Trigger != Moment.Trigger || (!Rule.Detail.IsNone() && Rule.Detail != Moment.Detail))
        {
            continue;
        }
        if (const FPSAudioCueDef* Def = FindCue(Rule.CueId))
        {
            Played += PlayCue(*Def, Moment) ? 1 : 0;
        }
    }
    return Played;
}

bool UPSAudioSubsystem::RequestCue(FName CueId, FVector Location, float Intensity)
{
    PS_PERF_SCOPE(Audio);
    const FPSAudioMoment Moment = PSAudioSubsystemPrivate::MakeMoment(EPSAudioTrigger::Manual, NAME_None, Intensity, Location);
    if (const FPSAudioCueDef* Def = FindCue(CueId))
    {
        return PlayCue(*Def, Moment);
    }
    FPSAudioCueRequest Unknown;
    Unknown.CueId = CueId;
    Unknown.Trigger = EPSAudioTrigger::Manual;
    Unknown.Time = Clock;
    Unknown.Location = Location;
    Unknown.Intensity = Moment.Intensity;
    Unknown.DropReason = EPSAudioDropReason::UnknownCue;
    Record(Unknown);
    return false;
}

bool UPSAudioSubsystem::PlayCue(const FPSAudioCueDef& Def, const FPSAudioMoment& Moment)
{
    FPSAudioCueRequest Request;
    Request.CueId = Def.CueId;
    Request.Layer = Def.Layer;
    Request.Trigger = Moment.Trigger;
    Request.Detail = Moment.Detail;
    Request.Time = Clock;
    Request.Intensity = Moment.Intensity;
    Request.Location = Moment.Location;
    Request.Volume = Def.Volume * GetLayerVolume(Def.Layer) * (Def.bScaleByIntensity ? Moment.Intensity : 1.f);

    const float* Last = LastPlayed.Find(Def.CueId);
    if (Last && Clock - *Last < Def.CooldownSeconds)
    {
        Request.DropReason = EPSAudioDropReason::Cooldown;
    }
    else if (!(Request.Volume > 0.f))
    {
        Request.DropReason = EPSAudioDropReason::Muted;
    }
    else if (Def.bLoop)
    {
        // One loop a group: the new bed replaces the old one (Epic 97 crossfades them).
        StopLoop(Def.LoopGroup);
        FVoice Loop;
        Loop.CueId = Def.CueId;
        Loop.Priority = Def.Priority;
        Loop.Component = StartSound(Def, Request.Volume, Moment.Location);
        Request.bSounded = Loop.Component.IsValid();
        Loops.Add(Def.LoopGroup, Loop);
        LoopBaseVolume.Add(Def.LoopGroup, Def.Volume * (Def.bScaleByIntensity ? Moment.Intensity : 1.f));
    }
    else
    {
        // Every voice busy: take the lowest-priority one below this cue (the soonest to end of
        // those), or give way.
        Voices.RemoveAll([this](const FVoice& Voice) { return Voice.EndsAt <= Clock; });
        if (Voices.Num() >= MaxVoices)
        {
            int32 Weakest = INDEX_NONE;
            for (int32 Index = 0; Index < Voices.Num(); ++Index)
            {
                if (Weakest == INDEX_NONE || Voices[Index].Priority < Voices[Weakest].Priority
                    || (Voices[Index].Priority == Voices[Weakest].Priority && Voices[Index].EndsAt < Voices[Weakest].EndsAt))
                {
                    Weakest = Index;
                }
            }
            if (Weakest != INDEX_NONE && Voices[Weakest].Priority < Def.Priority)
            {
                if (UAudioComponent* Stolen = Voices[Weakest].Component.Get())
                {
                    Stolen->Stop();
                }
                Voices.RemoveAt(Weakest);
            }
            else
            {
                Request.DropReason = EPSAudioDropReason::VoiceLimit;
            }
        }
        if (Request.DropReason == EPSAudioDropReason::None)
        {
            FVoice Voice;
            Voice.CueId = Def.CueId;
            Voice.Priority = Def.Priority;
            Voice.Component = StartSound(Def, Request.Volume, Moment.Location);
            Request.bSounded = Voice.Component.IsValid();
            USoundBase* const* Sound = LoadedSounds.Find(Def.CueId);
            const float Length = Sound && *Sound ? (*Sound)->GetDuration() : 0.f;
            Voice.EndsAt = Clock + (Length > 0.f && Length < INDEFINITELY_LOOPING_DURATION ? Length : Def.DurationSeconds);
            Voices.Add(Voice);
        }
    }

    if (Request.DropReason == EPSAudioDropReason::None)
    {
        LastPlayed.Add(Def.CueId, Clock);
    }
    Record(Request);
    return Request.DropReason == EPSAudioDropReason::None;
}

void UPSAudioSubsystem::StopLoop(FName LoopGroup)
{
    if (FVoice* Loop = Loops.Find(LoopGroup))
    {
        if (UAudioComponent* Component = Loop->Component.Get())
        {
            Component->Stop();
        }
        Loops.Remove(LoopGroup);
        LoopBaseVolume.Remove(LoopGroup);
    }
}

FName UPSAudioSubsystem::GetActiveLoop(FName LoopGroup) const
{
    const FVoice* Loop = Loops.Find(LoopGroup);
    return Loop ? Loop->CueId : NAME_None;
}

UAudioComponent* UPSAudioSubsystem::StartSound(const FPSAudioCueDef& Def, float Volume, const FVector& Location)
{
    USoundBase* const* Found = LoadedSounds.Find(Def.CueId);
    USoundBase* Sound = Found ? *Found : nullptr;
    if (!Sound || !GetWorld())
    {
        return nullptr;
    }
    return Def.bSpatial
        ? UGameplayStatics::SpawnSoundAtLocation(this, Sound, Location, FRotator::ZeroRotator, Volume)
        : UGameplayStatics::SpawnSound2D(this, Sound, Volume);
}

void UPSAudioSubsystem::PreloadSounds()
{
    // At the start of the match, so no cue loads in the middle of a play.
    for (const FPSAudioCueDef& Cue : GetTuning().Cues)
    {
        if (!Cue.SoundPath.IsEmpty() && !LoadedSounds.Contains(Cue.CueId))
        {
            if (USoundBase* Sound = Cast<USoundBase>(FSoftObjectPath(Cue.SoundPath).TryLoad()))
            {
                LoadedSounds.Add(Cue.CueId, Sound);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("UPSAudioSubsystem: cue %s's sound %s did not load"), *Cue.CueId.ToString(), *Cue.SoundPath);
            }
        }
    }
}

UPSSettingsSubsystem* UPSAudioSubsystem::GetSettings() const
{
    return SettingsOverride ? SettingsOverride : UPSSettingsSubsystem::Get(this);
}

float UPSAudioSubsystem::GetLayerVolume(EPSAudioLayer Layer)
{
    UPSSettingsSubsystem* Settings = GetSettings();
    const FPSAudioLayerSetting* Setting = GetTuning().LayerSettings.FindByPredicate([Layer](const FPSAudioLayerSetting& Entry) { return Entry.Layer == Layer; });
    if (!Settings || !Setting || !Settings->GetCatalog().FindSetting(Setting->SettingId))
    {
        return 1.f;
    }
    // The settings are percentages (Data/ui_settings.json).
    return FMath::Clamp(Settings->GetNumber(Setting->SettingId) / 100.f, 0.f, 1.f);
}

void UPSAudioSubsystem::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;
    SinceUpdate += DeltaSeconds;
    if (SinceUpdate < UpdateIntervalSeconds)
    {
        return;
    }
    SinceUpdate = 0.f;
    PS_PERF_SCOPE(Audio);
    Voices.RemoveAll([this](const FVoice& Voice) { return Voice.EndsAt <= Clock; });
    // The loops follow the volume settings as the player moves them.
    for (TPair<FName, FVoice>& Loop : Loops)
    {
        UAudioComponent* Component = Loop.Value.Component.Get();
        const FPSAudioCueDef* Def = Component ? FindCue(Loop.Value.CueId) : nullptr;
        const float* Base = LoopBaseVolume.Find(Loop.Key);
        if (Def && Base)
        {
            Component->SetVolumeMultiplier(*Base * GetLayerVolume(Def->Layer));
        }
    }
}

// --- The log -------------------------------------------------------------------------------

void UPSAudioSubsystem::Record(const FPSAudioCueRequest& Request)
{
    RequestLog.Add(Request);
    const int32 Excess = RequestLog.Num() - FMath::Max(1, GetTuning().MaxRequestsKept);
    if (Excess > 0)
    {
        RequestLog.RemoveAt(0, Excess);
    }
    OnCueRequestedMC.Broadcast(Request);
}

int32 UPSAudioSubsystem::CountRequests(FName CueId, bool bIncludeDropped) const
{
    int32 Count = 0;
    for (const FPSAudioCueRequest& Request : RequestLog)
    {
        Count += Request.CueId == CueId && (bIncludeDropped || !Request.WasDropped()) ? 1 : 0;
    }
    return Count;
}

bool UPSAudioSubsystem::FindLastRequest(FName CueId, FPSAudioCueRequest& OutRequest) const
{
    for (int32 Index = RequestLog.Num() - 1; Index >= 0; --Index)
    {
        if (RequestLog[Index].CueId == CueId)
        {
            OutRequest = RequestLog[Index];
            return true;
        }
    }
    return false;
}
