#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSTelemetryBus.generated.h"

UENUM(BlueprintType)
enum class EPSTelemetryEventType : uint8
{
    Snap,
    Throw,
    Catch,
    Tackle,
    Fumble,
    Score,
    PhaseChange
};

USTRUCT(BlueprintType)
struct FPSTelemetrySnapEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Down = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Distance = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FPSTelemetryThrowEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PasserName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TargetReceiverName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector StartLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector TargetLocation = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FPSTelemetryCatchEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString ReceiverName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector CatchLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardsGained = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bIsInterception = false;
};

USTRUCT(BlueprintType)
struct FPSTelemetryTackleEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TacklerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString BallCarrierName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardsGained = 0;
};

USTRUCT(BlueprintType)
struct FPSTelemetryFumbleEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString FumblerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString RecoveryName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bIsTurnover = false;
};

USTRUCT(BlueprintType)
struct FPSTelemetryScoreEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString ScoreType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHomeScored = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Points = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HomeScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 AwayScore = 0;
};

USTRUCT(BlueprintType)
struct FPSTelemetryPhaseChangeEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString OldPhase;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString NewPhase;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float PlayClockSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FPSTelemetryEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSTelemetryEventType EventType = EPSTelemetryEventType::Snap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Timestamp = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PayloadJson;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetrySnapSignature, const FPSTelemetrySnapEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryThrowSignature, const FPSTelemetryThrowEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryCatchSignature, const FPSTelemetryCatchEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryTackleSignature, const FPSTelemetryTackleEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryFumbleSignature, const FPSTelemetryFumbleEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryScoreSignature, const FPSTelemetryScoreEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPhaseChangeSignature, const FPSTelemetryPhaseChangeEvent&, Event);

DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetrySnapMC, const FPSTelemetrySnapEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryThrowMC, const FPSTelemetryThrowEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryCatchMC, const FPSTelemetryCatchEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryTackleMC, const FPSTelemetryTackleEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryFumbleMC, const FPSTelemetryFumbleEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryScoreMC, const FPSTelemetryScoreEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPhaseChangeMC, const FPSTelemetryPhaseChangeEvent&);

UCLASS(BlueprintType, Blueprintable)
class PLAYSPORTS_API UPSTelemetryBus : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishSnap(const FPSTelemetrySnapEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishThrow(const FPSTelemetryThrowEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishCatch(const FPSTelemetryCatchEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishTackle(const FPSTelemetryTackleEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishFumble(const FPSTelemetryFumbleEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishScore(const FPSTelemetryScoreEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    TArray<FPSTelemetryEvent> GetEventHistory() const { return EventHistory; }

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void ClearHistory();

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetrySnapSignature OnSnap;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryThrowSignature OnThrow;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryCatchSignature OnCatch;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryTackleSignature OnTackle;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryFumbleSignature OnFumble;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryScoreSignature OnScore;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPhaseChangeSignature OnPhaseChange;

    FPSTelemetrySnapMC OnSnapMC;
    FPSTelemetryThrowMC OnThrowMC;
    FPSTelemetryCatchMC OnCatchMC;
    FPSTelemetryTackleMC OnTackleMC;
    FPSTelemetryFumbleMC OnFumbleMC;
    FPSTelemetryScoreMC OnScoreMC;
    FPSTelemetryPhaseChangeMC OnPhaseChangeMC;

private:
    UPROPERTY(Transient)
    TArray<FPSTelemetryEvent> EventHistory;

    const int32 MaxHistorySize = 100;

    void RecordHistory(EPSTelemetryEventType EventType, const FString& Description, const FString& JsonPayload);
};
