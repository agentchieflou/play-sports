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

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardLine = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 YardLineToGain = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GameClockSeconds = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PlayClockSeconds = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHomeHasPossession = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 HomeScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AwayScore = 0;
};

USTRUCT(BlueprintType)
struct FDriveSummary
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Plays = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Yards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Result = TEXT("");
};

UENUM(BlueprintType)
enum class EPSPenaltyType : uint8
{
    None,
    Offsides,
    Holding
};

UENUM(BlueprintType)
enum class EPlayResultType : uint8
{
    Incomplete,
    Tackle,
    Touchdown,
    Safety
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

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void TriggerSnap();

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void SetPlayPhase(EPlayPhase NewPhase);

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void RecordTackle(int32 YardsGained);

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void EndPlayAndPrepareNext();

    UPROPERTY(BlueprintReadOnly, Category = "Simulation")
    FDriveSummary CurrentDriveSummary;

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    FDriveSummary GetDriveSummary() const { return CurrentDriveSummary; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void RecordTouchdown();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    EPSPenaltyType ActivePenalty;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    bool bPenaltyDeclined;

private:
    FPlayState CurrentState;
    TArray<FPlayerAttributes> OffenseRoster;
    TArray<FPlayerAttributes> DefenseRoster;
    FPlayResult CurrentPlayResult;
    float PhaseTimer;

    void ResolvePlayResult();
};
