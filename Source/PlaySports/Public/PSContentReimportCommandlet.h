#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PSContentReimportCommandlet.generated.h"

/** Re-imports all known Data/ JSON content (players, teams, playbook, routes,
 *  league config) through UPSDataIngestion/UPSPlaybookIngestion in one action,
 *  validating each file first and logging actionable errors (Epic 21). Run via:
 *  UnrealEditor-Cmd.exe play-sports.uproject -run=PSContentReimport */
UCLASS()
class PLAYSPORTS_API UPSContentReimportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;
};
