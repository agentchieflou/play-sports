#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.generated.h"

UENUM(BlueprintType)
enum class EPlayerRole : uint8
{
    Quarterback UMETA(DisplayName = "Quarterback"),
    RunningBack UMETA(DisplayName = "Running Back"),
    WideReceiver UMETA(DisplayName = "Wide Receiver"),
    TightEnd UMETA(DisplayName = "Tight End"),
    OffensiveLineman UMETA(DisplayName = "Offensive Lineman"),
    DefensiveLineman UMETA(DisplayName = "Defensive Lineman"),
    Linebacker UMETA(DisplayName = "Linebacker"),
    DefensiveBack UMETA(DisplayName = "Defensive Back")
};

/** Broad character-archetype grouping used for hitpoint/combat tuning (Epic 139):
 *  offensive skill players, defensive skill players, and linemen play by different
 *  combat rules even though they share the finer-grained EPlayerRole. */
UENUM(BlueprintType)
enum class EPlayerArchetypeClass : uint8
{
    OffenseSkill UMETA(DisplayName = "Offense Skill Player"),
    DefenseSkill UMETA(DisplayName = "Defense Skill Player"),
    Lineman UMETA(DisplayName = "Lineman")
};

USTRUCT(BlueprintType)
struct FPlayerAttributes : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightKg = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HeightCm = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Speed = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Agility = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Strength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Acceleration = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Awareness = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Stamina = 0.0f;
};

USTRUCT(BlueprintType)
struct FMovementTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMaxSpeedMin = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMaxSpeedMax = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseAccelerationMin = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseAccelerationMax = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseTurnRateMin = 180.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseTurnRateMaxMultiplier = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightMin = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightTurnRateReference = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DecelerationMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutDivergenceThreshold = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutSpeedMinMultiplierBase = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutSpeedAgilityMultiplier = 0.003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutSpeedWeightMultiplier = 0.001f;
};
