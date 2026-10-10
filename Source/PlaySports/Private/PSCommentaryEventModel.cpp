#include "PSCommentaryEventModel.h"
#include "PSDataIngestion.h"
#include "PSGameIntelligenceSubsystem.h"
#include "PSGameStateEvents.h"
#include "PSPerfBudget.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PSCommentaryEventModelPrivate
{
    /** Centimeters in a yard. */
    constexpr float CmPerYard = 91.44f;

    /** A name shortened to Max characters. */
    FString Clip(const FString& Name, int32 Max)
    {
        return Name.Len() <= Max ? Name : Name.Left(FMath::Max(0, Max));
    }

    FString WriteJson(const TSharedRef<FJsonObject>& Object)
    {
        FString Out;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
        FJsonSerializer::Serialize(Object, Writer);
        return Out;
    }

    /** The facts of Moment, with its players' names at most NameChars long (0 leaves them out). */
    FString BuildDescription(const FPSTelemetryCommentaryEvent& Moment, int32 NameChars)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("moment"), StaticEnum<EPSCommentaryMoment>()->GetNameStringByValue(static_cast<int64>(Moment.Moment)));
        if (!Moment.Detail.IsNone())
        {
            Object->SetStringField(TEXT("detail"), Moment.Detail.ToString());
        }
        Object->SetNumberField(TEXT("play"), Moment.PlayNumber);
        Object->SetStringField(TEXT("quarter"), PSGameStateEvents::QuarterLabel(Moment.Quarter));
        Object->SetStringField(TEXT("clock"), PSGameStateEvents::ClockText(Moment.GameClockSeconds));
        Object->SetNumberField(TEXT("down"), Moment.Down);
        Object->SetNumberField(TEXT("distance"), Moment.Distance);
        Object->SetNumberField(TEXT("yardLine"), Moment.YardLine);
        Object->SetStringField(TEXT("offense"), Moment.bHomeOffense ? TEXT("home") : TEXT("away"));
        Object->SetNumberField(TEXT("homeScore"), Moment.HomeScore);
        Object->SetNumberField(TEXT("awayScore"), Moment.AwayScore);
        if (NameChars > 0 && !Moment.PrimaryName.IsEmpty())
        {
            Object->SetStringField(TEXT("player"), Clip(Moment.PrimaryName, NameChars));
        }
        if (NameChars > 0 && !Moment.SecondaryName.IsEmpty())
        {
            Object->SetStringField(TEXT("otherPlayer"), Clip(Moment.SecondaryName, NameChars));
        }
        Object->SetNumberField(TEXT("yards"), Moment.Yards);
        Object->SetNumberField(TEXT("points"), Moment.Points);
        if (Moment.Magnitude != 0.f)
        {
            Object->SetNumberField(TEXT("magnitude"), FMath::RoundToFloat(Moment.Magnitude * 10.f) / 10.f);
        }
        Object->SetBoolField(TEXT("firstDown"), Moment.bFirstDown);
        Object->SetBoolField(TEXT("turnover"), Moment.bTurnover);
        Object->SetStringField(TEXT("favours"), Moment.bHomeFavoured ? TEXT("home") : TEXT("away"));
        return WriteJson(Object);
    }
}

void UPSCommentaryEventModel::Deinitialize()
{
    UnbindFromBus();
    SetIntelligence(nullptr);
    Super::Deinitialize();
}

