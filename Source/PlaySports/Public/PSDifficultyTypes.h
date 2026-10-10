// PSDifficultyTypes.h - Epic 84: difficulty tiers made of AI capability dials, and the assists
#pragma once

#include "CoreMinimal.h"
#include "PSDifficultyTypes.generated.h"

/** One capability dial a tier turns: a float field of an AI tuning, scaled for the CPU's players. */
USTRUCT(BlueprintType)
struct FPSDifficultyScale
{
    GENERATED_BODY()

    /** What the dial is, for people reading the data: RecognitionSpeed, and so on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName Dial;

    /** The tuning it scales: SkillAI, Pocket, DefenderAI, RouteRunning or Recognition (as
     *  PSPlayerDNA's bindings name them). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName Target;

    /** A float field of that tuning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName Field;

    /** What the field is multiplied by (above 0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    float Scale = 1.f;
};

/** One difficulty tier: how capable the CPU's AI is. It never touches a rating. */
USTRUCT(BlueprintType)
struct FPSDifficultyTier
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName TierId;

    /** Its name in the Difficulty setting's choices (Data/ui_settings.json), in the same order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FString Label;

    /** How far the CPU adapts to the human's play-calling (UPSOpponentModel, Epic 78), 0-1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    float AdaptationDial = 1.f;

    /** Execution variance: the CPU passer's throws scatter this many times as far as his
     *  Awareness alone makes them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    float ThrowScatterScale = 1.f;

    /** The recognition and execution dials on the AI tunings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    TArray<FPSDifficultyScale> Scales;
};

/** The difficulty tiers and the assists' settings (Data/difficulty.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSDifficultyCatalog
{
    GENERATED_BODY()

    /** Easiest first, in the Difficulty setting's order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    TArray<FPSDifficultyTier> DifficultyTiers;

    /** The settings (Data/ui_settings.json) that pick the tier and switch each assist. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName DifficultySetting = TEXT("Difficulty");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName PassLeadSetting = TEXT("PassLeadAssist");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName AutoSlideSetting = TEXT("AutoSlide");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FName SuggestedPlaySetting = TEXT("SuggestedPlayHighlight");

    /** The play-call screen's highlight on the suggested play ("#RRGGBB"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
    FString SuggestedPlayAccent = TEXT("#E8B33A");
};
