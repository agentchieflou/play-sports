// PSDeterminism.h - Epics 24/115: where two recordings of the same run part ways
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSReplayFormat.h"
#include "PSDeterminism.generated.h"

/** The first difference between two recordings of what should be the same run. */
USTRUCT(BlueprintType)
struct FPSReplayDivergence
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    bool bDiverged = false;

    /** Index into Events of the first event that differs. For a run that stops early or goes
     *  on longer, it is the shorter run's event count. INDEX_NONE when only the header differs,
     *  or nothing does. */
    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    int32 EventIndex = INDEX_NONE;

    /** The expected run's TickIndex at EventIndex (the actual run's when the expected one has
     *  no event there); INDEX_NONE otherwise. */
    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    int32 TickIndex = INDEX_NONE;

    /** What differs: "RandomSeed", "FixedDeltaSeconds", "TickIndex", "EventType", "Payload" or
     *  "EventCount". */
    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    FString Field;

    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    FString Expected;

    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    FString Actual;
};

/**
 * Regression comparison for recorded runs (FPSReplayRecording, Epic 115). A determinism harness
 * records the same seeded run twice, or a run against a golden recording, and asks where they
 * first differ. Event playback (Mode 1 in Specs/Determinism_Audit.md) needs no more than this.
 */
UCLASS()
class PLAYSPORTS_API UPSDeterminism : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** The first place Actual departs from Expected. The header's RandomSeed and
     *  FixedDeltaSeconds are checked first, then each event's TickIndex, EventType and
     *  PayloadJson in order, then the event count. Timestamps and the recording date are
     *  diagnostic and never compared. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    static FPSReplayDivergence FindFirstDivergence(const FPSReplayRecording& Expected, const FPSReplayRecording& Actual);

    /** One line for a log or a test failure: "identical", or where and how the runs differ. */
    UFUNCTION(BlueprintPure, Category = "Replay")
    static FString DescribeDivergence(const FPSReplayDivergence& Divergence);
};
