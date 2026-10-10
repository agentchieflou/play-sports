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
#include "PSSettingsSubsystem.h"
#include "PSSettingsComponent.h"
#include "PSInputDeviceComponent.h"
#include "PSInputGlyphs.h"
#include "PSLocalization.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSUIColorAccessibility.h"
#include "PSUIHintSubsystem.h"
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

namespace PSMenuText
{
    /** An input action's name (Data/ui_text.csv Input.Action.<ActionId>), else its ID. */
    static FText ActionName(FName ActionId)
    {
        const FString Key = FString::Printf(TEXT("Input.Action.%s"), *ActionId.ToString());
        return UPSLocalization::HasText(Key) ? UPSLocalization::GetText(Key) : UPSLocalization::Verbatim(ActionId.ToString());
    }

    /** Where an action works: its contexts' names (Input.Context.<ContextId>), as a list. */
    static FText ContextNames(const TArray<FName>& Contexts)
    {
        TArray<FText> Names;
        for (const FName& ContextId : Contexts)
        {
            const FString Key = FString::Printf(TEXT("Input.Context.%s"), *ContextId.ToString());
            Names.Add(UPSLocalization::HasText(Key) ? UPSLocalization::GetText(Key) : UPSLocalization::Verbatim(ContextId.ToString()));
        }
        return FText::Join(UPSLocalization::GetText(TEXT("Common.ListSeparator")), Names);
    }

    /** "Label: value", as an option shows a setting or an action's key. */
    static FString OptionWithValue(const FText& Label, const FText& Value)
    {
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Label"), Label);
        Arguments.Add(TEXT("Value"), Value);
        return UPSLocalization::Format(TEXT("Menu.Option"), Arguments).ToString();
    }
}

