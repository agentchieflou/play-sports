// PSUIHintSubsystem.h - Epic 105.4: contextual hints for first-time situations
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCoachingData.h"
#include "PSTelemetryBus.h"
#include "PSUIHintSubsystem.generated.h"

class UPSSettingsSubsystem;
class UPSSituationAI;

/** What makes a hint come up, as the play-call screen opens for the human. */
UENUM(BlueprintType)
enum class EPSHintTrigger : uint8
{
    /** The human's first offensive call. */
    OffenseCall,
    /** The human's first defensive call. */
    DefenseCall,
    /** The human's offense on 4th down. */
    FourthDown,
    /** The human's offense in the two-minute drill, as UPSSituationAI reads it. */
    TwoMinuteDrill,
    /** The human's side kicking off. */
    Kickoff
};

/** One hint (Data/ui_hints.json). Its text is in the player's language through
 *  Data/ui_text_data.csv (Hint.<HintId>). */
USTRUCT(BlueprintType)
struct FPSHintDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hints")
    FName HintId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hints")
    EPSHintTrigger Trigger = EPSHintTrigger::OffenseCall;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hints")
    FString Text;
};

/** Top-level shape of Data/ui_hints.json. Hints are tried in order and the first that applies
 *  and hasn't been seen comes up, so the specific ones go first. */
USTRUCT(BlueprintType)
struct FPSHintCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hints")
    TArray<FPSHintDef> Hints;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FPSHintShownMC, FName /* HintId */);

/**
 * UPSUIHintSubsystem teaches first-time situations as they happen (Epic 105.4). The first time
 * the play-call screen is shown for a side after a snap, UPSMenuComponent asks GetCallHint. The
 * first hint in Data/ui_hints.json that fits the down, and that this player hasn't seen, becomes
 * that side's hint until the next snap. The call screen shows it under the situation, and
 * narration reads it with the screen. It is marked seen in the profile at once
 * (UPSSettingsSubsystem), so it never comes back unless the player resets hints. The Hints
 * setting (Gameplay) turns hints off.
 *
 * Settings are the game instance's, or the ones given to SetSettings (headless tests).
 */
UCLASS()
class PLAYSPORTS_API UPSUIHintSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UPSUIHintSubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    static FString GetDefaultCatalogPath();

    /** The hints, loaded from the default path on first use. */
    const FPSHintCatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Problems with InCatalog (empty when sound): an empty or repeated HintId, a hint with no
     *  text. */
    static TArray<FString> ValidateCatalog(const FPSHintCatalog& InCatalog);

    void SetSettings(UPSSettingsSubsystem* InSettings) { SettingsOverride = InSettings; }
    UPSSettingsSubsystem* GetSettings() const;

    /** Whether Trigger applies to the human calling for bOffense in Situation. */
    bool Applies(EPSHintTrigger Trigger, const FPSSituationContext& Situation, bool bOffense);

    /** The first hint that applies to bOffense's side in Situation and hasn't been seen, or
     *  None (also with hints off). Marks nothing. */
    FName PickHint(const FPSSituationContext& Situation, bool bOffense);

    /** The hint for bOffense's call screen, in the player's language; empty for none. The first
     *  ask for a side after a snap picks it (PickHint) and marks it seen; later asks until the
     *  next snap return the same one. */
    FText GetCallHint(const FPSSituationContext& Situation, bool bOffense);

    /** The hint picked for bOffense's side since the last snap, or None. */
    UFUNCTION(BlueprintPure, Category = "Hints")
    FName GetActiveHintId(bool bOffense) const { return bOffense ? OffenseHintId : DefenseHintId; }

    /** A new down: the next call screen picks again (the snap does this). */
    UFUNCTION(BlueprintCallable, Category = "Hints")
    void ResetCallWindow();

    /** A hint came up. */
    FPSHintShownMC OnHintShownMC;

    /** The Data/ui_settings.json toggle for hints. */
    UPROPERTY(EditDefaultsOnly, Category = "Hints")
    FName HintsSettingId;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);

    UPROPERTY(Transient)
    FPSHintCatalog Catalog;

    UPROPERTY(Transient)
    UPSSettingsSubsystem* SettingsOverride = nullptr;

    UPROPERTY(Transient)
    UPSSituationAI* SituationAI = nullptr;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FName OffenseHintId;
    FName DefenseHintId;
    bool bOffensePicked = false;
    bool bDefensePicked = false;
    bool bCatalogLoaded = false;
};
