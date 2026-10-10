// PSPlatformTierTests.cpp -- Epic 129 (target platform audit and scalability tiers)
//
// Tests covered:
//   1. Data/platform_tiers.json loads and validates: desktop, phone and low-end phone tiers, the
//      phone tiers deciding less often than the desktop one.
//   2. Tier resolution: each platform's mapping, case-insensitive; an unknown platform gets the
//      default; a -PSTier override wins when it names a tier; this (Win64) run is DesktopHigh.
//   3. A tier-reading system honours its tier: the AI decides at the interval and keeps
//      steering in between.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSOffenseController.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlatformTierTests
{
    static bool LoadCatalog(FPSPlatformTierCatalog& OutCatalog)
    {
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        return Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), OutCatalog);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tier data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformTierDataTest,
    "PlaySports.Platform.TierCatalogValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformTierDataTest::RunTest(const FString& Parameters)
{
    FPSPlatformTierCatalog Catalog;
    if (!TestTrue(TEXT("Data/platform_tiers.json loads"), PSPlatformTierTests::LoadCatalog(Catalog)))
    {
        return false;
    }
    for (const FString& Problem : PSPlatformTiers::ValidateCatalog(Catalog))
    {
        AddError(FString::Printf(TEXT("platform_tiers.json: %s"), *Problem));
    }

    const FPSPlatformTier* Desktop = PSPlatformTiers::FindTier(Catalog, TEXT("DesktopHigh"));
    const FPSPlatformTier* Phone = PSPlatformTiers::FindTier(Catalog, TEXT("MobileBaseline"));
    const FPSPlatformTier* LowPhone = PSPlatformTiers::FindTier(Catalog, TEXT("MobileLow"));
    if (TestTrue(TEXT("Desktop, phone and low-end phone tiers exist"), Desktop && Phone && LowPhone))
    {
        TestEqual(TEXT("On desktop the AI decides every frame"), Desktop->AIDecisionInterval, 0.f);
        TestTrue(TEXT("On a phone it decides less often"), Phone->AIDecisionInterval > Desktop->AIDecisionInterval);
        TestTrue(TEXT("...and least often on a low-end phone"), LowPhone->AIDecisionInterval >= Phone->AIDecisionInterval);
    }

    FPSPlatformTierCatalog Broken = Catalog;
    Broken.DefaultTier = TEXT("NoSuchTier");
    if (Broken.Tiers.Num() > 0)
    {
        Broken.Tiers[0].AIDecisionInterval = -1.f;
    }
    TestTrue(TEXT("Validation catches an unknown default and a negative interval"), PSPlatformTiers::ValidateCatalog(Broken).Num() >= 2);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Which tier a run gets
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformTierResolutionTest,
    "PlaySports.Platform.TierResolution",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformTierResolutionTest::RunTest(const FString& Parameters)
{
    FPSPlatformTierCatalog Catalog;
    if (!TestTrue(TEXT("Data/platform_tiers.json loads"), PSPlatformTierTests::LoadCatalog(Catalog)))
    {
        return false;
    }

    TestEqual(TEXT("An iPhone runs the phone tier"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("IOS"), FString()), FName(TEXT("MobileBaseline")));
    TestEqual(TEXT("...whatever the platform name's case"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("ios"), FString()), FName(TEXT("MobileBaseline")));
    TestEqual(TEXT("Windows runs the desktop tier"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("Windows"), FString()), FName(TEXT("DesktopHigh")));
    TestEqual(TEXT("An unmapped platform gets the default"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("Switch"), FString()), Catalog.DefaultTier);
    TestEqual(TEXT("-PSTier overrides the platform"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("Windows"), TEXT("MobileLow")), FName(TEXT("MobileLow")));
    TestEqual(TEXT("...unless it names no tier"), PSPlatformTiers::ResolveTierId(Catalog, TEXT("IOS"), TEXT("Turbo")), FName(TEXT("MobileBaseline")));

    TestEqual(TEXT("This Win64 run is on the desktop tier"), PSPlatformTiers::GetActiveTier().TierId, FName(TEXT("DesktopHigh")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The AI decides at the tier's rate and steers in between
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlatformTierAIRateTest,
    "PlaySports.Platform.AIDecidesAtTierRate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlatformTierAIRateTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerPawn* Receiver = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(0.f, 900.f, 100.f), FRotator::ZeroRotator, SpawnParams);
    APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (!TestNotNull(TEXT("Receiver"), Receiver) || !TestNotNull(TEXT("AI"), AI) || !TestNotNull(TEXT("Bus"), Bus))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    FPlayerAttributes Attributes;
    Attributes.PlayerId = TEXT("WR");
    Attributes.DisplayName = TEXT("WR");
    Attributes.Role = EPlayerRole::WideReceiver;
    Receiver->InitializePlayer(Attributes);
    AI->Possess(Receiver);
    UPSSkillPlayerAIComponent* Brain = AI->GetSkillAI();
    Brain->BindToBus();

    // The snap (the play-call subsystem hands out a CPU play on it), then this test's route.
    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    const FVector Stem(300.f, 900.f, 100.f);
    const FVector Break(500.f, 500.f, 100.f);
    AI->SetAssignedRoute({ Stem, Break });

    const float Interval = 0.1f;
    Brain->UpdateAI(Interval, Interval);
    const FVector FirstLeg = Brain->GetDesiredDirection();
    TestTrue(TEXT("The first frame decides: up the stem"), FirstLeg.Equals(FVector(1.f, 0.f, 0.f), 0.01f));
    Receiver->ConsumeMovementInputVector();

    // He reaches the stem between decisions: the old direction holds, and he keeps moving.
    Receiver->SetActorLocation(Stem);
    Brain->UpdateAI(Interval * 0.4f, Interval);
    TestTrue(TEXT("Between decisions the last direction holds"), Brain->GetDesiredDirection().Equals(FirstLeg, 0.01f));
    TestTrue(TEXT("...and is still steered this frame"), Receiver->ConsumeMovementInputVector().GetSafeNormal().Equals(FirstLeg, 0.01f));

    Brain->UpdateAI(Interval * 0.7f, Interval);
    FVector Expected = Break - Stem;
    Expected.Z = 0.f;
    TestTrue(TEXT("Once the interval has passed he decides again: on to the break"), Brain->GetDesiredDirection().Equals(Expected.GetSafeNormal(), 0.01f));

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
