// PSBallActionComponentTests.cpp -- Epic C3: Ball action component and attributes reference tests
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayerPawn.h"
#include "PSBallActionComponent.h"
#include "PSBall.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBallActionComponentTest,
    "PlaySports.C3.BallActionComponentAndAttributes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBallActionComponentTest::RunTest(const FString& Parameters)
{
    // Test pawn attributes pointer mapping
    APSPlayerPawn* Pawn = NewObject<APSPlayerPawn>();
    TestNotNull(TEXT("Pawn created"), Pawn);
    if (!Pawn)
    {
        return false;
    }

    FPlayerAttributes AttributesSource;
    AttributesSource.PlayerId = TEXT("QB_TEST");
    AttributesSource.DisplayName = TEXT("Test QB");
    AttributesSource.Strength = 90.f;
    AttributesSource.Awareness = 85.f;

    Pawn->InitializePlayerPointer(&AttributesSource);

    // Verify pointer access
    FPlayerAttributes RetrievedAttributes = Pawn->GetAttributes();
    TestEqual(TEXT("PlayerId matches source"), RetrievedAttributes.PlayerId, AttributesSource.PlayerId);
    TestEqual(TEXT("Strength matches source"), RetrievedAttributes.Strength, AttributesSource.Strength);

    // Verify component exists
    UPSBallActionComponent* ActionComp = Pawn->GetBallActionComponent();
    TestNotNull(TEXT("BallActionComponent exists on pawn"), ActionComp);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
