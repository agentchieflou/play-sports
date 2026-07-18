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
    EPlayerRole Role;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightKg;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HeightCm;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Speed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Agility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Strength;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Acceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Awareness;
};
