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
    CPU
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
};