UPSMenuComponent::UPSMenuComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    ScreenWidgetClass = UPSMenuScreenWidget::StaticClass();
    MenuGameModeAlias = TEXT("Menu");
    MenuContextId = TEXT("Menu");
    CancelActionId = TEXT("Cancel");
    PauseActionId = TEXT("Pause");
    FavoriteActionId = TEXT("Favorite");
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
    if (InCatalog.FindScreenWithContent(EPSMenuScreenContent::Settings) && !InCatalog.FindScreenWithContent(EPSMenuScreenContent::SettingsCategory))
    {
        Errors.Add(TEXT("A Settings screen needs a screen with Content SettingsCategory to list a category's settings"));
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
    switch (Content)
    {
    case EPSMenuScreenContent::PlayCallFormations:
    case EPSMenuScreenContent::PlayCallPlays:
    case EPSMenuScreenContent::PlayCallRecent:
    case EPSMenuScreenContent::PlayCallFavorites:
    case EPSMenuScreenContent::PlayCallAdjustments:
        return true;
    default:
        return false;
    }
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

float UPSMenuComponent::GetTransitionSeconds()
{
    UPSUIAccessibilitySubsystem* Accessibility = GetWorld() ? GetWorld()->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    const float Authored = GetCatalog().TransitionSeconds;
    return Accessibility ? Accessibility->GetTransitionSeconds(Authored) : Authored;
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

bool UPSMenuComponent::IsFavoriteKey(const FKey& Key)
{
    APSPlayerController* Player = Cast<APSPlayerController>(GetOwner());
    UPSInputConfig* Config = Player ? Player->GetInputConfig() : nullptr;
    return Config && Config->GetKeysFor(FavoriteActionId, MenuContextId).Contains(Key);
}

bool UPSMenuComponent::ToggleFavoriteOption(FName OptionId)
{
    UPSPlayCallSubsystem* PlayCall = GetWorld() ? GetWorld()->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!PlayCall || !IsPlayCallScreenOpen())
    {
        return false;
    }

    const FPSMenuScreenDef Screen = GetPresentedScreen(GetTopScreenId());
    const FPSMenuOptionDef* Option = Screen.Options.FindByPredicate([OptionId](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == OptionId; });
    if (!Option || Option->Command != EPSMenuCommand::CallPlay)
    {
        return false;
    }
    PlayCall->ToggleFavorite(Option->Payload);

    // Redraw so the star shows, keeping the player's place on the screen.
    RedrawKeepingFocus(OptionId);
    return true;
}

void UPSMenuComponent::RedrawKeepingFocus(FName OptionId)
{
    ShowTopScreen();
    if (ActiveWidget)
    {
        ActiveWidget->FocusOption(OptionId, GetOwningPlayer());
    }
}

UPSSettingsSubsystem* UPSMenuComponent::GetSettings() const
{
    return SettingsOverride ? SettingsOverride : UPSSettingsSubsystem::Get(this);
}

void UPSMenuComponent::BeginRemap(FName ActionId)
{
    RemapActionId = ActionId;
    RemapMessage.Reset();
    RedrawKeepingFocus(ActionId);
}

bool UPSMenuComponent::HandleRemapKey(const FKey& Key)
{
    if (!IsListeningForRemap())
    {
        return false;
    }
    const FName ActionId = RemapActionId;
    RemapActionId = NAME_None;
    if (IsBackKey(Key))
    {
        RemapMessage = UPSLocalization::GetText(TEXT("Remap.Cancelled")).ToString();
    }
    else
    {
        const APSPlayerController* Player = Cast<APSPlayerController>(GetOwner());
        UPSSettingsComponent* PlayerSettings = Player ? Player->GetSettingsComponent() : nullptr;
        const bool bGamepad = UPSInputGlyphs::GetDeviceForKey(Key) == EPSInputDevice::Gamepad;
        FString Problem;
        FFormatNamedArguments Arguments;
        if (PlayerSettings && PlayerSettings->RequestRemap(ActionId, bGamepad, Key.GetFName(), Problem))
        {
            Arguments.Add(TEXT("Action"), PSMenuText::ActionName(ActionId));
            Arguments.Add(TEXT("Key"), UPSLocalization::Verbatim(Key.GetDisplayName().ToString()));
            RemapMessage = UPSLocalization::Format(TEXT("Remap.Done"), Arguments).ToString();
        }
        else if (Problem.IsEmpty())
        {
            RemapMessage = UPSLocalization::GetText(TEXT("Remap.Refused")).ToString();
        }
        else
        {
            // The catalog's reason is written for developers and is not translated yet.
            Arguments.Add(TEXT("Reason"), UPSLocalization::Verbatim(Problem));
            RemapMessage = UPSLocalization::Format(TEXT("Remap.RefusedBecause"), Arguments).ToString();
        }
    }
    RedrawKeepingFocus(ActionId);
    return true;
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
                // A defensive call goes on to the optional pre-snap adjustments (102.4).
                FPSPlayDefinition Called;
                const FPSMenuScreenDef* Adjustments = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::PlayCallAdjustments);
                if (Adjustments && PlayCall->FindPlay(Payload, Called) && !Called.bIsOffensivePlay && PlayCall->GetAdjustments().Num() > 0)
                {
                    if (Stack)
                    {
                        Stack->Clear();
                    }
                    OpenScreen(Adjustments->ScreenId);
                }
                else
                {
                    Resume();
                }
            }
        }
        break;
    case EPSMenuCommand::ApplyAdjustment:
        if (UPSPlayCallSubsystem* PlayCall = GetWorld() ? GetWorld()->GetSubsystem<UPSPlayCallSubsystem>() : nullptr)
        {
            PlayCall->SetDefensiveAdjustment(Payload);
        }
        Resume();
        break;
    case EPSMenuCommand::StepSetting:
        if (UPSSettingsSubsystem* Settings = GetSettings())
        {
            Settings->StepSetting(Payload);
            RedrawKeepingFocus(Payload);
        }
        break;
    case EPSMenuCommand::ResetSettings:
        if (UPSSettingsSubsystem* Settings = GetSettings())
        {
            Settings->ResetToDefaults(Payload);
            RedrawKeepingFocus(TEXT("Reset"));
        }
        break;
    case EPSMenuCommand::BeginRemap:
        BeginRemap(Payload);
        break;
    case EPSMenuCommand::ResetRemaps:
        if (const APSPlayerController* Player = Cast<APSPlayerController>(GetOwner()))
        {
            if (UPSSettingsComponent* PlayerSettings = Player->GetSettingsComponent())
            {
                PlayerSettings->ResetRemaps();
                RemapMessage = UPSLocalization::GetText(TEXT("Remap.AllReset")).ToString();
            }
        }
        RedrawKeepingFocus(TEXT("ResetRemaps"));
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

    // The screen in the player's language (Epic 106): its authored text through
    // Data/ui_text_data.csv, and everything built below through Data/ui_text.csv.
    FPSMenuScreenDef Presented = *Authored;
    Presented.Title = UPSLocalization::GetDataText(UPSLocalization::MenuKey(ScreenId, TEXT("Title")), Authored->Title).ToString();
    Presented.Body = UPSLocalization::GetDataText(UPSLocalization::MenuKey(ScreenId, TEXT("Body")), Authored->Body).ToString();
    for (FPSMenuOptionDef& AuthoredOption : Presented.Options)
    {
        AuthoredOption.Label = UPSLocalization::GetDataText(UPSLocalization::MenuOptionKey(ScreenId, AuthoredOption.OptionId, TEXT("Label")), AuthoredOption.Label).ToString();
        AuthoredOption.Detail = UPSLocalization::GetDataText(UPSLocalization::MenuOptionKey(ScreenId, AuthoredOption.OptionId, TEXT("Detail")), AuthoredOption.Detail).ToString();
    }

    if (Presented.Content == EPSMenuScreenContent::TeamSelect)
    {
        // Team colors as the player's color vision needs them (Epic 103.2), sizes in the
        // player's units (Epic 106). Names are the teams' own, not translated.
        const EPSColorblindMode ColorMode = UPSUIAccessibilitySubsystem::GetColorblindMode(GetSettings());
        const EPSUnitSystem Units = UPSLocalization::GetUnitSystem(GetSettings());
        for (const FPSTeamSummary& Team : GetTeamSummaries())
        {
            FFormatNamedArguments Arguments;
            Arguments.Add(TEXT("Team"), UPSLocalization::Verbatim(Team.DisplayName));
            Arguments.Add(TEXT("Abbreviation"), UPSLocalization::Verbatim(Team.Abbreviation));
            Arguments.Add(TEXT("Overall"), FText::AsNumber(Team.Overall));
            Arguments.Add(TEXT("Offense"), FText::AsNumber(Team.Offense));
            Arguments.Add(TEXT("Defense"), FText::AsNumber(Team.Defense));
            Arguments.Add(TEXT("Division"), UPSLocalization::Verbatim(Team.Division));
            FPSMenuOptionDef Option;
            Option.OptionId = Team.TeamId;
            Option.Label = UPSLocalization::Format(TEXT("Menu.TeamOption"), Arguments).ToString();
            if (Team.PlayerCount > 0)
            {
                FFormatNamedArguments Size;
                Size.Add(TEXT("Players"), FText::AsNumber(Team.PlayerCount));
                Size.Add(TEXT("Height"), UPSLocalization::FormatHeight(Team.AverageHeightCm, Units));
                Size.Add(TEXT("Weight"), UPSLocalization::FormatWeight(Team.AverageWeightKg, Units));
                Option.Detail = UPSLocalization::Format(TEXT("Menu.TeamDetail"), Size).ToString();
            }
            Option.Command = EPSMenuCommand::StartPlayNow;
            Option.Payload = Team.TeamId;
            Option.AccentColor = UPSUIColorLibrary::ResolveColor(Team.PrimaryColor, ColorMode);
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
            // A first-time situation's hint goes under it (Epic 105.4).
            UPSUIHintSubsystem* Hints = GetWorld() ? GetWorld()->GetSubsystem<UPSUIHintSubsystem>() : nullptr;
            const FText Hint = Hints ? Hints->GetCallHint(PlayCall->GetSituation(), bOffense) : FText::GetEmpty();
            if (!Hint.IsEmpty())
            {
                FFormatNamedArguments HintArguments;
                HintArguments.Add(TEXT("Hint"), Hint);
                Presented.Body = UPSLocalization::JoinLines({ UPSLocalization::FromLocalized(Presented.Body), UPSLocalization::Format(TEXT("Hint.Line"), HintArguments) }).ToString();
            }
            Presented.Options.Append(PlayCall->BuildSuggestionOptions(bOffense));
            const FPSMenuScreenDef* RecentScreen = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::PlayCallRecent);
            if (RecentScreen && PlayCall->GetRecentCalls(bOffense, 1).Num() > 0)
            {
                FPSMenuOptionDef Recent;
                Recent.OptionId = TEXT("Recent");
                Recent.Label = UPSLocalization::GetDataText(UPSLocalization::MenuKey(RecentScreen->ScreenId, TEXT("Title")), RecentScreen->Title).ToString();
                Recent.TargetScreen = RecentScreen->ScreenId;
                Presented.Options.Add(Recent);
            }
            const FPSMenuScreenDef* FavoritesScreen = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::PlayCallFavorites);
            if (FavoritesScreen && PlayCall->GetFavorites(bOffense).Num() > 0)
            {
                FPSMenuOptionDef Favorites;
                Favorites.OptionId = TEXT("Favorites");
                Favorites.Label = UPSLocalization::GetDataText(UPSLocalization::MenuKey(FavoritesScreen->ScreenId, TEXT("Title")), FavoritesScreen->Title).ToString();
                Favorites.TargetScreen = FavoritesScreen->ScreenId;
                Presented.Options.Add(Favorites);
            }
            const FPSMenuScreenDef* PlaysScreen = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::PlayCallPlays);
            Presented.Options.Append(PlayCall->BuildFormationOptions(bOffense, PlaysScreen ? PlaysScreen->ScreenId : NAME_None));
        }
        else if (PlayCall && Presented.Content == EPSMenuScreenContent::PlayCallPlays)
        {
            const FString Formation = ScreenPayloads.FindRef(ScreenId).ToString();
            Presented.Title = UPSLocalization::Verbatim(Formation).ToString();
            Presented.Options.Append(PlayCall->BuildPlayOptions(Formation, bOffense));
        }
        else if (PlayCall && Presented.Content == EPSMenuScreenContent::PlayCallRecent)
        {
            Presented.Options.Append(PlayCall->BuildRecentOptions(bOffense));
        }
        else if (PlayCall && Presented.Content == EPSMenuScreenContent::PlayCallFavorites)
        {
            Presented.Options.Append(PlayCall->BuildFavoriteOptions(bOffense));
        }
        else if (PlayCall)
        {
            Presented.Body = PlayCall->BuildAdjustmentScreenBody();
            Presented.Options.Append(PlayCall->BuildAdjustmentOptions());
        }
    }
    else if (Presented.Content == EPSMenuScreenContent::InputRemap)
    {
        // One option per remappable action, with its key on the device in use.
        APSPlayerController* Player = Cast<APSPlayerController>(GetOwner());
        UPSInputConfig* Config = Player ? Player->GetInputConfig() : nullptr;
        const UPSInputDeviceComponent* Devices = Player ? Player->GetInputDeviceComponent() : nullptr;
        const EPSInputDevice Device = Devices ? Devices->GetActiveDevice() : EPSInputDevice::KeyboardMouse;
        if (IsListeningForRemap())
        {
            FFormatNamedArguments Arguments;
            Arguments.Add(TEXT("Action"), PSMenuText::ActionName(RemapActionId));
            Presented.Body = UPSLocalization::Format(TEXT("Remap.Prompt"), Arguments).ToString();
        }
        else if (!RemapMessage.IsEmpty())
        {
            Presented.Body = RemapMessage;
        }
        if (Config)
        {
            for (const FPSInputActionDef& Action : Config->Catalog.Actions)
            {
                if (!Config->IsRemappable(Action.ActionId) || Action.Contexts.Num() == 0)
                {
                    continue;
                }
                FPSInputGlyph Glyph;
                const bool bHasGlyph = Config->GetGlyphForAction(Action.ActionId, Action.Contexts[0], Device, Glyph);
                // Button labels are the platform's own names, not translated.
                FPSMenuOptionDef Option;
                Option.OptionId = Action.ActionId;
                Option.Label = PSMenuText::OptionWithValue(PSMenuText::ActionName(Action.ActionId),
                    bHasGlyph ? UPSLocalization::Verbatim(Glyph.Label) : UPSLocalization::GetText(TEXT("Common.NoKey")));
                Option.Detail = PSMenuText::ContextNames(Action.Contexts).ToString();
                Option.Command = EPSMenuCommand::BeginRemap;
                Option.Payload = Action.ActionId;
                Presented.Options.Add(Option);
            }
        }
        FPSMenuOptionDef Reset;
        Reset.OptionId = TEXT("ResetRemaps");
        Reset.Label = UPSLocalization::GetText(TEXT("Menu.ResetAllKeys")).ToString();
        Reset.Command = EPSMenuCommand::ResetRemaps;
        Presented.Options.Add(Reset);
    }
    else if (UPSSettingsSubsystem* Settings = Presented.Content == EPSMenuScreenContent::Settings || Presented.Content == EPSMenuScreenContent::SettingsCategory ? GetSettings() : nullptr)
    {
        const FPSSettingsCatalog& SettingsCatalog = Settings->GetCatalog();
        if (Presented.Content == EPSMenuScreenContent::Settings)
        {
            // The categories, then the screen's own options (the key remapping screen).
            const FPSMenuScreenDef* CategoryScreen = GetCatalog().FindScreenWithContent(EPSMenuScreenContent::SettingsCategory);
            TArray<FPSMenuOptionDef> CategoryOptions;
            for (const FPSSettingCategoryDef& Category : SettingsCatalog.Categories)
            {
                FPSMenuOptionDef Option;
                Option.OptionId = Category.CategoryId;
                Option.Label = UPSLocalization::GetDataText(UPSLocalization::SettingCategoryKey(Category.CategoryId), Category.Label).ToString();
                Option.TargetScreen = CategoryScreen ? CategoryScreen->ScreenId : NAME_None;
                Option.Payload = Category.CategoryId;
                CategoryOptions.Add(Option);
            }
            Presented.Options.Insert(CategoryOptions, 0);
        }
        else
        {
            const FName CategoryId = ScreenPayloads.FindRef(ScreenId);
            if (const FPSSettingCategoryDef* Category = SettingsCatalog.FindCategory(CategoryId))
            {
                Presented.Title = UPSLocalization::GetDataText(UPSLocalization::SettingCategoryKey(CategoryId), Category->Label).ToString();
            }
            for (const FPSSettingDef& Def : SettingsCatalog.Settings)
            {
                if (Def.Category != CategoryId)
                {
                    continue;
                }
                FPSMenuOptionDef Option;
                Option.OptionId = Def.SettingId;
                Option.Label = PSMenuText::OptionWithValue(
                    UPSLocalization::GetDataText(UPSLocalization::SettingKey(Def.SettingId, TEXT("Label")), Def.Label), Settings->FormatValue(Def.SettingId));
                Option.Command = EPSMenuCommand::StepSetting;
                Option.Payload = Def.SettingId;
                Option.Detail = UPSLocalization::GetDataText(UPSLocalization::SettingKey(Def.SettingId, TEXT("Description")), Def.Description).ToString();
                Presented.Options.Add(Option);
            }
            FPSMenuOptionDef Reset;
            Reset.OptionId = TEXT("Reset");
            Reset.Label = UPSLocalization::GetText(TEXT("Menu.ResetToDefaults")).ToString();
            Reset.Command = EPSMenuCommand::ResetSettings;
            Reset.Payload = CategoryId;
            Presented.Options.Add(Reset);
        }
    }
    return Presented;
}

