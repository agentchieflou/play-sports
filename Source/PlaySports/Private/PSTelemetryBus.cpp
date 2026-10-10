#include "PSTelemetryBus.h"
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

void UPSTelemetryBus::RecordHistory(EPSTelemetryEventType EventType, const FString& Description, const FString& JsonPayload)
{
    FPSTelemetryEvent Event;
    Event.EventType = EventType;
    Event.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    Event.Description = Description;
    Event.PayloadJson = JsonPayload;

    EventHistory.Add(Event);
    if (EventHistory.Num() > MaxHistorySize)
    {
        EventHistory.RemoveAt(0);
    }
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

    FString Description = FString::Printf(TEXT("Tackle: Tackler=%s, Carrier=%s, YardsGained=%d%s"),
        *Event.TacklerName, *Event.BallCarrierName, Event.YardsGained, Event.bIsSack ? TEXT(" (sack)") : TEXT(""));
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