bool UPSCommentaryEventModel::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSCommentaryEventModel::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    BindToBus(InWorld.GetSubsystem<UPSTelemetryBus>());
    SetIntelligence(InWorld.GetSubsystem<UPSGameIntelligenceSubsystem>());
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSCommentaryEventModel::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/commentary_hooks.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSCommentaryHookTuning& UPSCommentaryEventModel::GetTuning()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSCommentaryEventModel::LoadTuningFromJson(const FString& JsonFilePath)
{
    FPSCommentaryHookTuning Loaded;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!Ingestion->LoadCommentaryHookTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCommentaryEventModel: could not read %s"), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSCommentaryEventModel::SetTuning(const FPSCommentaryHookTuning& InTuning)
{
    const TArray<FString> Problems = ValidateTuning(InTuning);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSCommentaryEventModel: %s"), *Problem);
        }
        return false;
    }
    Tuning = InTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSCommentaryEventModel::ValidateTuning(const FPSCommentaryHookTuning& InTuning)
{
    TArray<FString> Problems;
    if (!(InTuning.BigHitDamage > 0.f) || !(InTuning.DeepPassCm > 0.f) || !(InTuning.TwoMinuteWarningSeconds > 0.f))
    {
        Problems.Add(TEXT("BigHitDamage, DeepPassCm and TwoMinuteWarningSeconds must be above 0"));
    }
    if (InTuning.MaxMomentsKept < 1)
    {
        Problems.Add(TEXT("MaxMomentsKept must be 1 or more"));
    }
    TSet<EPSCommentaryMoment> Seen;
    for (const EPSCommentaryMoment Kind : InTuning.ModelMoments)
    {
        if (Seen.Contains(Kind))
        {
            Problems.Add(FString::Printf(TEXT("ModelMoments: %s is listed twice"), *UEnum::GetValueAsString(Kind)));
        }
        Seen.Add(Kind);
    }
    if (InTuning.ModelTask.IsEmpty() || InTuning.ModelInstructions.TrimStartAndEnd().IsEmpty())
    {
        Problems.Add(TEXT("ModelTask and ModelInstructions must not be empty"));
    }
    if (InTuning.ModelContextChars < 512)
    {
        Problems.Add(TEXT("ModelContextChars must be 512 or more"));
    }
    return Problems;
}

// --- Sources -------------------------------------------------------------------------------

void UPSCommentaryEventModel::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    Bus->OnGameStateMC.AddUObject(this, &UPSCommentaryEventModel::HandleGameState);
    Bus->OnSnapMC.AddUObject(this, &UPSCommentaryEventModel::HandleSnap);
    Bus->OnThrowMC.AddUObject(this, &UPSCommentaryEventModel::HandleThrow);
    Bus->OnCatchMC.AddUObject(this, &UPSCommentaryEventModel::HandleCatch);
    Bus->OnTackleMC.AddUObject(this, &UPSCommentaryEventModel::HandleTackle);
    Bus->OnDamageMC.AddUObject(this, &UPSCommentaryEventModel::HandleDamage);
    Bus->OnFumbleMC.AddUObject(this, &UPSCommentaryEventModel::HandleFumble);
    Bus->OnBlownCoverageMC.AddUObject(this, &UPSCommentaryEventModel::HandleBlownCoverage);
    Bus->OnBoundaryCrossedMC.AddUObject(this, &UPSCommentaryEventModel::HandleBoundaryCrossed);
    Bus->OnPenaltyMC.AddUObject(this, &UPSCommentaryEventModel::HandlePenalty);
    Bus->OnTimeoutMC.AddUObject(this, &UPSCommentaryEventModel::HandleTimeout);
    Bus->OnPlayResultMC.AddUObject(this, &UPSCommentaryEventModel::HandlePlayResult);
    Bus->OnRecordBrokenMC.AddUObject(this, &UPSCommentaryEventModel::HandleRecordBroken);
    BoundBus = Bus;
}

void UPSCommentaryEventModel::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
        Bus->OnDamageMC.RemoveAll(this);
        Bus->OnFumbleMC.RemoveAll(this);
        Bus->OnBlownCoverageMC.RemoveAll(this);
        Bus->OnBoundaryCrossedMC.RemoveAll(this);
        Bus->OnPenaltyMC.RemoveAll(this);
        Bus->OnTimeoutMC.RemoveAll(this);
        Bus->OnPlayResultMC.RemoveAll(this);
        Bus->OnRecordBrokenMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSCommentaryEventModel::SetIntelligence(UPSGameIntelligenceSubsystem* InIntelligence)
{
    if (UPSGameIntelligenceSubsystem* Old = Intelligence.Get())
    {
        Old->OnRequestAnsweredMC.Remove(AnsweredHandle);
    }
    AnsweredHandle.Reset();
    Intelligence = InIntelligence;
    if (InIntelligence)
    {
        AnsweredHandle = InIntelligence->OnRequestAnsweredMC.AddUObject(this, &UPSCommentaryEventModel::HandleRequestAnswered);
    }
}

