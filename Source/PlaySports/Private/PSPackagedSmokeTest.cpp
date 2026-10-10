#include "PSPackagedSmokeTest.h"
#include "PSDataPaths.h"
#include "PSMatchSetup.h"
#include "PSPlayerAttributes.h"
#include "PSQuickSimRunner.h"
#include "PSTelemetryBus.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PSPackagedSmokeTestPrivate
{
    const TCHAR* const SwitchName = TEXT("PSSmokeTest");
    const TCHAR* const SeedSwitch = TEXT("PSSmokeTestSeed=");
    const TCHAR* const ReportSwitch = TEXT("PSSmokeTestReport=");

    /** A team's roster, or a failure saying whose couldn't be read. */
    void LoadRoster(const FString& TeamsPath, FName TeamId, const TCHAR* Side, TArray<FPlayerAttributes>& OutPlayers, TArray<FString>& OutFailures)
    {
        if (!UPSMatchSetup::LoadTeamPlayers(TeamsPath, TeamId, OutPlayers) || OutPlayers.Num() == 0)
        {
            OutPlayers.Reset();
            OutFailures.Add(FString::Printf(TEXT("the %s team %s has no roster that can be read"), Side, *TeamId.ToString()));
        }
    }

    TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Lines)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& Line : Lines)
        {
            Values.Add(MakeShared<FJsonValueString>(Line));
        }
        return Values;
    }
}

bool UPSPackagedSmokeTest::ParseCommandLine(const TCHAR* CommandLine, int32& OutSeed, FString& OutReportPath)
{
    using namespace PSPackagedSmokeTestPrivate;

    OutSeed = DefaultSeed;
    OutReportPath.Reset();
    if (!CommandLine || !FParse::Param(CommandLine, SwitchName))
    {
        return false;
    }
    FParse::Value(CommandLine, SeedSwitch, OutSeed);
    FParse::Value(CommandLine, ReportSwitch, OutReportPath);
    return true;
}

FPSSmokeTestResult UPSPackagedSmokeTest::Run(int32 Seed, const FString& ProjectDir) const
{
    using namespace PSPackagedSmokeTestPrivate;

    FPSSmokeTestResult Result;
    Result.Seed = Seed;

    // 1. The build carries its data: every default data file and every team's roster.
    Result.MissingDataFiles = PSDataPaths::FindMissingDataFiles(ProjectDir);
    for (const FString& File : Result.MissingDataFiles)
    {
        Result.Failures.Add(FString::Printf(TEXT("data file %s is missing"), *File));
    }

    // 2. The teams Play Now picks without options: the league's first two, with their rosters.
    const FString TeamsPath = PSDataPaths::Resolve(PSDataPaths::GetDefaultTeamsFile(), ProjectDir);
    const TArray<FName> TeamIds = UPSMatchSetup::LoadLeagueTeamIds(TeamsPath);
    TArray<FPlayerAttributes> HomePlayers;
    TArray<FPlayerAttributes> AwayPlayers;
    if (TeamIds.Num() < 2)
    {
        Result.Failures.Add(FString::Printf(TEXT("the league in %s has %d team(s); a game needs two"), *TeamsPath, TeamIds.Num()));
    }
    else
    {
        Result.HomeTeamId = TeamIds[0];
        Result.AwayTeamId = TeamIds[1];
        LoadRoster(TeamsPath, Result.HomeTeamId, TEXT("home"), HomePlayers, Result.Failures);
        LoadRoster(TeamsPath, Result.AwayTeamId, TEXT("away"), AwayPlayers, Result.Failures);
    }

    // 3. A full game between them on the quick sim, as the scripted-game harness plays one.
    if (HomePlayers.Num() > 0 && AwayPlayers.Num() > 0)
    {
        UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();
        int32 Plays = 0;
        const FDelegateHandle Counter = Runner->OnPlayResolved.AddLambda([&Plays](const FPSTelemetryPlayResultEvent&)
        {
            ++Plays;
        });
        const FPSQuickSimResult Game = Runner->SimulateGame(HomePlayers, AwayPlayers, Seed);
        Runner->OnPlayResolved.Remove(Counter);

        Result.HomeScore = Game.HomeScore;
        Result.AwayScore = Game.AwayScore;
        Result.Plays = Plays;
        Result.bGameFinished = Game.bFinished;
        if (!Game.bFinished)
        {
            Result.Failures.Add(FString::Printf(TEXT("the game didn't reach the end of the fourth quarter in %d steps"), Runner->MaxPlaysPerGame));
        }
        if (Plays == 0)
        {
            Result.Failures.Add(TEXT("the game resolved no plays"));
        }
    }

    Result.bPassed = Result.Failures.Num() == 0;
    return Result;
}

