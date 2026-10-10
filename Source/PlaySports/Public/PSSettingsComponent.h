// PSSettingsComponent.h - Epic 103: the player's settings applied to his own controller
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSSettingsComponent.generated.h"

class APSPlayerController;
class UPSSettingsSubsystem;

/**
 * UPSSettingsComponent applies the player's settings (UPSSettingsSubsystem, Epic 103) to what
 * belongs to his APSPlayerController, and again whenever one changes:
 *
 *   Vibration          UPSForceFeedbackComponent::bEnabled
 *   VibrationStrength  APlayerController::ForceFeedbackScale (the authored patterns stay as
 *                      they are)
 *   StickDeadZone      UPSInputConfig::SetStickDeadZoneScale, then the controller re-applies
 *                      its mapping contexts
 *   InputBuffering     UPSInputBufferComponent::bBufferingEnabled
 *   input remaps       UPSInputConfig::ApplyRemaps (Epic 103.4); a saved remap the catalog no
 *                      longer accepts is left out
 *
 * Settings for the whole game (video, master volume) are the subsystem's own; the rest are read
 * by the systems they concern. The settings are the game instance's, or the ones given to
 * SetSettings (headless tests, which have no game instance).
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSSettingsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSSettingsComponent();

    /** The settings applied: the ones set with SetSettings, else the game instance's. */
    UFUNCTION(BlueprintPure, Category = "Settings")
    UPSSettingsSubsystem* GetSettings() const;

    /** Follows InSettings from now on and applies every setting. */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    void SetSettings(UPSSettingsSubsystem* InSettings);

    /** Applies every setting this component handles. */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    void ApplyAll();

    /** Gives ActionId the key Key on the gamepad (bGamepad) or the keyboard and mouse, over the
     *  catalog's (Epic 103.4). Checked against this player's input config first; on success the
     *  remap is saved and applied and the action's glyph follows. False with OutProblem (one
     *  line for the player) when the catalog refuses it: the action is fixed, the key is of the
     *  wrong kind, or another action in the same context already uses it. */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    bool RequestRemap(FName ActionId, bool bGamepad, FName Key, FString& OutProblem);

    /** Puts every action back on its catalog keys. */
    UFUNCTION(BlueprintCallable, Category = "Settings")
    void ResetRemaps();

    /** The Data/ui_settings.json IDs this component applies. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    FName VibrationSettingId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    FName VibrationStrengthSettingId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    FName StickDeadZoneSettingId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    FName InputBufferingSettingId;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void Bind(UPSSettingsSubsystem* InSettings);
    void Unbind();
    void HandleSettingChanged(FName SettingId, float Value);
    void ApplyRemaps();
    void Apply(FName SettingId);
    APSPlayerController* GetPlayerController() const;

    TWeakObjectPtr<UPSSettingsSubsystem> BoundSettings;
};
