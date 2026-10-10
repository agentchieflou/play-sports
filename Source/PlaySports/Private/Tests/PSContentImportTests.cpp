// PSContentImportTests.cpp -- Epic 125 (content validation and import)
//
// Tests covered:
//   1. Everything in Data/ imports through the game's own loaders with no errors: the league
//      config, its teams, every team's roster, the route library and the playbook. This is
//      the import half of `python tools/content.py import`, run on every CI build.
//   2. Broken references are reported per file, actionably: a team whose roster is missing,
//      a PlayerId on two teams, and a play running a route the library lacks.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PSContentReimportCommandlet.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSContentImportTests
{
    static bool ContainsLine(const TArray<FString>& Lines, const TCHAR* Text)
    {
        return Lines.ContainsByPredicate([Text](const FString& Line) { return Line.Contains(Text); });
    }

    static bool WriteContent(const FString& Root, const TCHAR* RelativePath, const TCHAR* Json)
    {
        return FFileHelper::SaveStringToFile(FString(Json), *(Root / RelativePath));
    }

    static FString PlayerJson(const TCHAR* PlayerId, const TCHAR* Role)
    {
        return FString::Printf(TEXT("{ \"PlayerId\": \"%s\", \"DisplayName\": \"%s\", \"Role\": \"%s\", \"WeightKg\": 100, \"HeightCm\": 190, ")
            TEXT("\"Speed\": 80, \"Agility\": 80, \"Strength\": 80, \"Acceleration\": 80, \"Awareness\": 80, \"Stamina\": 80 }"), PlayerId, PlayerId, Role);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSContentImportShippedTest,
    "PlaySports.Content.ImportShippedContent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContentImportShippedTest::RunTest(const FString& Parameters)
{
    using namespace PSContentImportTests;

    TArray<FString> Errors;
    TArray<FString> Loaded;
    const bool bClean = UPSContentReimportCommandlet::ReimportAll(FPaths::ProjectDir(), Errors, Loaded);
    for (const FString& Error : Errors)
    {
        AddError(FString::Printf(TEXT("Shipped content: %s"), *Error));
    }
    TestTrue(TEXT("All of Data/ imports cleanly"), bClean);
    TestTrue(TEXT("The league config loads"), ContainsLine(Loaded, TEXT("sample_league_config.json - league")));
    TestTrue(TEXT("Its teams load"), ContainsLine(Loaded, TEXT(" teams")));
    TestTrue(TEXT("Rosters load, one line per team"), ContainsLine(Loaded, TEXT("players (team '")));
    TestTrue(TEXT("The route library loads"), ContainsLine(Loaded, TEXT(" routes")));
    TestTrue(TEXT("The playbook loads"), ContainsLine(Loaded, TEXT(" plays")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSContentImportBrokenReferencesTest,
    "PlaySports.Content.ImportReportsBrokenReferences",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSContentImportBrokenReferencesTest::RunTest(const FString& Parameters)
{
    using namespace PSContentImportTests;

    const FString Root = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("ContentImport");
    IFileManager::Get().DeleteDirectory(*Root, false, true);

    bool bWritten = WriteContent(Root, TEXT("Data/sample_league_config.json"),
        TEXT("{ \"LeagueName\": \"Test League\", \"NumWeeks\": 4, \"ByeWeekNumbers\": [], \"NumPlayoffTeams\": 2, \"TeamsDataTablePath\": \"Data/teams.json\" }"));
    bWritten &= WriteContent(Root, TEXT("Data/teams.json"),
        TEXT("{ \"Teams\": [")
        TEXT("{ \"TeamId\": \"Alpha\", \"DisplayName\": \"Alpha\", \"Division\": \"East\", \"RosterDataTablePath\": \"Data/rosters/alpha.json\" },")
        TEXT("{ \"TeamId\": \"Beta\", \"DisplayName\": \"Beta\", \"Division\": \"East\", \"RosterDataTablePath\": \"Data/rosters/beta.json\" },")
        TEXT("{ \"TeamId\": \"Gamma\", \"DisplayName\": \"Gamma\", \"Division\": \"West\", \"RosterDataTablePath\": \"Data/rosters/missing.json\" }")
        TEXT("] }"));
    bWritten &= FFileHelper::SaveStringToFile(FString::Printf(TEXT("{ \"Players\": [ %s, %s ] }"),
        *PlayerJson(TEXT("ALP_QB"), TEXT("Quarterback")), *PlayerJson(TEXT("SHARED_WR"), TEXT("WideReceiver"))), *(Root / TEXT("Data/rosters/alpha.json")));
    bWritten &= FFileHelper::SaveStringToFile(FString::Printf(TEXT("{ \"Players\": [ %s, %s ] }"),
        *PlayerJson(TEXT("BET_QB"), TEXT("Quarterback")), *PlayerJson(TEXT("SHARED_WR"), TEXT("WideReceiver"))), *(Root / TEXT("Data/rosters/beta.json")));
    bWritten &= WriteContent(Root, TEXT("Data/sample_routes.json"),
        TEXT("{ \"Routes\": [ { \"RouteId\": \"Go\", \"Waypoints\": [ { \"Offset\": { \"X\": 1500, \"Y\": 0, \"Z\": 0 }, \"TimingSeconds\": 2.5 } ] } ] }"));
    bWritten &= WriteContent(Root, TEXT("Data/sample_playbook.json"),
        TEXT("{ \"Plays\": [ { \"PlayId\": \"Offense_Test\", \"DisplayName\": \"Test\", \"Formation\": \"Spread\", \"bIsOffensivePlay\": true, \"PlayCategory\": \"DeepPass\", \"Assignments\": [")
        TEXT("{ \"Role\": \"WideReceiver\", \"Kind\": \"Route\", \"RouteId\": \"Go\" },")
        TEXT("{ \"Role\": \"TightEnd\", \"Kind\": \"Route\", \"RouteId\": \"Wheel\" } ] } ] }"));
    if (!TestTrue(TEXT("Test content written"), bWritten))
    {
        return false;
    }

    TArray<FString> Errors;
    TArray<FString> Loaded;
    TestFalse(TEXT("Broken content does not import cleanly"), UPSContentReimportCommandlet::ReimportAll(Root, Errors, Loaded));

    TestTrue(TEXT("The missing roster names its file and team"), ContainsLine(Errors, TEXT("Data/rosters/missing.json (team 'Gamma')")));
    TestTrue(TEXT("The shared PlayerId names both teams"), ContainsLine(Errors, TEXT("PlayerId 'SHARED_WR' is also on team 'Alpha'")));
    TestTrue(TEXT("The unknown route names the play and the route"), ContainsLine(Errors, TEXT("play 'Offense_Test' runs route 'Wheel'")));
    TestFalse(TEXT("The known route is not reported"), ContainsLine(Errors, TEXT("route 'Go'")));
    TestEqual(TEXT("Exactly those three problems"), Errors.Num(), 3);
    TestTrue(TEXT("The league's teams file is followed"), ContainsLine(Loaded, TEXT("Data/teams.json - 3 teams")));
    TestTrue(TEXT("Good rosters still load"), ContainsLine(Loaded, TEXT("Data/rosters/alpha.json - 2 players")));

    IFileManager::Get().DeleteDirectory(*Root, false, true);
    return true;
}

#endif
