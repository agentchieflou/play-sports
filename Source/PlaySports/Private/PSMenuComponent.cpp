#include "PSMenuComponent.h"
#include "PSMenuStack.h"
#include "PSMenuScreenWidget.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"

UPSMenuComponent::UPSMenuComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    ScreenWidgetClass = UPSMenuScreenWidget::StaticClass();
    MenuGameModeAlias = TEXT("Menu");
    MenuContextId = TEXT("Menu");
    CancelActionId = TEXT("Cancel");
    PauseActionId = TEXT("Pause");
    GameplayContextId = TEXT("OnField");
    Stack = nullptr;
    ActiveWidget = nullptr;
}

const FPSMenuCatalog& UPSMenuComponent::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        bCatalogLoaded = true;
        FString Path = FPaths::ProjectDir() / TEXT("Data/ui_menus.json");
        FPaths::CollapseRelativeDirectories(Path);
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
        if (!Ingestion->LoadMenuCatalogFromJson(Path, Catalog))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSMenuComponent: Could not load the menu catalog from %s."), *Path);
        }
    }
    return Catalog;
}

TArray<FString> UPSMenuComponent::ValidateCatalog(const FPSMenuCatalog& InCatalog)
{
    TArray<FString> Errors;

    TSet<FName> ScreenIds;
    for (const FPSMenuScreenDef& Screen : InCatalog.Screens)
    {
        if (Screen.ScreenId.IsNone())
        {
            Errors.Add(TEXT("A screen has an empty ScreenId"));
        }
        else if (ScreenIds.Contains(Screen.ScreenId))
        {
            Errors.Add(FString::Printf(TEXT("Screen '%s': duplicate ScreenId"), *Screen.ScreenId.ToString()));
        }
        ScreenIds.Add(Screen.ScreenId);
    }

    if (!InCatalog.FindScreen(InCatalog.RootScreen))
    {
        Errors.Add(FString::Printf(TEXT("RootScreen '%s' is not a screen"), *InCatalog.RootScreen.ToString()));
    }
    else if (InCatalog.FindScreen(InCatalog.RootScreen)->bAllowBack)
    {
        Errors.Add(FString::Printf(TEXT("RootScreen '%s' must set bAllowBack to false, or Back would close the front end"), *InCatalog.RootScreen.ToString()));
    }
    if (!InCatalog.FindScreen(InCatalog.PauseScreen))
    {
        Errors.Add(FString::Printf(TEXT("PauseScreen '%s' is not a screen"), *InCatalog.PauseScreen.ToString()));
    }
    if (InCatalog.TransitionSeconds < 0.f)
    {
        Errors.Add(TEXT("TransitionSeconds must not be negative"));
    }

    for (const FPSMenuScreenDef& Screen : InCatalog.Screens)
    {
        const FString ScreenLabel = Screen.ScreenId.ToString();
        if (Screen.Options.Num() == 0 && !Screen.bAllowBack)
        {
            Errors.Add(FString::Printf(TEXT("Screen '%s' has no options and blocks Back, so it can never be left"), *ScreenLabel));
        }

        TSet<FName> OptionIds;
        for (const FPSMenuOptionDef& Option : Screen.Options)
        {
            const FString OptionLabel = Option.OptionId.ToString();
            if (Option.OptionId.IsNone())
            {
                Errors.Add(FString::Printf(TEXT("Screen '%s': an option has an empty OptionId"), *ScreenLabel));
            }
            else if (OptionIds.Contains(Option.OptionId))
            {
                Errors.Add(FString::Printf(TEXT("Screen '%s': duplicate option '%s'"), *ScreenLabel, *OptionLabel));
            }
            OptionIds.Add(Option.OptionId);

            if (Option.TargetScreen.IsNone() && Option.Command == EPSMenuCommand::None)
            {
                Errors.Add(FString::Printf(TEXT("Screen '%s', option '%s': needs a TargetScreen or a Command"), *ScreenLabel, *OptionLabel));
            }
            if (!Option.TargetScreen.IsNone() && !ScreenIds.Contains(Option.TargetScreen))
            {
                Errors.Add(FString::Printf(TEXT("Screen '%s', option '%s': unknown TargetScreen '%s'"), *ScreenLabel, *OptionLabel, *Option.TargetScreen.ToString()));
            }
        }
    }

    return Errors;
}

