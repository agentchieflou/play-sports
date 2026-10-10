// AgenticLinkProbeSubsystem.h - Epic 25: a subsystem an agent calls to check it reaches subsystems
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AgenticLinkProbeSubsystem.generated.h"

/**
 * UAgenticLinkProbeSubsystem is the bridge's own world subsystem: an agent calls Echo through
 * call_function {"subsystem": "AgenticLinkProbeSubsystem"} to check it reaches the open world's
 * subsystems before it calls a game's (Epic 82's hooks live on one). It holds nothing else.
 */
UCLASS()
class AGENTICLINK_API UAgenticLinkProbeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** "<Text> from <world name>", and counts the call. */
    UFUNCTION(BlueprintCallable, Category = "AgenticLink")
    FString Echo(const FString& Text);

    /** How many times Echo has been called in this world. */
    UFUNCTION(BlueprintPure, Category = "AgenticLink")
    int32 GetEchoCount() const { return EchoCount; }

private:
    int32 EchoCount = 0;
};
