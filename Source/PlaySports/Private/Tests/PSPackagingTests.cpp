// PSPackagingTests.cpp -- Epic 145: the packaged Win64 build's data staging and smoke test
//
// A packaged build stages the whole Data/ folder, loose, at the same place under its project
// directory (PlaySports.Build.cs), and every loader reads FPaths::ProjectDir() / "Data/...". These
// tests hold the audit to that: every default data file lies in the staged folder and resolves,
// and a project directory holding nothing but the staged folder (the packaged layout) resolves
// every one. CI's packaging workflow runs the same check inside the packaged build itself, as the
// first step of -PSSmokeTest.
//
// Tests covered:
//   1. Every default data file is in Data/ and resolves, as does every roster the default team
//      file names; the staged-folder rule keeps out paths outside Data/.
//   2. Under the packaged layout (a project directory with only Data/ copied in, as staged)
//      every file resolves; a build without Data/ lacks every one, and one with only the team
//      file still lacks the rosters it names.
//   3. The smoke test plays a full game between the league's first two teams, the same game for
//      the same seed, and reports it as one log line and as JSON.
//   4. Without its data, the smoke test fails and says what is missing.
//   5. The smoke test's switches: -PSSmokeTest, -PSSmokeTestSeed= and -PSSmokeTestReport=.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "PSDataPaths.h"
#include "PSMatchSetup.h"
#include "PSPackagedSmokeTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPackagingTests
{
    /** An empty scratch directory under Saved/Tests, standing in for a packaged build's project
     *  directory. */
    FString MakeScratchProjectDir(const TCHAR* Name)
    {
        const FString Dir = FPaths::ProjectSavedDir() / TEXT("Tests") / Name;
        IFileManager::Get().DeleteDirectory(*Dir, false, true);
        IFileManager::Get().MakeDirectory(*Dir, true);
        return Dir;
    }

    /** Copies the project's whole Data/ folder under ProjectDir, as the packaged build stages it;
     *  the number of files copied. */
    int32 StageDataFolder(const FString& ProjectDir)
    {
        const FString SourceDir = FPaths::ProjectDir() / PSDataPaths::GetDataFolder();
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *SourceDir, TEXT("*"), true, false);
        int32 Copied = 0;
        for (const FString& File : Files)
        {
            if (!File.StartsWith(SourceDir))
            {
                continue;
            }
            const FString Staged = ProjectDir / PSDataPaths::GetDataFolder() / File.RightChop(SourceDir.Len());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Staged), true);
            if (IFileManager::Get().Copy(*Staged, *File) == COPY_OK)
            {
                ++Copied;
            }
        }
        return Copied;
    }

    /** The smoke test's report, read back. */
    TSharedPtr<FJsonObject> ReadReport(const FString& Json)
    {
        TSharedPtr<FJsonObject> Report;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        FJsonSerializer::Deserialize(Reader, Report);
        return Report;
    }
}

