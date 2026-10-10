// PSCarrierMoveTests.cpp -- Epic 104.2 (the ball carrier's move set)
//
// Tests covered:
//   1. The move set data: all six moves, sound numbers, each on a BallCarrier button.
//   2. Moves are attribute-gated, cost stamina, respect their window and cooldown, need the
//      ball, and change the carrier's velocity the moment they start.
//   3. Moves change the tackle odds: the extracted tackle formula, scaled by the active move,
//      and a slide gives the carrier up.
//   4. The buttons: a BallCarrier action reaches the controlled pawn's move, the Move stick
//      picks a juke's side, and a button that names no move does nothing.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBallResolutionHelpers.h"
#include "PSCarrierInputComponent.h"
#include "PSCarrierMoveComponent.h"
#include "PSInputConfig.h"
#include "PSOffenseController.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "InputActionValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCarrierMoveTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** A running back with the ball, running upfield at 600 cm/s. */
    static APSPlayerPawn* SpawnCarrier(UWorld* World, float Agility, float Strength)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = TEXT("RB");
        Attributes.DisplayName = TEXT("RB");
        Attributes.Role = EPlayerRole::RunningBack;
        Attributes.Agility = Agility;
        Attributes.Strength = Strength;
        Attributes.Speed = 80.f;
        Attributes.Stamina = 100.f;
        Pawn->InitializePlayer(Attributes);
        Pawn->GainPossession();
        Pawn->GetFloatingMovementComponent()->Velocity = FVector(600.f, 0.f, 0.f);
        if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    static const FPSCarrierMoveDef* FindDef(const FPSCarrierMoveCatalog& Catalog, EPSCarrierMove Move)
    {
        return Catalog.Moves.FindByPredicate([Move](const FPSCarrierMoveDef& Def) { return Def.Move == Move; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The move set data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCarrierMoveDataTest,
    "PlaySports.Input.CarrierMoveSet",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCarrierMoveDataTest::RunTest(const FString& Parameters)
{
    using namespace PSCarrierMoveTests;

    UPSCarrierMoveComponent* Moves = NewObject<UPSCarrierMoveComponent>();
    TestTrue(TEXT("Data/carrier_moves.json loads"), Moves->LoadCatalogFromJson(UPSCarrierMoveComponent::GetDefaultCatalogPath()));
    const FPSCarrierMoveCatalog& Catalog = Moves->GetCatalog();
    for (const FString& Problem : UPSCarrierMoveComponent::ValidateCatalog(Catalog))
    {
        AddError(FString::Printf(TEXT("carrier_moves.json: %s"), *Problem));
    }

    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    Input->LoadDefaults();
    const TArray<EPSCarrierMove> All = { EPSCarrierMove::Juke, EPSCarrierMove::Spin, EPSCarrierMove::Truck, EPSCarrierMove::StiffArm, EPSCarrierMove::Hurdle, EPSCarrierMove::Slide };
    for (const EPSCarrierMove Move : All)
    {
        const FPSCarrierMoveDef* Def = FindDef(Catalog, Move);
        const FString Name = UEnum::GetValueAsString(Move);
        if (!TestNotNull(*FString::Printf(TEXT("%s is in the move set"), *Name), Def))
        {
            continue;
        }
        const FPSInputActionDef* Action = Input->Catalog.Actions.FindByPredicate(
            [Def](const FPSInputActionDef& Candidate) { return Candidate.ActionId == Def->ActionId; });
        TestTrue(*FString::Printf(TEXT("%s has a button in the BallCarrier context"), *Name),
            Action && Action->Contexts.Contains(FName(TEXT("BallCarrier"))) && Action->ValueType == EInputActionValueType::Boolean);
        TestEqual(*FString::Printf(TEXT("%s's button finds it"), *Name), Moves->FindMoveForAction(Def->ActionId), Move);
    }

    FPSCarrierMoveCatalog Broken = Catalog;
    if (Broken.Moves.Num() > 0)
    {
        const FPSCarrierMoveDef Copy = Broken.Moves[0];
        Broken.Moves.Add(Copy);
        Broken.Moves[0].Attribute = TEXT("Luck");
        TestTrue(TEXT("Validation catches a repeated move and an unknown attribute"), UPSCarrierMoveComponent::ValidateCatalog(Broken).Num() >= 2);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Gating, stamina, timing, physics
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCarrierMoveRulesTest,
    "PlaySports.Input.CarrierMovesGatedAndPhysical",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCarrierMoveRulesTest::RunTest(const FString& Parameters)
{
    using namespace PSCarrierMoveTests;

    UWorld* World = CreateTestWorld();
    APSPlayerPawn* Carrier = World ? SpawnCarrier(World, 80.f, 40.f) : nullptr;
    if (!TestNotNull(TEXT("Carrier"), Carrier))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSCarrierMoveComponent* Moves = Carrier->GetCarrierMoveComponent();
    const FPSCarrierMoveDef* Juke = FindDef(Moves->GetCatalog(), EPSCarrierMove::Juke);
    const FPSCarrierMoveDef* Truck = FindDef(Moves->GetCatalog(), EPSCarrierMove::Truck);
    const FPSCarrierMoveDef* Spin = FindDef(Moves->GetCatalog(), EPSCarrierMove::Spin);
    if (!TestTrue(TEXT("Juke, truck and spin are defined"), Juke && Truck && Spin))
    {
        DestroyTestWorld(World);
        return false;
    }

    // A juke to the left.
    const float StaminaBefore = Carrier->CurrentStamina;
    TestTrue(TEXT("The carrier jukes"), Moves->TryMove(EPSCarrierMove::Juke, FVector2D(-1.f, 0.f)));
    TestEqual(TEXT("...and the juke is on"), Moves->GetActiveMove(), EPSCarrierMove::Juke);
    TestTrue(TEXT("...costing stamina"), FMath::IsNearlyEqual(Carrier->CurrentStamina, StaminaBefore - Juke->StaminaCost));
    const FVector Expected(600.f * Juke->SpeedRetained, -Juke->LateralSpeed, 0.f);
    TestTrue(TEXT("...cutting him left at once"), Carrier->GetFloatingMovementComponent()->Velocity.Equals(Expected, 1.f));
    TestTrue(TEXT("While it lasts, tackles succeed less often (in proportion to his Agility)"),
        FMath::IsNearlyEqual(Moves->GetTackleChanceMultiplier(), FMath::Lerp(1.f, Juke->TackleChanceScale, 0.8f)));
    TestFalse(TEXT("No second juke on cooldown"), Moves->TryMove(EPSCarrierMove::Juke, FVector2D::ZeroVector));

    Moves->AdvanceTime(Juke->WindowSeconds + 0.01f);
    TestFalse(TEXT("The window closes"), Moves->IsMoveActive());
    TestEqual(TEXT("...and the tackle odds are back to normal"), Moves->GetTackleChanceMultiplier(), 1.f);
    Moves->AdvanceTime(Juke->CooldownSeconds);
    TestTrue(TEXT("After the cooldown he can juke again"), Moves->TryMove(EPSCarrierMove::Juke, FVector2D(1.f, 0.f)));

    // Gated by rating and stamina.
    TestFalse(TEXT("Strength 40 can't truck (the move needs more)"), Moves->TryMove(EPSCarrierMove::Truck, FVector2D::ZeroVector));
    Carrier->CurrentStamina = Spin->StaminaCost * 0.5f;
    TestFalse(TEXT("Too tired to spin"), Moves->TryMove(EPSCarrierMove::Spin, FVector2D::ZeroVector));
    Carrier->ResetFatigue();
    TestTrue(TEXT("Rested, he spins"), Moves->TryMove(EPSCarrierMove::Spin, FVector2D::ZeroVector));

    // Only with the ball.
    Carrier->LosePossession();
    Moves->AdvanceTime(0.01f);
    TestFalse(TEXT("Losing the ball ends the move"), Moves->IsMoveActive());
    Moves->AdvanceTime(5.f);
    TestFalse(TEXT("No moves without the ball"), Moves->TryMove(EPSCarrierMove::Juke, FVector2D::ZeroVector));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Tackle odds and the slide
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCarrierMoveTackleTest,
    "PlaySports.Input.CarrierMovesChangeTackleOdds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCarrierMoveTackleTest::RunTest(const FString& Parameters)
{
    using namespace PSCarrierMoveTests;

    // The formula ResolveTackle used inline, unchanged by the extraction.
    FPlayerAttributes Runner;
    Runner.Strength = 40.f;
    Runner.Agility = 80.f;
    FPlayerAttributes Tackler;
    Tackler.Strength = 70.f;
    const float Base = PSBallResolutionHelpers::ComputeTackleChance(Runner, Tackler, 600.f, 500.f);
    const float DefenderPower = 70.f * 0.5f + 500.f * 0.1f;
    const float CarrierPower = 40.f * 0.3f + 80.f * 0.3f + 600.f * 0.05f;
    TestTrue(TEXT("The tackle chance matches the old inline formula"), FMath::IsNearlyEqual(Base, 0.5f + (DefenderPower - CarrierPower) * 0.005f));
    TestTrue(TEXT("A move scales it"), FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeTackleChance(Runner, Tackler, 600.f, 500.f, 0.5f), Base * 0.5f));
    TestTrue(TEXT("...and a good enough move beats the floor"), PSBallResolutionHelpers::ComputeTackleChance(Runner, Tackler, 600.f, 500.f, 0.1f) < FTackleTuningRow().TackleChanceMin);
    FPlayerAttributes Mauler;
    Mauler.Strength = 100.f;
    TestTrue(TEXT("Nothing pushes it past the maximum"),
        PSBallResolutionHelpers::ComputeTackleChance(Runner, Mauler, 0.f, 2000.f, 3.f) <= FTackleTuningRow().TackleChanceMax);

    // The slide.
    UWorld* World = CreateTestWorld();
    APSPlayerPawn* Carrier = World ? SpawnCarrier(World, 50.f, 50.f) : nullptr;
    if (!TestNotNull(TEXT("Carrier"), Carrier))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSCarrierMoveComponent* Moves = Carrier->GetCarrierMoveComponent();
    TestFalse(TEXT("Running, he hasn't given up"), Moves->HasGivenUp());
    TestTrue(TEXT("He slides"), Moves->TryMove(EPSCarrierMove::Slide, FVector2D::ZeroVector));
    TestTrue(TEXT("...giving himself up"), Moves->HasGivenUp());
    TestTrue(TEXT("...and stopping"), Carrier->GetFloatingMovementComponent()->Velocity.IsNearlyZero());
    TestFalse(TEXT("No juke out of a slide"), Moves->TryMove(EPSCarrierMove::Juke, FVector2D::ZeroVector));
    Carrier->LosePossession();
    Moves->AdvanceTime(0.01f);
    TestFalse(TEXT("The slide ends with the ball out of his hands"), Moves->HasGivenUp());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The buttons
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCarrierButtonsTest,
    "PlaySports.Input.CarrierButtons",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCarrierButtonsTest::RunTest(const FString& Parameters)
{
    using namespace PSCarrierMoveTests;

    UWorld* World = CreateTestWorld();
    APSPlayerPawn* Carrier = World ? SpawnCarrier(World, 80.f, 60.f) : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Carrier"), Carrier) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSCarrierInputComponent* Buttons = Controller->GetCarrierInputComponent();
    UPSCarrierMoveComponent* Moves = Carrier->GetCarrierMoveComponent();
    const FPSCarrierMoveDef* Truck = FindDef(Moves->GetCatalog(), EPSCarrierMove::Truck);
    const FPSCarrierMoveDef* Juke = FindDef(Moves->GetCatalog(), EPSCarrierMove::Juke);
    if (!TestTrue(TEXT("Truck and juke are defined"), Truck && Juke))
    {
        DestroyTestWorld(World);
        return false;
    }

    TestFalse(TEXT("Controlling nobody, the buttons do nothing"), Buttons->PressMoveAction(Truck->ActionId));
    TestTrue(TEXT("The human takes the carrier"), Controller->TakeControlOf(Carrier));
    TestTrue(TEXT("The truck button trucks"), Buttons->PressMoveAction(Truck->ActionId));
    TestEqual(TEXT("...the controlled carrier's move"), Moves->GetActiveMove(), EPSCarrierMove::Truck);
    TestFalse(TEXT("A button that names no move does nothing"), Buttons->PressMoveAction(TEXT("PumpFake")));

    // Through the controller's events, with the stick held right.
    Buttons->BindToController();
    Moves->AdvanceTime(5.f);
    Carrier->GetFloatingMovementComponent()->Velocity = FVector(600.f, 0.f, 0.f);
    Controller->HandleMove(FInputActionValue(FVector2D(1.f, 0.f)));
    Controller->OnCatalogActionStarted.Broadcast(Juke->ActionId);
    TestEqual(TEXT("The juke button jukes"), Moves->GetActiveMove(), EPSCarrierMove::Juke);
    TestTrue(TEXT("...to the stick's side, right"), Carrier->GetFloatingMovementComponent()->Velocity.Y > 0.f);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
