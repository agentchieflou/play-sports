// PSPlatformServicesTests.cpp -- Epic 152 (platform services layer), against the null/local
// implementation
//
// Tests covered:
//   1. The configured implementation on Win64 is the null/local one, and it serves users, the
//      save storage root, achievements and presence; the engine names the platform without an
//      #if (UPSSessionService::GetCurrentPlatform).
//   2. Saves store through the services: slots land under the platform's storage root, a save
//      written under one platform's root loads under another's (the format is portable), and a
//      suspend finishes every write in flight.
//   3. Lifecycle events: published on the bus before the game instance's listeners hear them,
//      repeats dropped, suspended and constrained tracked.
//   4. A suspend or a constrain pauses a live game (over a play-call screen too, which comes back
//      on resume); a resume leaves it paused; the front end is left alone.
//   5. In a local head-to-head session a suspend pauses through the session's etiquette without
//      costing either player a pause.
//
// Headless worlds have no game instance, so the services are made with NewObject and handed a
// backend (UseBackend) and the test world (SetEventWorld).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAutoPauseSubsystem.h"
#include "PSLocalization.h"
#include "PSMenuComponent.h"
#include "PSMenuStack.h"
#include "PSPlatformBackendLocal.h"
#include "PSPlatformServices.h"
#include "PSPlayerController.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "PSSessionService.h"
#include "PSTelemetryBus.h"
#include "PSVersusSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlatformServicesTests
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

    /** Platform services on the null/local implementation, saving under StorageRoot when given. */
    static UPSPlatformServices* MakeServices(const FString& StorageRoot = FString())
    {
        UPSPlatformServices* Services = NewObject<UPSPlatformServices>(NewObject<UGameInstance>());
        UPSPlatformBackendLocal* Local = NewObject<UPSPlatformBackendLocal>(Services);
        Local->StorageRoot = StorageRoot;
        Services->UseBackend(Local);
        return Services;
    }

    static UPSSaveSubsystem* MakeSaves(UPSPlatformServices* Services)
    {
        UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
        Saves->SetPlatformServices(Services);
        return Saves;
    }

    static FString TestRoot(const TCHAR* Platform)
    {
        return FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("PlatformServices") / Platform;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The null/local implementation serves
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformNullImplementationTest,
    "PlaySports.Platform.NullImplementationServes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformNullImplementationTest::RunTest(const FString& Parameters)
{
    UPSLocalization::RegisterStringTables();
    UPSLocalization::SetPseudoLocalization(false);

    TestTrue(TEXT("Win64 is configured with the null/local implementation"),
        UPSPlatformServices::ResolveBackendClass() == UPSPlatformBackendLocal::StaticClass());

    UPSPlatformServices* Services = NewObject<UPSPlatformServices>(NewObject<UGameInstance>());
    TestEqual(TEXT("No implementation yet: no name"), Services->GetBackendName(), FName());
    TestFalse(TEXT("...and nobody signed in"), Services->GetSignedInUser(0).IsSignedIn());

    Services->UseBackend(NewObject<UPSPlatformBackend>(Services, UPSPlatformServices::ResolveBackendClass()));
    TestEqual(TEXT("The implementation is the local one"), Services->GetBackendName(), FName(TEXT("Local")));

    // Users: no sign-in on a PC, so every local slot has its player.
    const FPSPlatformUser First = Services->GetSignedInUser(0);
    TestTrue(TEXT("The first player is signed in"), First.IsSignedIn());
    TestEqual(TEXT("...as Local0"), First.UserId, FString(TEXT("Local0")));
    TestEqual(TEXT("...in slot 0"), First.LocalUserIndex, 0);
    TestEqual(TEXT("...shown as Player 1"), First.DisplayName.ToString(), FString(TEXT("Player 1")));
    const FPSPlatformUser Second = Services->GetSignedInUser(1);
    TestTrue(TEXT("The second player (head to head) is signed in too"), Second.IsSignedIn() && Second.UserId == TEXT("Local1"));
    TestEqual(TEXT("...shown as Player 2"), Second.DisplayName.ToString(), FString(TEXT("Player 2")));
    TestFalse(TEXT("No user in slot -1"), Services->GetSignedInUser(-1).IsSignedIn());
    TestFalse(TEXT("No user past the last slot"), Services->GetSignedInUser(GetDefault<UPSPlatformBackendLocal>()->MaxLocalUsers).IsSignedIn());

    // Storage: the project's Saved/SaveGames, the same as the configured default.
    const FString Expected = FPaths::ProjectSavedDir() / TEXT("SaveGames");
    TestEqual(TEXT("Saves go to Saved/SaveGames"), Services->GetSaveStorageRoot(), Expected);
    TestEqual(TEXT("...which is the default root"), UPSPlatformServices::GetDefaultSaveStorageRoot(), Expected);

    // Achievements and presence are remembered.
    TestFalse(TEXT("No achievement called None"), Services->UnlockAchievement(NAME_None));
    TestFalse(TEXT("Nothing unlocked yet"), Services->IsAchievementUnlocked(TEXT("FirstWin")));
    TestTrue(TEXT("Unlocking an achievement works once"), Services->UnlockAchievement(TEXT("FirstWin")));
    TestFalse(TEXT("...and not twice"), Services->UnlockAchievement(TEXT("FirstWin")));
    TestTrue(TEXT("It stays unlocked"), Services->IsAchievementUnlocked(TEXT("FirstWin")));
    TestFalse(TEXT("Others stay locked"), Services->IsAchievementUnlocked(TEXT("Shutout")));
    Services->SetPresence(TEXT("PlayingGame"));
    TestEqual(TEXT("Presence is what was set"), Services->GetPresence(), FName(TEXT("PlayingGame")));

    // Without an implementation the services still answer the storage question.
    Services->UseBackend(nullptr);
    TestEqual(TEXT("No implementation: the default root"), Services->GetSaveStorageRoot(), Expected);
    TestFalse(TEXT("...and no achievements"), Services->UnlockAchievement(TEXT("FirstWin")));

    // The session service names the platform from the engine, not from an #if.
    const EPSSessionPlatform Platform = UPSSessionService::GetCurrentPlatform();
    TestTrue(TEXT("This platform is known to the session service"), Platform != EPSSessionPlatform::Unknown);
    TestTrue(TEXT("...by the engine's name for it"), UEnum::GetValueAsString(Platform).Contains(FPlatformProperties::IniPlatformName()));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Saves store through the services and stay portable
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformSavesTest,
    "PlaySports.Platform.SavesStoreThroughServices",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformSavesTest::RunTest(const FString& Parameters)
{
    using namespace PSPlatformServicesTests;
    IFileManager& Files = IFileManager::Get();
    const FString RootA = TestRoot(TEXT("A"));
    const FString RootB = TestRoot(TEXT("B"));
    Files.DeleteDirectory(*RootA, false, true);
    Files.DeleteDirectory(*RootB, false, true);

    // Two platforms, each keeping saves in its own place.
    UPSPlatformServices* PlatformA = MakeServices(RootA);
    UPSPlatformServices* PlatformB = MakeServices(RootB);
    UPSSaveSubsystem* SavesA = MakeSaves(PlatformA);
    UPSSaveSubsystem* SavesB = MakeSaves(PlatformB);
    TestEqual(TEXT("Platform A's saves go to its root"), SavesA->GetStorageRoot(), RootA);
    TestEqual(TEXT("Platform B's saves go to its root"), SavesB->GetStorageRoot(), RootB);

    // A save subsystem without services (every other headless test) keeps the old place.
    UPSSaveSubsystem* Unbound = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("PlatformServices_Portable");
    TestEqual(TEXT("Without services: the default root"), Unbound->GetStorageRoot(), UPSPlatformServices::GetDefaultSaveStorageRoot());
    TestEqual(TEXT("...where GetSlotPath points"), Unbound->GetSlotFilePath(Slot), UPSSaveSubsystem::GetSlotPath(Slot));

    // Platform A writes a profile; it lands under A's root and nowhere else.
    UPSProfileSaveGame* Profile = NewObject<UPSProfileSaveGame>();
    Profile->FavoritePlays = { FName(TEXT("Slant_Flat")), FName(TEXT("Inside_Zone")) };
    Profile->Settings.Add(TEXT("MasterVolume"), 0.4f);
    TestTrue(TEXT("Platform A saves the profile"), SavesA->SaveToSlot(Profile, Slot));
    const FString PathA = SavesA->GetSlotFilePath(Slot);
    TestTrue(TEXT("...under its own root"), PathA.StartsWith(RootA) && FPaths::FileExists(PathA));
    TestTrue(TEXT("Platform A sees the slot"), SavesA->DoesSlotExist(Slot));
    TestFalse(TEXT("Platform B doesn't"), SavesB->DoesSlotExist(Slot));
    TestFalse(TEXT("Nothing in the default root"), FPaths::FileExists(UPSSaveSubsystem::GetSlotPath(Slot)));

    // The same file, moved to platform B, loads there: one format everywhere.
    TArray<uint8> Bytes;
    TestTrue(TEXT("Platform A's file reads"), FFileHelper::LoadFileToArray(Bytes, *PathA));
    TestTrue(TEXT("...and copies to platform B"), FFileHelper::SaveArrayToFile(Bytes, *SavesB->GetSlotFilePath(Slot)));
    const UPSProfileSaveGame* OnB = Cast<UPSProfileSaveGame>(SavesB->LoadFromSlot(Slot));
    if (TestNotNull(TEXT("Platform B loads platform A's save"), OnB))
    {
        TestEqual(TEXT("...with its favourite plays"), OnB->FavoritePlays.Num(), 2);
        TestTrue(TEXT("...in order"), OnB->FavoritePlays.Num() == 2 && OnB->FavoritePlays[0] == FName(TEXT("Slant_Flat")));
        const float* Volume = OnB->Settings.Find(TEXT("MasterVolume"));
        TestTrue(TEXT("...and its settings"), Volume && FMath::IsNearlyEqual(*Volume, 0.4f));
    }

    // Deleting on A leaves B's copy.
    TestTrue(TEXT("Platform A deletes its slot"), SavesA->DeleteSlot(Slot) && !SavesA->DoesSlotExist(Slot));
    TestTrue(TEXT("...and B's copy is untouched"), SavesB->DoesSlotExist(Slot));

    // A suspend finishes every write in flight before it returns.
    const FString AsyncSlot = TEXT("PlatformServices_Flush");
    SavesA->SaveToSlotAsync(Profile, AsyncSlot, FPSSaveOpComplete());
    TestTrue(TEXT("The platform suspends"), PlatformA->HandleLifecycle(EPSPlatformLifecycle::Suspend));
    TestEqual(TEXT("Nothing is left in flight"), SavesA->GetPendingWriteCount(), 0);
    TestTrue(TEXT("The write is on disk"), SavesA->DoesSlotExist(AsyncSlot));
    TestNotNull(TEXT("...and loads"), SavesA->LoadFromSlot(AsyncSlot));

    // Another platform's suspend is none of A's business, and a suspend with nothing in flight is fine.
    SavesB->SetPlatformServices(nullptr);
    TestEqual(TEXT("B unbound: the default root again"), SavesB->GetStorageRoot(), UPSPlatformServices::GetDefaultSaveStorageRoot());
    SavesA->FlushPendingWrites();

    Files.DeleteDirectory(*RootA, false, true);
    Files.DeleteDirectory(*RootB, false, true);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Lifecycle events reach the bus, then the game instance's listeners
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformLifecycleBusTest,
    "PlaySports.Platform.LifecycleOnTheBus",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformLifecycleBusTest::RunTest(const FString& Parameters)
{
    using namespace PSPlatformServicesTests;
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Test world with a bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSPlatformServices* Services = MakeServices();
    Services->SetEventWorld(World);

    TArray<FString> Heard;
    TArray<FPSTelemetryLifecycleEvent> Events;
    const FDelegateHandle BusHandle = Bus->OnLifecycleMC.AddLambda([&Heard, &Events](const FPSTelemetryLifecycleEvent& Event)
    {
        Heard.Add(TEXT("Bus"));
        Events.Add(Event);
    });
    Services->OnLifecycleMC.AddLambda([&Heard](EPSPlatformLifecycle)
    {
        Heard.Add(TEXT("GameInstance"));
    });

    TestFalse(TEXT("Not suspended at first"), Services->IsSuspended() || Services->IsConstrained());
    TestTrue(TEXT("A suspend is handled"), Services->HandleLifecycle(EPSPlatformLifecycle::Suspend));
    TestTrue(TEXT("...and the game is suspended"), Services->IsSuspended());
    TestTrue(TEXT("The bus heard it first, then the game instance's listeners"),
        Heard.Num() == 2 && Heard[0] == TEXT("Bus") && Heard[1] == TEXT("GameInstance"));
    if (TestEqual(TEXT("One event on the bus"), Events.Num(), 1))
    {
        TestEqual(TEXT("...a suspend"), Events[0].Lifecycle, EPSPlatformLifecycle::Suspend);
        TestTrue(TEXT("...saying the game is suspended"), Events[0].bSuspended && !Events[0].bConstrained);
        TestEqual(TEXT("...from the local implementation"), Events[0].Backend, FName(TEXT("Local")));
    }
    FPSTelemetryEvent Recorded;
    TestTrue(TEXT("The bus keeps it in its history"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Lifecycle, Recorded));

    TestFalse(TEXT("A second suspend changes nothing"), Services->HandleLifecycle(EPSPlatformLifecycle::Suspend));
    TestEqual(TEXT("...and publishes nothing"), Events.Num(), 1);
    TestFalse(TEXT("Unconstrained while not constrained changes nothing"), Services->HandleLifecycle(EPSPlatformLifecycle::Unconstrained));

    TestTrue(TEXT("Constrained"), Services->HandleLifecycle(EPSPlatformLifecycle::Constrained));
    TestTrue(TEXT("Resume"), Services->HandleLifecycle(EPSPlatformLifecycle::Resume));
    TestTrue(TEXT("Resumed but still constrained"), !Services->IsSuspended() && Services->IsConstrained());
    TestTrue(TEXT("Unconstrained"), Services->HandleLifecycle(EPSPlatformLifecycle::Unconstrained));
    TestFalse(TEXT("Back to normal"), Services->IsSuspended() || Services->IsConstrained());
    if (TestEqual(TEXT("Four events in all"), Events.Num(), 4))
    {
        TestTrue(TEXT("The constrain says suspended and constrained"), Events[1].bSuspended && Events[1].bConstrained);
        TestTrue(TEXT("The resume says constrained only"), !Events[2].bSuspended && Events[2].bConstrained);
        TestTrue(TEXT("The unconstrain says neither"), !Events[3].bSuspended && !Events[3].bConstrained);
    }

    Bus->OnLifecycleMC.Remove(BusHandle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- A suspend or a constrain pauses a live game
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformLifecyclePauseTest,
    "PlaySports.Platform.LifecyclePausesLiveGame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformLifecyclePauseTest::RunTest(const FString& Parameters)
{
    using namespace PSPlatformServicesTests;
    UWorld* World = CreateTestWorld();
    APSPlayerController* Human = World ? SpawnHuman(World) : nullptr;
    UPSMenuComponent* Menu = Human ? Human->GetMenuComponent() : nullptr;
    UPSAutoPauseSubsystem* AutoPause = World ? World->GetSubsystem<UPSAutoPauseSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("A human with menus"), Menu) || !TestNotNull(TEXT("The auto-pause subsystem"), AutoPause))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const FPSMenuCatalog& Catalog = Menu->GetCatalog();

    UPSPlatformServices* Services = MakeServices();
    Services->SetEventWorld(World);

    // In play, nothing open: a suspend pauses.
    TestFalse(TEXT("In play"), Menu->IsMenuOpen());
    Services->HandleLifecycle(EPSPlatformLifecycle::Suspend);
    TestEqual(TEXT("A suspend opens the pause screen"), Menu->GetTopScreenId(), Catalog.PauseScreen);
    Services->HandleLifecycle(EPSPlatformLifecycle::Resume);
    TestEqual(TEXT("A resume leaves the game paused for the player"), Menu->GetTopScreenId(), Catalog.PauseScreen);
    Menu->ChooseOption(TEXT("Resume"));
    TestFalse(TEXT("The player resumes"), Menu->IsMenuOpen());

    // Picking a play: a constrain pauses over the play-call screen, which comes back after.
    TestTrue(TEXT("The play-call screen opens"), Menu->OpenScreen(Catalog.PlayCallScreen));
    Services->HandleLifecycle(EPSPlatformLifecycle::Constrained);
    TestEqual(TEXT("A constrain opens the pause screen on top"), Menu->GetTopScreenId(), Catalog.PauseScreen);
    TestEqual(TEXT("...over the play-call screen"), Menu->GetStack()->Depth(), 2);
    TestEqual(TEXT("A second interruption doesn't stack another pause"), AutoPause->PauseLiveGame(TEXT("Test")), 0);
    TestEqual(TEXT("...the stack is as it was"), Menu->GetStack()->Depth(), 2);
    TestTrue(TEXT("Back on the pause screen resumes"), Menu->HandleBack());
    TestEqual(TEXT("...back to picking the play"), Menu->GetTopScreenId(), Catalog.PlayCallScreen);
    TestEqual(TEXT("...with only that screen up"), Menu->GetStack()->Depth(), 1);
    Services->HandleLifecycle(EPSPlatformLifecycle::Unconstrained);
    TestEqual(TEXT("Unconstrained changes no screen"), Menu->GetTopScreenId(), Catalog.PlayCallScreen);
    Menu->Resume();
    TestFalse(TEXT("The play-call screen closes as usual"), Menu->IsMenuOpen());

    // The front end is no live game.
    Menu->OpenRootScreen();
    Services->HandleLifecycle(EPSPlatformLifecycle::Suspend);
    TestEqual(TEXT("A suspend in the front end leaves it as it is"), Menu->GetTopScreenId(), Catalog.RootScreen);
    TestEqual(TEXT("...with no pause screen"), Menu->GetStack()->Depth(), 1);
    Services->HandleLifecycle(EPSPlatformLifecycle::Resume);

    // Only a suspend or a constrain pauses.
    Menu->Resume();
    FPSTelemetryLifecycleEvent Resumed;
    Resumed.Lifecycle = EPSPlatformLifecycle::Resume;
    TestEqual(TEXT("A resume pauses nobody"), AutoPause->HandleLifecycle(Resumed), 0);
    TestFalse(TEXT("...and opens nothing"), Menu->IsMenuOpen());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- In a head-to-head session the pause follows the etiquette
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformLifecycleVersusTest,
    "PlaySports.Platform.LifecyclePausesVersusSession",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformLifecycleVersusTest::RunTest(const FString& Parameters)
{
    using namespace PSPlatformServicesTests;
    UWorld* World = CreateTestWorld();
    UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    APSPlayerController* P1 = World ? SpawnHuman(World) : nullptr;
    APSPlayerController* P2 = World ? SpawnHuman(World) : nullptr;
    if (!TestNotNull(TEXT("The versus subsystem"), Versus) || !TestTrue(TEXT("Two humans"), P1 && P2))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const bool bStarted = Versus->ClaimSeat(P1, 0) == 0 && Versus->ClaimSeat(P2, 1) == 1
        && Versus->SelectTeam(0, EPSVersusTeam::Home) && Versus->SelectTeam(1, EPSVersusTeam::Away)
        && Versus->SetReady(0, true) && Versus->SetReady(1, true) && Versus->StartSession();
    if (!TestTrue(TEXT("Session started"), bStarted))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FName PauseScreen = P1->GetMenuComponent()->GetCatalog().PauseScreen;
    const int32 PausesLeft0 = Versus->GetPausesLeft(0);
    const int32 PausesLeft1 = Versus->GetPausesLeft(1);

    UPSPlatformServices* Services = MakeServices();
    Services->SetEventWorld(World);
    Services->HandleLifecycle(EPSPlatformLifecycle::Suspend);
    TestEqual(TEXT("A suspend pauses the session"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestEqual(TEXT("Player 1 sees the pause screen"), P1->GetMenuComponent()->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("Player 2 sees the pause screen"), P2->GetMenuComponent()->GetTopScreenId(), PauseScreen);
    TestEqual(TEXT("Player 1 keeps every pause"), Versus->GetPausesLeft(0), PausesLeft0);
    TestEqual(TEXT("Player 2 keeps every pause"), Versus->GetPausesLeft(1), PausesLeft1);

    Services->HandleLifecycle(EPSPlatformLifecycle::Constrained);
    TestEqual(TEXT("Already paused: still paused"), Versus->GetPhase(), EPSVersusPhase::Paused);
    TestEqual(TEXT("...with one pause screen each"), P1->GetMenuComponent()->GetStack()->Depth(), 1);

    Services->HandleLifecycle(EPSPlatformLifecycle::Resume);
    TestEqual(TEXT("A resume waits for the players"), Versus->GetPhase(), EPSVersusPhase::Paused);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
