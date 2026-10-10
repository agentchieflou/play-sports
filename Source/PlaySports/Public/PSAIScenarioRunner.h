// PSAIScenarioRunner.h - Epic 85: place the players, run one decision cycle, assert what the AI decided
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSAIDecisionTypes.h"
#include "PSAIScenarioRunner.generated.h"

class UWorld;

/**
 * UPSAIScenarioRunner plays scripted AI scenarios (Epic 85.4, extending Epic 24's gym): it places
 * the scenario's players in a world -- each under his side's AI controller, with his ratings,
 * DNA and the ball if he has it -- snaps the ball, gives each his route or assignment and the
 * offense its call, runs the scenario's decision cycles on every AI player, and checks each
 * expectation against that player's latest decision in UPSAIDecisionLog (what he did, at whom,
 * which way he went).
 *
 * Scenarios are data (Data/ai_scenarios.json, read through UPSDataIngestion). The automation test
 * PlaySports.Gym.AIScenarios runs every shipped one; an agent or the gym can run its own.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSAIScenarioRunner : public UObject
{
    GENERATED_BODY()

public:
    static FString GetDefaultScenariosPath();

    /** Replaces the scenarios with JsonFilePath's. False on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "AIScenario")
    bool LoadScenariosFromJson(const FString& JsonFilePath);

    UFUNCTION(BlueprintPure, Category = "AIScenario")
    const TArray<FPSAIScenario>& GetScenarios() const { return Catalog.Scenarios; }

    /** Problems with a scenario, one line each (empty when sound): no players or no
     *  expectations, a PlayerId used twice, a cover target or an expectation naming nobody in it,
     *  or no decision cycle to run. */
    static TArray<FString> ValidateScenario(const FPSAIScenario& Scenario);

    /** Plays Scenario out in World (which needs its telemetry bus and decision log) and checks
     *  its expectations. The players stay in the world. */
    UFUNCTION(BlueprintCallable, Category = "AIScenario")
    FPSAIScenarioResult RunScenario(UWorld* World, const FPSAIScenario& Scenario);

    /** The same in a game world of its own, made for it and destroyed after. */
    UFUNCTION(BlueprintCallable, Category = "AIScenario")
    FPSAIScenarioResult RunInNewWorld(const FPSAIScenario& Scenario);

private:
    UPROPERTY(Transient)
    FPSAIScenarioCatalog Catalog;
};
