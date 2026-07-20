#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerProgression.generated.h"

/** Age-curve tuning: growth before PeakAge, decline after. Tuning lives in data,
 *  not code (Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSProgressionTuning : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PeakAgeStart = 26;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PeakAgeEnd = 29;

    /** Attribute points gained per year while below PeakAgeStart, scaled by snap share. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GrowthPerYear = 2.5f;

    /** Attribute points lost per year past PeakAgeEnd. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DeclinePerYear = 2.0f;

    /** Snap share (0-1) below which growth is halved -- unused backups develop slower. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float LowSnapShareThreshold = 0.2f;
};

/** Applies offseason attribute progression/regression to a roster (Epic 19). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlayerProgression : public UObject
{
    GENERATED_BODY()

public:
    /** Mutates Attributes in place. SeasonSnapShare is this player's fraction (0-1)
     *  of their team's offensive/defensive snaps played this season. */
    UFUNCTION(BlueprintCallable, Category = "Roster|Progression")
    void ApplyOffseasonProgression(UPARAM(ref) FPlayerAttributes& Attributes, int32 Age, float SeasonSnapShare, const FPSProgressionTuning& Tuning) const;

private:
    static float ClampAttribute(float Value);
};
