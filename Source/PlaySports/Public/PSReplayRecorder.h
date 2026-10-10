// PSReplayRecorder.h - Epic 115: drains the telemetry bus into a replay recording
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSReplayFormat.h"
#include "PSTelemetryBus.h"
#include "PSReplayRecorder.generated.h"

/**
 * Records a run as an FPSReplayRecording (Epic 115): the header and initial state it is started
 * with, then every event published on a UPSTelemetryBus while it records, in publish order.
 *
 * The bus keeps only its last GetMaxHistorySize() events, so the recorder copies each event as
 * it is published (Specs/Determinism_Audit.md, D1) instead of reading the history at the end;
 * a whole game fits. It keeps no history of its own beyond the recording it is making.
 *
 * Each event is stamped with the recorder's tick, the integer step of whatever drives the run
 * (SetTick before each step), never a float clock (audit C1 and R5). Event types are written by
 * name, so a recording never depends on the enum's integer values.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSReplayRecorder : public UObject
{
    GENERATED_BODY()

public:
    /** Starts recording the events published on Bus from now on, on top of Start's header and
     *  initial state (UPSReplayFormat::MakeRecording, with the run's seed and step). Start's
     *  events are dropped. A recording already in progress is abandoned. The tick goes back
     *  to 0. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void BeginRecording(UPSTelemetryBus* Bus, const FPSReplayRecording& Start);

    /** The tick stamped on every event recorded from now on. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void SetTick(int32 TickIndex);

    UFUNCTION(BlueprintPure, Category = "Replay")
    int32 GetTick() const { return CurrentTick; }

    UFUNCTION(BlueprintPure, Category = "Replay")
    bool IsRecording() const;

    /** Stops listening to the bus and returns the recording. It stays readable through
     *  GetRecording until the next BeginRecording. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    FPSReplayRecording EndRecording();

    /** The recording so far. */
    const FPSReplayRecording& GetRecording() const { return Recording; }

    /** The format's record of a bus event, stamped with TickIndex. */
    static FPSReplayEventRecord MakeEventRecord(const FPSTelemetryEvent& Event, int32 TickIndex);

    /** Only events of these types are recorded; empty records every type. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<EPSTelemetryEventType> EventTypes;

    virtual void BeginDestroy() override;

private:
    void HandleEventRecorded(const FPSTelemetryEvent& Event);
    void Unbind();

    UPROPERTY(Transient)
    FPSReplayRecording Recording;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle RecordedHandle;
    int32 CurrentTick = 0;
};
