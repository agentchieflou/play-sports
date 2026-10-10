// PSUIHintTests.cpp -- Epic 105.4 (contextual hints for first-time situations)
//
// Tests covered:
//   1. Picking: Data/ui_hints.json loads and validates; a kickoff, a 4th down and the two-minute
//      drill (as UPSSituationAI reads it) each pick their own hint ahead of the first-call ones,
//      and a defense picks its own; with the Hints setting off nothing is picked.
//   2. Once per player: the first call screen of a window picks and marks a hint, later asks
//      in the window return the same one, and once seen it never comes back, across sessions
//      through the profile, until the player resets hints.
//   3. On screen: the play-call screen shows the hint under the situation as "Tip: ...".

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSMenuComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "PSSettingsSubsystem.h"
#include "PSSituationAI.h"
#include "PSUIHintSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSUIHintTests
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

    static FPSSituationContext Down(int32 InDown, int32 Quarter, float ClockSeconds)
    {
        FPSSituationContext Situation;
        Situation.Down = InDown;
        Situation.Distance = 10;
        Situation.YardLine = 30;
        Situation.Quarter = Quarter;
        Situation.GameClockSeconds = ClockSeconds;
        return Situation;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Picking
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHintPickingTest,
    "PlaySports.Hints.PickTheFirstTimeSituation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHintPickingTest::RunTest(const FString& Parameters)
{
    using namespace PSUIHintTests;

    UWorld* World = CreateTestWorld();
    UPSUIHintSubsystem* Hints = World ? World->GetSubsystem<UPSUIHintSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Hint subsystem"), Hints))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TestTrue(TEXT("Data/ui_hints.json loads"), Hints->LoadCatalogFromJson(UPSUIHintSubsystem::GetDefaultCatalogPath()));
    for (const FString& Problem : UPSUIHintSubsystem::ValidateCatalog(Hints->GetCatalog()))
    {
        AddError(FString::Printf(TEXT("ui_hints.json: %s"), *Problem));
    }
    auto IdFor = [Hints](EPSHintTrigger Trigger)
    {
        const FPSHintDef* Def = Hints->GetCatalog().Hints.FindByPredicate([Trigger](const FPSHintDef& Candidate) { return Candidate.Trigger == Trigger; });
        return Def ? Def->HintId : NAME_None;
    };
    for (const EPSHintTrigger Trigger : { EPSHintTrigger::OffenseCall, EPSHintTrigger::DefenseCall, EPSHintTrigger::FourthDown, EPSHintTrigger::TwoMinuteDrill, EPSHintTrigger::Kickoff })
    {
        TestFalse(*FString::Printf(TEXT("A hint for %s"), *UEnum::GetValueAsString(Trigger)), IdFor(Trigger).IsNone());
    }

    UPSSettingsSubsystem* Settings = MakeSettings();
    Hints->SetSettings(Settings);
    TestNotNull(TEXT("Hints is a setting"), Settings->GetCatalog().FindSetting(Hints->HintsSettingId));

    const FPSSituationContext FirstDown = Down(1, 1, 600.f);
    TestEqual(TEXT("A first down on offense: the first-call hint"), Hints->PickHint(FirstDown, true), IdFor(EPSHintTrigger::OffenseCall));
    TestEqual(TEXT("...on defense: the defense's"), Hints->PickHint(FirstDown, false), IdFor(EPSHintTrigger::DefenseCall));
    TestEqual(TEXT("4th down comes first"), Hints->PickHint(Down(4, 1, 600.f), true), IdFor(EPSHintTrigger::FourthDown));
    FPSSituationContext Kickoff = FirstDown;
    Kickoff.bKickoff = true;
    TestEqual(TEXT("A kickoff: the kick meter's hint"), Hints->PickHint(Kickoff, true), IdFor(EPSHintTrigger::Kickoff));
    TestEqual(TEXT("...for the kicking side only"), Hints->PickHint(Kickoff, false), FName(NAME_None));

    // The two-minute drill is the situation AI's call (Epic 76).
    UPSSituationAI* SituationAI = NewObject<UPSSituationAI>();
    SituationAI->LoadTuningFromJson(UPSSituationAI::GetDefaultTuningPath());
    const FPSSituationContext LateInTheHalf = Down(1, 2, 60.f);
    if (TestEqual(TEXT("A minute left in the half is a two-minute drill"), SituationAI->ClassifySituation(LateInTheHalf), EPSGameSituation::TwoMinuteDrill))
    {
        TestEqual(TEXT("...and its hint comes up"), Hints->PickHint(LateInTheHalf, true), IdFor(EPSHintTrigger::TwoMinuteDrill));
    }

    Settings->SetValue(Hints->HintsSettingId, 0.f);
    TestEqual(TEXT("With hints off nothing comes up"), Hints->PickHint(Down(4, 1, 600.f), true), FName(NAME_None));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Once per player
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHintOnceTest,
    "PlaySports.Hints.OncePerPlayer",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHintOnceTest::RunTest(const FString& Parameters)
{
    using namespace PSUIHintTests;

    UWorld* World = CreateTestWorld();
    UPSUIHintSubsystem* Hints = World ? World->GetSubsystem<UPSUIHintSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Hint subsystem"), Hints))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    const FString Slot = TEXT("Test_ProfileHints");
    DeleteSlot(Slot);
    UPSSettingsSubsystem* Settings = MakeSettings();
    Settings->LoadFromProfile(Saves, Slot);
    Hints->SetSettings(Settings);

    const FPSSituationContext FourthDown = Down(4, 1, 600.f);
    const FName FourthDownHint = Hints->PickHint(FourthDown, true);
    TestFalse(TEXT("A 4th down has a hint"), FourthDownHint.IsNone());
    TestFalse(TEXT("The call screen shows it"), Hints->GetCallHint(FourthDown, true).IsEmpty());
    TestEqual(TEXT("...as the active hint"), Hints->GetActiveHintId(true), FourthDownHint);
    TestTrue(TEXT("...marked seen at once"), Settings->HasSeenHint(FourthDownHint));
    TestFalse(TEXT("Redrawn in the same window, it stays"), Hints->GetCallHint(FourthDown, true).IsEmpty());

    // The snap ends the window; the next 4th down picks again, and the hint is spent.
    Hints->ResetCallWindow();
    TestTrue(TEXT("The next 4th down doesn't repeat it"), Hints->PickHint(FourthDown, true) != FourthDownHint);

    UPSSettingsSubsystem* NextSession = MakeSettings();
    TestTrue(TEXT("A new session reads the profile"), NextSession->LoadFromProfile(Saves, Slot));
    TestTrue(TEXT("...and remembers the hint was seen"), NextSession->HasSeenHint(FourthDownHint));
    const UPSProfileSaveGame* Profile = Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot));
    TestTrue(TEXT("The profile holds it"), Profile && Profile->SeenHints.Contains(FourthDownHint));

    NextSession->ResetHints();
    Hints->SetSettings(NextSession);
    Hints->ResetCallWindow();
    TestEqual(TEXT("After a reset it comes up again"), Hints->PickHint(FourthDown, true), FourthDownHint);

    DeleteSlot(Slot);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- On screen
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHintOnScreenTest,
    "PlaySports.Hints.ShownOnTheCallScreen",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHintOnScreenTest::RunTest(const FString& Parameters)
{
    using namespace PSUIHintTests;

    UWorld* World = CreateTestWorld();
    UPSUIHintSubsystem* Hints = World ? World->GetSubsystem<UPSUIHintSubsystem>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("Hint subsystem"), Hints) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Menu"), Menu))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    Hints->SetSettings(Settings);
    Menu->SetSettings(Settings);

    const FPSSituationContext FourthDown = Down(4, 1, 600.f);
    PlayCall->OpenPlayCall(FourthDown);
    const FName Expected = Hints->PickHint(FourthDown, true);
    const FPSHintDef* Def = Hints->GetCatalog().Hints.FindByPredicate([Expected](const FPSHintDef& Candidate) { return Candidate.HintId == Expected; });
    const FPSMenuScreenDef CallScreen = Menu->GetPresentedScreen(Menu->GetCatalog().PlayCallScreen);
    if (TestNotNull(TEXT("A 4th-down hint"), Def))
    {
        TestTrue(TEXT("The call screen shows it under the situation"),
            CallScreen.Body.Contains(UPSPlayCallSubsystem::DescribeSituation(FourthDown)) && CallScreen.Body.Contains(Def->Text));
        TestTrue(TEXT("...as a tip"), CallScreen.Body.Contains(TEXT("Tip: ")));
    }

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