bool UPSMenuComponent::IsMenuOpen() const
{
    return Stack && !Stack->IsEmpty();
}

FName UPSMenuComponent::GetTopScreenId() const
{
    return Stack ? Stack->Top() : NAME_None;
}

void UPSMenuComponent::BeginPlay()
{
    Super::BeginPlay();

    if (bOpenRootOnBeginPlay)
    {
        bOpenRootOnBeginPlay = false;
        OpenRootScreen();
    }
}

void UPSMenuComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveActiveWidget();
    Super::EndPlay(EndPlayReason);
}

void UPSMenuComponent::RequestRootScreenOnBeginPlay()
{
    if (HasBegunPlay())
    {
        OpenRootScreen();
    }
    else
    {
        bOpenRootOnBeginPlay = true;
    }
}

bool UPSMenuComponent::OpenScreen(FName ScreenId)
{
    if (!GetCatalog().FindScreen(ScreenId))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMenuComponent: Screen '%s' is not in the menu catalog."), *ScreenId.ToString());
        return false;
    }

    if (!Stack)
    {
        Stack = NewObject<UPSMenuStack>(this);
        Stack->OnChanged.AddUObject(this, &UPSMenuComponent::HandleStackChanged);
    }
    return Stack->Push(ScreenId);
}

void UPSMenuComponent::OpenRootScreen()
{
    const FName Root = GetCatalog().RootScreen;
    if (Stack)
    {
        Stack->Clear();
    }
    OpenScreen(Root);
}

bool UPSMenuComponent::HandleBack()
{
    if (!IsMenuOpen())
    {
        return false;
    }

    const FName Top = Stack->Top();
    if (Top == GetCatalog().PauseScreen && Stack->Depth() == 1)
    {
        Resume();
        return true;
    }

    const FPSMenuScreenDef* Screen = GetCatalog().FindScreen(Top);
    if (Screen && !Screen->bAllowBack)
    {
        return false;
    }
    return Stack->Pop();
}

void UPSMenuComponent::ChooseOption(FName OptionId)
{
    const FPSMenuScreenDef* Screen = GetCatalog().FindScreen(GetTopScreenId());
    const FPSMenuOptionDef* Option = Screen
        ? Screen->Options.FindByPredicate([OptionId](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == OptionId; })
        : nullptr;
    if (!Option)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMenuComponent: Option '%s' is not on screen '%s'."), *OptionId.ToString(), *GetTopScreenId().ToString());
        return;
    }

    // Copy before acting: opening a screen or running a command can change the top screen.
    const FName TargetScreen = Option->TargetScreen;
    const EPSMenuCommand Command = Option->Command;
    if (!TargetScreen.IsNone())
    {
        OpenScreen(TargetScreen);
    }
    if (Command != EPSMenuCommand::None)
    {
        ExecuteCommand(Command);
    }
}

void UPSMenuComponent::TogglePause()
{
    if (IsMenuOpen())
    {
        if (GetTopScreenId() == GetCatalog().PauseScreen)
        {
            Resume();
        }
        return;
    }

    APlayerController* Player = GetOwningPlayer();
    bPausedByMenu = Player && Player->SetPause(true);
    OpenScreen(GetCatalog().PauseScreen);
}

void UPSMenuComponent::Resume()
{
    if (Stack)
    {
        Stack->Clear();
    }

    APlayerController* Player = GetOwningPlayer();
    if (bPausedByMenu && Player)
    {
        Player->SetPause(false);
    }
    bPausedByMenu = false;
}