// --- Moments -------------------------------------------------------------------------------

bool UPSCommentaryEventModel::FindLatestMoment(EPSCommentaryMoment Kind, FPSTelemetryCommentaryEvent& OutMoment) const
{
    for (int32 Index = Moments.Num() - 1; Index >= 0; --Index)
    {
        if (Moments[Index].Moment == Kind)
        {
            OutMoment = Moments[Index];
            return true;
        }
    }
    return false;
}

FString UPSCommentaryEventModel::DescribeForModel(const FPSTelemetryCommentaryEvent& Moment, int32 MaxChars)
{
    // Long names are shortened, then left out; the facts themselves always fit a sane budget.
    for (const int32 NameChars : { 48, 16, 0 })
    {
        const FString Described = PSCommentaryEventModelPrivate::BuildDescription(Moment, NameChars);
        if (Described.Len() <= MaxChars || NameChars == 0)
        {
            return Described;
        }
    }
    return FString();
}

FPSTelemetryCommentaryEvent UPSCommentaryEventModel::MakeMoment(EPSCommentaryMoment Kind, EPSTelemetryEventType SourceType) const
{
    FPSTelemetryCommentaryEvent Moment;
    Moment.Moment = Kind;
    Moment.PlayNumber = PlayNumber;
    Moment.Quarter = LastState.Quarter;
    Moment.GameClockSeconds = LastState.GameClockSeconds;
    Moment.Down = LastState.Down;
    Moment.Distance = LastState.Distance;
    Moment.YardLine = LastState.YardLine;
    Moment.bHomeOffense = LastState.bHomeHasPossession;
    Moment.HomeScore = LastState.HomeScore;
    Moment.AwayScore = LastState.AwayScore;
    Moment.bHomeFavoured = LastState.bHomeHasPossession;
    FPSTelemetryEvent Source;
    if (const UPSTelemetryBus* Bus = BoundBus.Get())
    {
        // The event being handled is the latest of its type (nothing republishes its own type).
        Moment.SourceSequence = Bus->FindLatestEventOfType(SourceType, Source) ? Source.Sequence : 0;
    }
    return Moment;
}

void UPSCommentaryEventModel::Publish(const FPSTelemetryCommentaryEvent& Moment)
{
    const FPSCommentaryHookTuning& Active = GetTuning();
    Moments.Add(Moment);
    if (Moments.Num() > Active.MaxMomentsKept)
    {
        Moments.RemoveAt(0, Moments.Num() - Active.MaxMomentsKept);
    }
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->PublishCommentary(Moment);
    }

    // Bridge-gated (Epic 18's pattern, Epic 82's hooks): offline, nothing is asked.
    UPSGameIntelligenceSubsystem* Hooks = Intelligence.Get();
    if (Hooks && Active.bOfferToModels && Active.ModelMoments.Contains(Moment.Moment) && UPSGameIntelligenceSubsystem::IsBridgeOnline())
    {
        const int32 RequestId = Hooks->OpenTextRequest(EPSIntelRequestKind::Commentary, Active.ModelTask, Active.ModelInstructions,
            DescribeForModel(Moment, Active.ModelContextChars));
        if (RequestId > 0)
        {
            OpenRequests.Add(RequestId, Moment);
        }
    }
}

void UPSCommentaryEventModel::HandleRequestAnswered(const FPSIntelRequest& Request)
{
    FPSTelemetryCommentaryEvent Moment;
    if (Request.Kind != EPSIntelRequestKind::Commentary || !OpenRequests.RemoveAndCopyValue(Request.RequestId, Moment))
    {
        return;
    }
    FPSCommentaryModelLine& Line = ModelLines.AddDefaulted_GetRef();
    Line.RequestId = Request.RequestId;
    Line.Moment = Moment;
    Line.Text = Request.Answer;
    const int32 Kept = GetTuning().MaxMomentsKept;
    if (ModelLines.Num() > Kept)
    {
        ModelLines.RemoveAt(0, ModelLines.Num() - Kept);
    }
    OnModelLineMC.Broadcast(Moment, Request.Answer);
}

