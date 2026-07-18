#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSPlayerAttributes.h"
#include "PSPlaySimulation.generated.h"

UENUM(BlueprintType)
enum class EPlayPhase : uint8
{
    PreSnap,
    Snap,
    PassRush,
    BallCarrierMovement,
    Scoring
};

USTRUCT(BlueprintType)
struct FPlayState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayPhase Phase = EPlayPhase::PreSnap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameTimeSeconds = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Distance = 10;
};

UENUM(BlueprintType)
enum class EPlayResultType : uint8
{
    Incomplete,
    Tackle,
    Touchdown
};

USTRUCT(BlueprintType)
struct FPlayResult
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardsGained = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayResultType ResultType = EPlayResultType::Incomplete;
};

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlaySimulation : public UObject
{
    GENERATED_BODY()

public:
    UPSPlaySimulation();

    UFUNCTION(BlueprintCallable)
    void InitializePlay(const TArray<FPlayerAttributes>& Offense, const TArray<FPlayerAttributes>& Defense);

    UFUNCTION(BlueprintCallable)
    void AdvancePlay(float DeltaSeconds);

    UFUNCTION(BlueprintCallable)
    FPlayState GetPlayState() const;

    UFUNCTION(BlueprintCallable)
    FPlayResult GetPlayResult() const;

private:
    FPlayState CurrentState;
    TArray<FPlayerAttributes> OffenseRoster;
    TArray<FPlayerAttributes> DefenseRoster;
    FPlayResult CurrentPlayResult;

    void ResolvePlayResult();
};
