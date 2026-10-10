// PSInputRemapTests.cpp -- Epic 103.4 (input remapping through the action catalog)
//
// Tests covered:
//   1. The catalog takes a remap: the action's key for that kind of device is replaced, its
//      other keys stay, the mapping context is rebuilt on the same action object, and the glyph
//      follows with no glyph edit. A key already used in the context, a key of the wrong kind,
//      a menu action and an unknown action are refused and change nothing; clearing restores.
//   2. The player's remaps: a request is checked against his input config, saved in the profile
//      and applied; a refused one is neither; a new session applies the saved ones; reset clears.
//   3. The menu: the remap screen lists the remappable actions with their keys, choosing one
//      waits for a key, the key pressed becomes the action's, Back cancels, and a key pressed
//      when nothing waits is left to the menu.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSInputConfig.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "PSSaveSubsystem.h"
#include "PSSettingsComponent.h"
#include "PSSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSInputRemapTests
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

    static APSPlayerController* SpawnPlayerController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    static FPSInputRemap MakeRemap(const TCHAR* ActionId, bool bGamepad, const FKey& Key)
    {
        FPSInputRemap Remap;
        Remap.ActionId = ActionId;
        Remap.bGamepad = bGamepad;
        Remap.Key = Key.GetFName();
        return Remap;
    }

    /** True when ContextId's mapping context maps ActionId to Key. */
    static bool IsMapped(const UPSInputConfig* Config, FName ActionId, FName ContextId, const FKey& Key)
    {
        const UInputMappingContext* Context = Config->FindContext(ContextId);
        const UInputAction* Action = Config->FindAction(ActionId);
        if (!Context || !Action)
        {
            return false;
        }
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            if (Mapping.Action == Action && Mapping.Key == Key)
            {
                return true;
            }
        }
        return false;
    }

    static void DeleteSlot(const FString& Slot)
    {
        const FString Path = UPSSaveSubsystem::GetSlotPath(Slot);
        IFileManager::Get().Delete(*Path, false, true, true);
        IFileManager::Get().Delete(*(Path + TEXT(".bak")), false, true, true);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The catalog takes a remap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputRemapCatalogTest,
    "PlaySports.Input.RemapThroughTheCatalog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputRemapCatalogTest::RunTest(const FString& Parameters)
{
    using namespace PSInputRemapTests;

    UPSInputConfig* Config = NewObject<UPSInputConfig>();
    Config->LoadDefaults();
    const FName Juke(TEXT("Juke"));
    const FName BallCarrier(TEXT("BallCarrier"));
    TestTrue(TEXT("A move button can be remapped"), Config->IsRemappable(Juke));
    TestFalse(TEXT("Confirm can't: menus read it"), Config->IsRemappable(TEXT("Confirm")));
    TestFalse(TEXT("Move can't: it is a stick, not a button"), Config->IsRemappable(TEXT("Move")));

    const TArray<FKey> Before = Config->GetKeysFor(Juke, BallCarrier);
    const FKey* KeyboardKey = Before.FindByPredicate([](const FKey& Key) { return !Key.IsGamepadKey(); });
    const FKey* PadKey = Before.FindByPredicate([](const FKey& Key) { return Key.IsGamepadKey(); });
    const UInputAction* JukeAction = Config->FindAction(Juke);
    if (!TestTrue(TEXT("Juke has a keyboard and a gamepad key"), KeyboardKey && PadKey && JukeAction))
    {
        return false;
    }
    const FKey OldPad = *PadKey;
    const FKey OldKeyboard = *KeyboardKey;

    // A key another move already uses in the BallCarrier context is refused.
    const TArray<FKey> HurdleKeys = Config->GetKeysFor(TEXT("Hurdle"), BallCarrier);
    const FKey* HurdlePad = HurdleKeys.FindByPredicate([](const FKey& Key) { return Key.IsGamepadKey(); });
    TArray<FString> Problems;
    if (HurdlePad)
    {
        TestFalse(TEXT("A button another move uses is refused"), Config->ApplyRemaps({ MakeRemap(TEXT("Juke"), true, *HurdlePad) }, Problems));
        TestTrue(TEXT("...saying why"), Problems.Num() > 0);
        TestTrue(TEXT("...and nothing changes"), Config->GetKeysFor(Juke, BallCarrier).Contains(OldPad));
    }

    // A free button is taken.
    const FKey NewPad = EKeys::Gamepad_DPad_Up;
    TestFalse(TEXT("(D-pad up is free in BallCarrier)"), IsMapped(Config, TEXT("Hurdle"), BallCarrier, NewPad));
    TestTrue(TEXT("A free button is accepted"), Config->ApplyRemaps({ MakeRemap(TEXT("Juke"), true, NewPad) }, Problems));
    const TArray<FKey> After = Config->GetKeysFor(Juke, BallCarrier);
    TestTrue(TEXT("...replacing the gamepad key"), After.Contains(NewPad) && !After.Contains(OldPad));
    TestTrue(TEXT("...keeping the keyboard key"), After.Contains(OldKeyboard));
    TestTrue(TEXT("The mapping context is rebuilt with it"), IsMapped(Config, Juke, BallCarrier, NewPad) && !IsMapped(Config, Juke, BallCarrier, OldPad));
    TestTrue(TEXT("...on the same action object, so bindings survive"), Config->FindAction(Juke) == JukeAction);
    FPSInputGlyph Shown;
    FPSInputGlyph Expected;
    TestTrue(TEXT("The glyph follows the new button"), Config->GetGlyphForAction(Juke, BallCarrier, EPSInputDevice::Gamepad, Shown)
        && Config->GetGlyphs() && Config->GetGlyphs()->GetGlyphForKey(NewPad, EPSInputDevice::Gamepad, Expected) && Shown.Label == Expected.Label);
    TestEqual(TEXT("The remap is remembered"), Config->GetRemaps().Num(), 1);

    // Refusals.
    TestFalse(TEXT("A gamepad button for the keyboard is refused"), Config->ApplyRemaps({ MakeRemap(TEXT("Juke"), false, EKeys::Gamepad_DPad_Down) }, Problems));
    TestFalse(TEXT("A menu action is refused"), Config->ApplyRemaps({ MakeRemap(TEXT("Cancel"), false, EKeys::BackSpace) }, Problems));
    TestFalse(TEXT("An unknown action is refused"), Config->ApplyRemaps({ MakeRemap(TEXT("NoSuchAction"), false, EKeys::K) }, Problems));
    TestTrue(TEXT("...each leaving the last good remap in place"), Config->GetKeysFor(Juke, BallCarrier).Contains(NewPad));

    TestTrue(TEXT("No remaps puts the catalog back"), Config->ApplyRemaps({}, Problems) && Config->GetKeysFor(Juke, BallCarrier).Contains(OldPad));
    for (const FString& Problem : Config->Validate())
    {
        AddError(FString::Printf(TEXT("The catalog after remapping: %s"), *Problem));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The player's remaps
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputRemapPlayerTest,
    "PlaySports.Input.RemapSavedAndApplied",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputRemapPlayerTest::RunTest(const FString& Parameters)
{
    using namespace PSInputRemapTests;

    UWorld* World = CreateTestWorld();
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_ProfileRemaps");
    DeleteSlot(Slot);

    UPSSettingsSubsystem* Settings = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    Settings->LoadFromProfile(Saves, Slot);
    UPSSettingsComponent* PlayerSettings = Controller->GetSettingsComponent();
    PlayerSettings->SetSettings(Settings);
    UPSInputConfig* Config = Controller->GetInputConfig();
    const FName Truck(TEXT("Truck"));
    const FName BallCarrier(TEXT("BallCarrier"));

    FString Problem;
    TestTrue(TEXT("The player gives Truck D-pad down"), PlayerSettings->RequestRemap(Truck, true, EKeys::Gamepad_DPad_Down.GetFName(), Problem));
    TestTrue(TEXT("...his controller's catalog has it"), Config->GetKeysFor(Truck, BallCarrier).Contains(EKeys::Gamepad_DPad_Down));
    TestEqual(TEXT("...and it is saved"), Settings->GetInputRemaps().Num(), 1);

    TestFalse(TEXT("A key another move uses is refused"), PlayerSettings->RequestRemap(TEXT("Spin"), true, EKeys::Gamepad_DPad_Down.GetFName(), Problem));
    TestFalse(TEXT("...with a reason for the player"), Problem.IsEmpty());
    TestEqual(TEXT("...and not saved"), Settings->GetInputRemaps().Num(), 1);

    // A new session applies the saved remap to a fresh controller.
    UPSSettingsSubsystem* NextSession = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    NextSession->LoadFromProfile(Saves, Slot);
    APSPlayerController* NextController = SpawnPlayerController(World);
    if (TestNotNull(TEXT("Second controller"), NextController))
    {
        NextController->GetSettingsComponent()->SetSettings(NextSession);
        TestTrue(TEXT("The next session starts with the player's key"), NextController->GetInputConfig()->GetKeysFor(Truck, BallCarrier).Contains(EKeys::Gamepad_DPad_Down));
    }

    PlayerSettings->ResetRemaps();
    TestEqual(TEXT("Reset clears the remaps"), Settings->GetInputRemaps().Num(), 0);
    TestFalse(TEXT("...and the catalog key is back"), Config->GetKeysFor(Truck, BallCarrier).Contains(EKeys::Gamepad_DPad_Down));

    DeleteSlot(Slot);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The menu
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputRemapMenuTest,
    "PlaySports.Input.RemapMenuListensForAKey",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputRemapMenuTest::RunTest(const FString& Parameters)
{
    using namespace PSInputRemapTests;

    UWorld* World = CreateTestWorld();
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    Controller->GetSettingsComponent()->SetSettings(Settings);
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    Menu->SetSettings(Settings);
    UPSInputConfig* Config = Controller->GetInputConfig();
    const FName Juke(TEXT("Juke"));
    const FName BallCarrier(TEXT("BallCarrier"));

    Menu->OpenRootScreen();
    Menu->ChooseOption(TEXT("Settings"));
    Menu->ChooseOption(TEXT("Keys"));
    const FPSMenuScreenDef Screen = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestEqual(TEXT("Settings leads to the keys screen"), Screen.Content, EPSMenuScreenContent::InputRemap);
    const FPSMenuOptionDef* JukeOption = Screen.Options.FindByPredicate([Juke](const FPSMenuOptionDef& Option) { return Option.OptionId == Juke; });
    TestNotNull(TEXT("...listing Juke"), JukeOption);
    TestFalse(TEXT("...but not Confirm, which menus read"), Screen.Options.ContainsByPredicate([](const FPSMenuOptionDef& Option) { return Option.OptionId == FName(TEXT("Confirm")); }));

    // The keyboard is the active device in a headless world: give Juke B.
    TestFalse(TEXT("A key with no remap waiting is the menu's"), Menu->HandleRemapKey(EKeys::B));
    Menu->ChooseOption(Juke);
    TestTrue(TEXT("Choosing Juke waits for a key"), Menu->IsListeningForRemap());
    TestTrue(TEXT("...and says so"), Menu->GetPresentedScreen(Menu->GetTopScreenId()).Body.Contains(TEXT("Juke")));
    TestTrue(TEXT("The next key is taken"), Menu->HandleRemapKey(EKeys::B));
    TestFalse(TEXT("...ending the wait"), Menu->IsListeningForRemap());
    TestTrue(TEXT("Juke is now on B"), Config->GetKeysFor(Juke, BallCarrier).Contains(EKeys::B));
    const FPSMenuScreenDef After = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    JukeOption = After.Options.FindByPredicate([Juke](const FPSMenuOptionDef& Option) { return Option.OptionId == Juke; });
    FPSInputGlyph Glyph;
    TestTrue(TEXT("...and the screen shows B's glyph"), JukeOption && Config->GetGlyphForAction(Juke, BallCarrier, EPSInputDevice::KeyboardMouse, Glyph)
        && JukeOption->Label.EndsWith(Glyph.Label));

    // Back cancels a waiting remap.
    const TArray<FKey> SpinBefore = Config->GetKeysFor(TEXT("Spin"), BallCarrier);
    Menu->BeginRemap(TEXT("Spin"));
    TestTrue(TEXT("Back cancels the wait"), Menu->HandleRemapKey(EKeys::Escape) && !Menu->IsListeningForRemap());
    TestTrue(TEXT("...leaving Spin's keys alone"), Config->GetKeysFor(TEXT("Spin"), BallCarrier) == SpinBefore);
    TestEqual(TEXT("...and the menu is still on the keys screen"), Menu->GetPresentedScreen(Menu->GetTopScreenId()).Content, EPSMenuScreenContent::InputRemap);

    Menu->ChooseOption(TEXT("ResetRemaps"));
    TestFalse(TEXT("Reset gives Juke its usual key back"), Config->GetKeysFor(Juke, BallCarrier).Contains(EKeys::B));

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
