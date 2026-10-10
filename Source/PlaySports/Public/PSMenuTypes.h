// PSMenuTypes.h - Epic 101: the front-end menu catalog as authored in Data/ui_menus.json
#pragma once

#include "CoreMinimal.h"
#include "PSMenuTypes.generated.h"

/** What choosing a menu option does besides (or instead of) opening another screen. */
UENUM(BlueprintType)
enum class EPSMenuCommand : uint8
{
    None,
    Resume,
    StartPlayNow,
    StartFranchise,
    StartPractice,
    QuitToMainMenu,
    QuitGame
};

/** How the screen stack changed; screen widgets use it to pick a transition. */
UENUM(BlueprintType)
enum class EPSMenuTransition : uint8
{
    Push,
    Pop,
    Replace,
    Clear
};

/** One choice on a menu screen. An option opens TargetScreen, runs Command, or both. */
USTRUCT(BlueprintType)
struct FPSMenuOptionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName OptionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName TargetScreen;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    EPSMenuCommand Command = EPSMenuCommand::None;
};

/** One screen: its text, whether Back may leave it, and its options in display order. */
USTRUCT(BlueprintType)
struct FPSMenuScreenDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName ScreenId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FString Title;

    /** Optional line under the title. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FString Body;

    /** False for a root screen Back must not close (the main menu). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    bool bAllowBack = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    TArray<FPSMenuOptionDef> Options;
};

/** Top-level shape of Data/ui_menus.json, loaded by UPSDataIngestion::LoadMenuCatalogFromJson. */
USTRUCT(BlueprintType)
struct FPSMenuCatalog
{
    GENERATED_BODY()

    /** The screen the front end opens on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName RootScreen;

    /** The screen the in-game Pause action opens. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName PauseScreen;

    /** Fade-in time for a newly shown screen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    float TransitionSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    TArray<FPSMenuScreenDef> Screens;

    const FPSMenuScreenDef* FindScreen(FName ScreenId) const
    {
        return Screens.FindByPredicate([ScreenId](const FPSMenuScreenDef& Screen) { return Screen.ScreenId == ScreenId; });
    }
};