bool UPSMenuComponent::IsBackKey(const FKey& Key)
{
    APSPlayerController* Player = Cast<APSPlayerController>(GetOwner());
    UPSInputConfig* Config = Player ? Player->GetInputConfig() : nullptr;
    if (!Config)
    {
        return false;
    }

    if (Config->GetKeysFor(CancelActionId, MenuContextId).Contains(Key))
    {
        return true;
    }
    return GetTopScreenId() == GetCatalog().PauseScreen && Config->GetKeysFor(PauseActionId, GameplayContextId).Contains(Key);
}

FString UPSMenuComponent::BuildTravelOptions(EPSMenuCommand Command) const
{
    switch (Command)
    {
    case EPSMenuCommand::StartPlayNow:
        return TEXT("mode=PlayNow");
    case EPSMenuCommand::StartFranchise:
        return TEXT("mode=Franchise");
    case EPSMenuCommand::StartPractice:
        return TEXT("mode=Practice");
    case EPSMenuCommand::QuitToMainMenu:
        return FString::Printf(TEXT("game=%s"), *MenuGameModeAlias);
    default:
        return FString();
    }
}

void UPSMenuComponent::ExecuteCommand(EPSMenuCommand Command)
{
    switch (Command)
    {
    case EPSMenuCommand::Resume:
        Resume();
        break;
    case EPSMenuCommand::StartPlayNow:
    case EPSMenuCommand::StartFranchise:
    case EPSMenuCommand::StartPractice:
    case EPSMenuCommand::QuitToMainMenu:
        TravelTo(Command);
        break;
    case EPSMenuCommand::QuitGame:
        UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

void UPSMenuComponent::TravelTo(EPSMenuCommand Command)
{
    // Unpause first: a paused world would carry the pause into the next level's start.
    Resume();

    const FString Map = UGameMapsSettings::GetGameDefaultMap();
    const FString Options = BuildTravelOptions(Command);
    UE_LOG(LogTemp, Display, TEXT("UPSMenuComponent: Travelling to %s?%s"), *Map, *Options);
    UGameplayStatics::OpenLevel(this, FName(*Map), true, Options);
}

void UPSMenuComponent::HandleStackChanged(FName PreviousTop, FName NewTop, EPSMenuTransition Transition)
{
    ShowTopScreen();
}

void UPSMenuComponent::ShowTopScreen()
{
    RemoveActiveWidget();

    const bool bMenuOpen = IsMenuOpen();
    ApplyInputMode(bMenuOpen);
    if (!bMenuOpen)
    {
        return;
    }

    // Headless worlds (and dedicated servers) have no local player to show widgets to.
    APlayerController* Player = GetOwningPlayer();
    const FPSMenuScreenDef* Screen = GetCatalog().FindScreen(Stack->Top());
    if (!Player || !Player->GetLocalPlayer() || !Screen || !ScreenWidgetClass)
    {
        return;
    }

    ActiveWidget = CreateWidget<UPSMenuScreenWidget>(Player, ScreenWidgetClass);
    if (ActiveWidget)
    {
        ActiveWidget->SetScreen(*Screen, this, GetCatalog().TransitionSeconds);
        ActiveWidget->AddToViewport();
        ActiveWidget->FocusFirstOption(Player);
    }
}

void UPSMenuComponent::RemoveActiveWidget()
{
    if (ActiveWidget)
    {
        ActiveWidget->RemoveFromParent();
        ActiveWidget = nullptr;
    }
}

void UPSMenuComponent::ApplyInputMode(bool bMenuOpen)
{
    APlayerController* Player = GetOwningPlayer();
    if (!Player || !Player->GetLocalPlayer())
    {
        return;
    }

    if (bMenuOpen)
    {
        Player->SetInputMode(FInputModeUIOnly());
    }
    else
    {
        Player->SetInputMode(FInputModeGameOnly());
    }
    Player->bShowMouseCursor = bMenuOpen;
}

APlayerController* UPSMenuComponent::GetOwningPlayer() const
{
    return Cast<APlayerController>(GetOwner());
}
