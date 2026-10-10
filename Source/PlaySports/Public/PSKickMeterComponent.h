// PSKickMeterComponent.h - Epic 104.5: the human kicker's meter
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSTelemetryBus.h"
#include "PSKickMeterComponent.generated.h"

class APSPlayerController;

/** Where the kick meter is. */
UENUM(BlueprintType)
enum class EPSKickMeterStage : uint8
{
    /** No kick for the human. */
    Idle,
    /** Lined up, waiting for the first press. */
    LiningUp,
    /** The button is held and the power bar moves. */
    Power,
    /** Power is locked and the accuracy needle sweeps. */
    Accuracy
};

/** Kick meter tuning (Data/kick_meter.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FKickMeterTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** The catalog action (Kicking context) that works the meter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kicking")
    FName KickAction = TEXT("Kick");

    /** The play waits this long into the kick for the human; after that the kick is the CPU's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kicking")
    float LineUpSeconds = 6.f;

    /** Held, the power bar fills from empty to full in this long, then drains back. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kicking")
    float PowerFillSeconds = 1.f;

    /** With power locked, the accuracy needle runs from hooked left to pushed right in this
     *  long; left alone, it ends pushed right. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kicking")
    float AccuracySweepSeconds = 0.8f;

    /** How much missing power costs the kick: Roll = PowerWeight x (1 - power) + AccuracyWeight x
     *  |needle|, clamped to 0..1, where 0 is a perfect kick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kicking")
    float PowerWeight = 0.6f;

    /** How much a needle off center costs the kick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kicking")
    float AccuracyWeight = 0.4f;
};

/**
 * UPSKickMeterComponent is the human kicker (Epic 104.5). When a kick phase starts (Kickoff,
 * Punt or FieldGoal on the bus) and the human controls a player on the kicking side (offense:
 * there are no separate special-teams units yet), the meter lines up and tells the bus, so the
 * play waits up to LineUpSeconds for the kick. Then the Kick button:
 *
 *   1. hold: the power bar fills, and drains back if held past full;
 *   2. release: power locks and the accuracy needle sweeps from left to right;
 *   3. press: the needle stops (left alone, it ends pushed right).
 *
 * The kick goes on the bus as a Kick event: power, needle, and a Roll from 0 (perfect) to 1.
 * UPSPlaySimulation, the authority on the kick's result, uses the Roll where the CPU kicker
 * rolls a random number. Presses arrive through the controller's UPSInputBufferComponent; the
 * Kicking context (UPSPlayContextComponent) is what makes the button reach it. The meter keeps
 * its own clock (ticked, or advanced by AdvanceTime in headless tests); the HUD reads GetStage,
 * GetPower and GetNeedle.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSKickMeterComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSKickMeterComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FKickMeterTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with InTuning (empty when sound): no action, a time that isn't positive,
     *  negative weights or both zero. */
    static TArray<FString> ValidateTuning(const FKickMeterTuningRow& InTuning);

    /** The kick's quality from the meter: 0 is perfect, 1 the worst. */
    static float ComputeRoll(float Power, float Needle, const FKickMeterTuningRow& InTuning);

    /** True for the play phases that are kicks: Kickoff, Punt, FieldGoal. */
    static bool IsKickPhase(const FString& PhaseName);

    /** Listens to the owning controller's input buffer. Idempotent. */
    void BindToController();

    /** Listens for kick phases. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** Lines the controlled player up for InKickType if he is on the kicking side, and tells
     *  the bus. False when the human doesn't kick. */
    UFUNCTION(BlueprintCallable, Category = "Kicking")
    bool LineUp(const FString& InKickType);

    /** Drops the kick in progress (the kick phase ended without it). */
    UFUNCTION(BlueprintCallable, Category = "Kicking")
    void CancelKick();

    /** The Kick button went down: starts the power bar, or stops the needle. */
    UFUNCTION(BlueprintCallable, Category = "Kicking")
    bool PressKick();

    /** The Kick button came up: locks the power. */
    UFUNCTION(BlueprintCallable, Category = "Kicking")
    bool ReleaseKick();

    /** Moves the meter's clock on. Ticking does this; headless tests call it. */
    void AdvanceTime(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "Kicking")
    EPSKickMeterStage GetStage() const { return Stage; }

    /** The power bar: filling while held, then locked (0..1). */
    UFUNCTION(BlueprintPure, Category = "Kicking")
    float GetPower() const;

    /** The accuracy needle while it sweeps (-1 hooked left .. 1 pushed right). */
    UFUNCTION(BlueprintPure, Category = "Kicking")
    float GetNeedle() const;

    /** The phase being kicked (Kickoff, Punt, FieldGoal), or empty. */
    UFUNCTION(BlueprintPure, Category = "Kicking")
    FString GetKickType() const { return KickType; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    UFUNCTION()
    void HandleActionPressed(FName ActionId, float HeldSeconds);

    UFUNCTION()
    void HandleActionReleased(FName ActionId, float HeldSeconds);

    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void Kick(float Needle);
    float PowerAfter(float HeldSeconds) const;
    float NeedleAfter(float SweptSeconds) const;
    UPSTelemetryBus* GetBus() const;
    APSPlayerController* GetPlayerController() const;

    UPROPERTY(Transient)
    FKickMeterTuningRow Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    EPSKickMeterStage Stage = EPSKickMeterStage::Idle;
    FString KickType;
    FString KickerName;
    float Clock = 0.f;
    float LinedUpAt = 0.f;
    float StageStartedAt = 0.f;
    float LockedPower = 0.f;
    bool bTuningLoaded = false;
};
