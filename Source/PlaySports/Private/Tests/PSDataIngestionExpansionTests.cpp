#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSLeagueData.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Teams and league config JSON ingestion
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDataIngestionTeamsAndConfigTest,
    "PlaySports.Data.TeamsAndLeagueConfigIngestion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDataIngestionTeamsAndConfigTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();

    UDataTable* TeamsTable = NewObject<UDataTable>();
    TeamsTable->RowStruct = FPSTeamInfo::StaticStruct();
    const bool bTeamsLoaded = Ingestion->LoadTeamsFromJson(FPaths::ProjectDir() / TEXT("Data/sample_teams.json"), TeamsTable);
    TestTrue(TEXT("sample_teams.json loads successfully"), bTeamsLoaded);
    if (bTeamsLoaded)
    {
        TestTrue(TEXT("At least 4 teams loaded"), TeamsTable->GetRowMap().Num() >= 4);
    }

    FPSLeagueConfig Config;
    const bool bConfigLoaded = Ingestion->LoadLeagueConfigFromJson(FPaths::ProjectDir() / TEXT("Data/sample_league_config.json"), Config);
    TestTrue(TEXT("sample_league_config.json loads successfully"), bConfigLoaded);
    if (bConfigLoaded)
    {
        TestTrue(TEXT("League config has a non-empty name"), !Config.LeagueName.IsEmpty());
        TestTrue(TEXT("League config NumWeeks is positive"), Config.NumWeeks > 0);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Schema validation catches missing/invalid fields with actionable errors
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDataIngestionValidationTest,
    "PlaySports.Data.SchemaValidationCatchesBadRows",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDataIngestionValidationTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();

    // Sanity: the real sample file should validate clean.
    TArray<FString> CleanErrors;
    const bool bCleanValid = Ingestion->ValidatePlayersJson(FPaths::ProjectDir() / TEXT("Data/sample_players.json"), CleanErrors);
    TestTrue(TEXT("sample_players.json validates with no errors"), bCleanValid);
    TestEqual(TEXT("No errors reported for the clean file"), CleanErrors.Num(), 0);

    // Write a deliberately broken temp file: missing PlayerId, bad Role, negative Speed.
    const FString BadJson = TEXT(R"({
        "Players": [
            { "DisplayName": "No ID", "Role": "NotARealRole", "Speed": -5.0 }
        ]
    })");
    const FString TempPath = FPaths::ProjectDir() / TEXT("Data/__test_bad_players.json");
    FFileHelper::SaveStringToFile(BadJson, *TempPath);

    TArray<FString> Errors;
    const bool bValid = Ingestion->ValidatePlayersJson(TempPath, Errors);
    TestFalse(TEXT("Broken file fails validation"), bValid);
    TestTrue(TEXT("At least 3 actionable errors reported (missing id, bad role, negative speed)"), Errors.Num() >= 3);

    IFileManager::Get().Delete(*TempPath);

    return true;
}

#endif
