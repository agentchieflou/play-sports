// PSXboxReadinessTests.cpp -- Epic 150 (Xbox readiness without a console), headless on Win64
//
// Tests covered:
//   1. The XboxSeries tier: Xbox Series X|S ("XSX") resolves to it through the tier system, it is
//      budgeted for 60 fps on the XSX profile, and its title-safe area is the inner 90%.
//   2. The title-safe area: the anchors and margins for a share of the screen, clamped to what a
//      title-safe area can be, and this (Win64) run's share is its tier's.
//   3. Xbox glyphs on every screen: every action a screen's input context offers on a gamepad
//      draws an Xbox glyph, and the menus' continue button is A.
//   4. A lone player's controller disconnects mid-game: the game pauses, the pause screen offers
//      another controller, A on another controller takes over; the same controller coming back
//      restores the pairing; a spare pad going away while on the keyboard changes nothing.
//   5. Local head-to-head: each player paired with a user and a controller; a lost seat is taken
//      over by an unseated controller's A, never by the other seat's, and the session plays on.
//   6. Quick Resume: a suspend mid-game pauses it and finishes the save in flight, a resume
//      leaves it paused, and a user who signed out while the game was away is asked for on the
//      pause screen until they are back.
//
// Headless worlds have no game instance: the platform services are made with NewObject on the
// null/local implementation and handed over by hand.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSControllerPairingSubsystem.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSInputGlyphs.h"
#include "PSLocalization.h"
#include "PSMenuComponent.h"
#include "PSMenuStack.h"
#include "PSPlatformBackendLocal.h"
#include "PSPlatformServices.h"
#include "PSPlatformTiers.h"
#include "PSPlayerController.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "PSTelemetryBus.h"
#include "PSTitleSafeArea.h"
#include "PSVersusSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSXboxReadinessTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static APSPlayerController* SpawnHuman(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    /** Platform services on the null/local implementation; OutLocal is that implementation. */
    static UPSPlatformServices* MakeServices(UWorld* World, UPSPlatformBackendLocal*& OutLocal, const FString& StorageRoot = FString())
    {
        UPSPlatformServices* Services = NewObject<UPSPlatformServices>(NewObject<UGameInstance>());
        OutLocal = NewObject<UPSPlatformBackendLocal>(Services);
        OutLocal->StorageRoot = StorageRoot;
        Services->UseBackend(OutLocal);
        Services->SetEventWorld(World);
        return Services;
    }

    static int32 CountKind(const TArray<FPSTelemetryControllerPairingEvent>& Events, EPSControllerPairingKind Kind)
    {
        return Events.FilterByPredicate([Kind](const FPSTelemetryControllerPairingEvent& Event) { return Event.Kind == Kind; }).Num();
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The XboxSeries tier
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSXboxSeriesTierTest,
    "PlaySports.Platform.XboxSeriesTier",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSXboxSeriesTierTest::RunTest(const FString& Parameters)
{
    FPSPlatformTierCatalog Catalog;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!TestTrue(TEXT("Data/platform_tiers.json loads"), Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), Catalog)))
    {
        return false;
    }
    TestEqual(TEXT("The tier catalog is sound"), PSPlatformTiers::ValidateCatalog(Catalog).Num(), 0);

    // The platform's own name picks the tier: no console check anywhere in code.
    TestEqual(TEXT("Xbox Series X|S runs the XboxSeries tier"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("XSX"), FString()), FName(TEXT("XboxSeries")));
    TestEqual(TEXT("-PSTier=XboxSeries puts a PC run on it, to profile the console's budgets"),
        PSPlatformTiers::ResolveTierId(Catalog, TEXT("Windows"), TEXT("XboxSeries")), FName(TEXT("XboxSeries")));
    TestEqual(TEXT("Windows stays on the desktop tier"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("Windows"), FString()), FName(TEXT("DesktopHigh")));

    const FPSPlatformTier* Xbox = PSPlatformTiers::FindTier(Catalog, TEXT("XboxSeries"));
    const FPSPlatformTier* Desktop = PSPlatformTiers::FindTier(Catalog, TEXT("DesktopHigh"));
    if (TestNotNull(TEXT("The XboxSeries tier"), Xbox) && TestNotNull(TEXT("The desktop tier"), Desktop))
    {
        TestEqual(TEXT("Its rendering half is the engine's XSX profile"), Xbox->DeviceProfile, FName(TEXT("XSX")));
        TestEqual(TEXT("Budgeted for 60 fps (Series S, the floor)"), Xbox->TargetFrameRate, 60.f);
        TestEqual(TEXT("HUD and menus keep to the inner 90% of a TV"), Xbox->TitleSafeArea, 0.9f);
        TestEqual(TEXT("A monitor uses the whole screen"), Desktop->TitleSafeArea, 1.f);
        TestEqual(TEXT("Its system budgets are sound"), PSPlatformTiers::ValidateSystemBudgets(*Xbox, 0).Num(), 0);
    }

    FPSPlatformTierCatalog Broken = Catalog;
    if (Broken.Tiers.Num() > 0)
    {
        Broken.Tiers[0].TitleSafeArea = 0.3f;
        TestEqual(TEXT("A title-safe area under half the screen is a problem"), PSPlatformTiers::ValidateCatalog(Broken).Num(), 1);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The title-safe area
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTitleSafeAreaTest,
    "PlaySports.Platform.TitleSafeArea",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTitleSafeAreaTest::RunTest(const FString& Parameters)
{
    FVector2D Min;
    FVector2D Max;
    PSTitleSafeArea::GetAnchorBounds(0.9f, Min, Max);
    TestTrue(TEXT("90%: anchored 5% in from each edge"), Min.Equals(FVector2D(0.05f, 0.05f), 1e-4f) && Max.Equals(FVector2D(0.95f, 0.95f), 1e-4f));
    PSTitleSafeArea::GetAnchorBounds(1.f, Min, Max);
    TestTrue(TEXT("100%: the whole screen"), Min.Equals(FVector2D::ZeroVector) && Max.Equals(FVector2D(1.f, 1.f)));

    const FMargin Tv = PSTitleSafeArea::MakeMargin(FVector2D(1920.f, 1080.f), 0.9f);
    TestEqual(TEXT("A 1080p screen at 90%: 96 px each side"), Tv.Left, 96.f);
    TestEqual(TEXT("...and 54 px top and bottom"), Tv.Top, 54.f);
    TestTrue(TEXT("...the same on the far sides"), FMath::IsNearlyEqual(Tv.Right, 96.f) && FMath::IsNearlyEqual(Tv.Bottom, 54.f));
    const FMargin Whole = PSTitleSafeArea::MakeMargin(FVector2D(1920.f, 1080.f), 1.f);
    TestTrue(TEXT("The whole screen needs no margin"), Whole.Left == 0.f && Whole.Top == 0.f && Whole.Right == 0.f && Whole.Bottom == 0.f);

    TestEqual(TEXT("Under half the screen clamps to half"), PSTitleSafeArea::ClampFraction(0.2f), 0.5f);
    TestEqual(TEXT("Over the whole screen clamps to it"), PSTitleSafeArea::ClampFraction(1.4f), 1.f);
    TestEqual(TEXT("This run's area is its tier's"), PSTitleSafeArea::GetActiveFraction(),
        PSTitleSafeArea::ClampFraction(PSPlatformTiers::GetActiveTier().TitleSafeArea));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Xbox glyphs on every screen
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSXboxGlyphsEveryScreenTest,
    "PlaySports.Platform.XboxGlyphsOnEveryScreen",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSXboxGlyphsEveryScreenTest::RunTest(const FString& Parameters)
{
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    if (!TestTrue(TEXT("The input catalog and glyphs load"), Input->LoadDefaults() && Input->GetGlyphs() != nullptr))
    {
        return false;
    }

    // Every screen takes its input from a context: the menus from Menu, the field from OnField
    // and its depth contexts, replay, photo mode and the telestrator from theirs. So every
    // action of every context, on a gamepad, is what a player sees on some screen.
    int32 Checked = 0;
    for (const FPSInputActionDef& Action : Input->Catalog.Actions)
    {
        const bool bHasPadKey = Action.Bindings.ContainsByPredicate([](const FPSInputKeyBinding& Binding)
        {
            return FKey(Binding.Key).IsGamepadKey();
        });
        if (!bHasPadKey)
        {
            continue;
        }
        for (const FName& ContextId : Action.Contexts)
        {
            FPSInputGlyph Glyph;
            const FString Where = FString::Printf(TEXT("%s in %s"), *Action.ActionId.ToString(), *ContextId.ToString());
            if (!TestTrue(*FString::Printf(TEXT("%s has a gamepad glyph"), *Where), Input->GetGlyphForAction(Action.ActionId, ContextId, EPSInputDevice::Gamepad, Glyph)))
            {
                continue;
            }
            TestEqual(*FString::Printf(TEXT("%s draws from the Xbox set"), *Where), Glyph.GlyphSetId, FName(TEXT("Xbox")));
            TestTrue(*FString::Printf(TEXT("%s is an Xbox glyph with a label"), *Where), Glyph.GlyphId.ToString().StartsWith(TEXT("Xbox_")) && !Glyph.Label.IsEmpty());
            ++Checked;
        }
    }
    TestTrue(TEXT("Every context's gamepad actions were checked"), Checked >= Input->Catalog.Contexts.Num());

    // The menus' continue and back buttons, as every menu screen shows them.
    FPSInputGlyph Continue;
    TestTrue(TEXT("Menus continue with A"), Input->GetGlyphForAction(TEXT("Confirm"), TEXT("Menu"), EPSInputDevice::Gamepad, Continue) && Continue.Label == TEXT("A"));
    FPSInputGlyph Back;
    TestTrue(TEXT("...and go back with B"), Input->GetGlyphForAction(TEXT("Cancel"), TEXT("Menu"), EPSInputDevice::Gamepad, Back) && Back.Label == TEXT("B"));
    FPSInputGlyph Pause;
    TestTrue(TEXT("Pause is the Menu button"), Input->GetGlyphForAction(TEXT("Pause"), TEXT("OnField"), EPSInputDevice::Gamepad, Pause) && Pause.Label == TEXT("Menu"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- A lone player's controller disconnects
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSControllerLostTest,
    "PlaySports.Platform.ControllerLostPausesAndReassigns",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSControllerLostTest::RunTest(const FString& Parameters)
{
    using namespace PSXboxReadinessTests;
    UPSLocalization::RegisterStringTables();
    UPSLocalization::SetPseudoLocalization(false);

    UWorld* World = CreateTestWorld();
    APSPlayerController* Human = World ? SpawnHuman(World) : nullptr;
    UPSMenuComponent* Menu = Human ? Human->GetMenuComponent() : nullptr;
    UPSInputDeviceComponent* Devices = Human ? Human->GetInputDeviceComponent() : nullptr;
    UPSControllerPairingSubsystem* Pairing = World ? World->GetSubsystem<UPSControllerPairingSubsystem>() : nullptr;
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestTrue(TEXT("A human with menus and devices, the pairing subsystem and the bus"), Menu && Devices && Pairing && Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSPlatformBackendLocal* Local = nullptr;
    UPSPlatformServices* Services = MakeServices(World, Local);
    Pairing->SetPlatformServices(Services);
    const FName PauseScreen = Menu->GetCatalog().PauseScreen;

    TArray<FPSTelemetryControllerPairingEvent> Events;
    const FDelegateHandle Handle = Bus->OnControllerPairingMC.AddLambda([&Events](const FPSTelemetryControllerPairingEvent& Event) { Events.Add(Event); });

    // One player: the first user, driven by any controller.
    const FPSControllerPairing Start = Pairing->GetPairing(0);
    TestEqual(TEXT("The player is the first user"), Start.User.UserId, FString(TEXT("Local0")));
    TestEqual(TEXT("...driven by any controller"), Start.InputUserIndex, static_cast<int32>(INDEX_NONE));
    TestFalse(TEXT("...with nothing lost"), Start.bControllerLost || Start.bUserSignedOut);

    // Playing on a pad, the pad dies: paused, and the pause screen says what to do.
    Devices->NotifyInput(EKeys::Gamepad_FaceButton_Bottom, 1.f);
    Devices->NotifyConnectionChange(false, true);
    TestTrue(TEXT("The controller is lost"), Pairing->IsControllerLost(0));
    TestEqual(TEXT("The game paused"), Menu->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("The loss is on the bus"), CountKind(Events, EPSControllerPairingKind::ControllerLost), 1);
    const FString Body = Menu->GetPresentedScreen(PauseScreen).Body;
    TestTrue(TEXT("The pause screen asks for the controller back"), Body.Contains(TEXT("reconnect")) && Body.Contains(TEXT("Player 1")));
    TestTrue(TEXT("...or A on another one"), Body.Contains(TEXT("press A on another controller")));

    // Another controller: B does nothing, A takes over.
    TestFalse(TEXT("B on another controller is not the continue button"), Pairing->HandleButtonPress(1, EKeys::Gamepad_FaceButton_Right));
    TestTrue(TEXT("...still lost"), Pairing->IsControllerLost(0));
    Devices->NotifyUserInput(1, EKeys::Gamepad_FaceButton_Bottom, 1.f);
    TestFalse(TEXT("A on another controller takes over"), Pairing->IsControllerLost(0));
    TestTrue(TEXT("...announced with that controller's user"), Events.Num() > 0 && Events.Last().Kind == EPSControllerPairingKind::ControllerReassigned && Events.Last().InputUserIndex == 1);
    TestEqual(TEXT("The game stays paused for the player to resume"), Menu->GetTopScreenId(), PauseScreen);
    TestTrue(TEXT("Nothing more to say"), Pairing->DescribeStatus(0).IsEmpty());
    TestTrue(TEXT("A second press changes nothing"), !Pairing->HandleButtonPress(2, EKeys::Gamepad_FaceButton_Bottom));
    Menu->Resume();

    // Lost again, and the same pad comes back.
    Devices->NotifyInput(EKeys::Gamepad_FaceButton_Bottom, 1.f);
    Devices->NotifyConnectionChange(false, true);
    TestTrue(TEXT("Lost again"), Pairing->IsControllerLost(0) && Menu->GetTopScreenId() == PauseScreen);
    Devices->NotifyConnectionChange(true, true);
    TestFalse(TEXT("The controller is back"), Pairing->IsControllerLost(0));
    TestEqual(TEXT("...announced"), CountKind(Events, EPSControllerPairingKind::ControllerRestored), 1);
    Menu->Resume();

    // On the keyboard, a pad going away is no matter.
    Devices->NotifyInput(EKeys::W, 1.f);
    Devices->NotifyConnectionChange(false, true);
    TestFalse(TEXT("A spare pad going away loses nothing"), Pairing->IsControllerLost(0));
    TestFalse(TEXT("...and pauses nothing"), Menu->IsMenuOpen());
    TestEqual(TEXT("Two losses in all"), CountKind(Events, EPSControllerPairingKind::ControllerLost), 2);

    Bus->OnControllerPairingMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Local head-to-head: users, controllers and reassignment
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSVersusPairingTest,
    "PlaySports.Platform.VersusControllerReassignment",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSVersusPairingTest::RunTest(const FString& Parameters)
{
    using namespace PSXboxReadinessTests;
    UPSLocalization::RegisterStringTables();
    UPSLocalization::SetPseudoLocalization(false);

    UWorld* World = CreateTestWorld();
    UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    UPSControllerPairingSubsystem* Pairing = World ? World->GetSubsystem<UPSControllerPairingSubsystem>() : nullptr;
    APSPlayerController* P1 = World ? SpawnHuman(World) : nullptr;
    APSPlayerController* P2 = World ? SpawnHuman(World) : nullptr;
    if (!TestTrue(TEXT("Two humans, the session and the pairing subsystem"), Versus && Pairing && P1 && P2))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSPlatformBackendLocal* Local = nullptr;
    Pairing->SetPlatformServices(MakeServices(World, Local));

    const bool bStarted = Versus->ClaimSeat(P1, 0) == 0 && Versus->ClaimSeat(P2, 1) == 1
        && Versus->SelectTeam(0, EPSVersusTeam::Home) && Versus->SelectTeam(1, EPSVersusTeam::Away)
        && Versus->SetReady(0, true) && Versus->SetReady(1, true) && Versus->StartSession();
    if (!TestTrue(TEXT("Session started"), bStarted))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Each player is a user with their own controller.
    const TArray<FPSControllerPairing> Pairings = Pairing->GetPairings();
    if (TestEqual(TEXT("Two pairings"), Pairings.Num(), 2))
    {
        TestTrue(TEXT("Player 1 is user 0 on user 0's controller"), Pairings[0].InputUserIndex == 0 && Pairings[0].User.UserId == TEXT("Local0"));
        TestTrue(TEXT("Player 2 is user 1 on user 1's controller"), Pairings[1].InputUserIndex == 1 && Pairings[1].User.UserId == TEXT("Local1"));
    }

    // Player 2's controller dies: the session pauses by its etiquette, and offers another.
    UPSInputDeviceComponent* Devices1 = P1->GetInputDeviceComponent();
    UPSInputDeviceComponent* Devices2 = P2->GetInputDeviceComponent();
    Devices2->NotifyUserConnectionChange(1, false, true);
    TestEqual(TEXT("The session paused"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestTrue(TEXT("Player 2's controller is lost"), Pairing->IsControllerLost(1));
    TestFalse(TEXT("Player 1's is not"), Pairing->IsControllerLost(0));
    TestTrue(TEXT("Player 2's pause screen offers another controller"), Pairing->DescribeStatus(1).ToString().Contains(TEXT("press A on another controller")));

    // Player 1's own A never takes player 2's seat; an unseated controller's does.
    TestFalse(TEXT("The other seat's controller can't take the seat"), Pairing->HandleButtonPress(0, EKeys::Gamepad_FaceButton_Bottom));
    Devices1->NotifyUserInput(2, EKeys::Gamepad_FaceButton_Bottom, 1.f);
    TestFalse(TEXT("A spare controller's A takes player 2's seat"), Pairing->IsControllerLost(1));
    TestEqual(TEXT("The seat counts that controller's user now"), Versus->GetSeat(1).UserIndex, 2);
    TestEqual(TEXT("...and so does player 2's device tracking"), Devices2->OwnerUserIndex, 2);
    TestEqual(TEXT("The pairing follows"), Pairing->GetPairing(1).InputUserIndex, 2);
    TestEqual(TEXT("Player 1 is untouched"), Pairing->GetPairing(0).InputUserIndex, 0);

    // Both ready: play resumes.
    Versus->ConfirmResume(0);
    Versus->ConfirmResume(1);
    Versus->AdvanceResume(Versus->GetRules().ResumeCountdownSeconds + 1.f);
    TestEqual(TEXT("The session plays on"), Versus->GetPhase(), EPSVersusPhase::Playing);
    TestEqual(TEXT("Nobody's user went missing"), Pairing->ValidateUsers(), 0);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Quick Resume
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSQuickResumeTest,
    "PlaySports.Platform.QuickResumeKeepsTheGame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSQuickResumeTest::RunTest(const FString& Parameters)
{
    using namespace PSXboxReadinessTests;
    UPSLocalization::RegisterStringTables();
    UPSLocalization::SetPseudoLocalization(false);

    UWorld* World = CreateTestWorld();
    APSPlayerController* Human = World ? SpawnHuman(World) : nullptr;
    UPSMenuComponent* Menu = Human ? Human->GetMenuComponent() : nullptr;
    UPSControllerPairingSubsystem* Pairing = World ? World->GetSubsystem<UPSControllerPairingSubsystem>() : nullptr;
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestTrue(TEXT("A human with menus, the pairing subsystem and the bus"), Menu && Pairing && Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const FString Root = FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("QuickResume");
    IFileManager::Get().DeleteDirectory(*Root, false, true);
    UPSPlatformBackendLocal* Local = nullptr;
    UPSPlatformServices* Services = MakeServices(World, Local, Root);
    Pairing->SetPlatformServices(Services);
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    Saves->SetPlatformServices(Services);
    const FName PauseScreen = Menu->GetCatalog().PauseScreen;
    TestEqual(TEXT("Everyone is signed in to start"), Pairing->ValidateUsers(), 0);

    TArray<FPSTelemetryControllerPairingEvent> Events;
    const FDelegateHandle Handle = Bus->OnControllerPairingMC.AddLambda([&Events](const FPSTelemetryControllerPairingEvent& Event) { Events.Add(Event); });

    // Mid-game, a save in flight: the console suspends the game (the player switched games).
    UPSProfileSaveGame* Profile = NewObject<UPSProfileSaveGame>();
    Profile->FavoritePlays.Add(TEXT("Slant_Flat"));
    const FString Slot = TEXT("QuickResume_Profile");
    Saves->SaveToSlotAsync(Profile, Slot, FPSSaveOpComplete());
    Services->HandleLifecycle(EPSPlatformLifecycle::Suspend);
    TestEqual(TEXT("The game paused"), Menu->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("No write is left in flight"), Saves->GetPendingWriteCount(), 0);
    TestTrue(TEXT("The save is on disk"), Saves->DoesSlotExist(Slot));

    // Quick Resume: the game comes back where it was, still paused for the player.
    Services->HandleLifecycle(EPSPlatformLifecycle::Resume);
    TestEqual(TEXT("Back, still paused"), Menu->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("The same user is playing"), CountKind(Events, EPSControllerPairingKind::UserSignedOut), 0);
    TestFalse(TEXT("The pause screen has nothing to ask"), Menu->GetPresentedScreen(PauseScreen).Body.Contains(TEXT("signed out")));
    Menu->Resume();

    // Suspended again, and the player signs out meanwhile: on resume the game asks for them.
    Services->HandleLifecycle(EPSPlatformLifecycle::Suspend);
    Local->SetUserSignedIn(0, false);
    Services->HandleLifecycle(EPSPlatformLifecycle::Resume);
    TestTrue(TEXT("The player's user is gone"), Pairing->GetPairing(0).bUserSignedOut);
    TestEqual(TEXT("...announced once"), CountKind(Events, EPSControllerPairingKind::UserSignedOut), 1);
    TestEqual(TEXT("The game stays paused"), Menu->GetTopScreenId(), PauseScreen);
    TestTrue(TEXT("The pause screen asks them to sign in"), Menu->GetPresentedScreen(PauseScreen).Body.Contains(TEXT("signed out")));

    // They sign back in: the pairing is whole again.
    Local->SetUserSignedIn(0, true);
    TestFalse(TEXT("The user is back"), Pairing->GetPairing(0).bUserSignedOut);
    TestEqual(TEXT("...announced"), CountKind(Events, EPSControllerPairingKind::UserRestored), 1);
    TestTrue(TEXT("Nothing more to ask"), Pairing->DescribeStatus(0).IsEmpty());

    Bus->OnControllerPairingMC.Remove(Handle);
    IFileManager::Get().DeleteDirectory(*Root, false, true);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
