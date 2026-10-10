#include "PSAIScenarioRunner.h"
#include "PSAIDecisionLog.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSAIScenarioRunnerPrivate
{
    /** A rostered player for Player: every rating at his Rating, his DNA, full stamina. */
    FPlayerAttributes MakeAttributes(const FPSAIScenarioPlayer& Player)
    {
        FPlayerAttributes Attributes;
        Attributes.PlayerId = Player.PlayerId;
        Attributes.DisplayName = Player.PlayerId.ToString();
        Attributes.Role = Player.Role;
        Attributes.Speed = Player.Rating;
        Attributes.Agility = Player.Rating;
        Attributes.Strength = Player.Rating;
        Attributes.Acceleration = Player.Rating;
        Attributes.Awareness = Player.Rating;
        Attributes.Stamina = 100.f;
        Attributes.DNA = Player.DNA;
        return Attributes;
    }

    bool HasPlayer(const FPSAIScenario& Scenario, FName PlayerId)
    {
        return Scenario.Players.ContainsByPredicate([PlayerId](const FPSAIScenarioPlayer& Player) { return Player.PlayerId == PlayerId; });
    }
}

FString UPSAIScenarioRunner::GetDefaultScenariosPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ai_scenarios.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSAIScenarioRunner::LoadScenariosFromJson(const FString& JsonFilePath)
{
    FPSAIScenarioCatalog Loaded;
    if (!NewObject<UPSDataIngestion>(this)->LoadAIScenariosFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSAIScenarioRunner: Could not load scenarios from %s."), *JsonFilePath);
        return false;
    }
    Catalog = Loaded;
    return true;
}

TArray<FString> UPSAIScenarioRunner::ValidateScenario(const FPSAIScenario& Scenario)
{
    using namespace PSAIScenarioRunnerPrivate;

    TArray<FString> Problems;
    const FString Name = Scenario.ScenarioId.ToString();
    if (Scenario.Players.Num() == 0)
    {
        Problems.Add(FString::Printf(TEXT("%s: places no players"), *Name));
    }
    if (Scenario.Expectations.Num() == 0)
    {
        Problems.Add(FString::Printf(TEXT("%s: expects nothing"), *Name));
    }
    if (Scenario.Steps < 1 || Scenario.StepSeconds <= 0.f)
    {
        Problems.Add(FString::Printf(TEXT("%s: needs a decision cycle (Steps 1 or more, StepSeconds above 0)"), *Name));
    }
    TSet<FName> Seen;
    for (const FPSAIScenarioPlayer& Player : Scenario.Players)
    {
        if (Player.PlayerId.IsNone() || Seen.Contains(Player.PlayerId))
        {
            Problems.Add(FString::Printf(TEXT("%s: player '%s' needs a PlayerId of his own"), *Name, *Player.PlayerId.ToString()));
        }
        Seen.Add(Player.PlayerId);
        if (!Player.CoverTarget.IsNone() && !HasPlayer(Scenario, Player.CoverTarget))
        {
            Problems.Add(FString::Printf(TEXT("%s: %s covers '%s', who isn't in the scenario"), *Name, *Player.PlayerId.ToString(), *Player.CoverTarget.ToString()));
        }
    }
    for (const FPSAIScenarioExpectation& Expectation : Scenario.Expectations)
    {
        if (!HasPlayer(Scenario, Expectation.PlayerId))
        {
            Problems.Add(FString::Printf(TEXT("%s: expects something of '%s', who isn't in the scenario"), *Name, *Expectation.PlayerId.ToString()));
        }
        if (Expectation.Action.IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s: the expectation of %s names no Action"), *Name, *Expectation.PlayerId.ToString()));
        }
    }
    return Problems;
}

