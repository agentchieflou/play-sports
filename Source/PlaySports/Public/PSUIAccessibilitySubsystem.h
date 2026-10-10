// PSUIAccessibilitySubsystem.h - Epic 103.2/103.3: captions, UI narration and color vision
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/DataTable.h"
#include "Templates/SubclassOf.h"
#include "PSTelemetryBus.h"
#include "PSUIColorAccessibility.h"
#include "PSUIAccessibilitySubsystem.generated.h"

class UPSSettingsSubsystem;
class UPSUICaptionWidget;
class UCameraShakeBase;
class APlayerController;

/** Caption and color tuning (Data/ui_accessibility.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSUIAccessibilityTuning : public FTableRowBase
{
    GENERATED_BODY()

    /** A caption stays up at least this long... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
    float CaptionMinSeconds = 2.f;

    /** ...and at most this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
    float CaptionMaxSeconds = 8.f;

    /** A caption without a spoken length is timed at this reading pace. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
    float CaptionWordsPerSecond = 2.5f;

    /** The most captions shown at once; the oldest goes first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
    int32 CaptionMaxLines = 2;

    /** Home and away colors must look at least this different (CIE76 delta E) to the player;
     *  closer, they fall back to a secondary (UPSUIColorLibrary::ResolveMatchupColors). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
    float MinMatchupColorDistance = 20.f;
};

/** One caption on screen. */
USTRUCT(BlueprintType)
struct FPSCaptionLine
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Accessibility")
    FString Speaker;

    UPROPERTY(BlueprintReadOnly, Category = "Accessibility")
    FString Text;

    UPROPERTY(BlueprintReadOnly, Category = "Accessibility")
    FName Channel;

    /** World time it went up and comes down. */
    UPROPERTY(BlueprintReadOnly, Category = "Accessibility")
    float ShownAt = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Accessibility")
    float ExpiresAt = 0.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FPSNarrationMC, const FString&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSNarrationSignature, const FString&, Text);

/**
 * UPSUIAccessibilitySubsystem is the world's accessibility layer for what the player reads and
 * sees (Epic 103):
 *
 *   - Captions (103.3). Every Speech event on UPSTelemetryBus -- the commentary booth once
 *     Epic 96 speaks, the PA, a referee -- becomes a caption while the Captions setting is on,
 *     timed by its spoken length or its reading length (Data/ui_accessibility.json). A
 *     UPSUICaptionWidget on the local player draws them at the bottom of the screen at the
 *     CaptionSize setting's size.
 *   - UI narration hooks (103.3). Menus call Narrate with what a screen reader would say: a
 *     screen's title and text as it opens, an option as it takes focus. With the Narration
 *     setting on, OnNarration carries it to whatever speaks it (a platform text-to-speech
 *     voice; none is wired yet).
 *   - Color vision (103.2). GetColorblindMode reads the ColorblindMode setting for
 *     UPSUIColorLibrary, which every team and overlay color goes through.
 *   - Motion and flashes (103.5). With Reduced motion on, transitions are cuts
 *     (GetTransitionSeconds: APSPlayerController's camera blends, menu fades), cameras follow
 *     without lag (GetCameraFollowSpeed), and nothing shakes. StartCameraShake plays a shake at
 *     the Camera shake setting's strength, so every gameplay shake goes through it.
 *     GetFlashScale is the Flashes and pyro setting, for stadium pyro and screen flashes.
 *
 * Settings are the game instance's (UPSSettingsSubsystem), or the ones given to SetSettings
 * (headless tests).
 */
