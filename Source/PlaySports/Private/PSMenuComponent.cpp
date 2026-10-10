#include "PSMenuComponent.h"
#include "PSMenuStack.h"
#include "PSMenuScreenWidget.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSLoadingTips.h"
#include "PSLoadingScreenSubsystem.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
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
    FallbackTips = nullptr;
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
    if (!InCatalog.LoadingScreen.IsNone())
    {
        const FPSMenuScreenDef* Loading = InCatalog.FindScreen(InCatalog.LoadingScreen);
        if (!Loading)
        {
            Errors.Add(FString::Printf(TEXT("LoadingScreen '%s' is not a screen"), *InCatalog.LoadingScreen.ToString()));
        }
        else if (Loading->Content != EPSMenuScreenContent::Loading)
        {
            Errors.Add(FString::Printf(TEXT("LoadingScreen '%s' must have Content Loading"), *InCatalog.LoadingScreen.ToString()));
        }
    }
    if (!InCatalog.PlayCallScreen.IsNone())
    {
        const FPSMenuScreenDef* PlayCall = InCatalog.FindScreen(InCatalog.PlayCallScreen);
        if (!PlayCall)
        {
            Errors.Add(FString::Printf(TEXT("PlayCallScreen '%s' is not a screen"), *InCatalog.PlayCallScreen.ToString()));
        }
        else if (PlayCall->Content != EPSMenuScreenContent::PlayCallFormations)
        {
            Errors.Add(FString::Printf(TEXT("PlayCallScreen '%s' must have Content PlayCallFormations"), *InCatalog.PlayCallScreen.ToString()));
        }
        if (!InCatalog.FindScreenWithContent(EPSMenuScreenContent::PlayCallPlays))
        {
            Errors.Add(TEXT("A PlayCallScreen needs a screen with Content PlayCallPlays to list a formation's plays"));
        }
    }
    if (InCatalog.TransitionSeconds < 0.f)
    {
        Errors.Add(TEXT("TransitionSeconds must not be negative"));
    }

    for (const FPSMenuScreenDef& Screen : InCatalog.Screens)
    {
        const FString ScreenLabel = Screen.ScreenId.ToString();
        // Generated screens get their options at runtime; a Loading screen is left by the
        // travel it announces, not by input.
        if (Screen.Options.Num() == 0 && !Screen.bAllowBack && Screen.Content == EPSMenuScreenContent::Static)
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

bool UPSMenuComponent::IsPlayCallContent(EPSMenuScreenContent Content)
{
    return Content == EPSMenuScreenContent::PlayCallFormations || Content == EPSMenuScreenContent::PlayCallPlays || Content == EPSMenuScreenContent::PlayCallRecent;
}

bool UPSMenuComponent::IsPlayCallScreenOpen()
{
    const FPSMenuScreenDef* Top = IsMenuOpen() ? GetCatalog().FindScreen(GetTopScreenId()) : nullptr;
    return Top && IsPlayCallContent(Top->Content);
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
    const FPSMenuScreenDef Screen = GetPresentedScreen(GetTopScreenId());
    const FPSMenuOptionDef* Option = Screen.Options.FindByPredicate([OptionId](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == OptionId; });
    if (!Option)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSMenuComponent: Option '%s' is not on screen '%s'."), *OptionId.ToString(), *GetTopScreenId().ToString());
        return;
    }

    // Copy before acting: opening a screen or running a command can change the top screen.
    const FName TargetScreen = Option->TargetScreen;
    const EPSMenuCommand Command = Option->Command;
    const FName Payload = Option->Payload;
    if (!TargetScreen.IsNone())
    {
        // The opened screen may be generated from the payload (a formation's plays).
        ScreenPayloads.Add(TargetScreen, Payload);
        OpenScreen(TargetScreen);
    }
    if (Command != EPSMenuCommand::None)
    {
        ExecuteCommand(Command, Payload);
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

FName UPSMenuComponent::GetTipContext(EPSMenuCommand Command)
{
    switch (Command)
    {
    case EPSMenuCommand::StartPlayNow:
        return TEXT("PlayNow");
    case EPSMenuCommand::StartFranchise:
        return TEXT("Franchise");
    case EPSMenuCommand::StartPractice:
        return TEXT("Practice");
    default:
        return UPSLoadingTips::AnyContext;
    }
}

FString UPSMenuComponent::BuildTravelOptions(EPSMenuCommand Command, FName Payload) const
{
    switch (Command)
    {
    case EPSMenuCommand::StartPlayNow:
        return Payload.IsNone() ? FString(TEXT("mode=PlayNow")) : FString::Printf(TEXT("mode=PlayNow?team=%s"), *Payload.ToString());
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

void UPSMenuComponent::ExecuteCommand(EPSMenuCommand Command, FName Payload)
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
        BeginTravel(Command, Payload);
        break;
    case EPSMenuCommand::QuitGame:
        UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
        break;
    case EPSMenuCommand::CallPlay:
        if (UPSPlayCallSubsystem* PlayCall = GetWorld() ? GetWorld()->GetSubsystem<UPSPlayCallSubsystem>() : nullptr)
        {
            if (PlayCall->CallPlay(Payload, EPSPlayCaller::Human))
            {
                Resume();
            }
        }
        break;
    default:
        break;
    }
}

void UPSMenuComponent::BeginTravel(EPSMenuCommand Command, FName Payload)
{
    // Unpause first: a paused world would carry the pause into the next level's start.
    Resume();

    PendingTravelOptions = BuildTravelOptions(Command, Payload);
    PendingLoadingTip = PrepareLoadingTip(GetTipContext(Command));

    // Show the Loading screen this frame and travel on the next, so it is drawn first.
    if (!GetCatalog().LoadingScreen.IsNone())
    {
        OpenScreen(GetCatalog().LoadingScreen);
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(this, &UPSMenuComponent::PerformPendingTravel);
    }
}

void UPSMenuComponent::PerformPendingTravel()
{
    const FString Map = UGameMapsSettings::GetGameDefaultMap();
    UE_LOG(LogTemp, Display, TEXT("UPSMenuComponent: Travelling to %s?%s"), *Map, *PendingTravelOptions);
    UGameplayStatics::OpenLevel(this, FName(*Map), true, PendingTravelOptions);
}

FString UPSMenuComponent::PrepareLoadingTip(FName Context)
{
    // The game instance's subsystem also puts this tip on the engine loading screen.
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (UPSLoadingScreenSubsystem* LoadingScreen = GameInstance ? GameInstance->GetSubsystem<UPSLoadingScreenSubsystem>() : nullptr)
    {
        return LoadingScreen->PrepareTip(Context);
    }

    if (!FallbackTips)
    {
        FallbackTips = NewObject<UPSLoadingTips>(this);
        FallbackTips->EnsureLoaded();
    }
    return FallbackTips->NextTip(Context);
}

const TArray<FPSTeamSummary>& UPSMenuComponent::GetTeamSummaries()
{
    if (!bTeamSummariesBuilt)
    {
        bTeamSummariesBuilt = true;
        TArray<FString> Errors;
        UPSUITeamCatalog::BuildSummaries(UPSUITeamCatalog::GetDefaultTeamsPath(), TeamSummaries, Errors);
        for (const FString& Error : Errors)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSMenuComponent: %s"), *Error);
        }
    }
    return TeamSummaries;
}

FPSMenuScreenDef UPSMenuComponent::GetPresentedScreen(FName ScreenId)
{
    const FPSMenuScreenDef* Authored = GetCatalog().FindScreen(ScreenId);
    if (!Authored)
    {
        return FPSMenuScreenDef();
    }

    FPSMenuScreenDef Presented = *Authored;
    if (Presented.Content == EPSMenuScreenContent::TeamSelect)
    {
        for (const FPSTeamSummary& Team : GetTeamSummaries())
        {
            FPSMenuOptionDef Option;
            Option.OptionId = Team.TeamId;
            Option.Label = FString::Printf(TEXT("%s  (%s)    OVR %d   OFF %d   DEF %d    %s"),
                *Team.DisplayName, *Team.Abbreviation, Team.Overall, Team.Offense, Team.Defense, *Team.Division);
            Option.Command = EPSMenuCommand::StartPlayNow;
            Option.Payload = Team.TeamId;
            Option.AccentColor = Team.PrimaryColor;
            Presented.Options.Add(Option);
        }
    }
    else if (Presented.Content == EPSMenuScreenContent::Loading && !PendingLoadingTip.IsEmpty())
    {
        Presented.Body = PendingLoadingTip;
    }
    else if (IsPlayCallContent(Presented.Content))
    {
        UPSPlayCallSubsystem* PlayCall = GetWorld() ? GetWorld()->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
        const APSPlayerController* Player = Cast<APSPlayerController>(GetOwner());
        const bool bOffense = !Player || !Player->GetPlayCallComponent() || Player->GetPlayCallComponent()->IsCallingForOffense();
        if (PlayCall && Presented.Content == EPSMenuScreenContent::PlayCallFormations)
        {
            // The situation and the player's tendencies, then the suggestion, the recent
            // plays (once there are any), then every formation.
            Presented.Body = PlayCall->BuildCallScreenBody(bOffense);
            Presented.Options.Append(PlayCall->BuildSuggestionOptions(bOffense));
            const FPSMenuScreenDef* RecentScreen = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::PlayCallRecent);
            if (RecentScreen && PlayCall->GetRecentCalls(bOffense, 1).Num() > 0)
            {
                FPSMenuOptionDef Recent;
                Recent.OptionId = TEXT("Recent");
                Recent.Label = RecentScreen->Title;
                Recent.TargetScreen = RecentScreen->ScreenId;
                Presented.Options.Add(Recent);
            }
            const FPSMenuScreenDef* PlaysScreen = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::PlayCallPlays);
            Presented.Options.Append(PlayCall->BuildFormationOptions(bOffense, PlaysScreen ? PlaysScreen->ScreenId : NAME_None));
        }
        else if (PlayCall && Presented.Content == EPSMenuScreenContent::PlayCallPlays)
        {
            const FString Formation = ScreenPayloads.FindRef(ScreenId).ToString();
            Presented.Title = Formation;
            Presented.Options.Append(PlayCall->BuildPlayOptions(Formation, bOffense));
        }
        else if (PlayCall)
        {
            Presented.Options.Append(PlayCall->BuildRecentOptions(bOffense));
        }
    }
    return Presented;
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
    if (!Player || !Player->GetLocalPlayer() || !ScreenWidgetClass)
    {
        return;
    }

    ActiveWidget = CreateWidget<UPSMenuScreenWidget>(Player, ScreenWidgetClass);
    if (ActiveWidget)
    {
        ActiveWidget->SetScreen(GetPresentedScreen(Stack->Top()), this, GetCatalog().TransitionSeconds);
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