void UPSCommentaryEventModel::ResetPlay()
{
    PasserName.Reset();
    TargetName.Reset();
    ReceiverName.Reset();
    CarrierName.Reset();
    TacklerName.Reset();
    InterceptorName.Reset();
    bTurnoverThisPlay = false;
}

// --- The bus's events ------------------------------------------------------------------------

void UPSCommentaryEventModel::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    // A new game: the drive count went back, or the clock is back in regulation after a final.
    if (bHaveState && (Event.CompletedDrives < LastState.CompletedDrives || (bGameOver && Event.Quarter <= 4)))
    {
        bHaveState = false;
        bGameOver = false;
        PlayNumber = 0;
        ResetPlay();
    }
    const bool bFirst = !bHaveState;
    const FPSTelemetryGameStateEvent Previous = LastState;
    LastState = Event;
    bHaveState = true;
    const float Warning = GetTuning().TwoMinuteWarningSeconds;

    if (bFirst)
    {
        Publish(MakeMoment(EPSCommentaryMoment::GameStart, EPSTelemetryEventType::GameState));
        return;
    }
    if (Event.Quarter > 4 && !bGameOver)
    {
        bGameOver = true;
        FPSTelemetryCommentaryEvent End = MakeMoment(EPSCommentaryMoment::GameEnd, EPSTelemetryEventType::GameState);
        End.Quarter = Previous.Quarter;
        End.Detail = TEXT("Final");
        End.bHomeFavoured = Event.HomeScore >= Event.AwayScore;
        Publish(End);
    }
    else if (Event.Quarter > Previous.Quarter && Event.Quarter <= 4)
    {
        FPSTelemetryCommentaryEvent End = MakeMoment(EPSCommentaryMoment::QuarterEnd, EPSTelemetryEventType::GameState);
        End.Quarter = Previous.Quarter;
        End.Detail = Previous.Quarter == 2 ? FName(TEXT("Halftime")) : NAME_None;
        End.bHomeFavoured = Event.HomeScore >= Event.AwayScore;
        Publish(End);
    }
    else if (Event.Quarter == Previous.Quarter && (Event.Quarter == 2 || Event.Quarter == 4)
        && Previous.GameClockSeconds > Warning && Event.GameClockSeconds <= Warning)
    {
        Publish(MakeMoment(EPSCommentaryMoment::TwoMinuteWarning, EPSTelemetryEventType::GameState));
    }
}

void UPSCommentaryEventModel::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    ++PlayNumber;
    ResetPlay();
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Snap, EPSTelemetryEventType::Snap);
    Moment.Down = Event.Down;
    Moment.Distance = Event.Distance;
    Moment.YardLine = Event.YardLine;
    Moment.GameClockSeconds = Event.GameClockSeconds;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    PasserName = Event.PasserName;
    TargetName = Event.TargetReceiverName;
    const float AirCm = FVector::Dist2D(Event.StartLocation, Event.TargetLocation);
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Pass, EPSTelemetryEventType::Throw);
    Moment.PrimaryName = Event.PasserName;
    Moment.SecondaryName = Event.TargetReceiverName;
    Moment.Magnitude = AirCm / PSCommentaryEventModelPrivate::CmPerYard;
    Moment.Detail = AirCm >= GetTuning().DeepPassCm ? FName(TEXT("Deep")) : FName(TEXT("Short"));
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    if (Event.bIsInterception)
    {
        InterceptorName = Event.ReceiverName;
        bTurnoverThisPlay = true;
        FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Interception, EPSTelemetryEventType::Catch);
        Moment.PrimaryName = Event.ReceiverName;
        Moment.SecondaryName = PasserName;
        Moment.bTurnover = true;
        Moment.bHomeFavoured = !LastState.bHomeHasPossession;
        Publish(Moment);
        return;
    }
    ReceiverName = Event.ReceiverName;
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Catch, EPSTelemetryEventType::Catch);
    Moment.PrimaryName = Event.ReceiverName;
    Moment.SecondaryName = PasserName;
    Moment.Yards = Event.YardsGained;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    TacklerName = Event.TacklerName;
    CarrierName = Event.BallCarrierName;
    if (Event.bIsSack)
    {
        FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Sack, EPSTelemetryEventType::Tackle);
        Moment.PrimaryName = Event.TacklerName;
        Moment.SecondaryName = Event.BallCarrierName;
        // The tackle carries no yards: the simulation measures them from the line of scrimmage
        // and they come with the play's result (the PlayResult moment).
        Moment.bHomeFavoured = !LastState.bHomeHasPossession;
        Publish(Moment);
    }
}

