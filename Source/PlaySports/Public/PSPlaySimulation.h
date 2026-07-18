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
    EPlayPhase Phase;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameTimeSeconds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Down;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Distance;
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
    int32 YardsGained;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayResultType ResultType;
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
