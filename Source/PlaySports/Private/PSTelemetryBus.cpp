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

    FString Description = FString::Printf(TEXT("Tackle: Tackler=%s, Carrier=%s, YardsGained=%d"), 
        *Event.TacklerName, *Event.BallCarrierName, Event.YardsGained);
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