void UPSCommentaryEventModel::HandleDamage(const FPSTelemetryDamageEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    if (Event.Amount < GetTuning().BigHitDamage)
    {
        return;
    }
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::BigHit, EPSTelemetryEventType::Damage);
    Moment.PrimaryName = Event.TargetName;
    Moment.Magnitude = Event.Amount;
    Moment.bHomeFavoured = !LastState.bHomeHasPossession;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleFumble(const FPSTelemetryFumbleEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    bTurnoverThisPlay = bTurnoverThisPlay || Event.bIsTurnover;
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Fumble, EPSTelemetryEventType::Fumble);
    Moment.PrimaryName = Event.FumblerName;
    Moment.SecondaryName = Event.RecoveryName;
    Moment.bTurnover = Event.bIsTurnover;
    Moment.bHomeFavoured = Event.bIsTurnover != LastState.bHomeHasPossession;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleBlownCoverage(const FPSTelemetryBlownCoverageEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::OpenReceiver, EPSTelemetryEventType::BlownCoverage);
    Moment.PrimaryName = Event.ReceiverName;
    Moment.SecondaryName = Event.HelperName;
    Moment.Magnitude = Event.Separation / PSCommentaryEventModelPrivate::CmPerYard;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    // The offense's carrier into the end zone it attacks, or a defender returning a turnover
    // into the offense's own.
    const bool bOffenseIn = Event.YardLine >= 100 && !bTurnoverThisPlay;
    const bool bReturnIn = Event.YardLine <= 0 && bTurnoverThisPlay;
    if (!Event.bEndZone || Event.CarrierName.IsEmpty() || (!bOffenseIn && !bReturnIn))
    {
        return;
    }
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::GoalLine, EPSTelemetryEventType::BoundaryCrossed);
    Moment.PrimaryName = Event.CarrierName;
    Moment.bHomeFavoured = bOffenseIn == LastState.bHomeHasPossession;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandlePenalty(const FPSTelemetryPenaltyEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    if (Event.Kind != EPSPenaltyEventKind::Flag)
    {
        return;
    }
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Flag, EPSTelemetryEventType::Penalty);
    Moment.PrimaryName = Event.PlayerName;
    Moment.Detail = FName(*Event.Penalty);
    Moment.bHomeFavoured = !Event.bHomeTeam;
    Publish(Moment);
}

void UPSCommentaryEventModel::HandleTimeout(const FPSTelemetryTimeoutEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::Timeout, EPSTelemetryEventType::Timeout);
    Moment.GameClockSeconds = Event.GameClockSeconds;
    Moment.bHomeFavoured = Event.bOffense == LastState.bHomeHasPossession;
    Moment.Detail = Event.bOffense ? FName(TEXT("Offense")) : FName(TEXT("Defense"));
    Publish(Moment);
}

