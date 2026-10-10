// AgenticLinkProbeSubsystem.cpp - Epic 25: a subsystem an agent calls to check it reaches subsystems
#include "AgenticLinkProbeSubsystem.h"
#include "Engine/World.h"

FString UAgenticLinkProbeSubsystem::Echo(const FString& Text)
{
    ++EchoCount;
    const UWorld* World = GetWorld();
    return FString::Printf(TEXT("%s from %s"), *Text, World ? *World->GetName() : TEXT("no world"));
}
