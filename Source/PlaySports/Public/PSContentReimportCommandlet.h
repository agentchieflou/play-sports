#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PSContentReimportCommandlet.generated.h"

/** Re-imports all Data/ content (league config, teams, every team's roster, routes, playbook)
 *  through UPSDataIngestion/UPSPlaybookIngestion in one action, validating each file first and
 *  logging actionable errors (Epic 21; Epic 125 follows the references). Run via:
 *  UnrealEditor-Cmd.exe play-sports.uproject -run=PSContentReimport
 *  or python tools/content.py import. */
UCLASS()
class PLAYSPORTS_API UPSContentReimportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;

    /**
     * Validates and loads the content under ProjectDir/Data the way the game does. It follows the
     * league config's TeamsDataTablePath to the teams and each team's RosterDataTablePath to its
     * roster, and checks what no single file can: PlayerIds unique across the league, every play's
     * RouteId in the route library. OutErrors entries read "<file> - <problem>"; OutLoaded has one
     * line per file loaded. Logs nothing, so a headless test can run it. True with no errors.
     */
    static bool ReimportAll(const FString& ProjectDir, TArray<FString>& OutErrors, TArray<FString>& OutLoaded);
};