void UPSCommentaryEventModel::HandlePlayResult(const FPSTelemetryPlayResultEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::PlayResult, EPSTelemetryEventType::PlayResult);
    // The situation the play was snapped in, and the score it left.
    Moment.PlayNumber = FMath::Max(PlayNumber, 1);
    Moment.Quarter = Event.Quarter;
    Moment.GameClockSeconds = Event.GameClockSeconds;
    Moment.Down = Event.Down;
    Moment.Distance = Event.Distance;
    Moment.YardLine = Event.YardLine;
    Moment.bHomeOffense = Event.bHomeOffense;
    Moment.HomeScore = LastState.HomeScore + Event.HomePoints;
    Moment.AwayScore = LastState.AwayScore + Event.AwayPoints;
    Moment.Yards = Event.YardsGained;
    Moment.Points = Event.HomePoints + Event.AwayPoints;
    Moment.bFirstDown = Event.bFirstDown;
    Moment.bTurnover = Event.bInterception || Event.bTurnoverOnDowns || bTurnoverThisPlay;
    Moment.Detail = FName(*Event.Result);

    // Who: the interceptor, the catch, the sack, the incomplete pass, or the run.
    if (Event.bInterception)
    {
        Moment.PrimaryName = InterceptorName;
        Moment.PrimaryId = Event.InterceptorId;
        Moment.SecondaryName = PasserName;
        Moment.SecondaryId = Event.PasserId;
    }
    else if (Event.bSack)
    {
        Moment.PrimaryName = TacklerName;
        Moment.PrimaryId = Event.TacklerId;
        Moment.SecondaryName = PasserName.IsEmpty() ? CarrierName : PasserName;
        Moment.SecondaryId = Event.PasserId;
    }
    else if (Event.bPass && Event.bComplete)
    {
        Moment.PrimaryName = ReceiverName.IsEmpty() ? CarrierName : ReceiverName;
        Moment.PrimaryId = Event.ReceiverId;
        Moment.SecondaryName = PasserName;
        Moment.SecondaryId = Event.PasserId;
    }
    else if (Event.bPass)
    {
        Moment.PrimaryName = PasserName;
        Moment.PrimaryId = Event.PasserId;
        Moment.SecondaryName = TargetName;
        Moment.SecondaryId = Event.ReceiverId;
    }
    else
    {
        Moment.PrimaryName = CarrierName;
        Moment.PrimaryId = Event.RusherId;
        Moment.SecondaryName = TacklerName;
        Moment.SecondaryId = Event.TacklerId;
    }

    const bool bHomeOff = Event.bHomeOffense;
    Moment.bHomeFavoured = Moment.Points > 0 ? Event.HomePoints > Event.AwayPoints
        : Moment.bTurnover ? !bHomeOff
        : (Event.YardsGained > 0 || Event.bFirstDown) ? bHomeOff : !bHomeOff;
    Publish(Moment);

    if (Moment.Points > 0)
    {
        FPSTelemetryCommentaryEvent Score = Moment;
        Score.Moment = EPSCommentaryMoment::Score;
        Score.Detail = Moment.Points >= 6 ? FName(TEXT("Touchdown"))
            : Event.Result == TEXT("FieldGoalGood") ? FName(TEXT("FieldGoalGood"))
            : Event.Result == TEXT("Safety") ? FName(TEXT("Safety"))
            : FName(*Event.Result);
        Publish(Score);
    }
    ResetPlay();
}

void UPSCommentaryEventModel::HandleRecordBroken(const FPSTelemetryRecordBrokenEvent& Event)
{
    PS_PERF_SCOPE_NESTED(Audio);
    FPSTelemetryCommentaryEvent Moment = MakeMoment(EPSCommentaryMoment::RecordBroken, EPSTelemetryEventType::RecordBroken);
    Moment.PrimaryId = Event.HolderId;
    Moment.PrimaryName = Event.HolderId.ToString();
    Moment.SecondaryId = Event.PreviousHolderId;
    Moment.SecondaryName = Event.PreviousHolderId.IsNone() ? FString() : Event.PreviousHolderId.ToString();
    Moment.Magnitude = static_cast<float>(Event.Value);
    Moment.Detail = FName(*StaticEnum<EPSStatCategory>()->GetNameStringByValue(static_cast<int64>(Event.Category)));
    Publish(Moment);
}
