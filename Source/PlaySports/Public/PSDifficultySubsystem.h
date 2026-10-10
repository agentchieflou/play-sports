// PSDifficultySubsystem.h - Epic 84: difficulty tiers made of AI capability dials, the assists, and no rubber band
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSDifficultyTypes.h"
#include "PSPocketComponent.h"
#include "PSSettingsTypes.h"
#include "PSTelemetryBus.h"
#include "PSDifficultySubsystem.generated.h"

class APSPlayerPawn;
class UPSSettingsSubsystem;

/** The pure rules of the difficulty tiers. */
namespace PSDifficulty
{
    /** Scales Data (an instance of Struct, the AI tuning Target names) by each of Tier's scales
     *  on Target. Returns how many fields it changed. */
    PLAYSPORTS_API int32 ApplyScales(const FPSDifficultyTier& Tier, FName Target, const UScriptStruct* Struct, void* Data);

    /** Problems with Catalog, one line each (empty when sound): no tiers; a tier without an ID or
     *  a label, or an ID used twice; an adaptation dial outside 0-1 or a scatter scale not above
     *  0; a scale on an unknown target, on a field that isn't one of its floats, on a field the
     *  tier already scales, or not above 0; an accent that isn't "#RRGGBB". */
    PLAYSPORTS_API TArray<FString> ValidateCatalog(const FPSDifficultyCatalog& Catalog);
}

/**
 * UPSDifficultySubsystem is how hard the game plays and how much it helps (Epic 84). It reads the
 * player's settings (Data/ui_settings.json's Gameplay category) against Data/difficulty.json.
 *
 *  - Difficulty tiers are AI capability dials, never ratings. A tier scales the CPU's AI tunings
 *    as each play starts (recognition speed: how fast defenders react, how far a quarterback
 *    anticipates and how open he needs a man), sets how far the CPU adapts to the human's
 *    play-calling (UPSOpponentModel's dial, Epic 78) and how far its passes scatter (execution
 *    variance). Each AI component applies it after its player's style (Epic 79), through
 *    ApplyTo. Only the CPU's players get it: those on a side no human controls. The human's
 *    teammates play as tuned, and no attribute is ever touched.
 *  - Assists help the human: pass lead (his throws lead the receiver; off, they go at him and
 *    the stick does the leading), auto-slide (his quarterback slides when a tackler closes on him
 *    past the line, as the CPU's does) and the suggested play highlighted on the play-call screens.
 *  - Rubber band: none. Nothing here reads the score or the game state, so a tier plays the same
 *    up four touchdowns as down four.
 *
 * A world with no player settings (a headless test, the scenario gym) has no tier: the AI plays
 * as tuned. Its assists are at their settings' defaults.
 */
UCLASS()
class PLAYSPORTS_API UPSDifficultySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultCatalogPath();

    /** World's difficulty, or null. */
    static UPSDifficultySubsystem* Get(const UWorld* World);

    /** The catalog in use, loaded from the default path on first use. */
    const FPSDifficultyCatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Replaces the catalog (tests, or a mode with its own). */
    void SetCatalog(const FPSDifficultyCatalog& InCatalog);

    /** The settings the tier and the assists come from: the one SetSettings gave, else the game
     *  instance's. Null in a world without either. */
    UPSSettingsSubsystem* GetSettings() const;

    /** Reads these settings in place of the game instance's (tests, or a front end of its own). */
    void SetSettings(UPSSettingsSubsystem* InSettings);

    /** The tier the player picked (the Difficulty setting), or null with no settings. */
    const FPSDifficultyTier* GetActiveTier();

    /** True when Player plays for the CPU: no human controls a player on his side
     *  (UPSPlayCallSubsystem::IsHumanSide). */
    bool IsCpuPlayer(const APSPlayerPawn* Player) const;

    /** Scales Row (the tuning Target names) by the active tier when Player plays for the CPU.
     *  Returns how many fields it changed. */
    template <typename TRow>
    int32 ApplyTo(const APSPlayerPawn* Player, FName Target, TRow& Row)
    {
        const FPSDifficultyTier* Tier = IsCpuPlayer(Player) ? GetActiveTier() : nullptr;
        return Tier ? PSDifficulty::ApplyScales(*Tier, Target, TRow::StaticStruct(), &Row) : 0;
    }

    /** How many times as far Passer's throws scatter as his Awareness alone makes them: the
     *  tier's for a CPU passer, otherwise 1. */
    float GetThrowScatterScale(const APSPlayerPawn* Passer);

    /** How far the CPU adapts to the human (UPSOpponentModel): the tier's dial, or a negative
     *  number with no tier (the model's own default then holds). */
    float GetAdaptationDial();

    /** The assists, each its setting (its default with no settings). */
    UFUNCTION(BlueprintPure, Category = "Difficulty")
    bool IsPassLeadOn();

    UFUNCTION(BlueprintPure, Category = "Difficulty")
    bool IsAutoSlideOn();

    UFUNCTION(BlueprintPure, Category = "Difficulty")
    bool IsSuggestedPlayHighlightOn();

    /** The suggested play's highlight on the play-call screens, as the player's color vision
     *  setting draws it; transparent while the highlight is off. */
    UFUNCTION(BlueprintPure, Category = "Difficulty")
    FLinearColor GetSuggestedPlayAccent();

    /** Auto-slide: while it is on and a play is live, a human-controlled quarterback carrying the
     *  ball slides when the CPU's quarterback would (UPSPocketComponent::FindSlideThreat, with
     *  the pocket tuning). Each tick calls it. True when he slid. */
    bool UpdateAutoSlide();

    /** Listens for the snap and the end of the play. Initialize binds the world's. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    /** A toggle's state: the player's, or the settings catalog's default with no settings. */
    bool IsSettingOn(FName SettingId);

    const FPocketTuningRow& GetPocketTuning();

    UPROPERTY(Transient)
    FPSDifficultyCatalog Catalog;

    /** Data/ui_settings.json, for the assists' defaults when there are no settings. */
    UPROPERTY(Transient)
    FPSSettingsCatalog DefaultSettings;

    UPROPERTY(Transient)
    FPocketTuningRow PocketTuning;

    UPROPERTY(Transient)
    TObjectPtr<UPSSettingsSubsystem> SettingsOverride;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FVector LineOfScrimmage = FVector::ZeroVector;
    bool bPlayLive = false;
    bool bCatalogLoaded = false;
    bool bDefaultSettingsLoaded = false;
    bool bPocketTuningLoaded = false;
};
