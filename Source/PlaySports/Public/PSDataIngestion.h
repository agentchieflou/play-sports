#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSLeagueData.h"
#include "PSArchetypeTuning.h"
#include "PSInputConfigTypes.h"
#include "PSMenuTypes.h"
#include "PSLoadingTips.h"
#include "PSForceFeedbackTypes.h"
#include "PSInputGlyphs.h"
#include "PSPlayCallTypes.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSDefenderAIComponent.h"
#include "PSPassingComponent.h"
#include "PSPlatformTiers.h"
#include "PSCarrierMoveComponent.h"
#include "PSInputBufferComponent.h"
#include "PSRushMoveComponent.h"
#include "PSTelemetrySamplingTypes.h"
#include "PSDataIngestion.generated.h"

/** JSON-to-engine-data ingestion (Epic 21: generalized beyond just players to
 *  teams and league config, all through this one validated path -- no ad-hoc
 *  parsers elsewhere per Architecture rule 4). */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSDataIngestion : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadPlayerAttributesFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);

    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadTeamsFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable);

    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadLeagueConfigFromJson(const FString& JsonFilePath, FPSLeagueConfig& OutConfig);

    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadArchetypeTuningFromJson(const FString& JsonFilePath, FPSArchetypeTuning& OutTuning);

    /** Loads the input action catalog (Data/input_actions.json) consumed by
     *  UPSInputConfig (Epic 142/126). False on a missing file, malformed JSON, or an
     *  unrecognized ValueType string. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadInputCatalogFromJson(const FString& JsonFilePath, FPSInputCatalog& OutCatalog);

    /** Loads the gamepad response tuning (Data/input_tuning.json, Epic 127). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadInputTuningFromJson(const FString& JsonFilePath, FInputTuningRow& OutTuning);

    /** Loads the front-end menu catalog (Data/ui_menus.json, Epic 101). False on a missing
     *  file, malformed JSON, or an unrecognized Command string. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadMenuCatalogFromJson(const FString& JsonFilePath, FPSMenuCatalog& OutCatalog);

    /** Loads the loading-screen tips (Data/loading_tips.json, Epic 101). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadLoadingTipsFromJson(const FString& JsonFilePath, FPSLoadingTipCatalog& OutCatalog);

    /** Loads the controller rumble patterns (Data/force_feedback.json, Epic 128). False on a
     *  missing file, malformed JSON, or an unrecognized Cue string. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadForceFeedbackTuningFromJson(const FString& JsonFilePath, FPSForceFeedbackTuning& OutTuning);

    /** Loads the button glyph table (Data/input_glyphs.json, Epic 128). False on a missing
     *  file, malformed JSON, or an unrecognized Device string. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadInputGlyphsFromJson(const FString& JsonFilePath, FPSInputGlyphCatalog& OutCatalog);

    /** Loads the play-call timing (Data/play_call.json, Epic 102). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadPlayCallTuningFromJson(const FString& JsonFilePath, FPlayCallTuningRow& OutTuning);

    /** Loads the pre-snap defensive adjustments (Data/defensive_adjustments.json, Epic 102).
     *  False on a missing file, malformed JSON, or an unrecognized Role or Kind string. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadDefensiveAdjustmentsFromJson(const FString& JsonFilePath, FPSDefensiveAdjustmentCatalog& OutCatalog);

    /** Loads the offensive AI tuning (Data/skill_ai_tuning.json, Epic 14). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadSkillPlayerAITuningFromJson(const FString& JsonFilePath, FSkillPlayerAITuningRow& OutTuning);

    /** Loads the defensive AI tuning (Data/defense_ai_tuning.json). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadDefenderAITuningFromJson(const FString& JsonFilePath, FDefenderAITuningRow& OutTuning);

    /** Loads the human passing tuning (Data/passing_input.json, Epic 104). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadPassingInputTuningFromJson(const FString& JsonFilePath, FPassingInputTuningRow& OutTuning);

    /** Loads the platform tiers (Data/platform_tiers.json, Epic 129). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadPlatformTiersFromJson(const FString& JsonFilePath, FPSPlatformTierCatalog& OutCatalog);

    /** Loads the ball carrier's move set (Data/carrier_moves.json, Epic 104.2). False on a
     *  missing file, malformed JSON, or an unrecognized Move. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadCarrierMovesFromJson(const FString& JsonFilePath, FPSCarrierMoveCatalog& OutCatalog);

    /** Loads the input buffer windows (Data/input_buffer.json, Epic 104.4). */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadInputBufferTuningFromJson(const FString& JsonFilePath, FInputBufferTuningRow& OutTuning);

    /** Loads the pass-rush move library (Data/pass_rush_moves.json, Epic 70). False on a
     *  missing file or malformed JSON. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadRushMovesFromJson(const FString& JsonFilePath, FPSRushMoveCatalog& OutCatalog);

    /** Loads the telemetry sampler's rate, history and budget (Data/telemetry_sampling.json,
     *  Epic 26). False on a missing file, malformed JSON, or an unrecognized event type. */
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadTelemetrySamplingTuningFromJson(const FString& JsonFilePath, FPSTelemetrySamplingTuning& OutTuning);

    /** Validates a Players JSON file's schema without loading it into a DataTable:
     *  missing PlayerId, unrecognized Role string, or out-of-range (negative)
     *  numeric attributes. OutErrors entries are "Row N: <field> <problem>" so
     *  content authors get an actionable pointer back to the bad row. */
    UFUNCTION(BlueprintCallable, Category = "Data|Validation")
    bool ValidatePlayersJson(const FString& JsonFilePath, TArray<FString>& OutErrors);

    UFUNCTION(BlueprintCallable, Category = "Data|Validation")
    bool ValidateTeamsJson(const FString& JsonFilePath, TArray<FString>& OutErrors);

private:
    static bool IsValidPlayerRoleString(const FString& RoleString);
};
