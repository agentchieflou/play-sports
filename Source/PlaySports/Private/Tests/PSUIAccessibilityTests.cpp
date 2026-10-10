// PSUIAccessibilityTests.cpp -- Epic 103.2/103.3/103.5 (color vision, captions, UI narration,
// motion and flashes)
//
// Tests covered:
//   1. Color vision: the tuning validates; Standard leaves colors alone; for each deficiency a red
//      and a green that look alike are pushed apart; a matchup whose primaries look alike falls
//      back to colors that look different enough; team select draws its accents through it.
//   2. Captions: a Speech event on the bus becomes a caption "Speaker: text", timed by its
//      reading length within the tuned bounds or by its spoken length; the screen keeps at most
//      CaptionMaxLines; with captions off nothing is shown.
//   3. UI narration: with the setting off nothing is said; on, a menu screen is said as it
//      opens and an option as it takes focus.
//   4. Reduced motion: shakes and flashes follow their settings; with reduced motion nothing
//      shakes, a camera follows without lag, menus don't fade and the controller's blended
//      change of view is a cut.
//   5. Reduced motion on the broadcast cameras: the director's camera stays on its shot
//      instead of easing after it, the all-22 frame closes in at once, and the plain sideline
//      follow is on its target in one step.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBroadcastCamera.h"
#include "PSCameraAll22Component.h"
#include "PSCameraDirectorComponent.h"
#include "PSCameraFraming.h"
#include "PSFieldGrid.h"
#include "PSMenuComponent.h"
#include "PSPlayerPawn.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "PSPlayerController.h"
#include "PSSettingsSubsystem.h"
#include "PSTelemetryBus.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSUIColorAccessibility.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSUIAccessibilityTests
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
}

