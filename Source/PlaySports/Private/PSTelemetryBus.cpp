#include "PSTelemetryBus.h"
#include "PSPerfBudget.h"
#include "JsonObjectConverter.h"
#include "Engine/World.h"

void UPSTelemetryBus::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    EventHistory.Empty();
}

void UPSTelemetryBus::ClearHistory()
{
    EventHistory.Empty();
}

int32 UPSTelemetryBus::GetOldestEventSequence() const
{
    return EventHistory.Num() > 0 ? EventHistory[0].Sequence : 0;
}

bool UPSTelemetryBus::FindEventBySequence(int32 Sequence, FPSTelemetryEvent& OutEvent) const
{
    // Sequences in the history are consecutive, so the event's index is its distance from
    // the oldest one.
    if (EventHistory.Num() == 0)
    {
        return false;
    }
    const int32 Index = Sequence - EventHistory[0].Sequence;
    if (!EventHistory.IsValidIndex(Index) || EventHistory[Index].Sequence != Sequence)
    {
        return false;
    }
    OutEvent = EventHistory[Index];
    return true;
}

bool UPSTelemetryBus::FindLatestEventOfType(EPSTelemetryEventType EventType, FPSTelemetryEvent& OutEvent) const
{
    for (int32 Index = EventHistory.Num() - 1; Index >= 0; --Index)
    {
        if (EventHistory[Index].EventType == EventType)
        {
            OutEvent = EventHistory[Index];
            return true;
        }
    }
    return false;
}

