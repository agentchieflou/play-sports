// PSSettingsTypes.h - Epic 103: the player's settings as authored in Data/ui_settings.json
#pragma once

#include "CoreMinimal.h"
#include "PSSettingsTypes.generated.h"

/** How a setting is changed. */
UENUM(BlueprintType)
enum class EPSSettingKind : uint8
{
    /** On or off (0 or 1). */
    Toggle,
    /** One of a list of named choices (the choice's index). */
    Choice,
    /** A number from Min to Max in steps of Step. */
    Slider
};

/** A settings category: a screen of its own in the settings menu. */
USTRUCT(BlueprintType)
struct FPSSettingCategoryDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FName CategoryId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FString Label;
};

/** One setting. Its value is a number: 0 or 1 for a toggle, the choice's index, or the slider's
 *  value. */
USTRUCT(BlueprintType)
struct FPSSettingDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FName SettingId;

    /** The FPSSettingCategoryDef it is listed under. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FName Category;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    EPSSettingKind Kind = EPSSettingKind::Toggle;

    /** A choice setting's choices, as shown, in order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    TArray<FString> Choices;

    /** What each choice stands for, when that is a number other than its index (a frame-rate
     *  cap, a scale). Empty: the index. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    TArray<float> Values;

    /** A slider's range and step. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float Min = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float Max = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float Step = 1.f;

    /** Shown after a slider's value ("%"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FString Unit;

    /** The value before the player changes it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float Default = 0.f;

    /** One line on what it does, for the settings screen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FString Description;
};

/** Top-level shape of Data/ui_settings.json. */
USTRUCT(BlueprintType)
struct FPSSettingsCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    TArray<FPSSettingCategoryDef> Categories;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    TArray<FPSSettingDef> Settings;

    const FPSSettingDef* FindSetting(FName SettingId) const
    {
        return Settings.FindByPredicate([SettingId](const FPSSettingDef& Def) { return Def.SettingId == SettingId; });
    }

    const FPSSettingCategoryDef* FindCategory(FName CategoryId) const
    {
        return Categories.FindByPredicate([CategoryId](const FPSSettingCategoryDef& Def) { return Def.CategoryId == CategoryId; });
    }
};
