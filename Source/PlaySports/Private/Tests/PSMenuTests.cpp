// PSMenuTests.cpp -- Epic 101 (front-end shell: screen stack, main menu/mode select, pause)
//
// Tests covered:
//   1. Data/ui_menus.json loads through UPSDataIngestion and validates; the mode-select screen
//      offers Play Now, Franchise and Practice; ValidateCatalog catches each class of mistake.
//   2. UPSMenuStack push/pop/replace/clear and the change events widgets transition on.
//   3. UPSMenuComponent navigation on a player controller: root screen can't be backed out
//      of, options open screens, Back returns, the pause screen opens/closes with Pause and
//      Back, Settings is reachable from both menus, Back keys come from the input catalog,
//      and each mode command travels with the right options.
//
// Headless worlds have no local player, so no widget is created and engine pause (which
// needs a game mode) is not exercised; the stack is what these tests observe.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSMenuComponent.h"
#include "PSMenuStack.h"
#include "PSPlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSMenuTests
{
    static bool AnyErrorContains(const TArray<FString>& Errors, const FString& Needle)
    {
        return Errors.ContainsByPredicate([&Needle](const FString& Error) { return Error.Contains(Needle); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The authored menu catalog loads and validates
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSMenuCatalogTest,
    "PlaySports.UI.MenuCatalogValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSMenuCatalogTest::RunTest(const FString& Parameters)
{
    FPSMenuCatalog Catalog;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    const bool bLoaded = Ingestion->LoadMenuCatalogFromJson(FPaths::ProjectDir() / TEXT("Data/ui_menus.json"), Catalog);
    TestTrue(TEXT("ui_menus.json loads through UPSDataIngestion"), bLoaded);
    if (!bLoaded)
    {
        return false;
    }

    for (const FString& Error : UPSMenuComponent::ValidateCatalog(Catalog))
    {
        AddError(FString::Printf(TEXT("ui_menus.json: %s"), *Error));
    }

    const FPSMenuScreenDef* Root = Catalog.FindScreen(Catalog.RootScreen);
    TestNotNull(TEXT("Root screen exists"), Root);
    TestNotNull(TEXT("Pause screen exists"), Catalog.FindScreen(Catalog.PauseScreen));
    TestNotNull(TEXT("Settings screen exists"), Catalog.FindScreen(TEXT("Settings")));

    const FPSMenuScreenDef* ModeSelect = Catalog.FindScreen(TEXT("ModeSelect"));
    if (TestNotNull(TEXT("Mode select screen exists"), ModeSelect))
    {
        TArray<EPSMenuCommand> Commands;
        bool bPlayNowPicksATeam = false;
        for (const FPSMenuOptionDef& Option : ModeSelect->Options)
        {
            Commands.Add(Option.Command);
            const FPSMenuScreenDef* Target = Catalog.FindScreen(Option.TargetScreen);
            bPlayNowPicksATeam |= Option.OptionId == FName(TEXT("PlayNow")) && Target && Target->Content == EPSMenuScreenContent::TeamSelect;
        }
        // Play Now goes through team select, whose generated options start the game.
        TestTrue(TEXT("Mode select offers Play Now through team select"), bPlayNowPicksATeam);
        TestTrue(TEXT("Mode select offers Franchise"), Commands.Contains(EPSMenuCommand::StartFranchise));
        TestTrue(TEXT("Mode select offers Practice/Gym"), Commands.Contains(EPSMenuCommand::StartPractice));
    }

    // A broken catalog: every rule fires.
    FPSMenuCatalog Broken;
    Broken.RootScreen = TEXT("Home");
    Broken.PauseScreen = TEXT("Missing");

    FPSMenuScreenDef Home;
    Home.ScreenId = TEXT("Home");
    Home.bAllowBack = true;
    FPSMenuOptionDef DoesNothing;
    DoesNothing.OptionId = TEXT("Idle");
    Home.Options.Add(DoesNothing);
    FPSMenuOptionDef Dangling;
    Dangling.OptionId = TEXT("Lost");
    Dangling.TargetScreen = TEXT("Nowhere");
    Home.Options.Add(Dangling);
    Broken.Screens.Add(Home);

    FPSMenuScreenDef DeadEnd;
    DeadEnd.ScreenId = TEXT("Home");
    DeadEnd.bAllowBack = false;
    Broken.Screens.Add(DeadEnd);

    const TArray<FString> Errors = UPSMenuComponent::ValidateCatalog(Broken);
    TestTrue(TEXT("Duplicate screen IDs are reported"), PSMenuTests::AnyErrorContains(Errors, TEXT("duplicate ScreenId")));
    TestTrue(TEXT("A root screen Back could close is reported"), PSMenuTests::AnyErrorContains(Errors, TEXT("must set bAllowBack to false")));
    TestTrue(TEXT("A missing pause screen is reported"), PSMenuTests::AnyErrorContains(Errors, TEXT("PauseScreen 'Missing'")));
    TestTrue(TEXT("An option that does nothing is reported"), PSMenuTests::AnyErrorContains(Errors, TEXT("option 'Idle': needs a TargetScreen or a Command")));
    TestTrue(TEXT("An unknown target screen is reported"), PSMenuTests::AnyErrorContains(Errors, TEXT("unknown TargetScreen 'Nowhere'")));
    TestTrue(TEXT("A screen that can never be left is reported"), PSMenuTests::AnyErrorContains(Errors, TEXT("can never be left")));

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Screen stack operations and change events
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSMenuStackTest,
    "PlaySports.UI.ScreenStackNavigation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSMenuStackTest::RunTest(const FString& Parameters)
{
    UPSMenuStack* Stack = NewObject<UPSMenuStack>();

    struct FChange
    {
        FName Previous;
        FName Current;
        EPSMenuTransition Transition;
    };
    TArray<FChange> Changes;
    Stack->OnChanged.AddLambda([&Changes](FName Previous, FName Current, EPSMenuTransition Transition)
    {
        Changes.Add({ Previous, Current, Transition });
    });

    TestTrue(TEXT("Starts empty"), Stack->IsEmpty());
    TestFalse(TEXT("Pop on empty does nothing"), Stack->Pop());

    TestTrue(TEXT("Push A"), Stack->Push(TEXT("A")));
    TestTrue(TEXT("Push B"), Stack->Push(TEXT("B")));
    TestFalse(TEXT("Pushing the top screen again is ignored"), Stack->Push(TEXT("B")));
    TestEqual(TEXT("Depth is 2"), Stack->Depth(), 2);
    TestTrue(TEXT("Top is B"), Stack->Top() == FName(TEXT("B")));

    Stack->Replace(TEXT("C"));
    TestTrue(TEXT("Replace swaps the top"), Stack->Top() == FName(TEXT("C")) && Stack->Depth() == 2);

    TestTrue(TEXT("Pop succeeds"), Stack->Pop());
    TestTrue(TEXT("Back to A"), Stack->Top() == FName(TEXT("A")));

    Stack->Clear();
    TestTrue(TEXT("Clear empties the stack"), Stack->IsEmpty());

    TestEqual(TEXT("One event per change (push, push, replace, pop, clear)"), Changes.Num(), 5);
    if (Changes.Num() == 5)
    {
        TestTrue(TEXT("First push: none -> A"), Changes[0].Previous.IsNone() && Changes[0].Current == FName(TEXT("A")) && Changes[0].Transition == EPSMenuTransition::Push);
        TestTrue(TEXT("Replace: B -> C"), Changes[2].Previous == FName(TEXT("B")) && Changes[2].Current == FName(TEXT("C")) && Changes[2].Transition == EPSMenuTransition::Replace);
        TestTrue(TEXT("Pop: C -> A"), Changes[3].Previous == FName(TEXT("C")) && Changes[3].Current == FName(TEXT("A")) && Changes[3].Transition == EPSMenuTransition::Pop);
        TestTrue(TEXT("Clear: A -> none"), Changes[4].Previous == FName(TEXT("A")) && Changes[4].Current.IsNone() && Changes[4].Transition == EPSMenuTransition::Clear);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Front-end and pause flow on a player controller
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSMenuFlowTest,
    "PlaySports.UI.MenuFlow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSMenuFlowTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("Controller has a menu component"), Menu))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    const FName Root = Menu->GetCatalog().RootScreen;
    const FName PauseScreen = Menu->GetCatalog().PauseScreen;

    // Front end.
    Menu->OpenRootScreen();
    TestTrue(TEXT("Root screen opens"), Menu->IsMenuOpen() && Menu->GetTopScreenId() == Root);
    TestFalse(TEXT("Back on the root screen does nothing"), Menu->HandleBack());
    TestTrue(TEXT("Still on the root screen"), Menu->GetTopScreenId() == Root);

    Menu->ChooseOption(TEXT("Play"));
    TestTrue(TEXT("Play opens mode select"), Menu->GetTopScreenId() == FName(TEXT("ModeSelect")));
    TestTrue(TEXT("Back returns to the root"), Menu->HandleBack() && Menu->GetTopScreenId() == Root);

    Menu->ChooseOption(TEXT("Settings"));
    TestTrue(TEXT("Settings is reachable from the main menu"), Menu->GetTopScreenId() == FName(TEXT("Settings")));
    TestTrue(TEXT("Back leaves settings"), Menu->HandleBack() && Menu->GetTopScreenId() == Root);

    // Mode commands travel to the default map with the selected mode; quitting to the main
    // menu travels with the front end's game mode alias.
    TestEqual(TEXT("Play Now travel options"), Menu->BuildTravelOptions(EPSMenuCommand::StartPlayNow), FString(TEXT("mode=PlayNow")));
    TestEqual(TEXT("Franchise travel options"), Menu->BuildTravelOptions(EPSMenuCommand::StartFranchise), FString(TEXT("mode=Franchise")));
    TestEqual(TEXT("Practice travel options"), Menu->BuildTravelOptions(EPSMenuCommand::StartPractice), FString(TEXT("mode=Practice")));
    TestEqual(TEXT("Quit to main menu travel options"), Menu->BuildTravelOptions(EPSMenuCommand::QuitToMainMenu), FString(TEXT("game=Menu")));
    TestTrue(TEXT("Resume does not travel"), Menu->BuildTravelOptions(EPSMenuCommand::Resume).IsEmpty());

    // In game: Pause opens the pause screen; Back and Pause both close it.
    Menu->Resume();
    TestFalse(TEXT("Resume closes every screen"), Menu->IsMenuOpen());

    Menu->TogglePause();
    TestTrue(TEXT("Pause opens the pause screen"), Menu->GetTopScreenId() == PauseScreen);
    Menu->ChooseOption(TEXT("Settings"));
    TestTrue(TEXT("Settings is reachable from the pause menu"), Menu->GetTopScreenId() == FName(TEXT("Settings")));
    Menu->TogglePause();
    TestTrue(TEXT("Pause does nothing while a sub-screen is open"), Menu->GetTopScreenId() == FName(TEXT("Settings")));
    TestTrue(TEXT("Back returns to the pause screen"), Menu->HandleBack() && Menu->GetTopScreenId() == PauseScreen);
    TestTrue(TEXT("Back on the pause screen resumes"), Menu->HandleBack() && !Menu->IsMenuOpen());

    Menu->TogglePause();
    Menu->TogglePause();
    TestFalse(TEXT("Pause twice resumes"), Menu->IsMenuOpen());

    Menu->TogglePause();
    Menu->ChooseOption(TEXT("Resume"));
    TestFalse(TEXT("The Resume option resumes"), Menu->IsMenuOpen());
    TestFalse(TEXT("Nothing left paused"), Menu->IsPausedByMenu());

    // Back keys come from the input catalog: Cancel in the Menu context everywhere, and the
    // Pause keys only while the pause screen is on top.
    TestTrue(TEXT("Escape is Back"), Menu->IsBackKey(EKeys::Escape));
    TestTrue(TEXT("B is Back"), Menu->IsBackKey(EKeys::Gamepad_FaceButton_Right));
    TestFalse(TEXT("Start is not Back outside the pause screen"), Menu->IsBackKey(EKeys::Gamepad_Special_Right));
    TestFalse(TEXT("W is not Back"), Menu->IsBackKey(EKeys::W));
    Menu->TogglePause();
    TestTrue(TEXT("Start closes the pause screen"), Menu->IsBackKey(EKeys::Gamepad_Special_Right));
    TestTrue(TEXT("P closes the pause screen"), Menu->IsBackKey(EKeys::P));
    Menu->Resume();

    TestFalse(TEXT("Unknown screens are refused"), Menu->OpenScreen(TEXT("NoSuchScreen")));

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