// ---------------------------------------------------------------------------
// 1. Every default data file is staged and resolves
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSDefaultDataFilesResolveTest,
    "PlaySports.Packaging.DataStaging.DefaultDataFilesResolve",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefaultDataFilesResolveTest::RunTest(const FString& Parameters)
{
    const TArray<FString>& Files = PSDataPaths::GetDefaultDataFiles();
    AddInfo(FString::Printf(TEXT("%d default data files"), Files.Num()));
    TestTrue(TEXT("The audit lists the game's data files"), Files.Num() >= 80);

    TSet<FString> Seen;
    for (const FString& File : Files)
    {
        if (Seen.Contains(File))
        {
            AddError(FString::Printf(TEXT("%s is listed twice"), *File));
        }
        Seen.Add(File);
        if (!PSDataPaths::IsStaged(File))
        {
            AddError(FString::Printf(TEXT("%s is outside the staged Data/ folder: a packaged build wouldn't carry it"), *File));
        }
        if (!IFileManager::Get().FileExists(*PSDataPaths::Resolve(File, FString())))
        {
            AddError(FString::Printf(TEXT("%s doesn't resolve under the project directory"), *File));
        }
    }
    TestTrue(TEXT("The default team file is listed"), Seen.Contains(FString(PSDataPaths::GetDefaultTeamsFile())));

    const TArray<FString> Rosters = PSDataPaths::GetTeamRosterFiles(FString());
    TestTrue(TEXT("The default team file names its teams' rosters"), Rosters.Num() >= 2);
    for (const FString& Roster : Rosters)
    {
        TestTrue(*FString::Printf(TEXT("Roster %s is in the staged Data/ folder"), *Roster), PSDataPaths::IsStaged(Roster));
    }

    const TArray<FString> Missing = PSDataPaths::FindMissingDataFiles(FString());
    TestEqual(*FString::Printf(TEXT("Nothing is missing from the project (%s)"), *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);

    // The staged-folder rule.
    TestTrue(TEXT("A file in Data/ is staged"), PSDataPaths::IsStaged(TEXT("Data/formations.json")));
    TestTrue(TEXT("...so is one in a folder under it"), PSDataPaths::IsStaged(TEXT("Data/rosters/team_hawks.json")));
    TestTrue(TEXT("...written with backslashes too"), PSDataPaths::IsStaged(TEXT("Data\\rosters\\team_hawks.json")));
    TestFalse(TEXT("A file outside Data/ is not"), PSDataPaths::IsStaged(TEXT("Config/DefaultGame.ini")));
    TestFalse(TEXT("...nor one that climbs out of it"), PSDataPaths::IsStaged(TEXT("Data/../Config/DefaultGame.ini")));
    TestFalse(TEXT("...nor the folder itself"), PSDataPaths::IsStaged(TEXT("Data")));
    return true;
}

// ---------------------------------------------------------------------------
// 2. Under the packaged layout
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSDataResolvesUnderPackagedLayoutTest,
    "PlaySports.Packaging.DataStaging.ResolvesUnderThePackagedLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDataResolvesUnderPackagedLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSPackagingTests;

    const FString ProjectDir = MakeScratchProjectDir(TEXT("DataStaging"));
    const TArray<FString>& Files = PSDataPaths::GetDefaultDataFiles();

    // No Data/ at all: every default data file is missing, and nothing more is known.
    TArray<FString> Missing = PSDataPaths::FindMissingDataFiles(ProjectDir);
    TestEqual(TEXT("A build without Data/ lacks every default data file"), Missing.Num(), Files.Num());

    // Only the team file: it is found, and the rosters it names are missing.
    const FString TeamsFile = PSDataPaths::GetDefaultTeamsFile();
    const FString StagedTeams = PSDataPaths::Resolve(TeamsFile, ProjectDir);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(StagedTeams), true);
    IFileManager::Get().Copy(*StagedTeams, *PSDataPaths::Resolve(TeamsFile, FString()));
    Missing = PSDataPaths::FindMissingDataFiles(ProjectDir);
    TestFalse(TEXT("With the team file copied in, it is found"), Missing.Contains(TeamsFile));
    for (const FString& Roster : PSDataPaths::GetTeamRosterFiles(FString()))
    {
        TestTrue(*FString::Printf(TEXT("...and the roster it names, %s, is missing"), *Roster), Missing.Contains(Roster));
    }

    // The whole Data/ folder, as the packaged build stages it: everything resolves.
    const int32 Copied = StageDataFolder(ProjectDir);
    AddInfo(FString::Printf(TEXT("Staged %d files under %s"), Copied, *ProjectDir));
    TestTrue(TEXT("The staged folder holds at least the default data files"), Copied >= Files.Num());
    Missing = PSDataPaths::FindMissingDataFiles(ProjectDir);
    TestEqual(*FString::Printf(TEXT("Under the packaged layout every data file resolves (%s)"), *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);

    IFileManager::Get().DeleteDirectory(*ProjectDir, false, true);
    return true;
}