FString UPSPackagedSmokeTest::FormatResultLine(const FPSSmokeTestResult& Result)
{
    FString Line = FString::Printf(TEXT("PSSmokeTest: %s seed=%d home=%s %d away=%s %d plays=%d finished=%s"),
        Result.bPassed ? TEXT("PASS") : TEXT("FAIL"), Result.Seed,
        *Result.HomeTeamId.ToString(), Result.HomeScore, *Result.AwayTeamId.ToString(), Result.AwayScore,
        Result.Plays, Result.bGameFinished ? TEXT("true") : TEXT("false"));
    for (const FString& Failure : Result.Failures)
    {
        Line += TEXT(" | ") + Failure;
    }
    return Line;
}

FString UPSPackagedSmokeTest::ToJson(const FPSSmokeTestResult& Result)
{
    using namespace PSPackagedSmokeTestPrivate;

    TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetBoolField(TEXT("Passed"), Result.bPassed);
    Object->SetNumberField(TEXT("Seed"), Result.Seed);
    Object->SetStringField(TEXT("HomeTeam"), Result.HomeTeamId.ToString());
    Object->SetNumberField(TEXT("HomeScore"), Result.HomeScore);
    Object->SetStringField(TEXT("AwayTeam"), Result.AwayTeamId.ToString());
    Object->SetNumberField(TEXT("AwayScore"), Result.AwayScore);
    Object->SetNumberField(TEXT("Plays"), Result.Plays);
    Object->SetBoolField(TEXT("GameFinished"), Result.bGameFinished);
    Object->SetArrayField(TEXT("MissingDataFiles"), ToJsonStrings(Result.MissingDataFiles));
    Object->SetArrayField(TEXT("Failures"), ToJsonStrings(Result.Failures));

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Object, Writer);
    return Json;
}

void UPSPackagedSmokeTest::RegisterCommandLineHook()
{
    if (FParse::Param(FCommandLine::Get(), PSPackagedSmokeTestPrivate::SwitchName))
    {
        FCoreDelegates::OnPostEngineInit.AddStatic(&UPSPackagedSmokeTest::RunFromCommandLine);
    }
}

void UPSPackagedSmokeTest::RunFromCommandLine()
{
    int32 Seed = DefaultSeed;
    FString ReportPath;
    if (!ParseCommandLine(FCommandLine::Get(), Seed, ReportPath))
    {
        return;
    }
    if (GIsEditor || IsRunningCommandlet())
    {
        UE_LOG(LogTemp, Warning, TEXT("PSSmokeTest: -PSSmokeTest runs in a packaged game (or -game), not in the editor or a commandlet; ignored."));
        return;
    }

    const FPSSmokeTestResult Result = GetDefault<UPSPackagedSmokeTest>()->Run(Seed, FString());
    if (Result.bPassed)
    {
        UE_LOG(LogTemp, Display, TEXT("%s"), *FormatResultLine(Result));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("%s"), *FormatResultLine(Result));
    }

    bool bReported = true;
    if (!ReportPath.IsEmpty())
    {
        bReported = FFileHelper::SaveStringToFile(ToJson(Result), *ReportPath);
        if (!bReported)
        {
            UE_LOG(LogTemp, Error, TEXT("PSSmokeTest: could not write the report to %s"), *ReportPath);
        }
    }

    // Exit now, before the engine starts the game and loads a map: the code is the result, and a
    // forced exit is the one every platform hands back to the caller.
    if (GLog)
    {
        GLog->Flush();
    }
    const uint8 ExitCode = (Result.bPassed && bReported) ? 0 : 1;
    FPlatformMisc::RequestExitWithStatus(true, ExitCode);
}
