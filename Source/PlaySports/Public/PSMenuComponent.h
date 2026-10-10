// PSMenuComponent.h - Epic 101: the front-end shell and pause menu on the player controller
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "PSMenuTypes.h"
#include "PSUITeamCatalog.h"
#include "PSMenuComponent.generated.h"

class APlayerController;
class UPSMenuStack;
class UPSMenuScreenWidget;
class UPSLoadingTips;

/**
 * UPSMenuComponent runs every menu for its player: the front end (main menu, mode select),
 * the in-game pause menu and anything Epics 102-106 push on top. Screens, their options and
 * Back rules come from Data/ui_menus.json (read through UPSDataIngestion); navigation lives
 * in a UPSMenuStack; the top screen is shown as a UPSMenuScreenWidget when the owner has a
 * local player (headless worlds keep the stack only, so the flow is testable).
 *
 * While any screen is open the player is in UI input mode with a cursor; when the stack
 * empties, game input returns. Pausing goes through APlayerController::SetPause.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSMenuComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSMenuComponent();

    /** The menu catalog, loaded from Data/ui_menus.json on first use. */
    const FPSMenuCatalog& GetCatalog();

    /** Problems that would strand or confuse a player, one line each: missing root/pause
     *  screen, duplicate IDs, an option that does nothing or targets an unknown screen, a
     *  root screen Back could close, or an option-less screen Back can't leave. */
    static TArray<FString> ValidateCatalog(const FPSMenuCatalog& InCatalog);

    UPSMenuStack* GetStack() const { return Stack; }

    UFUNCTION(BlueprintPure, Category = "Menu")
    bool IsMenuOpen() const;

    UFUNCTION(BlueprintPure, Category = "Menu")
    FName GetTopScreenId() const;

    /** True while the top screen is one of the play-call screens (Epic 102). */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    bool IsPlayCallScreenOpen();

    /** The play-call content kinds: formations, plays, recent plays. */
    static bool IsPlayCallContent(EPSMenuScreenContent Content);

    /** ScreenId as it is shown: catalog text and options, plus generated content -- one
     *  option per team on a TeamSelect screen, the loading tip as a Loading screen's body,
     *  the player's formations and a formation's plays on the play-call screens (Epic 102). */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    FPSMenuScreenDef GetPresentedScreen(FName ScreenId);

    /** Teams for team select, built once from the league data (UPSUITeamCatalog). */
    const TArray<FPSTeamSummary>& GetTeamSummaries();

    /** True while the game is paused because this component paused it. */
    UFUNCTION(BlueprintPure, Category = "Menu")
    bool IsPausedByMenu() const { return bPausedByMenu; }

    /** Pushes ScreenId. Unknown screens are refused with a warning. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    bool OpenScreen(FName ScreenId);

    /** Opens the catalog's root screen as the only screen (the front end's start). */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void OpenRootScreen();

    /** Back: closes the top screen if it allows it. Back on the pause screen resumes.
     *  False when nothing changed (no menu, or a root screen). */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    bool HandleBack();

    /** Runs an option of the top screen: opens its target screen and/or runs its command. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ChooseOption(FName OptionId);

    /** The Pause action: pauses and opens the pause screen, or resumes if it is open. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void TogglePause();

    /** Closes every screen and unpauses if this component paused the game. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void Resume();

    /** Keys that mean Back in menus: Cancel in the Menu context, plus Pause on the pause
     *  screen (so Start toggles it closed), all from the input catalog. */
    bool IsBackKey(const FKey& Key);

    /** Keys that star the focused play on the play-call screens: the Favorite action in the
     *  Menu context (Epic 102.3). */
    bool IsFavoriteKey(const FKey& Key);

    /** Stars or unstars the play behind OptionId on the top play-call screen and redraws it.
     *  False when the option doesn't call a play. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    bool ToggleFavoriteOption(FName OptionId);

    /** Travel options a command uses ("mode=PlayNow?team=Hawks", "game=Menu"); empty when
     *  the command does not travel. Payload is the chosen team for Play Now. */
    FString BuildTravelOptions(EPSMenuCommand Command, FName Payload = NAME_None) const;

    /** The loading-tip context for a travel command ("PlayNow", "Franchise", ...). */
    static FName GetTipContext(EPSMenuCommand Command);

    /** Called by APSMenuGameMode: open the root screen once play begins. */
    void RequestRootScreenOnBeginPlay();

    /** Widget class for every screen; a Widget Blueprint subclass restyles all menus. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    TSubclassOf<UPSMenuScreenWidget> ScreenWidgetClass;

    /** Game mode alias (Config/DefaultEngine.ini GameModeClassAliases) for the front end. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    FString MenuGameModeAlias;

    /** Input catalog IDs used for Back. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    FName MenuContextId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    FName CancelActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    FName PauseActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    FName FavoriteActionId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu")
    FName GameplayContextId;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleStackChanged(FName PreviousTop, FName NewTop, EPSMenuTransition Transition);
    void ShowTopScreen();
    void RemoveActiveWidget();
    void ApplyInputMode(bool bMenuOpen);
    void ExecuteCommand(EPSMenuCommand Command, FName Payload);
    void BeginTravel(EPSMenuCommand Command, FName Payload);
    void PerformPendingTravel();
    FString PrepareLoadingTip(FName Context);
    APlayerController* GetOwningPlayer() const;

    UPROPERTY(Transient)
    UPSMenuStack* Stack;

    UPROPERTY(Transient)
    UPSMenuScreenWidget* ActiveWidget;

    UPROPERTY(Transient)
    FPSMenuCatalog Catalog;

    UPROPERTY(Transient)
    TArray<FPSTeamSummary> TeamSummaries;

    /** The payload of the option that last opened each screen (a formation for its plays). */
    UPROPERTY(Transient)
    TMap<FName, FName> ScreenPayloads;

    /** Tips for worlds without a game instance (tests); the game uses the subsystem's. */
    UPROPERTY(Transient)
    UPSLoadingTips* FallbackTips;

    FString PendingTravelOptions;
    FString PendingLoadingTip;
    bool bTeamSummariesBuilt = false;

    bool bCatalogLoaded = false;
    bool bPausedByMenu = false;
    bool bOpenRootOnBeginPlay = false;
};
