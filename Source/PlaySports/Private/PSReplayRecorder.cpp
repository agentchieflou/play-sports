#include "PSReplayRecorder.h"

void UPSReplayRecorder::BeginRecording(UPSTelemetryBus* Bus, const FPSReplayRecording& Start)
{
    Unbind();
    Recording = Start;
    Recording.Events.Reset();
    CurrentTick = 0;

    if (!Bus)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplayRecorder: No telemetry bus to record from."));
        return;
    }
    BoundBus = Bus;
    RecordedHandle = Bus->OnEventRecordedMC.AddUObject(this, &UPSReplayRecorder::HandleEventRecorded);
}

void UPSReplayRecorder::SetTick(int32 TickIndex)
{
    CurrentTick = TickIndex;
}

bool UPSReplayRecorder::IsRecording() const
{
    return RecordedHandle.IsValid() && BoundBus.IsValid();
}

FPSReplayRecording UPSReplayRecorder::EndRecording()
{
    Unbind();
    return Recording;
}

FPSReplayEventRecord UPSReplayRecorder::MakeEventRecord(const FPSTelemetryEvent& Event, int32 TickIndex)
{
    FPSReplayEventRecord Record;
    Record.TickIndex = TickIndex;
    Record.TimestampSeconds = Event.Timestamp;
    Record.EventType = StaticEnum<EPSTelemetryEventType>()->GetNameStringByValue(static_cast<int64>(Event.EventType));
    Record.PayloadJson = Event.PayloadJson;
    return Record;
}

void UPSReplayRecorder::BeginDestroy()
{
    Unbind();
    Super::BeginDestroy();
}

void UPSReplayRecorder::HandleEventRecorded(const FPSTelemetryEvent& Event)
{
    if (EventTypes.Num() > 0 && !EventTypes.Contains(Event.EventType))
    {
        return;
    }
    Recording.Events.Add(MakeEventRecord(Event, CurrentTick));
}

void UPSReplayRecorder::Unbind()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnEventRecordedMC.Remove(RecordedHandle);
    }
    BoundBus.Reset();
    RecordedHandle.Reset();
}
