#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlaybookIngestion.h"
#include "PSPlaybookData.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Playbook JSON ingestion loads plays into a DataTable
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlaybookIngestionPlaysTest,
    "PlaySports.Data.PlaybookIngestionPlays",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlaybookIngestionPlaysTest::RunTest(const FString& Parameters)
{
    UPSPlaybookIngestion* Ingestion = NewObject<UPSPlaybookIngestion>();
    UDataTable* PlaysTable = NewObject<UDataTable>();
    PlaysTable->RowStruct = FPSPlayDefinition::StaticStruct();

    const FString JsonPath = FPaths::ProjectDir() / TEXT("Data/sample_playbook.json");
    const bool bLoaded = Ingestion->LoadPlaysFromJson(JsonPath, PlaysTable);

    TestTrue(TEXT("Playbook JSON loads successfully"), bLoaded);
    if (bLoaded)
    {
        TestTrue(TEXT("At least 6 offensive and 4 defensive plays loaded"), PlaysTable->GetRowMap().Num() >= 10);

        FPSPlayDefinition* SlantFlat = PlaysTable->FindRow<FPSPlayDefinition>(FName("Offense_SlantFlat"), TEXT("Test"));
        if (TestNotNull(TEXT("Offense_SlantFlat row found"), SlantFlat))
        {
            TestTrue(TEXT("Offense_SlantFlat is an offensive play"), SlantFlat->bIsOffensivePlay);
            TestTrue(TEXT("Offense_SlantFlat has assignments"), SlantFlat->Assignments.Num() > 0);
        }

        FPSPlayDefinition* Cover2 = PlaysTable->FindRow<FPSPlayDefinition>(FName("Defense_43Cover2"), TEXT("Test"));
        if (TestNotNull(TEXT("Defense_43Cover2 row found"), Cover2))
        {
            TestFalse(TEXT("Defense_43Cover2 is not an offensive play"), Cover2->bIsOffensivePlay);
            TestEqual(TEXT("Defense_43Cover2 front is 4-3"), Cover2->Front, FString(TEXT("4-3")));
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Route library JSON ingestion loads waypoint routes
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlaybookIngestionRoutesTest,
    "PlaySports.Data.PlaybookIngestionRoutes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlaybookIngestionRoutesTest::RunTest(const FString& Parameters)
{
    UPSPlaybookIngestion* Ingestion = NewObject<UPSPlaybookIngestion>();
    UDataTable* RoutesTable = NewObject<UDataTable>();
    RoutesTable->RowStruct = FPSRoute::StaticStruct();

    const FString JsonPath = FPaths::ProjectDir() / TEXT("Data/sample_routes.json");
    const bool bLoaded = Ingestion->LoadRoutesFromJson(JsonPath, RoutesTable);

    TestTrue(TEXT("Route library JSON loads successfully"), bLoaded);
    if (bLoaded)
    {
        TestTrue(TEXT("At least 3 routes loaded"), RoutesTable->GetRowMap().Num() >= 3);

        FPSRoute* Slant = RoutesTable->FindRow<FPSRoute>(FName("Slant"), TEXT("Test"));
        if (TestNotNull(TEXT("Slant route found"), Slant))
        {
            TestTrue(TEXT("Slant route has waypoints"), Slant->Waypoints.Num() > 0);
        }
    }

    return true;
}

#endif
