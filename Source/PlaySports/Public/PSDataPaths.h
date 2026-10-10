// PSDataPaths.h - Epic 145.2: the data files the game reads, and where a packaged build finds them
#pragma once

#include "CoreMinimal.h"

/**
 * Every loader reads its data by path under the project directory: FPaths::ProjectDir() /
 * "Data/<file>". A packaged build stages the whole Data/ folder as loose files at the same place
 * under its own project directory (PlaySports.Build.cs: a runtime dependency on
 * $(ProjectDir)/Data/...), so the same paths resolve in the editor and in a packaged build.
 *
 * GetDefaultDataFiles is the staging audit: every data file a loader reads by default.
 * tools/tests/test_data_staging.py fails when a loader's default path is missing from it, and
 * the packaged smoke test (UPSPackagedSmokeTest) fails when a packaged build lacks one.
 */
namespace PSDataPaths
{
    /** The folder a packaged build stages, relative to the project directory: "Data". */
    PLAYSPORTS_API const TCHAR* GetDataFolder();

    /** The league's default team file, relative to the project directory:
     *  "Data/sample_teams.json". Each of its rows names that team's roster file. */
    PLAYSPORTS_API const TCHAR* GetDefaultTeamsFile();

    /** Every data file the game reads by default, relative to the project directory
     *  ("Data/formations.json"), in name order. */
    PLAYSPORTS_API const TArray<FString>& GetDefaultDataFiles();

    /** RelativePath under ProjectDir, or under FPaths::ProjectDir() when ProjectDir is empty,
     *  with "." and ".." collapsed. */
    PLAYSPORTS_API FString Resolve(const FString& RelativePath, const FString& ProjectDir);

    /** Whether RelativePath is inside the staged data folder ("Data/...", no ".."), so a packaged
     *  build carries it. */
    PLAYSPORTS_API bool IsStaged(const FString& RelativePath);

    /** The roster files the default team file (Data/sample_teams.json) under ProjectDir names
     *  (each team's RosterDataTablePath), read through UPSDataIngestion; empty when it can't be
     *  read. */
    PLAYSPORTS_API TArray<FString> GetTeamRosterFiles(const FString& ProjectDir);

    /** Every default data file, and every roster file the default team file names, that
     *  doesn't resolve to a file under ProjectDir (FPaths::ProjectDir() when empty), relative to
     *  the project directory: empty when a build carries all of them. */
    PLAYSPORTS_API TArray<FString> FindMissingDataFiles(const FString& ProjectDir);
}