void UPSMenuComponent::HandleStackChanged(FName PreviousTop, FName NewTop, EPSMenuTransition Transition)
{
    ShowTopScreen();

    // The UI narration hook (Epic 103.3): a screen reader says where the player is now.
    UPSUIAccessibilitySubsystem* Accessibility = GetWorld() ? GetWorld()->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    if (Accessibility && IsMenuOpen())
    {
        const FPSMenuScreenDef Screen = GetPresentedScreen(GetTopScreenId());
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Title"), UPSLocalization::FromLocalized(Screen.Title));
        Arguments.Add(TEXT("Body"), UPSLocalization::FromLocalized(Screen.Body));
        Accessibility->Narrate(Screen.Body.IsEmpty() ? Screen.Title : UPSLocalization::Format(TEXT("Narration.Screen"), Arguments).ToString());
    }
}

void UPSMenuComponent::NarrateOption(FName OptionId)
{
    UPSUIAccessibilitySubsystem* Accessibility = GetWorld() ? GetWorld()->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    if (!Accessibility || !IsMenuOpen())
    {
        return;
    }
    const FPSMenuScreenDef Screen = GetPresentedScreen(GetTopScreenId());
    if (const FPSMenuOptionDef* Option = Screen.Options.FindByPredicate([OptionId](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == OptionId; }))
    {
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Label"), UPSLocalization::FromLocalized(Option->Label));
        Arguments.Add(TEXT("Detail"), UPSLocalization::FromLocalized(Option->Detail));
        Accessibility->Narrate(Option->Detail.IsEmpty() ? Option->Label : UPSLocalization::Format(TEXT("Narration.Option"), Arguments).ToString());
    }
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
        ActiveWidget->SetScreen(GetPresentedScreen(Stack->Top()), this, GetTransitionSeconds());
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