void UPSTelemetryBus::RecordHistory(EPSTelemetryEventType EventType, const FString& Description, const FString& JsonPayload)
{
    PS_PERF_SCOPE(Telemetry);
    PSPerf::AddCount(EPSPerfCounter::BusEvents);
    FPSTelemetryEvent Event;
    Event.EventType = EventType;
    Event.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    Event.Description = Description;
    Event.PayloadJson = JsonPayload;
    Event.Sequence = ++LastEventSequence;

    EventHistory.Add(Event);
    if (EventHistory.Num() > MaxHistorySize)
    {
        EventHistory.RemoveAt(0);
    }

    // A local copy: a listener may publish, which changes EventHistory under it.
    OnEventRecordedMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishSnap(const FPSTelemetrySnapEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetrySnapEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Snap: YardLine=%d, Down=%d, Distance=%d"), Event.YardLine, Event.Down, Event.Distance);
    RecordHistory(EPSTelemetryEventType::Snap, Description, JsonPayload);

    if (OnSnap.IsBound())
    {
        OnSnap.Broadcast(Event);
    }
    OnSnapMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishThrow(const FPSTelemetryThrowEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryThrowEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Throw: Passer=%s, Target=%s"), *Event.PasserName, *Event.TargetReceiverName);
    RecordHistory(EPSTelemetryEventType::Throw, Description, JsonPayload);

    if (OnThrow.IsBound())
    {
        OnThrow.Broadcast(Event);
    }
    OnThrowMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishCatch(const FPSTelemetryCatchEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryCatchEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Catch: Receiver=%s, YardsGained=%d, Interception=%s"), 
        *Event.ReceiverName, Event.YardsGained, Event.bIsInterception ? TEXT("True") : TEXT("False"));
    RecordHistory(EPSTelemetryEventType::Catch, Description, JsonPayload);

    if (OnCatch.IsBound())
    {
        OnCatch.Broadcast(Event);
    }
    OnCatchMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishTackle(const FPSTelemetryTackleEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryTackleEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Tackle: Tackler=%s, Carrier=%s, YardLine=%d%s"),
        *Event.TacklerName, *Event.BallCarrierName, Event.YardLine, Event.bIsSack ? TEXT(" (sack)") : TEXT(""));
    RecordHistory(EPSTelemetryEventType::Tackle, Description, JsonPayload);

    if (OnTackle.IsBound())
    {
        OnTackle.Broadcast(Event);
    }
    OnTackleMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishFumble(const FPSTelemetryFumbleEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryFumbleEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Fumble: Fumbler=%s, Recovery=%s, Turnover=%s"), 
        *Event.FumblerName, *Event.RecoveryName, Event.bIsTurnover ? TEXT("True") : TEXT("False"));
    RecordHistory(EPSTelemetryEventType::Fumble, Description, JsonPayload);

    if (OnFumble.IsBound())
    {
        OnFumble.Broadcast(Event);
    }
    OnFumbleMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishScore(const FPSTelemetryScoreEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryScoreEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Score: Type=%s, Points=%d, Score=%d-%d"), 
        *Event.ScoreType, Event.Points, Event.HomeScore, Event.AwayScore);
    RecordHistory(EPSTelemetryEventType::Score, Description, JsonPayload);

    if (OnScore.IsBound())
    {
        OnScore.Broadcast(Event);
    }
    OnScoreMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPhaseChangeEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("PhaseChange: %s -> %s"), *Event.OldPhase, *Event.NewPhase);
    RecordHistory(EPSTelemetryEventType::PhaseChange, Description, JsonPayload);

    if (OnPhaseChange.IsBound())
    {
        OnPhaseChange.Broadcast(Event);
    }
    OnPhaseChangeMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishDamage(const FPSTelemetryDamageEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryDamageEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Damage: Target=%s, Amount=%.1f, Remaining=%.1f"),
        *Event.TargetName, Event.Amount, Event.RemainingHitPoints);
    RecordHistory(EPSTelemetryEventType::Damage, Description, JsonPayload);

    if (OnDamage.IsBound())
    {
        OnDamage.Broadcast(Event);
    }
    OnDamageMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishDeath(const FPSTelemetryDeathEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryDeathEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Death: Player=%s, Cause=%s"),
        *Event.PlayerName, *UEnum::GetValueAsString(Event.Cause));
    RecordHistory(EPSTelemetryEventType::Death, Description, JsonPayload);

    if (OnDeath.IsBound())
    {
        OnDeath.Broadcast(Event);
    }
    OnDeathMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishRespawn(const FPSTelemetryRespawnEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryRespawnEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Respawn: Player=%s"), *Event.PlayerName);
    RecordHistory(EPSTelemetryEventType::Respawn, Description, JsonPayload);

    if (OnRespawn.IsBound())
    {
        OnRespawn.Broadcast(Event);
    }
    OnRespawnMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishInputDeviceChange(const FPSTelemetryInputDeviceEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryInputDeviceEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("InputDeviceChange: Active=%s, Previous=%s, Connection=%s"),
        *UEnum::GetValueAsString(Event.ActiveDevice), *UEnum::GetValueAsString(Event.PreviousDevice),
        Event.bFromConnectionChange ? (Event.bConnected ? TEXT("Connected") : TEXT("Disconnected")) : TEXT("None"));
    RecordHistory(EPSTelemetryEventType::InputDeviceChange, Description, JsonPayload);

    if (OnInputDeviceChange.IsBound())
    {
        OnInputDeviceChange.Broadcast(Event);
    }
    OnInputDeviceChangeMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishControlChange(const FPSTelemetryControlChangeEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryControlChangeEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("ControlChange: Player=%s, Human=%s"),
        *Event.PlayerName, Event.bHumanControlled ? TEXT("true") : TEXT("false"));
    RecordHistory(EPSTelemetryEventType::ControlChange, Description, JsonPayload);

    if (OnControlChange.IsBound())
    {
        OnControlChange.Broadcast(Event);
    }
    OnControlChangeMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPlayCallEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("PlayCall: %s %s (%s), by %s"),
        Event.bOffense ? TEXT("Offense") : TEXT("Defense"), *Event.DisplayName, *Event.Formation, Event.bHumanCall ? TEXT("human") : TEXT("CPU"));
    RecordHistory(EPSTelemetryEventType::PlayCall, Description, JsonPayload);

    if (OnPlayCall.IsBound())
    {
        OnPlayCall.Broadcast(Event);
    }
    OnPlayCallMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPumpFake(const FPSTelemetryPumpFakeEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPumpFakeEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("PumpFake: %s toward %s"), *Event.PasserName, *Event.TargetReceiverName);
    RecordHistory(EPSTelemetryEventType::PumpFake, Description, JsonPayload);

    if (OnPumpFake.IsBound())
    {
        OnPumpFake.Broadcast(Event);
    }
    OnPumpFakeMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPassRushMove(const FPSTelemetryPassRushEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPassRushEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Outcome = Event.bWon ? FString(TEXT("won")) : FString::Printf(TEXT("stopped by %s"), *Event.Response);
    FString Description = FString::Printf(TEXT("PassRushMove: %s %s on %s, %s"), *Event.RusherName, *Event.Move, *Event.BlockerName, *Outcome);
    RecordHistory(EPSTelemetryEventType::PassRushMove, Description, JsonPayload);

    if (OnPassRushMove.IsBound())
    {
        OnPassRushMove.Broadcast(Event);
    }
    OnPassRushMoveMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishKick(const FPSTelemetryKickEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryKickEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Description = Event.bLiningUp
        ? FString::Printf(TEXT("Kick: %s lines up for the %s"), *Event.KickerName, *Event.KickType)
        : FString::Printf(TEXT("Kick: %s's %s, power %.2f, accuracy %.2f (roll %.2f)"), *Event.KickerName, *Event.KickType, Event.Power, Event.Accuracy, Event.Roll);
    RecordHistory(EPSTelemetryEventType::Kick, Description, JsonPayload);

    if (OnKick.IsBound())
    {
        OnKick.Broadcast(Event);
    }
    OnKickMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishJumpSnap(const FPSTelemetryJumpSnapEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryJumpSnapEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Description = FString::Printf(TEXT("JumpSnap: %s moved %.2fs before the snap%s"),
        *Event.DefenderName, Event.LeadSeconds, Event.bOffside ? TEXT(", offside") : TEXT(""));
    RecordHistory(EPSTelemetryEventType::JumpSnap, Description, JsonPayload);

    if (OnJumpSnap.IsBound())
    {
        OnJumpSnap.Broadcast(Event);
    }
    OnJumpSnapMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPreSnap(const FPSTelemetryPreSnapEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPreSnapEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Tell = Event.bManIndicator ? FString::Printf(TEXT(" (%s travels: man)"), *Event.DefenderName) : FString();
    FString Description = FString::Printf(TEXT("PreSnap: %s %s %s by %s%s"),
        *UEnum::GetValueAsString(Event.Action), *Event.PlayerName, *Event.Detail.ToString(), Event.bHumanCall ? TEXT("human") : TEXT("CPU"), *Tell);
    RecordHistory(EPSTelemetryEventType::PreSnap, Description, JsonPayload);

    if (OnPreSnap.IsBound())
    {
        OnPreSnap.Broadcast(Event);
    }
    OnPreSnapMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishTimeout(const FPSTelemetryTimeoutEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryTimeoutEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Timeout: %s, by %s, %.0f s left"),
        Event.bOffense ? TEXT("Offense") : TEXT("Defense"), Event.bHumanCall ? TEXT("human") : TEXT("CPU"), Event.GameClockSeconds);
    RecordHistory(EPSTelemetryEventType::Timeout, Description, JsonPayload);

    if (OnTimeout.IsBound())
    {
        OnTimeout.Broadcast(Event);
    }
    OnTimeoutMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishSpeech(const FPSTelemetrySpeechEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetrySpeechEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Description = FString::Printf(TEXT("Speech: %s (%s): %s"), *Event.Speaker, *Event.Channel.ToString(), *Event.Text);
    RecordHistory(EPSTelemetryEventType::Speech, Description, JsonPayload);

    if (OnSpeech.IsBound())
    {
        OnSpeech.Broadcast(Event);
    }
    OnSpeechMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishGapIntegrity(const FPSTelemetryGapIntegrityEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryGapIntegrityEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("GapIntegrity: %s, %d open (%s)%s, exchanges %d"), *Event.Front, Event.OpenGapCount, *Event.OpenGaps,
        Event.bRunRead ? TEXT(", run read") : TEXT(""), Event.ScrapeExchanges);
    RecordHistory(EPSTelemetryEventType::GapIntegrity, Description, JsonPayload);

    if (OnGapIntegrity.IsBound())
    {
        OnGapIntegrity.Broadcast(Event);
    }
    OnGapIntegrityMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishRouteRunning(const FPSTelemetryRouteEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryRouteEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("RouteRunning: %s %s vs %s: %s"),
        *UEnum::GetValueAsString(Event.Kind), *Event.ReceiverName, *Event.DefenderName, *Event.Outcome.ToString());
    RecordHistory(EPSTelemetryEventType::RouteRunning, Description, JsonPayload);

    if (OnRouteRunning.IsBound())
    {
        OnRouteRunning.Broadcast(Event);
    }
    OnRouteRunningMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishGameState(const FPSTelemetryGameStateEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryGameStateEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("GameState: Q%d %.0f s, %s, down %d & %d at %d, %d-%d"),
        Event.Quarter, Event.GameClockSeconds, *Event.Phase, Event.Down, Event.Distance, Event.YardLine, Event.HomeScore, Event.AwayScore);
    RecordHistory(EPSTelemetryEventType::GameState, Description, JsonPayload);

    if (OnGameState.IsBound())
    {
        OnGameState.Broadcast(Event);
    }
    OnGameStateMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishBlownCoverage(const FPSTelemetryBlownCoverageEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryBlownCoverageEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Help = Event.HelperName.IsEmpty() ? FString(TEXT("no help")) : Event.HelperName + TEXT(" helps");
    FString Description = FString::Printf(TEXT("BlownCoverage: %s free by %.0f cm, %s"), *Event.ReceiverName, Event.Separation, *Help);
    RecordHistory(EPSTelemetryEventType::BlownCoverage, Description, JsonPayload);

    if (OnBlownCoverage.IsBound())
    {
        OnBlownCoverage.Broadcast(Event);
    }
    OnBlownCoverageMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPersonnel(const FPSTelemetryPersonnelEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPersonnelEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Personnel: %s %s (%d in, %d out)"),
        Event.bOffense ? TEXT("Offense") : TEXT("Defense"), *Event.PackageName, Event.PlayersIn.Num(), Event.PlayersOut.Num());
    RecordHistory(EPSTelemetryEventType::Personnel, Description, JsonPayload);

    if (OnPersonnel.IsBound())
    {
        OnPersonnel.Broadcast(Event);
    }
    OnPersonnelMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPocket(const FPSTelemetryPocketEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPocketEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Pocket: %s %s vs %s%s"),
        *UEnum::GetValueAsString(Event.Kind), *Event.PasserName, *Event.DefenderName, Event.bSuccess ? TEXT(" (success)") : TEXT(""));
    RecordHistory(EPSTelemetryEventType::Pocket, Description, JsonPayload);

    if (OnPocket.IsBound())
    {
        OnPocket.Broadcast(Event);
    }
    OnPocketMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishDefensivePreSnap(const FPSTelemetryDefensivePreSnapEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryDefensivePreSnapEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("DefensivePreSnap: %s %s %s by %s"),
        *Event.Action.ToString(), *Event.PlayerName, *Event.Detail.ToString(), Event.bHumanCall ? TEXT("human") : TEXT("CPU"));
    RecordHistory(EPSTelemetryEventType::DefensivePreSnap, Description, JsonPayload);

    if (OnDefensivePreSnap.IsBound())
    {
        OnDefensivePreSnap.Broadcast(Event);
    }
    OnDefensivePreSnapMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishOpponentAdjustment(const FPSTelemetryOpponentAdjustmentEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryOpponentAdjustmentEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("OpponentAdjustment: CPU %s in Q%d leans %.2f -> %.2f on %s %.0f%% (%.0f calls)"),
        Event.bCpuOffense ? TEXT("offense") : TEXT("defense"), Event.Quarter, Event.PreviousStrength, Event.Strength,
        *Event.TopCategory, Event.TopShare * 100.f, Event.Samples);
    RecordHistory(EPSTelemetryEventType::OpponentAdjustment, Description, JsonPayload);

    if (OnOpponentAdjustment.IsBound())
    {
        OnOpponentAdjustment.Broadcast(Event);
    }
    OnOpponentAdjustmentMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishVersus(const FPSTelemetryVersusEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryVersusEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Versus: %s, seat %d"), *UEnum::GetValueAsString(Event.Kind), Event.Seat);
    if (!Event.Reason.IsEmpty())
    {
        Description += FString::Printf(TEXT(" (%s)"), *Event.Reason);
    }
    RecordHistory(EPSTelemetryEventType::Versus, Description, JsonPayload);

    if (OnVersus.IsBound())
    {
        OnVersus.Broadcast(Event);
    }
    OnVersusMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPlayResult(const FPSTelemetryPlayResultEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPlayResultEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Description = FString::Printf(TEXT("PlayResult: play %d, %s %d yards (home %d, away %d)"),
        Event.PlayNumber, *Event.Result, Event.YardsGained, Event.HomePoints, Event.AwayPoints);
    RecordHistory(EPSTelemetryEventType::PlayResult, Description, JsonPayload);

    if (OnPlayResult.IsBound())
    {
        OnPlayResult.Broadcast(Event);
    }
    OnPlayResultMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishRecordBroken(const FPSTelemetryRecordBrokenEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryRecordBrokenEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    RecordHistory(EPSTelemetryEventType::RecordBroken, FString::Printf(TEXT("RecordBroken: %s"), *Event.Description), JsonPayload);

    if (OnRecordBroken.IsBound())
    {
        OnRecordBroken.Broadcast(Event);
    }
    OnRecordBrokenMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishCoverage(const FPSTelemetryCoverageEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryCoverageEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Coverage: %s %s on %s: %s"),
        *UEnum::GetValueAsString(Event.Kind), *Event.DefenderName, *Event.ReceiverName, *Event.Outcome.ToString());
    RecordHistory(EPSTelemetryEventType::Coverage, Description, JsonPayload);

    if (OnCoverage.IsBound())
    {
        OnCoverage.Broadcast(Event);
    }
    OnCoverageMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishLooseBall(const FPSTelemetryLooseBallEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryLooseBallEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("LooseBall: %s %s %s"), *UEnum::GetValueAsString(Event.Kind), *Event.KickType, *Event.PlayerName);
    RecordHistory(EPSTelemetryEventType::LooseBall, Description, JsonPayload);

    if (OnLooseBall.IsBound())
    {
        OnLooseBall.Broadcast(Event);
    }
    OnLooseBallMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishDeception(const FPSTelemetryDeceptionEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryDeceptionEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Deception: %s %s %s: %s"), *UEnum::GetValueAsString(Event.Kind), *Event.PlayerName, *Event.OtherName, *Event.Outcome.ToString());
    RecordHistory(EPSTelemetryEventType::Deception, Description, JsonPayload);

    if (OnDeception.IsBound())
    {
        OnDeception.Broadcast(Event);
    }
    OnDeceptionMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryBoundaryCrossedEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("BoundaryCrossed: %s %s at the %d"), Event.CarrierName.IsEmpty() ? TEXT("the ball") : *Event.CarrierName,
        Event.bEndZone ? TEXT("into the end zone") : TEXT("out of bounds"), Event.YardLine);
    RecordHistory(EPSTelemetryEventType::BoundaryCrossed, Description, JsonPayload);

    if (OnBoundaryCrossed.IsBound())
    {
        OnBoundaryCrossed.Broadcast(Event);
    }
    OnBoundaryCrossedMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishRecognition(const FPSTelemetryRecognitionEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryRecognitionEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = Event.Kind == EPSRecognitionEventKind::Formation
        ? FString::Printf(TEXT("Recognition: formation %s (%s personnel), run lean %.2f"), *Event.Formation.ToString(), *Event.Personnel, Event.RunLean)
        : FString::Printf(TEXT("Recognition: %s reads %s (%s) at %.2fs"), *Event.PlayerName, *Event.Read.ToString(), *Event.Key.ToString(), Event.Seconds);
    RecordHistory(EPSTelemetryEventType::Recognition, Description, JsonPayload);

    if (OnRecognition.IsBound())
    {
        OnRecognition.Broadcast(Event);
    }
    OnRecognitionMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishPenalty(const FPSTelemetryPenaltyEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryPenaltyEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Penalty: %s %s on the %s %s"), *UEnum::GetValueAsString(Event.Kind), *Event.Penalty,
        Event.bOnDefense ? TEXT("defense") : TEXT("offense"), *Event.PlayerName);
    RecordHistory(EPSTelemetryEventType::Penalty, Description, JsonPayload);

    if (OnPenalty.IsBound())
    {
        OnPenalty.Broadcast(Event);
    }
    OnPenaltyMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishCrowd(const FPSTelemetryCrowdEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryCrowdEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Crowd: %s %s %s (%.2f)"), *UEnum::GetValueAsString(Event.Kind), *UEnum::GetValueAsString(Event.Level),
        *UEnum::GetValueAsString(Event.Reaction), Event.Excitement);
    RecordHistory(EPSTelemetryEventType::Crowd, Description, JsonPayload);

    if (OnCrowd.IsBound())
    {
        OnCrowd.Broadcast(Event);
    }
    OnCrowdMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishCommentary(const FPSTelemetryCommentaryEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryCommentaryEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    FString Description = FString::Printf(TEXT("Commentary: %s %s %s"), *UEnum::GetValueAsString(Event.Moment), *Event.PrimaryName, *Event.Detail.ToString());
    RecordHistory(EPSTelemetryEventType::Commentary, Description, JsonPayload);

    if (OnCommentary.IsBound())
    {
        OnCommentary.Broadcast(Event);
    }
    OnCommentaryMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishTrade(const FPSTelemetryTradeEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryTradeEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    RecordHistory(EPSTelemetryEventType::Trade, FString::Printf(TEXT("Trade: %s"), *Event.Description), JsonPayload);

    if (OnTrade.IsBound())
    {
        OnTrade.Broadcast(Event);
    }
    OnTradeMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishBallGrounded(const FPSTelemetryBallGroundedEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryBallGroundedEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Description = FString::Printf(TEXT("BallGrounded: at %s"), *Event.Location.ToString());
    RecordHistory(EPSTelemetryEventType::BallGrounded, Description, JsonPayload);

    if (OnBallGrounded.IsBound())
    {
        OnBallGrounded.Broadcast(Event);
    }
    OnBallGroundedMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishLineup(const FPSTelemetryLineupEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryLineupEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    const FString Call = Event.bOffense ? Event.Formation : FString::Printf(TEXT("%s %s"), *Event.Front, *Event.CoverageShell);
    RecordHistory(EPSTelemetryEventType::Lineup, FString::Printf(TEXT("Lineup: %s in %s%s"),
        Event.bOffense ? TEXT("Offense") : TEXT("Defense"), *Call, Event.bFromData ? TEXT("") : TEXT(" (by role)")), JsonPayload);

    if (OnLineup.IsBound())
    {
        OnLineup.Broadcast(Event);
    }
    OnLineupMC.Broadcast(Event);
}

void UPSTelemetryBus::PublishLifecycle(const FPSTelemetryLifecycleEvent& Event)
{
    FString JsonPayload;
    FJsonObjectConverter::UStructToJsonObjectString(FPSTelemetryLifecycleEvent::StaticStruct(), &Event, JsonPayload, 0, 0);

    RecordHistory(EPSTelemetryEventType::Lifecycle, FString::Printf(TEXT("Lifecycle: %s (%s)"),
        *UEnum::GetValueAsString(Event.Lifecycle), *Event.Backend.ToString()), JsonPayload);

    if (OnLifecycle.IsBound())
    {
        OnLifecycle.Broadcast(Event);
    }
    OnLifecycleMC.Broadcast(Event);
}
