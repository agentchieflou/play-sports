// PSInputTests.cpp -- Epic 142 (input catalog) and Epic 126 (Enhanced Input foundation)
//
// Tests covered:
//   1. The authored catalog (Data/input_actions.json) loads through UPSDataIngestion, validates
//      clean, and gives every action a keyboard/mouse and a gamepad binding in every context it
//      is declared for (Specs/Input_Architecture.md section 4).
//   2. Validate() catches the mistakes the catalog can contain, with actionable messages.
//   3. Possessing an APSPlayerPawn applies the gameplay mapping context; unpossessing removes it.
//   4. The Move action is bound on the controller's Enhanced Input component, and a Move value
//      injected into that binding's handler reaches the pawn's movement input.
//
// Headless worlds have no ULocalPlayer, so the Enhanced Input subsystem and the device ->
// action dispatch are not exercised here; tests 3 and 4 observe the controller's own context
// stack and its Move handler, which is where the subsystem and the binding hand off.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSInputConfig.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSInputTests
{
    static bool AnyErrorContains(const TArray<FString>& Errors, const FString& First, const FString& Second)
    {
        for (const FString& Error : Errors)
        {
            if (Error.Contains(First) && Error.Contains(Second))
            {
                return true;
            }
        }
        return false;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The authored catalog covers keyboard and gamepad in every context
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputCatalogCoverageTest,
    "PlaySports.Input.CatalogCoversKeyboardAndGamepad",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputCatalogCoverageTest::RunTest(const FString& Parameters)
{
    UPSInputConfig* Config = NewObject<UPSInputConfig>();
    const bool bLoaded = Config->LoadFromJson(UPSInputConfig::GetDefaultCatalogPath());
    TestTrue(TEXT("Data/input_actions.json loads through UPSDataIngestion"), bLoaded);
    if (!bLoaded)
    {
        return false;
    }

    // Validate() is the section 4 check: every action has a keyboard/mouse and a gamepad
    // binding in every context it is declared for, and every key is a real engine key.
    const TArray<FString> Errors = Config->Validate();
    for (const FString& Error : Errors)
    {
        AddError(FString::Printf(TEXT("input_actions.json: %s"), *Error));
    }
    TestEqual(TEXT("Catalog validates with no errors"), Errors.Num(), 0);

    // Epic 126's catalog must be present in the gameplay context.
    const FName OnField(TEXT("OnField"));
    const TArray<FName> GameplayActions = { TEXT("Move"), TEXT("Sprint"), TEXT("Confirm"), TEXT("Cancel"), TEXT("SwitchPlayer") };
    for (const FName& ActionId : GameplayActions)
    {
        const FPSInputActionDef* Def = Config->Catalog.Actions.FindByPredicate(
            [&ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; });
        if (TestNotNull(*FString::Printf(TEXT("Catalog declares %s"), *ActionId.ToString()), Def))
        {
            TestTrue(*FString::Printf(TEXT("%s lives in the OnField context"), *ActionId.ToString()), Def->Contexts.Contains(OnField));
        }
    }

    // Every declared action and context resolves to a runtime Enhanced Input object.
    for (const FPSInputContextDef& ContextDef : Config->Catalog.Contexts)
    {
        TestNotNull(*FString::Printf(TEXT("Context %s builds a UInputMappingContext"), *ContextDef.ContextId.ToString()),
            Config->FindContext(ContextDef.ContextId));
    }
    for (const FPSInputActionDef& ActionDef : Config->Catalog.Actions)
    {
        const UInputAction* Action = Config->FindAction(ActionDef.ActionId);
        if (TestNotNull(*FString::Printf(TEXT("Action %s builds a UInputAction"), *ActionDef.ActionId.ToString()), Action))
        {
            TestTrue(*FString::Printf(TEXT("Action %s keeps its declared value type"), *ActionDef.ActionId.ToString()),
                Action->ValueType == ActionDef.ValueType);
            TestTrue(*FString::Printf(TEXT("Action %s reverse-looks-up to its ID"), *ActionDef.ActionId.ToString()),
                Config->FindActionId(Action) == ActionDef.ActionId);
        }
    }

    TestTrue(TEXT("Move is a 2D axis"), Config->FindAction(TEXT("Move")) && Config->FindAction(TEXT("Move"))->ValueType == EInputActionValueType::Axis2D);
    TestTrue(TEXT("OnField outranks World"), Config->GetContextPriority(OnField) > Config->GetContextPriority(TEXT("World")));
    TestEqual(TEXT("Unknown context has no priority"), Config->GetContextPriority(TEXT("NoSuchContext")), static_cast<int32>(INDEX_NONE));

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Validate() reports each class of catalog mistake
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputCatalogValidationTest,
    "PlaySports.Input.ValidateReportsCatalogMistakes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputCatalogValidationTest::RunTest(const FString& Parameters)
{
    UPSInputConfig* Config = NewObject<UPSInputConfig>();

    FPSInputContextDef OnField;
    OnField.ContextId = TEXT("OnField");
    Config->Catalog.Contexts.Add(OnField);

    FPSInputKeyBinding Enter;
    Enter.Key = TEXT("Enter");
    FPSInputKeyBinding FaceBottom;
    FaceBottom.Key = TEXT("Gamepad_FaceButton_Bottom");
    FPSInputKeyBinding BadKey;
    BadKey.Key = TEXT("NotARealKey");

    // Keyboard only -> missing gamepad binding.
    FPSInputActionDef KeyboardOnly;
    KeyboardOnly.ActionId = TEXT("KeyboardOnly");
    KeyboardOnly.Contexts.Add(TEXT("OnField"));
    KeyboardOnly.Bindings.Add(Enter);
    Config->Catalog.Actions.Add(KeyboardOnly);

    // Reuses Enter in the same context -> one button with two meanings; also references an
    // unknown context and an invalid key.
    FPSInputActionDef Clashing;
    Clashing.ActionId = TEXT("Clashing");
    Clashing.Contexts.Add(TEXT("OnField"));
    Clashing.Contexts.Add(TEXT("NoSuchContext"));
    Clashing.Bindings.Add(Enter);
    Clashing.Bindings.Add(FaceBottom);
    Clashing.Bindings.Add(BadKey);
    Config->Catalog.Actions.Add(Clashing);

    // No contexts at all -> unreachable.
    FPSInputActionDef Orphan;
    Orphan.ActionId = TEXT("Orphan");
    Orphan.Bindings.Add(Enter);
    Orphan.Bindings.Add(FaceBottom);
    Config->Catalog.Actions.Add(Orphan);

    const TArray<FString> Errors = Config->Validate();
    TestTrue(TEXT("Missing gamepad binding is reported for its context"),
        PSInputTests::AnyErrorContains(Errors, TEXT("KeyboardOnly"), TEXT("no gamepad binding in context 'OnField'")));
    TestTrue(TEXT("A key bound twice in one context is reported"),
        PSInputTests::AnyErrorContains(Errors, TEXT("Clashing"), TEXT("already bound to 'KeyboardOnly'")));
    TestTrue(TEXT("An unknown context reference is reported"),
        PSInputTests::AnyErrorContains(Errors, TEXT("Clashing"), TEXT("unknown context 'NoSuchContext'")));
    TestTrue(TEXT("An invalid engine key is reported"),
        PSInputTests::AnyErrorContains(Errors, TEXT("Clashing"), TEXT("'NotARealKey' is not a valid engine key")));
    TestTrue(TEXT("An action with no context is reported"),
        PSInputTests::AnyErrorContains(Errors, TEXT("Orphan"), TEXT("declares no context")));

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Possession applies (and unpossession removes) the gameplay context
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputPossessionContextTest,
    "PlaySports.Input.PossessionAppliesGameplayContext",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputPossessionContextTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("Test world created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(
        APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(
        APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("Pawn spawned"), Pawn);
    TestNotNull(TEXT("PSPlayerController spawned"), Controller);

    if (Pawn && Controller)
    {
        const FName GameplayContext = Controller->GameplayContextId;
        TestNotNull(TEXT("Gameplay context exists in the catalog"), Controller->GetInputConfig()->FindContext(GameplayContext));
        TestFalse(TEXT("No gameplay context before possession"), Controller->IsInputContextActive(GameplayContext));

        Controller->Possess(Pawn);
        TestTrue(TEXT("Controller possesses the pawn"), Pawn->GetController() == Controller);
        TestTrue(TEXT("Possessing an APSPlayerPawn applies the gameplay context"), Controller->IsInputContextActive(GameplayContext));

        Controller->UnPossess();
        TestFalse(TEXT("Unpossessing removes the gameplay context"), Controller->IsInputContextActive(GameplayContext));
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- An injected Move value reaches the pawn's movement input
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputMoveReachesPawnTest,
    "PlaySports.Input.InjectedMoveReachesPawn",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputMoveReachesPawnTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("Test world created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(
        APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(
        APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("Pawn spawned"), Pawn);
    TestNotNull(TEXT("PSPlayerController spawned"), Controller);

    if (Pawn && Controller)
    {
        // The catalog's Move action is bound (Triggered) on an Enhanced Input component.
        UEnhancedInputComponent* InputComp = NewObject<UEnhancedInputComponent>(Controller);
        Controller->BindCatalogActions(*InputComp);
        const UInputAction* MoveAction = Controller->GetInputConfig()->FindAction(Controller->MoveActionId);
        TestNotNull(TEXT("Catalog has a Move action"), MoveAction);

        bool bMoveBound = false;
        for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : InputComp->GetActionEventBindings())
        {
            if (Binding && Binding->GetAction() == MoveAction && Binding->GetTriggerEvent() == ETriggerEvent::Triggered)
            {
                bMoveBound = true;
            }
        }
        TestTrue(TEXT("Move is bound on the controller's Enhanced Input component"), bMoveBound);

        Controller->Possess(Pawn);
        Pawn->ConsumeMovementInputVector();

        // Forward (Y) on a pawn spawned facing +X: control yaw is 0 after possession.
        Controller->HandleMove(FInputActionValue(FVector2D(0.f, 1.f)));
        const FVector Forward = Pawn->ConsumeMovementInputVector();
        TestTrue(TEXT("Forward Move input reaches the pawn along +X"), Forward.Equals(FVector(1.f, 0.f, 0.f), 0.01f));

        // Right (X) maps onto +Y.
        Controller->HandleMove(FInputActionValue(FVector2D(1.f, 0.f)));
        const FVector Right = Pawn->ConsumeMovementInputVector();
        TestTrue(TEXT("Right Move input reaches the pawn along +Y"), Right.Equals(FVector(0.f, 1.f, 0.f), 0.01f));

        // A zero value adds nothing.
        Controller->HandleMove(FInputActionValue(FVector2D::ZeroVector));
        TestTrue(TEXT("Zero Move input leaves no pending movement"), Pawn->ConsumeMovementInputVector().IsNearlyZero());

        Controller->UnPossess();
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
