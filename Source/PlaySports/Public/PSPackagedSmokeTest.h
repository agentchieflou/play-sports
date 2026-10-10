// PSPackagedSmokeTest.h - Epic 145.3: a packaged build plays a scripted full game and exits with the score
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPackagedSmokeTest.generated.h"

/** What one smoke run found: logged as one line, and written as JSON to the report file CI reads. */
USTRUCT(BlueprintType)
struct FPSSmokeTestResult
{
    GENERATED_BODY()

    /** Every check passed: the build carries its data, both teams loaded and the game was played
     *  to its end. */
    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    bool bPassed = false;

    /** The quick sim's seed: the same build plays the same game for the same seed. */
    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    int32 Seed = 0;

    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    FName HomeTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    FName AwayTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    int32 HomeScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    int32 AwayScore = 0;

    /** Plays the simulation resolved, kicks included. */
    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    int32 Plays = 0;

    /** The game reached the end of the fourth quarter (FPSQuickSimResult::bFinished). */
    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    bool bGameFinished = false;

    /** Data files the build doesn't carry (PSDataPaths::FindMissingDataFiles). */
    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    TArray<FString> MissingDataFiles;

    /** One line per failed check; empty on a pass. */
    UPROPERTY(BlueprintReadOnly, Category = "SmokeTest")
    TArray<FString> Failures;
};

/**
 * UPSPackagedSmokeTest proves a packaged build boots and plays football, with no map and no player
 * (Epic 145.3). With -PSSmokeTest on a game's command line, it runs once the engine is up and
 * before any map loads, so it needs no level:
 *   1. every default data file resolves (PSDataPaths: the build's staged Data/ folder);
 *   2. the league's first two teams load from the default team file with their rosters
 *      (UPSMatchSetup), the teams Play Now picks when given no options;
 *   3. they play a full game on the quick sim (UPSQuickSimRunner, the path
 *      PlaySports.Gym.ScriptedFullGame plays), seeded, so a build plays the same game each run;
 *   4. it logs one result line (FormatResultLine), writes the result as JSON to the file
 *      -PSSmokeTestReport=<path> names, and exits with 0 on a pass and 1 on a failure.
 * -PSSmokeTestSeed=<n> picks the seed (DefaultSeed otherwise; 0 rolls on the global stream). The
 * editor and commandlets ignore the switch.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSPackagedSmokeTest : public UObject
{
    GENERATED_BODY()

public:
    /** The seed a run uses unless -PSSmokeTestSeed= gives another. */
    static constexpr int32 DefaultSeed = 145;

    /** Reads CommandLine: true when -PSSmokeTest is on it, with OutSeed from -PSSmokeTestSeed=
     *  (DefaultSeed without it) and OutReportPath from -PSSmokeTestReport= (empty without it). */
    static bool ParseCommandLine(const TCHAR* CommandLine, int32& OutSeed, FString& OutReportPath);

    /** Runs the checks and plays the game with Seed, reading the data under ProjectDir
     *  (FPaths::ProjectDir() when empty). */
    UFUNCTION(BlueprintCallable, Category = "SmokeTest")
    FPSSmokeTestResult Run(int32 Seed, const FString& ProjectDir) const;

    /** The line the log gets, for CI and people to read:
     *  "PSSmokeTest: PASS seed=145 home=Falcons 24 away=Hawks 17 plays=150 finished=true",
     *  with FAIL and " | " plus the failures after it on a failure. */
    static FString FormatResultLine(const FPSSmokeTestResult& Result);

    /** Result as a JSON object: Passed, Seed, HomeTeam, HomeScore, AwayTeam, AwayScore, Plays,
     *  GameFinished, MissingDataFiles and Failures. */
    static FString ToJson(const FPSSmokeTestResult& Result);

    /** When -PSSmokeTest is on the command line, has the run happen once the engine has started
     *  (FCoreDelegates::OnPostEngineInit, before the first map loads). PlaySports' module calls
     *  it at startup. */
    static void RegisterCommandLineHook();

private:
    /** The run the switch asks for: runs, logs, writes the report and exits with the result. */
    static void RunFromCommandLine();
};