UCLASS()
class PLAYSPORTS_API UPSUIAccessibilitySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UPSUIAccessibilitySubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSUIAccessibilityTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with InTuning (empty when sound): times or pace not positive, max below min,
     *  no lines, a negative color distance. */
    static TArray<FString> ValidateTuning(const FPSUIAccessibilityTuning& InTuning);

    /** The settings read: the ones given here, else the game instance's. */
    void SetSettings(UPSSettingsSubsystem* InSettings) { SettingsOverride = InSettings; }
    UPSSettingsSubsystem* GetSettings() const;

    /** The ColorblindMode setting's mode in Settings (Off without settings). */
    static EPSColorblindMode GetColorblindMode(UPSSettingsSubsystem* Settings);

    /** The Reduced motion setting (Epic 103.5); off without settings. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    bool IsReducedMotion();

    /** How hard camera shakes play, 0..1: the Camera shake setting, 0 with reduced motion. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    float GetCameraShakeScale();

    /** How bright flashes and pyro get, 0..1: the Flashes and pyro setting. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    float GetFlashScale();

    /** A blend or fade as the player wants it: AuthoredSeconds, or 0 (a cut) with reduced
     *  motion. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    float GetTransitionSeconds(float AuthoredSeconds);

    /** A camera's follow speed (an FMath::FInterpTo speed): AuthoredSpeed, or 0 with reduced
     *  motion, where FInterpTo snaps so there is no lag to swing through. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    float GetCameraFollowSpeed(float AuthoredSpeed);

    /** Starts Shake on Player at Scale times GetCameraShakeScale. Returns false and plays
     *  nothing when that is 0, or without a player or shake. */
    UFUNCTION(BlueprintCallable, Category = "Accessibility")
    bool StartCameraShake(APlayerController* Player, TSubclassOf<UCameraShakeBase> Shake, float Scale);

    UFUNCTION(BlueprintPure, Category = "Accessibility")
    bool AreCaptionsOn();

    UFUNCTION(BlueprintPure, Category = "Accessibility")
    bool IsNarrationOn();

    /** The CaptionSize setting's font size. */
    UFUNCTION(BlueprintPure, Category = "Accessibility")
    int32 GetCaptionFontSize();

    /** How long Text stays up when nobody says how long it is spoken. */
    float GetCaptionSeconds(const FString& Text);

    /** Puts a caption up at world time Now for DurationSeconds (0: timed from its length).
     *  False with captions off or nothing to show. */
    bool ShowCaption(const FString& Speaker, const FString& Text, FName Channel, float DurationSeconds, float Now);

    /** The captions up at Now, oldest first, at most CaptionMaxLines. */
    TArray<FPSCaptionLine> GetActiveCaptions(float Now) const;

    /** How a caption reads: "Speaker: text", or the text alone. The speaker and text come
     *  from whoever spoke, already in the player's language; the pattern from
     *  Data/ui_text.csv (Epic 106). */
    static FText FormatCaptionText(const FPSCaptionLine& Line);
    static FString FormatCaption(const FPSCaptionLine& Line);

    /** The UI narration hook: says Text when the Narration setting is on. */
    UFUNCTION(BlueprintCallable, Category = "Accessibility")
    void Narrate(const FString& Text);

    /** Every narration, for a text-to-speech voice. */
    UPROPERTY(BlueprintAssignable, Category = "Accessibility")
    FPSNarrationSignature OnNarration;

    FPSNarrationMC OnNarrationMC;

    /** The Data/ui_settings.json IDs read here. */
    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName CaptionsSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName CaptionSizeSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName NarrationSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName ColorblindSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName ReducedMotionSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName CameraShakeSettingId;

    UPROPERTY(EditDefaultsOnly, Category = "Accessibility")
    FName FlashSettingId;

private:
    void HandleSpeech(const FPSTelemetrySpeechEvent& Event);

    UPROPERTY(Transient)
    FPSUIAccessibilityTuning Tuning;

    UPROPERTY(Transient)
    TArray<FPSCaptionLine> Lines;

    UPROPERTY(Transient)
    UPSSettingsSubsystem* SettingsOverride = nullptr;

    UPROPERTY(Transient)
    UPSUICaptionWidget* CaptionWidget = nullptr;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    bool bTuningLoaded = false;
};
