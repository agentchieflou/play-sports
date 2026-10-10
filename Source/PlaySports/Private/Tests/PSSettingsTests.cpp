// PSSettingsTests.cpp -- Epic 103 (settings and accessibility): the settings framework
//
// Tests covered:
//   1. The catalog: it loads and validates, has the video, audio, gameplay and controls
//      categories, holds every setting code applies, and a broken one is caught. Toggles,
//      choices and sliders step, snap, format and report changes as the menu needs.
//   2. Persistence: the player's values go into the profile save, come back in a new session,
//      and leave the rest of the profile (favourite plays) alone.
//   3. The player's controller: vibration, its strength, the stick dead zone and input
//      buffering follow the settings, and a dead-zone change rebuilds the stick mapping without
//      replacing the actions already bound.
//   4. The menu: Settings lists the categories, a category lists its settings with their
//      values, choosing one steps it, and Reset puts the category back.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSForceFeedbackComponent.h"
#include "PSInputBufferComponent.h"
#include "PSInputConfig.h"
#include "PSMenuComponent.h"
#include "PSPlayerController.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "PSSettingsComponent.h"
#include "PSSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSettingsTests
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

    /** Settings outside any running game: nothing is applied to the engine or saved. */
    static UPSSettingsSubsystem* MakeSettings()
    {
        return NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    }

    static void DeleteSlot(const FString& Slot)
    {
        const FString Path = UPSSaveSubsystem::GetSlotPath(Slot);
        IFileManager::Get().Delete(*Path, false, true, true);
        IFileManager::Get().Delete(*(Path + TEXT(".bak")), false, true, true);
    }

    /** The stick's dead-zone modifier on Move in the OnField context. */
    static const UInputModifierDeadZone* FindStickDeadZone(const UPSInputConfig* Config)
    {
        const UInputMappingContext* OnField = Config->FindContext(TEXT("OnField"));
        const UInputAction* Move = Config->FindAction(TEXT("Move"));
        if (!OnField || !Move)
        {
            return nullptr;
        }
        for (const FEnhancedActionKeyMapping& Mapping : OnField->GetMappings())
        {
            if (Mapping.Action == Move && Mapping.Key == EKeys::Gamepad_Left2D)
            {
                for (const UInputModifier* Modifier : Mapping.Modifiers)
                {
                    if (const UInputModifierDeadZone* DeadZone = Cast<UInputModifierDeadZone>(Modifier))
                    {
                        return DeadZone;
                    }
                }
            }
        }
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The catalog and the values
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSettingsCatalogTest,
    "PlaySports.Settings.CatalogAndValues",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSettingsCatalogTest::RunTest(const FString& Parameters)
{
    using namespace PSSettingsTests;

    UPSSettingsSubsystem* Settings = MakeSettings();
    TestTrue(TEXT("Data/ui_settings.json loads"), Settings->LoadCatalogFromJson(UPSSettingsSubsystem::GetDefaultCatalogPath()));
    const FPSSettingsCatalog& Catalog = Settings->GetCatalog();
    for (const FString& Problem : UPSSettingsSubsystem::ValidateCatalog(Catalog))
    {
        AddError(FString::Printf(TEXT("ui_settings.json: %s"), *Problem));
    }
    for (const TCHAR* Category : { TEXT("Video"), TEXT("Audio"), TEXT("Gameplay"), TEXT("Controls") })
    {
        TestNotNull(*FString::Printf(TEXT("The %s category exists"), Category), Catalog.FindCategory(Category));
    }

    // Every setting code applies is in the catalog.
    const UPSSettingsComponent* Applier = GetDefault<UPSSettingsComponent>();
    const TArray<FName> Applied = {
        Settings->WindowModeSettingId, Settings->ResolutionScaleSettingId, Settings->QualitySettingId, Settings->VSyncSettingId,
        Settings->FrameRateLimitSettingId, Settings->MasterVolumeSettingId,
        Applier->VibrationSettingId, Applier->VibrationStrengthSettingId, Applier->StickDeadZoneSettingId, Applier->InputBufferingSettingId };
    for (const FName& SettingId : Applied)
    {
        TestNotNull(*FString::Printf(TEXT("%s is a setting"), *SettingId.ToString()), Catalog.FindSetting(SettingId));
    }

    TArray<FName> Changes;
    const FDelegateHandle Handle = Settings->OnSettingChangedMC.AddLambda([&Changes](FName SettingId, float Value) { Changes.Add(SettingId); });

    // A toggle flips.
    const FName VSync = Settings->VSyncSettingId;
    TestEqual(TEXT("VSync starts on"), Settings->FormatValue(VSync), FString(TEXT("On")));
    TestTrue(TEXT("Stepping a toggle flips it"), Settings->StepSetting(VSync) && !Settings->GetBool(VSync));
    TestEqual(TEXT("...shown Off"), Settings->FormatValue(VSync), FString(TEXT("Off")));
    TestTrue(TEXT("...and the change is told"), Changes.Num() == 1 && Changes[0] == VSync);

    // A choice moves to the next and wraps; its number is the choice's value.
    const FName FrameCap = Settings->FrameRateLimitSettingId;
    const FPSSettingDef* FrameCapDef = Catalog.FindSetting(FrameCap);
    if (TestTrue(TEXT("The frame cap is a choice with values"), FrameCapDef && FrameCapDef->Kind == EPSSettingKind::Choice && FrameCapDef->Values.Num() == FrameCapDef->Choices.Num()))
    {
        const int32 Start = FMath::RoundToInt(Settings->GetValue(FrameCap));
        TestEqual(TEXT("Its number is the chosen value"), Settings->GetNumber(FrameCap), FrameCapDef->Values[Start]);
        Settings->StepSetting(FrameCap);
        TestEqual(TEXT("Stepping moves to the next choice"), Settings->FormatValue(FrameCap), FrameCapDef->Choices[(Start + 1) % FrameCapDef->Choices.Num()]);
        for (int32 Step = 1; Step < FrameCapDef->Choices.Num(); ++Step)
        {
            Settings->StepSetting(FrameCap);
        }
        TestEqual(TEXT("...and wraps round"), FMath::RoundToInt(Settings->GetValue(FrameCap)), Start);
    }

    // A slider snaps to its step, stays in range, and wraps past the top.
    const FName Volume = Settings->MasterVolumeSettingId;
    const FPSSettingDef* VolumeDef = Catalog.FindSetting(Volume);
    if (TestTrue(TEXT("Master volume is a slider"), VolumeDef && VolumeDef->Kind == EPSSettingKind::Slider))
    {
        Settings->SetValue(Volume, VolumeDef->Min + VolumeDef->Step * 1.4f);
        TestEqual(TEXT("A value between steps snaps to the nearest"), Settings->GetValue(Volume), VolumeDef->Min + VolumeDef->Step);
        Settings->SetValue(Volume, VolumeDef->Max * 3.f);
        TestEqual(TEXT("...and never past the top"), Settings->GetValue(Volume), VolumeDef->Max);
        TestEqual(TEXT("...shown with its unit"), Settings->FormatValue(Volume), FString::SanitizeFloat(VolumeDef->Max, 0) + VolumeDef->Unit);
        Settings->StepSetting(Volume);
        TestEqual(TEXT("Stepping past the top wraps to the bottom"), Settings->GetValue(Volume), VolumeDef->Min);
        Settings->ResetToDefaults(VolumeDef->Category);
        TestEqual(TEXT("Reset puts the category back"), Settings->GetValue(Volume), VolumeDef->Default);
        TestFalse(TEXT("...and only that category"), Settings->GetBool(VSync));
    }
    TestFalse(TEXT("An unknown setting can't be set"), Settings->SetValue(TEXT("NoSuchSetting"), 1.f));

    FPSSettingsCatalog Broken = Catalog;
    if (Broken.Settings.Num() > 0)
    {
        const FPSSettingDef Copy = Broken.Settings[0];
        Broken.Settings.Add(Copy);
        Broken.Settings[0].Category = TEXT("Nowhere");
    }
    FPSSettingDef BadSlider;
    BadSlider.SettingId = TEXT("BadSlider");
    BadSlider.Category = Broken.Categories.Num() > 0 ? Broken.Categories[0].CategoryId : NAME_None;
    BadSlider.Kind = EPSSettingKind::Slider;
    BadSlider.Min = 10.f;
    BadSlider.Max = 0.f;
    Broken.Settings.Add(BadSlider);
    TestTrue(TEXT("Validation catches a repeated ID, an unknown category and a bad slider"), UPSSettingsSubsystem::ValidateCatalog(Broken).Num() >= 3);

    Settings->OnSettingChangedMC.Remove(Handle);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Persistence
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSettingsPersistenceTest,
    "PlaySports.Settings.PersistInTheProfile",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSettingsPersistenceTest::RunTest(const FString& Parameters)
{
    using namespace PSSettingsTests;

    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_ProfileSettings");
    DeleteSlot(Slot);

    // A profile that already holds favourite plays.
    UPSProfileSaveGame* Existing = NewObject<UPSProfileSaveGame>();
    Existing->FavoritePlays = { FName(TEXT("Shotgun_Slants")) };
    TestTrue(TEXT("The profile saves"), Saves->SaveToSlot(Existing, Slot));

    UPSSettingsSubsystem* Session = MakeSettings();
    TestTrue(TEXT("The settings read the profile"), Session->LoadFromProfile(Saves, Slot));
    TestEqual(TEXT("...with nothing set, everything is at its default"), Session->GetValue(Session->MasterVolumeSettingId),
        Session->GetCatalog().FindSetting(Session->MasterVolumeSettingId) ? Session->GetCatalog().FindSetting(Session->MasterVolumeSettingId)->Default : -1.f);
    Session->SetValue(Session->MasterVolumeSettingId, 40.f);
    Session->StepSetting(Session->VSyncSettingId);

    UPSSettingsSubsystem* NextSession = MakeSettings();
    TestTrue(TEXT("A new session reads the profile"), NextSession->LoadFromProfile(Saves, Slot));
    TestEqual(TEXT("...and finds the volume"), NextSession->GetValue(NextSession->MasterVolumeSettingId), 40.f);
    TestFalse(TEXT("...and VSync off"), NextSession->GetBool(NextSession->VSyncSettingId));
    const UPSProfileSaveGame* Profile = Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot));
    if (TestNotNull(TEXT("The profile is still a profile"), Profile))
    {
        TestEqual(TEXT("The favourite plays are untouched"), Profile->FavoritePlays.Num(), 1);
        TestEqual(TEXT("Only changed settings are stored"), Profile->Settings.Num(), 2);
    }

    DeleteSlot(Slot);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The player's controller
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSettingsControllerTest,
    "PlaySports.Settings.AppliedToThePlayersController",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSettingsControllerTest::RunTest(const FString& Parameters)
{
    using namespace PSSettingsTests;

    UWorld* World = CreateTestWorld();
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSSettingsSubsystem* Settings = MakeSettings();
    UPSSettingsComponent* Applier = Controller->GetSettingsComponent();
    UPSInputConfig* Config = Controller->GetInputConfig();
    const UInputAction* MoveBefore = Config->FindAction(TEXT("Move"));
    const float TunedDeadZone = Config->Tuning.StickDeadZoneLower;
    Controller->GetForceFeedbackComponent()->bEnabled = false;
    Applier->SetSettings(Settings);
    TestTrue(TEXT("Vibration follows the setting: on by default"), Controller->GetForceFeedbackComponent()->bEnabled);

    Settings->SetValue(Applier->VibrationSettingId, 0.f);
    TestFalse(TEXT("Turned off, the controller doesn't rumble"), Controller->GetForceFeedbackComponent()->bEnabled);
    Settings->SetValue(Applier->VibrationStrengthSettingId, 50.f);
    TestTrue(TEXT("The strength scales the controller's force feedback"), FMath::IsNearlyEqual(Controller->ForceFeedbackScale, 0.5f));

    const FPSSettingDef* DeadZoneDef = Settings->GetCatalog().FindSetting(Applier->StickDeadZoneSettingId);
    if (TestTrue(TEXT("The dead zone is a choice of scales"), DeadZoneDef && DeadZoneDef->Values.Num() == DeadZoneDef->Choices.Num() && DeadZoneDef->Values.Num() >= 2))
    {
        const int32 Largest = DeadZoneDef->Values.Num() - 1;
        Settings->SetValue(Applier->StickDeadZoneSettingId, float(Largest));
        const float Expected = FMath::Min(TunedDeadZone * DeadZoneDef->Values[Largest], Config->Tuning.StickDeadZoneUpper - 0.05f);
        TestTrue(TEXT("The stick dead zone scales from the tuned one"), FMath::IsNearlyEqual(Config->Tuning.StickDeadZoneLower, Expected));
        const UInputModifierDeadZone* DeadZone = FindStickDeadZone(Config);
        TestTrue(TEXT("...and the stick mapping is rebuilt with it"), DeadZone && FMath::IsNearlyEqual(DeadZone->LowerThreshold, Expected));
        TestTrue(TEXT("...keeping the Move action the controller bound"), Config->FindAction(TEXT("Move")) == MoveBefore);
        Settings->ResetToDefaults(DeadZoneDef->Category);
        TestTrue(TEXT("Reset gives the tuned dead zone back"), FMath::IsNearlyEqual(Config->Tuning.StickDeadZoneLower, TunedDeadZone));
    }

    Settings->SetValue(Applier->InputBufferingSettingId, 0.f);
    TestFalse(TEXT("Input buffering can be turned off"), Controller->GetInputBufferComponent()->bBufferingEnabled);
    Settings->SetValue(Applier->InputBufferingSettingId, 1.f);
    TestTrue(TEXT("...and on"), Controller->GetInputBufferComponent()->bBufferingEnabled);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The menu
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSettingsMenuTest,
    "PlaySports.Settings.MenuScreens",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSettingsMenuTest::RunTest(const FString& Parameters)
{
    using namespace PSSettingsTests;

    UWorld* World = CreateTestWorld();
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    Menu->SetSettings(Settings);
    const FName Vibration = Controller->GetSettingsComponent()->VibrationSettingId;
    const FPSSettingDef* VibrationDef = Settings->GetCatalog().FindSetting(Vibration);
    if (!TestNotNull(TEXT("Vibration is a setting"), VibrationDef))
    {
        DestroyTestWorld(World);
        return false;
    }

    Menu->OpenRootScreen();
    Menu->ChooseOption(TEXT("Settings"));
    const FPSMenuScreenDef Categories = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestEqual(TEXT("Settings lists one option per category"), Categories.Options.Num(), Settings->GetCatalog().Categories.Num());

    Menu->ChooseOption(VibrationDef->Category);
    FPSMenuScreenDef Category = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    const FPSSettingCategoryDef* CategoryDef = Settings->GetCatalog().FindCategory(VibrationDef->Category);
    TestEqual(TEXT("A category opens its own screen, titled with it"), Category.Title, CategoryDef ? CategoryDef->Label : FString());
    const FPSMenuOptionDef* Option = Category.Options.FindByPredicate([Vibration](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == Vibration; });
    if (TestNotNull(TEXT("...listing vibration"), Option))
    {
        TestEqual(TEXT("...with its value"), Option->Label, FString::Printf(TEXT("%s: On"), *VibrationDef->Label));
    }
    TestTrue(TEXT("...and a reset"), Category.Options.ContainsByPredicate([](const FPSMenuOptionDef& Candidate) { return Candidate.Command == EPSMenuCommand::ResetSettings; }));

    Menu->ChooseOption(Vibration);
    TestFalse(TEXT("Choosing the setting steps it"), Settings->GetBool(Vibration));
    Category = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    Option = Category.Options.FindByPredicate([Vibration](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == Vibration; });
    TestTrue(TEXT("...and the screen shows the new value"), Option && Option->Label == FString::Printf(TEXT("%s: Off"), *VibrationDef->Label));

    Menu->ChooseOption(TEXT("Reset"));
    TestTrue(TEXT("Reset puts it back"), Settings->GetBool(Vibration));
    TestTrue(TEXT("Back returns to the categories"), Menu->HandleBack() && Menu->GetTopScreenId() == FName(TEXT("Settings")));

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
