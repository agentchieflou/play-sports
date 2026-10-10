// PSDefenderTechniqueComponent.h - Epic 104.5: a defender's get-off at the snap and his strip attempt
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSDefenderTechniqueComponent.generated.h"

class APSPlayerPawn;

/** Defensive technique tuning (Data/defensive_techniques.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FDefensiveTechniqueTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** The catalog action (PreSnap context) a defender times his jump with. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    FName JumpSnapAction = TEXT("JumpSnap");

    /** The catalog action (Defense context) that rips at the ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    FName StripAction = TEXT("Strip");

    /** A jump pressed at most this long before the snap is clean; an earlier one is offside. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    float JumpWindowSeconds = 0.2f;

    /** Speed (cm/s) a clean jump adds toward the line of scrimmage at the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    float GetOffSpeed = 400.f;

    /** How long a strip attempt lasts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    float StripWindowSeconds = 0.4f;

    /** From one strip attempt to the next. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    float StripCooldownSeconds = 1.5f;

    /** During the attempt the stripper's tackles succeed this many times as often: going for the
     *  ball, he wraps up less. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    float StripTackleScale = 0.8f;

    /** During the attempt his tackles add this to the fumble chance for a stripper with Strength
     *  100; a weaker one adds proportionally less. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defense")
    float StripFumbleChance = 0.3f;
};

/**
 * UPSDefenderTechniqueComponent is a defender's technique around the tackle (Epic 104.5), on
 * every APSPlayerPawn like the carrier's UPSCarrierMoveComponent:
 *
 *   - GetOff: a clean jump at the snap bursts him toward the line of scrimmage. The timing is
 *     judged by the human's UPSDefenseInputComponent; an AI could call it the same way.
 *   - TryStrip: for StripWindowSeconds he rips at the ball. A tackle he makes in that window
 *     succeeds less often (StripTackleScale) but adds to the fumble chance (StripFumbleChance,
 *     scaled by his Strength). UPSBallActionComponent::ResolveTackle reads both.
 *
 * Only a defender without the ball does either. It keeps its own clock (ticked, or advanced by
 * AdvanceTime in headless tests).
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSDefenderTechniqueComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSDefenderTechniqueComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FDefensiveTechniqueTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with InTuning (empty when sound): empty actions, negative numbers, a tackle scale
     *  or fumble chance outside 0..1. */
    static TArray<FString> ValidateTuning(const FDefensiveTechniqueTuningRow& InTuning);

    /** Bursts the owner toward LineOfScrimmage at GetOffSpeed: a clean jump at the snap. False
     *  when the owner is not a defender. */
    UFUNCTION(BlueprintCallable, Category = "Defense")
    bool GetOff(const FVector& LineOfScrimmage);

    /** Starts a strip attempt if the owner is a defender without the ball and the last attempt
     *  has cooled down. */
    UFUNCTION(BlueprintCallable, Category = "Defense")
    bool TryStrip();

    /** True during a strip attempt. */
    UFUNCTION(BlueprintPure, Category = "Defense")
    bool IsStripping() const { return Clock < StripUntil; }

    /** True while a strip can't start only because the last one is still cooling down. */
    UFUNCTION(BlueprintPure, Category = "Defense")
    bool IsStripBusy() const;

    /** What a tackle by the owner has its chance of success multiplied by (1 unless stripping). */
    UFUNCTION(BlueprintPure, Category = "Defense")
    float GetTackleChanceScale() const { return IsStripping() ? ActiveTackleScale : 1.f; }

    /** What a tackle by the owner adds to the fumble chance (0 unless stripping). */
    UFUNCTION(BlueprintPure, Category = "Defense")
    float GetFumbleChanceBonus() const { return IsStripping() ? ActiveFumbleBonus : 0.f; }

    /** Ends the strip attempt and its cooldown (a new play). */
    UFUNCTION(BlueprintCallable, Category = "Defense")
    void ResetTechniques();

    /** Moves the component's clock on. Ticking does this; headless tests call it. */
    void AdvanceTime(float DeltaSeconds);

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    APSPlayerPawn* GetDefender() const;

    UPROPERTY(Transient)
    FDefensiveTechniqueTuningRow Tuning;

    float Clock = 0.f;
    float StripUntil = -1.f;
    float StripReadyAt = 0.f;
    float ActiveTackleScale = 1.f;
    float ActiveFumbleBonus = 0.f;
    bool bTuningLoaded = false;
};