FPSAIScenarioResult UPSAIScenarioRunner::RunScenario(UWorld* World, const FPSAIScenario& Scenario)
{
    using namespace PSAIScenarioRunnerPrivate;

    FPSAIScenarioResult Result;
    Result.ScenarioId = Scenario.ScenarioId;
    Result.Failures = ValidateScenario(Scenario);
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSAIDecisionLog* DecisionLog = UPSAIDecisionLog::Get(World);
    if (!Bus || !DecisionLog)
    {
        Result.Failures.Add(TEXT("The world has no telemetry bus or decision log"));
    }
    if (Result.Failures.Num() > 0)
    {
        return Result;
    }
    DecisionLog->SetLogging(true);

    // The players, each under his side's AI.
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    TArray<APSPlayerPawn*> Placed;
    for (const FPSAIScenarioPlayer& Player : Scenario.Players)
    {
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Player.Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            Result.Failures.Add(FString::Printf(TEXT("%s could not be placed"), *Player.PlayerId.ToString()));
            Placed.Add(nullptr);
            continue;
        }
        Pawn->InitializePlayer(MakeAttributes(Player));
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Player.Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
                AI->GetDefenderAI()->BindToBus();
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Player.Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        if (Player.bHasBall)
        {
            if (APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Player.Location, FRotator::ZeroRotator, SpawnParams))
            {
                Ball->AttachToCarrier(Pawn, TEXT("HandSocket"));
                Pawn->GainPossession();
            }
        }
        Placed.Add(Pawn);
    }

    // The snap hands out the CPU's called plays; the scenario's own routes and assignments go
    // on after it, and the offense's call with them.
    FPSTelemetrySnapEvent Snap;
    Snap.Down = Scenario.Down;
    Snap.Distance = Scenario.Distance;
    Bus->PublishSnap(Snap);
    for (int32 Index = 0; Index < Placed.Num(); ++Index)
    {
        const FPSAIScenarioPlayer& Player = Scenario.Players[Index];
        APSPlayerPawn* Pawn = Placed[Index];
        if (APSOffenseController* Offense = Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr)
        {
            TArray<FVector> Waypoints;
            for (const FVector& Offset : Player.Route)
            {
                Waypoints.Add(Player.Location + Offset);
            }
            Offense->SetAssignedRoute(Waypoints);
            Offense->GetRouteRunner()->ClearRoutePlan();
        }
        else if (APSDefenseController* Defense = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr)
        {
            const int32 Covered = Scenario.Players.IndexOfByPredicate([&Player](const FPSAIScenarioPlayer& Other) { return Other.PlayerId == Player.CoverTarget; });
            AActor* CoverTarget = Placed.IsValidIndex(Covered) ? Placed[Covered] : nullptr;
            Defense->SetAssignment(Player.Assignment, CoverTarget, Player.ZoneOffset.IsZero() ? FVector::ZeroVector : Player.Location + Player.ZoneOffset);
        }
    }
    FPSTelemetryPlayCallEvent Call;
    Call.bOffense = true;
    Call.PlayCategory = Scenario.OffenseCategory;
    Bus->PublishPlayCall(Call);

    // The decision cycles, every AI player in the scenario's order.
    for (int32 Step = 0; Step < Scenario.Steps; ++Step)
    {
        for (APSPlayerPawn* Pawn : Placed)
        {
            if (const APSOffenseController* Offense = Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr)
            {
                Offense->GetSkillAI()->TickAI(Scenario.StepSeconds);
            }
            else if (const APSDefenseController* Defense = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr)
            {
                Defense->GetDefenderAI()->TickAI(Scenario.StepSeconds);
            }
        }
    }

    // What each decided, against what was expected.
    for (const FPSAIScenarioPlayer& Player : Scenario.Players)
    {
        FPSAIDecisionRecord Decision;
        if (DecisionLog->GetLatest(Player.PlayerId, Decision))
        {
            Result.Decisions.Add(Decision);
        }
    }
    for (const FPSAIScenarioExpectation& Expectation : Scenario.Expectations)
    {
        const FString Who = Expectation.PlayerId.ToString();
        FPSAIDecisionRecord Decision;
        if (!DecisionLog->GetLatest(Expectation.PlayerId, Decision))
        {
            Result.Failures.Add(FString::Printf(TEXT("%s made no decision"), *Who));
            continue;
        }
        if (!Decision.Action.Equals(Expectation.Action, ESearchCase::IgnoreCase))
        {
            Result.Failures.Add(FString::Printf(TEXT("%s: expected %s, decided %s (%s)"), *Who, *Expectation.Action, *Decision.Action, *Decision.Reason));
        }
        if (!Expectation.Target.IsEmpty() && !Decision.Target.Equals(Expectation.Target, ESearchCase::IgnoreCase))
        {
            Result.Failures.Add(FString::Printf(TEXT("%s: expected to go at %s, went at '%s'"), *Who, *Expectation.Target, *Decision.Target));
        }
        const FVector Wanted = Expectation.Heading.GetSafeNormal2D();
        if (!Wanted.IsZero())
        {
            const FVector Went = Decision.Direction.GetSafeNormal2D();
            const float Degrees = Went.IsZero() ? 180.f : FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Went, Wanted), -1.f, 1.f)));
            if (Degrees > Expectation.MaxAngleDegrees)
            {
                Result.Failures.Add(FString::Printf(TEXT("%s: went %.0f degrees off the expected heading (at most %.0f)"), *Who, Degrees, Expectation.MaxAngleDegrees));
            }
        }
    }
    Result.bPassed = Result.Failures.Num() == 0;
    return Result;
}

FPSAIScenarioResult UPSAIScenarioRunner::RunInNewWorld(const FPSAIScenario& Scenario)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!World || !GEngine)
    {
        FPSAIScenarioResult Result;
        Result.ScenarioId = Scenario.ScenarioId;
        Result.Failures.Add(TEXT("Could not create a world to run it in"));
        return Result;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);
    const FPSAIScenarioResult Result = RunScenario(World, Scenario);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return Result;
}