// ---------------------------------------------------------------------------
// 3. The smoke test plays a full game
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSmokeTestPlaysAFullGameTest,
    "PlaySports.Packaging.SmokeTest.PlaysAFullGame",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSmokeTestPlaysAFullGameTest::RunTest(const FString& Parameters)
{
    using namespace PSPackagingTests;

    const UPSPackagedSmokeTest* SmokeTest = GetDefault<UPSPackagedSmokeTest>();
    const FPSSmokeTestResult Smoke = SmokeTest->Run(UPSPackagedSmokeTest::DefaultSeed, FString());
    const FString Line = UPSPackagedSmokeTest::FormatResultLine(Smoke);
    AddInfo(Line);

    TestTrue(*FString::Printf(TEXT("The smoke test passes (%s)"), *FString::Join(Smoke.Failures, TEXT("; "))), Smoke.bPassed);
    TestEqual(TEXT("Nothing is missing"), Smoke.MissingDataFiles.Num(), 0);
    TestEqual(TEXT("The run was played with its seed"), Smoke.Seed, UPSPackagedSmokeTest::DefaultSeed);

    const TArray<FName> League = UPSMatchSetup::LoadLeagueTeamIds(PSDataPaths::Resolve(PSDataPaths::GetDefaultTeamsFile(), FString()));
    if (TestTrue(TEXT("The league has two teams to play"), League.Num() >= 2))
    {
        TestEqual(TEXT("The league's first team is at home, as Play Now has it"), Smoke.HomeTeamId, League[0]);
        TestEqual(TEXT("...against the second"), Smoke.AwayTeamId, League[1]);
    }
    TestTrue(TEXT("The game is played to the end of the fourth quarter"), Smoke.bGameFinished);
    TestTrue(TEXT("A whole game's worth of plays"), Smoke.Plays > 50);
    TestTrue(TEXT("Somebody scores"), Smoke.HomeScore + Smoke.AwayScore > 0);

    // The same seed plays the same game, so a build's score is the same on every run.
    const FPSSmokeTestResult Again = SmokeTest->Run(UPSPackagedSmokeTest::DefaultSeed, FString());
    TestEqual(TEXT("Same seed, same home score"), Again.HomeScore, Smoke.HomeScore);
    TestEqual(TEXT("Same seed, same away score"), Again.AwayScore, Smoke.AwayScore);
    TestEqual(TEXT("Same seed, same plays"), Again.Plays, Smoke.Plays);

    // The log line CI and people read.
    TestTrue(TEXT("The line says it passed, with the seed"), Line.StartsWith(FString::Printf(TEXT("PSSmokeTest: PASS seed=%d "), Smoke.Seed)));
    TestTrue(TEXT("The line gives the final score"), Line.Contains(FString::Printf(TEXT("home=%s %d away=%s %d"),
        *Smoke.HomeTeamId.ToString(), Smoke.HomeScore, *Smoke.AwayTeamId.ToString(), Smoke.AwayScore)));
    TestFalse(TEXT("A pass lists no failures"), Line.Contains(TEXT(" | ")));

    // The report CI reads.
    const TSharedPtr<FJsonObject> Report = ReadReport(UPSPackagedSmokeTest::ToJson(Smoke));
    if (TestTrue(TEXT("The report is a JSON object"), Report.IsValid()))
    {
        TestTrue(TEXT("The report says it passed"), Report->GetBoolField(TEXT("Passed")));
        TestEqual(TEXT("The report has the home team"), Report->GetStringField(TEXT("HomeTeam")), Smoke.HomeTeamId.ToString());
        TestEqual(TEXT("...its score"), Report->GetIntegerField(TEXT("HomeScore")), Smoke.HomeScore);
        TestEqual(TEXT("...the away team"), Report->GetStringField(TEXT("AwayTeam")), Smoke.AwayTeamId.ToString());
        TestEqual(TEXT("...its score"), Report->GetIntegerField(TEXT("AwayScore")), Smoke.AwayScore);
        TestEqual(TEXT("...the plays"), Report->GetIntegerField(TEXT("Plays")), Smoke.Plays);
        TestTrue(TEXT("...that the game finished"), Report->GetBoolField(TEXT("GameFinished")));
        TestEqual(TEXT("...and no failures"), Report->GetArrayField(TEXT("Failures")).Num(), 0);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 4. Without its data the smoke test fails
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSmokeTestFailsWithoutDataTest,
    "PlaySports.Packaging.SmokeTest.FailsWithoutItsData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSmokeTestFailsWithoutDataTest::RunTest(const FString& Parameters)
{
    using namespace PSPackagingTests;

    const FString ProjectDir = MakeScratchProjectDir(TEXT("SmokeTestNoData"));
    const FPSSmokeTestResult Smoke = GetDefault<UPSPackagedSmokeTest>()->Run(UPSPackagedSmokeTest::DefaultSeed, ProjectDir);
    const FString Line = UPSPackagedSmokeTest::FormatResultLine(Smoke);
    AddInfo(Line.Left(300));

    TestFalse(TEXT("A build without its data fails"), Smoke.bPassed);
    TestEqual(TEXT("It names every default data file as missing"), Smoke.MissingDataFiles.Num(), PSDataPaths::GetDefaultDataFiles().Num());
    TestTrue(TEXT("...and that there are no teams to play"), Smoke.Failures.Num() > Smoke.MissingDataFiles.Num());
    TestEqual(TEXT("No game is played"), Smoke.Plays, 0);
    TestFalse(TEXT("...so none finishes"), Smoke.bGameFinished);
    TestTrue(TEXT("The line says it failed"), Line.StartsWith(TEXT("PSSmokeTest: FAIL ")));
    TestTrue(TEXT("...and why"), Line.Contains(FString::Printf(TEXT(" | data file %s is missing"), PSDataPaths::GetDefaultTeamsFile())));

    const TSharedPtr<FJsonObject> Report = ReadReport(UPSPackagedSmokeTest::ToJson(Smoke));
    if (TestTrue(TEXT("The report is a JSON object"), Report.IsValid()))
    {
        TestFalse(TEXT("The report says it failed"), Report->GetBoolField(TEXT("Passed")));
        TestEqual(TEXT("...and lists the missing files"), Report->GetArrayField(TEXT("MissingDataFiles")).Num(), Smoke.MissingDataFiles.Num());
    }

    IFileManager::Get().DeleteDirectory(*ProjectDir, false, true);
    return true;
}

// ---------------------------------------------------------------------------
// 5. The switches
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSmokeTestSwitchesTest,
    "PlaySports.Packaging.SmokeTest.ReadsItsSwitches",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSmokeTestSwitchesTest::RunTest(const FString& Parameters)
{
    int32 Seed = 0;
    FString ReportPath;

    TestFalse(TEXT("Without -PSSmokeTest there is no run"), UPSPackagedSmokeTest::ParseCommandLine(TEXT("-nullrhi -unattended"), Seed, ReportPath));
    TestFalse(TEXT("...and a longer switch is not it"), UPSPackagedSmokeTest::ParseCommandLine(TEXT("-nullrhi -PSSmokeTestSeed=7"), Seed, ReportPath));

    TestTrue(TEXT("-PSSmokeTest asks for a run"), UPSPackagedSmokeTest::ParseCommandLine(TEXT("-nullrhi -PSSmokeTest -unattended"), Seed, ReportPath));
    TestEqual(TEXT("...with the default seed"), Seed, UPSPackagedSmokeTest::DefaultSeed);
    TestTrue(TEXT("...and no report file"), ReportPath.IsEmpty());

    TestTrue(TEXT("Its options are read"),
        UPSPackagedSmokeTest::ParseCommandLine(TEXT("-PSSmokeTest -PSSmokeTestSeed=7 -PSSmokeTestReport=C:/Build/SmokeTest/result.json -nullrhi"), Seed, ReportPath));
    TestEqual(TEXT("...the seed"), Seed, 7);
    TestEqual(TEXT("...and the report file"), ReportPath, FString(TEXT("C:/Build/SmokeTest/result.json")));

    TestTrue(TEXT("A quoted report path"),
        UPSPackagedSmokeTest::ParseCommandLine(TEXT("-PSSmokeTest -PSSmokeTestReport=\"C:/Build Dir/result.json\""), Seed, ReportPath));
    TestEqual(TEXT("...keeps its spaces"), ReportPath, FString(TEXT("C:/Build Dir/result.json")));
    TestEqual(TEXT("...and the seed is the default again"), Seed, UPSPackagedSmokeTest::DefaultSeed);
    return true;
}

#endif
