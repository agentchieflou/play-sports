// PSPlayCallTypes.h - Epic 102: who called which play, and the play-call tuning
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayCallTypes.generated.h"

/** Who made a side's call. */
UENUM(BlueprintType)
enum class EPSPlayCaller : uint8
{
    None,
    Human,
    CPU,
    /** Called for a human who ran low on play clock: the top suggestion (Epic 102.5). */
    QuickCall
};

/** One side's call for the coming snap. */
USTRUCT(BlueprintType)
struct FPSPlayCall
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FName PlayId;

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    EPSPlayCaller Caller = EPSPlayCaller::None;

    bool IsSet() const { return Caller != EPSPlayCaller::None; }
};

/** Play-call timing (Data/play_call.json), so the pace of the game is data (rule 4). */
USTRUCT(BlueprintType)
struct FPlayCallTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Seconds a CPU offense waits after both calls are in before it snaps, so the call
     *  can be seen and the field settles. A human offense snaps when its player hikes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    float CpuSnapDelaySeconds = 2.f;

    /** When the play clock reaches this with a human's side still uncalled, the top
     *  suggestion is called for them (it then snaps like a CPU call, after the delay above). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    float QuickCallPlayClockSeconds = 5.f;

    /** How many recent calls the Recent plays screen lists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    int32 RecentPlaysShown = 5;
};

/** A play a human called and ran, kept for the recent list and the tendency readout. */
USTRUCT(BlueprintType)
struct FPSPlayCallRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FName PlayId;

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FString PlayCategory;

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    bool bOffense = true;
};
