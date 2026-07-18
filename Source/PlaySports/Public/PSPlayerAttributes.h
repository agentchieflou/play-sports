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
};
