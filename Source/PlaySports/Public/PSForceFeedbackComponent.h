// PSForceFeedbackComponent.h - Epic 128: gameplay events rumble the controller
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSForceFeedbackTypes.h"
#include "PSTelemetryBus.h"
#include "PSForceFeedbackComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FPSForceFeedbackDispatchMC, const FPSForceFeedbackDispatch&);

/**
 * UPSForceFeedbackComponent turns gameplay into controller rumble. It is a UPSTelemetryBus
 * subscriber (Architecture rule 5): Damage, Tackle, Catch, Fumble and Score events become
 * cues (EPSForceFeedbackCue), each cue plays the pattern authored for it in
 * Data/force_feedback.json (rule 4: intensities are tuning, not code), and the result goes
 * to the owning player controller's PlayDynamicForceFeedback.
 *
 * It learns everything it needs from the bus too -- which pawn the player controls
 * (ControlChange) and whether a gamepad is the active device (InputDeviceChange) -- so it
 * never asks the controller or game mode. Cues marked bOnlyWhenInvolved play only when the
 * event names the controlled pawn; nothing reaches the motors while the player is on
 * keyboard/mouse.
 *
 * APSPlayerController owns one. It subscribes at BeginPlay; headless tests call BindToBus.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSForceFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSForceFeedbackComponent();

    /** Absolute path of the authored patterns: <ProjectDir>/Data/force_feedback.json. */
    static FString GetDefaultTuningPath();

    /** The patterns in use, loaded from the default path on first use. */
    const FPSForceFeedbackTuning& GetTuning();

    /** Replaces the patterns with JsonFilePath's, read through UPSDataIngestion. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with a pattern table, one line each: a cue with no pattern or two, values
     *  out of range, a cue that would rumble no motor, or one that would never stop. */
    static TArray<FString> ValidateTuning(const FPSForceFeedbackTuning& InTuning);

    /** Longest rumble a cue may ask for. A sanity bound for validation, not tuning. */
    static constexpr float MaxCueDurationSeconds = 3.f;

    /** Subscribes to the world's telemetry bus. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    bool IsBoundToBus() const { return BoundBus.IsValid(); }

    /** The mapping step on its own: the rumble Cue plays at for a player who is (bInvolved)
     *  or isn't named in the event. False when it plays nothing: disabled, an unknown cue,
     *  zero intensity, or an involvement-only cue the player had no part in. */
    bool ResolveCue(EPSForceFeedbackCue Cue, bool bInvolved, FPSForceFeedbackDispatch& OutDispatch);

    UFUNCTION(BlueprintPure, Category = "ForceFeedback")
    EPSInputDevice GetActiveDevice() const { return ActiveDevice; }

    /** Display name of the pawn this player controls, from the bus; empty when none. */
    UFUNCTION(BlueprintPure, Category = "ForceFeedback")
    FString GetControlledPlayerName() const { return ControlledPlayerName; }

    /** The last few rumbles, oldest first (debugging and tests). */
    const TArray<FPSForceFeedbackDispatch>& GetRecentDispatches() const { return RecentDispatches; }

    /** The player's vibration setting; Epic 103's settings screen owns the toggle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ForceFeedback")
    bool bEnabled;

    /** Fires for every rumble the component decides on, played or not. */
    FPSForceFeedbackDispatchMC OnDispatched;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleDamage(const FPSTelemetryDamageEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleFumble(const FPSTelemetryFumbleEvent& Event);
    void HandleScore(const FPSTelemetryScoreEvent& Event);
    void HandleControlChange(const FPSTelemetryControlChangeEvent& Event);
    void HandleInputDeviceChange(const FPSTelemetryInputDeviceEvent& Event);

    /** True when Name is the controlled pawn's. */
    bool IsControlled(const FString& Name) const;

    void Play(EPSForceFeedbackCue Cue, bool bInvolved);

    static constexpr int32 MaxRecentDispatches = 16;

    UPROPERTY(Transient)
    FPSForceFeedbackTuning Tuning;

    UPROPERTY(Transient)
    TArray<FPSForceFeedbackDispatch> RecentDispatches;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FString ControlledPlayerName;
    EPSInputDevice ActiveDevice;
    bool bTuningLoaded;
};
