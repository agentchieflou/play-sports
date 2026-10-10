// PSMenuTypes.h - Epic 101/102/103: the menu catalog as authored in Data/ui_menus.json
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
    QuitGame,
    /** Call the play named by the option's Payload (Epic 102). */
    CallPlay,
    /** Apply the defensive adjustment named by the Payload (none clears it) and close. */
    ApplyAdjustment,
    /** Move the setting named by the Payload on a step (Epic 103): flip, next choice, slider up. */
    StepSetting,
    /** Put the settings of the category named by the Payload back to their defaults. */
    ResetSettings,
    /** Wait for the next key, then give it to the action named by the Payload (Epic 103.4). */
    BeginRemap,
    /** Put every action back on its catalog keys. */
    ResetRemaps
};

/** Where a screen's options come from. */
UENUM(BlueprintType)
enum class EPSMenuScreenContent : uint8
{
    /** The options authored in the catalog. */
    Static,
    /** One option per team (UPSUITeamCatalog), each starting Play Now with that team. */
    TeamSelect,
    /** Shown while travelling; its body is the loading tip. Left only by the travel. */
    Loading,
    /** One option per formation of the player's side (Epic 102), each opening the
     *  PlayCallPlays screen for that formation. */
    PlayCallFormations,
    /** One option per play in the chosen formation, each calling that play. */
    PlayCallPlays,
    /** The plays the player called most recently for their side, each calling it again. */
    PlayCallRecent,
    /** The player's starred plays for their side. */
    PlayCallFavorites,
    /** After a human defense calls: optional pre-snap adjustments over the call. */
    PlayCallAdjustments,
    /** One option per settings category (Epic 103), each opening the SettingsCategory screen
     *  for it. */
    Settings,
    /** The chosen category's settings, each showing its value and stepping it when chosen,
     *  then a reset to defaults. */
    SettingsCategory,
    /** One option per action a player may remap, showing its key on the active device; choosing
     *  one waits for the new key (Epic 103.4). Then a reset. */
    InputRemap
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

    /** What the command acts on, e.g. the team a generated team-select option starts with. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName Payload;

    /** Identity color for the option (team select); transparent means none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FLinearColor AccentColor = FLinearColor::Transparent;

    /** Optional second line under the label, e.g. a play's assignments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FString Detail;
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    EPSMenuScreenContent Content = EPSMenuScreenContent::Static;

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

    /** The screen shown while travelling to a level (Content = Loading). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName LoadingScreen;

    /** The screen a play call opens on (Content = PlayCallFormations, Epic 102). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    FName PlayCallScreen;

    /** Fade-in time for a newly shown screen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    float TransitionSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
    TArray<FPSMenuScreenDef> Screens;

    const FPSMenuScreenDef* FindScreen(FName ScreenId) const
    {
        return Screens.FindByPredicate([ScreenId](const FPSMenuScreenDef& Screen) { return Screen.ScreenId == ScreenId; });
    }

    /** The first screen with Content, or null. */
    const FPSMenuScreenDef* FindScreenWithContent(EPSMenuScreenContent Content) const
    {
        return Screens.FindByPredicate([Content](const FPSMenuScreenDef& Screen) { return Screen.Content == Content; });
    }
};