// ---------------------------------------------------------------------------
// Test 1 -- Color vision
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSColorVisionTest,
    "PlaySports.Accessibility.ColorblindSafeColors",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSColorVisionTest::RunTest(const FString& Parameters)
{
    using namespace PSUIAccessibilityTests;

    UWorld* World = CreateTestWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Accessibility subsystem"), Accessibility) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TestTrue(TEXT("Data/ui_accessibility.json loads"), Accessibility->LoadTuningFromJson(UPSUIAccessibilitySubsystem::GetDefaultTuningPath()));
    for (const FString& Problem : UPSUIAccessibilitySubsystem::ValidateTuning(Accessibility->GetTuning()))
    {
        AddError(FString::Printf(TEXT("ui_accessibility.json: %s"), *Problem));
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    for (const FName& SettingId : { Accessibility->CaptionsSettingId, Accessibility->CaptionSizeSettingId, Accessibility->NarrationSettingId, Accessibility->ColorblindSettingId })
    {
        TestNotNull(*FString::Printf(TEXT("%s is a setting"), *SettingId.ToString()), Settings->GetCatalog().FindSetting(SettingId));
    }

    const FLinearColor Red(0.6f, 0.08f, 0.05f);
    const FLinearColor Green(0.12f, 0.35f, 0.04f);
    TestTrue(TEXT("Standard vision changes nothing"), UPSUIColorLibrary::ResolveColor(Red, EPSColorblindMode::Off).Equals(Red));
    for (const EPSColorblindMode Mode : { EPSColorblindMode::Protanopia, EPSColorblindMode::Deuteranopia })
    {
        const float Before = UPSUIColorLibrary::PerceivedDistance(Red, Green, Mode);
        const float After = UPSUIColorLibrary::PerceivedDistance(UPSUIColorLibrary::ResolveColor(Red, Mode), UPSUIColorLibrary::ResolveColor(Green, Mode), Mode);
        TestTrue(*FString::Printf(TEXT("%s: red and green look closer than with standard vision"), *UEnum::GetValueAsString(Mode)),
            Before < UPSUIColorLibrary::PerceivedDistance(Red, Green, EPSColorblindMode::Off));
        TestTrue(*FString::Printf(TEXT("%s: resolved, they stand further apart"), *UEnum::GetValueAsString(Mode)), After > Before);
    }
    const FLinearColor Blue(0.05f, 0.1f, 0.6f);
    const FLinearColor Teal(0.05f, 0.4f, 0.45f);
    const float TritanBefore = UPSUIColorLibrary::PerceivedDistance(Blue, Teal, EPSColorblindMode::Tritanopia);
    TestTrue(TEXT("Tritanopia: blue and teal stand further apart resolved"),
        UPSUIColorLibrary::PerceivedDistance(UPSUIColorLibrary::ResolveColor(Blue, EPSColorblindMode::Tritanopia), UPSUIColorLibrary::ResolveColor(Teal, EPSColorblindMode::Tritanopia), EPSColorblindMode::Tritanopia) > TritanBefore);

    // A red team against a green one, for a deuteranope: the matchup falls back until it reads.
    const float MinDistance = Accessibility->GetTuning().MinMatchupColorDistance;
    FLinearColor Home;
    FLinearColor Away;
    UPSUIColorLibrary::ResolveMatchupColors(Red, FLinearColor::Black, Green, FLinearColor::White, EPSColorblindMode::Deuteranopia, MinDistance, Home, Away);
    TestTrue(TEXT("Home and away look different enough"), UPSUIColorLibrary::PerceivedDistance(Home, Away, EPSColorblindMode::Deuteranopia) >= MinDistance);
    UPSUIColorLibrary::ResolveMatchupColors(Red, FLinearColor::Black, Blue, FLinearColor::White, EPSColorblindMode::Off, MinDistance, Home, Away);
    TestTrue(TEXT("Primaries that already read stay"), Home.Equals(Red) && Away.Equals(Blue));

    // Team select's accents go through the player's mode.
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    Menu->SetSettings(Settings);
    Settings->SetValue(Accessibility->ColorblindSettingId, static_cast<float>(static_cast<uint8>(EPSColorblindMode::Deuteranopia)));
    TestEqual(TEXT("The setting picks the mode"), UPSUIAccessibilitySubsystem::GetColorblindMode(Settings), EPSColorblindMode::Deuteranopia);
    const FPSMenuScreenDef* TeamScreen = Menu->GetCatalog().FindScreenWithContent(EPSMenuScreenContent::TeamSelect);
    const TArray<FPSTeamSummary>& Teams = Menu->GetTeamSummaries();
    if (TestNotNull(TEXT("Team select exists"), TeamScreen) && TestTrue(TEXT("Teams exist"), Teams.Num() > 0))
    {
        const FPSMenuScreenDef Presented = Menu->GetPresentedScreen(TeamScreen->ScreenId);
        const FPSMenuOptionDef* First = Presented.Options.FindByPredicate([&Teams](const FPSMenuOptionDef& Option) { return Option.OptionId == Teams[0].TeamId; });
        TestTrue(TEXT("A team's accent is its color resolved for the player"),
            First && First->AccentColor.Equals(UPSUIColorLibrary::ResolveColor(Teams[0].PrimaryColor, EPSColorblindMode::Deuteranopia)));
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Captions
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCaptionsTest,
    "PlaySports.Accessibility.CaptionsFollowSpeech",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCaptionsTest::RunTest(const FString& Parameters)
{
    using namespace PSUIAccessibilityTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Accessibility subsystem"), Accessibility))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    Accessibility->SetSettings(Settings);
    const FPSUIAccessibilityTuning& Tuning = Accessibility->GetTuning();
    const float Now = World->GetTimeSeconds();

    FPSTelemetrySpeechEvent Speech;
    Speech.Speaker = TEXT("Booth");
    Speech.Text = TEXT("What a catch by the rookie on third down");
    Speech.Channel = TEXT("Commentary");
    Bus->PublishSpeech(Speech);
    TArray<FPSCaptionLine> Active = Accessibility->GetActiveCaptions(Now);
    if (TestEqual(TEXT("Speech on the bus becomes a caption"), Active.Num(), 1))
    {
        TestEqual(TEXT("...naming the speaker"), UPSUIAccessibilitySubsystem::FormatCaption(Active[0]), FString(TEXT("Booth: What a catch by the rookie on third down")));
    }
    const float Reading = FMath::Clamp(9.f / Tuning.CaptionWordsPerSecond, Tuning.CaptionMinSeconds, Tuning.CaptionMaxSeconds);
    TestTrue(TEXT("It is timed by its reading length"), FMath::IsNearlyEqual(Accessibility->GetCaptionSeconds(Speech.Text), Reading));
    TestEqual(TEXT("...still up just before"), Accessibility->GetActiveCaptions(Now + Reading - 0.05f).Num(), 1);
    TestEqual(TEXT("...and gone after"), Accessibility->GetActiveCaptions(Now + Reading + 0.05f).Num(), 0);
    TestTrue(TEXT("A short line still stays up the minimum"), FMath::IsNearlyEqual(Accessibility->GetCaptionSeconds(TEXT("Flag.")), Tuning.CaptionMinSeconds));

    // A spoken length wins; the screen keeps the newest lines.
    Speech.DurationSeconds = 1.f;
    Speech.Text = TEXT("Holding, offense.");
    Speech.Speaker = TEXT("Referee");
    Bus->PublishSpeech(Speech);
    TestEqual(TEXT("A spoken length is used as given"), Accessibility->GetActiveCaptions(Now + 1.05f).Num(), 1);
    for (int32 Index = 0; Index < Tuning.CaptionMaxLines + 1; ++Index)
    {
        Accessibility->ShowCaption(TEXT("PA"), FString::Printf(TEXT("Announcement %d"), Index), TEXT("PA"), 0.f, Now);
    }
    Active = Accessibility->GetActiveCaptions(Now);
    TestEqual(TEXT("At most CaptionMaxLines are up"), Active.Num(), Tuning.CaptionMaxLines);
    TestTrue(TEXT("...the newest"), Active.Num() > 0 && Active.Last().Text == FString::Printf(TEXT("Announcement %d"), Tuning.CaptionMaxLines));

    Settings->SetValue(Accessibility->CaptionsSettingId, 0.f);
    TestFalse(TEXT("With captions off nothing is shown"), Accessibility->ShowCaption(TEXT("Booth"), TEXT("Touchdown"), TEXT("Commentary"), 0.f, Now));
    Settings->SetValue(Accessibility->CaptionSizeSettingId, 2.f);
    TestEqual(TEXT("The caption size setting sets the font"), Accessibility->GetCaptionFontSize(),
        FMath::RoundToInt(Settings->GetNumber(Accessibility->CaptionSizeSettingId)));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- UI narration
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSNarrationTest,
    "PlaySports.Accessibility.MenusNarrate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNarrationTest::RunTest(const FString& Parameters)
{
    using namespace PSUIAccessibilityTests;

    UWorld* World = CreateTestWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Accessibility subsystem"), Accessibility) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    Accessibility->SetSettings(Settings);
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    Menu->SetSettings(Settings);
    TArray<FString> Said;
    const FDelegateHandle Handle = Accessibility->OnNarrationMC.AddLambda([&Said](const FString& Text) { Said.Add(Text); });

    Menu->OpenRootScreen();
    TestEqual(TEXT("With narration off, nothing is said"), Said.Num(), 0);

    Settings->SetValue(Accessibility->NarrationSettingId, 1.f);
    Menu->ChooseOption(TEXT("Settings"));
    const FPSMenuScreenDef SettingsScreen = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestTrue(TEXT("Opening a screen says its title"), Said.Num() > 0 && Said.Last().StartsWith(SettingsScreen.Title));
    if (SettingsScreen.Options.Num() > 0)
    {
        const FPSMenuOptionDef Focused = SettingsScreen.Options[0];
        Menu->NarrateOption(Focused.OptionId);
        TestTrue(TEXT("An option taking focus says its label"), Said.Num() > 1 && Said.Last().StartsWith(Focused.Label));
    }
    TestTrue(TEXT("Back says where the player is again"), Menu->HandleBack() && Said.Last().StartsWith(Menu->GetPresentedScreen(Menu->GetTopScreenId()).Title));

    Accessibility->OnNarrationMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Reduced motion
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSReducedMotionTest,
    "PlaySports.Accessibility.ReducedMotion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReducedMotionTest::RunTest(const FString& Parameters)
{
    using namespace PSUIAccessibilityTests;

    UWorld* World = CreateTestWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Accessibility subsystem"), Accessibility) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    Accessibility->SetSettings(Settings);
    for (const FName& SettingId : { Accessibility->ReducedMotionSettingId, Accessibility->CameraShakeSettingId, Accessibility->FlashSettingId })
    {
        TestNotNull(*FString::Printf(TEXT("%s is a setting"), *SettingId.ToString()), Settings->GetCatalog().FindSetting(SettingId));
    }
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    const float AuthoredFade = Menu->GetCatalog().TransitionSeconds;
    const float AuthoredFollow = 5.f;
    const float AuthoredBlend = 2.f;

    // By default everything moves as authored, at full strength.
    TestFalse(TEXT("Reduced motion starts off"), Accessibility->IsReducedMotion());
    TestEqual(TEXT("Shakes play at full strength"), Accessibility->GetCameraShakeScale(), 1.f);
    TestEqual(TEXT("...and flashes"), Accessibility->GetFlashScale(), 1.f);
    TestEqual(TEXT("Blends take their time"), Accessibility->GetTransitionSeconds(AuthoredBlend), AuthoredBlend);
    TestEqual(TEXT("Cameras follow at their speed"), Accessibility->GetCameraFollowSpeed(AuthoredFollow), AuthoredFollow);
    TestEqual(TEXT("Menus fade as authored"), Menu->GetTransitionSeconds(), AuthoredFade);

    // The sliders scale shakes and flashes.
    Settings->SetValue(Accessibility->CameraShakeSettingId, 50.f);
    Settings->SetValue(Accessibility->FlashSettingId, 25.f);
    TestTrue(TEXT("Camera shake at 50% halves shakes"), FMath::IsNearlyEqual(Accessibility->GetCameraShakeScale(), 0.5f));
    TestTrue(TEXT("Flashes at 25% are a quarter as bright"), FMath::IsNearlyEqual(Accessibility->GetFlashScale(), 0.25f));
    TestFalse(TEXT("No shake plays without a shake to play"), Accessibility->StartCameraShake(Controller, nullptr, 1.f));

    // Reduced motion: no shake, no lag, no fades, cuts.
    Settings->SetValue(Accessibility->ReducedMotionSettingId, 1.f);
    TestTrue(TEXT("Reduced motion is on"), Accessibility->IsReducedMotion());
    TestEqual(TEXT("Nothing shakes"), Accessibility->GetCameraShakeScale(), 0.f);
    TestTrue(TEXT("...flashes still follow their own setting"), FMath::IsNearlyEqual(Accessibility->GetFlashScale(), 0.25f));
    TestEqual(TEXT("A blend is a cut"), Accessibility->GetTransitionSeconds(AuthoredBlend), 0.f);
    TestEqual(TEXT("Menus don't fade"), Menu->GetTransitionSeconds(), 0.f);
    TestEqual(TEXT("A camera following at the reduced speed is on its target at once"),
        FMath::FInterpTo(0.f, 100.f, 1.f / 60.f, Accessibility->GetCameraFollowSpeed(AuthoredFollow)), 100.f);

    // A headless world never initializes actors for play, so the controller spawned no camera
    // manager; give it one.
    APlayerCameraManager* CameraManager = Controller->PlayerCameraManager;
    if (!CameraManager)
    {
        CameraManager = World->SpawnActor<APlayerCameraManager>(APlayerCameraManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (CameraManager)
        {
            CameraManager->InitializeFor(Controller);
            Controller->PlayerCameraManager = CameraManager;
        }
    }
    AActor* First = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator, SpawnParams);
    AActor* Second = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(200.f, 0.f, 0.f), FRotator::ZeroRotator, SpawnParams);
    if (CameraManager && First && Second)
    {
        // GetViewTarget answers the pending target while blending, so read the two slots.
        Controller->SetViewTargetWithBlend(First, AuthoredBlend);
        TestTrue(TEXT("With reduced motion a blended change of view cuts straight to it"),
            CameraManager->ViewTarget.Target == First && CameraManager->PendingViewTarget.Target == nullptr);
        Settings->SetValue(Accessibility->ReducedMotionSettingId, 0.f);
        Controller->SetViewTargetWithBlend(Second, AuthoredBlend);
        TestTrue(TEXT("...and without it, the view blends over"),
            CameraManager->ViewTarget.Target == First && CameraManager->PendingViewTarget.Target == Second);
    }
    else
    {
        AddInfo(TEXT("No camera manager in this headless world; the view-blend cut was not checked."));
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Reduced motion on the broadcast cameras
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSReducedMotionCamerasTest,
    "PlaySports.Accessibility.ReducedMotionCameras",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReducedMotionCamerasTest::RunTest(const FString& Parameters)
{
    using namespace PSUIAccessibilityTests;

    const float StepSeconds = 0.1f;
    const int32 QuarterbackIndex = 5;
    const int32 RunningBackIndex = 6;
    const TArray<EPlayerRole> Roles = {
        EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman,
        EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::Quarterback,
        EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
        EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
        EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::DefensiveLineman,
        EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::Linebacker,
        EPlayerRole::Linebacker, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack,
        EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack
    };

    UWorld* World = CreateTestWorld();
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Accessibility subsystem"), Accessibility) || !TestNotNull(TEXT("Sampler"), Sampler) || !TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    Accessibility->SetSettings(Settings);
    Settings->SetValue(Accessibility->ReducedMotionSettingId, 1.f);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, 0.f);
    TArray<APSPlayerPawn*> Pawns;
    for (int32 Index = 0; Index < Roles.Num() && Index < Lineup.Num(); ++Index)
    {
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Lineup[Index], FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(*FString::Printf(TEXT("P%02d"), Index));
            Attributes.DisplayName = FString::Printf(TEXT("Player %d"), Index);
            Attributes.Role = Roles[Index];
            Pawn->InitializePlayer(Attributes);
            Pawn->SetActorLocation(Lineup[Index]);
            Pawns.Add(Pawn);
        }
    }
    APSBroadcastCamera* Camera = World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector(0.0, -2800.0, 600.0), FRotator(-10.0, 90.0, 0.0), SpawnParams);
    UPSCameraDirectorComponent* Director = Camera ? Camera->GetDirectorComponent() : nullptr;
    UPSCameraAll22Component* Film = Camera ? Camera->GetAll22Component() : nullptr;
    if (!TestEqual(TEXT("22 players"), Pawns.Num(), 22) || !TestNotNull(TEXT("Director"), Director) || !TestNotNull(TEXT("Film component"), Film))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Sample every step (Epic 26) so the director sees the field.
    FPSTelemetrySamplingTuning Steady = Sampler->GetTuning();
    Steady.SampleRateHz = 10.f;
    Steady.SampleBudgetMs = 1000.f;
    Steady.RecoverAfterSamples = 100000;
    Sampler->SetTuning(Steady);
    Director->BindToBus();
    Film->BindToBus();
    const float MinShotSeconds = Director->GetTuning().MinShotSeconds;
    APSPlayerPawn* Quarterback = Pawns[QuarterbackIndex];
    Quarterback->GainPossession();
    auto Step = [Sampler, Director, StepSeconds]()
    {
        Sampler->AdvanceTime(StepSeconds);
        Director->AdvanceTime(StepSeconds);
    };

    // The director: once on the snap's follow, the camera stays on its shot as the ball moves.
    Step();
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    for (int32 Guard = 0; Guard < 100 && (Director->GetPendingShot() != EPSDirectorShot::None || Director->GetShotAge() < MinShotSeconds); ++Guard)
    {
        Step();
    }
    Quarterback->SetActorLocation(Quarterback->GetActorLocation() + FVector(800.0, 0.0, 0.0));
    const FVector Before = Camera->GetActorLocation();
    Step();
    TestTrue(TEXT("The shot moved with the ball, so there was a move to ease"), FVector::Dist(Before, Director->GetTargetShot().Location) > 1.0);
    TestTrue(TEXT("With reduced motion the director's camera is on its shot, not easing after it"),
        Camera->GetActorLocation().Equals(Director->GetTargetShot().Location, 1.0));

    // The all-22 film view: a wide frame closes in on a pile in one step.
    const FPSAll22CameraTuning All22 = Film->GetTuning();
    const FPSAll22RigDef* Sideline = All22.All22Rigs.FindByPredicate([](const FPSAll22RigDef& Rig) { return Rig.Placement == EPSAll22RigPlacement::Sideline; });
    if (TestNotNull(TEXT("A sideline rig"), Sideline) && TestTrue(TEXT("Film view on the sideline rig"), Film->SetFilmView(Sideline->RigId)))
    {
        Pawns[RunningBackIndex]->SetActorLocation(Pawns[RunningBackIndex]->GetActorLocation() + FVector(4000.0, 0.0, 0.0));
        Film->StepFraming(StepSeconds);
        const FVector Tackle = Pawns[RunningBackIndex]->GetActorLocation();
        TArray<FVector> Pile;
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            Pile.Add(Tackle + FVector((Index % 4) * 60.0 - 90.0, (Index / 4) * 60.0 - 150.0, 0.0));
            Pawns[Index]->SetActorLocation(Pile.Last());
        }
        const FPSCameraShot Settled = UPSCameraFraming::FrameAll22(*Sideline, All22, Pile, Film->GetAttackDirection(), All22.AspectRatio);
        Film->StepFraming(StepSeconds);
        TestTrue(TEXT("With reduced motion the all-22 frame closes in on the pile at once"), Film->GetCurrentShot().Location.Equals(Settled.Location, 10.0));
        Film->SetFilmView(NAME_None);
    }

    // The plain sideline follow (director off): on its target's X in one step.
    Director->SetDirectorEnabled(false);
    Camera->bIsFollowing = true;
    Camera->SetTargetActor(Quarterback);
    Camera->Tick(StepSeconds);
    const double TargetX = FMath::Clamp(Quarterback->GetActorLocation().X, static_cast<double>(Camera->MinX), static_cast<double>(Camera->MaxX));
    TestTrue(TEXT("With reduced motion the sideline follow is on its target at once"), FMath::Abs(Camera->GetActorLocation().X - TargetX) < 1.0);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
