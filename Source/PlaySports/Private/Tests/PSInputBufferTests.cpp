// PSInputBufferTests.cpp -- Epic 104.4 (input buffering against commitment windows)
//
// Tests covered:
//   1. The buffer data: it loads and validates, every move and pass button is buffered, a
//      broken table is caught, and the catalog answers which action a key means in a context.
//   2. Carrier moves wait out commitment: a move pressed during another's commitment fires when
//      it ends, the newest press wins, a press near the end of a cooldown fires after it, an
//      early press is dropped when its window passes, a press waiting can't help passes straight
//      through, and letting the player go drops what waits.
//   3. Pass buttons wait for the ball: a slot pressed and released before the passer holds the
//      ball throws once he does; a press older than its window is dropped with its release; a
//      hold is timed from the physical press; a press whose context went away is dropped.
//   4. A press just before its context comes on counts there: a slot key pressed in the frame
//      before Passing replaces PreSnap throws on release, while the hike key and a stale press
//      are never replayed as throws.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSCarrierInputComponent.h"
#include "PSCarrierMoveComponent.h"
#include "PSInputBufferComponent.h"
#include "PSInputConfig.h"
#include "PSOffenseController.h"
#include "PSPassingComponent.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSInputBufferTests
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

    /** An offensive pawn at Location under an AI controller. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Agility = 80.f, float Strength = 60.f)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
        Attributes.Agility = Agility;
        Attributes.Strength = Strength;
        Attributes.Speed = 80.f;
        Attributes.Awareness = 100.f;
        Attributes.Stamina = 100.f;
        Pawn->InitializePlayer(Attributes);
        if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    static APSPlayerController* SpawnPlayerController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    static void GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams))
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
    }

    /** A press and its release, the way the controller broadcasts a tap. */
    static void Tap(APSPlayerController* Controller, FName ActionId)
    {
        Controller->OnCatalogActionStarted.Broadcast(ActionId);
        Controller->OnCatalogActionCompleted.Broadcast(ActionId);
    }

    static const FPSCarrierMoveDef* FindMove(const FPSCarrierMoveCatalog& Catalog, EPSCarrierMove Move)
    {
        return Catalog.Moves.FindByPredicate([Move](const FPSCarrierMoveDef& Def) { return Def.Move == Move; });
    }

    static float FindBufferSeconds(const FInputBufferTuningRow& Tuning, FName ActionId)
    {
        const FPSInputBufferDef* Def = Tuning.Actions.FindByPredicate([ActionId](const FPSInputBufferDef& Candidate) { return Candidate.ActionId == ActionId; });
        return Def ? Def->BufferSeconds : 0.f;
    }

    /** The passer's full arm (APSPlayerPawn::ThrowPass: 1500 cm/s plus 15 per Strength point). */
    static float FullArm(float Strength)
    {
        return 1500.f + Strength * 15.f;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The buffer data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputBufferDataTest,
    "PlaySports.Input.BufferTuningValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputBufferDataTest::RunTest(const FString& Parameters)
{
    UPSInputBufferComponent* Buffer = NewObject<UPSInputBufferComponent>();
    TestTrue(TEXT("Data/input_buffer.json loads"), Buffer->LoadTuningFromJson(UPSInputBufferComponent::GetDefaultTuningPath()));
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    Input->LoadDefaults();
    for (const FString& Problem : UPSInputBufferComponent::ValidateTuning(Buffer->GetTuning(), &Input->Catalog))
    {
        AddError(FString::Printf(TEXT("input_buffer.json: %s"), *Problem));
    }

    // Every action with a busy target is buffered: the moves and the passer's buttons.
    UPSCarrierMoveComponent* Moves = NewObject<UPSCarrierMoveComponent>();
    for (const FPSCarrierMoveDef& Def : Moves->GetCatalog().Moves)
    {
        TestTrue(*FString::Printf(TEXT("The %s button is buffered"), *Def.ActionId.ToString()), Buffer->IsBuffered(Def.ActionId));
    }
    UPSPassingComponent* Passing = NewObject<UPSPassingComponent>();
    for (const FName& SlotAction : Passing->GetTuning().SlotActions)
    {
        TestTrue(*FString::Printf(TEXT("The %s button is buffered"), *SlotAction.ToString()), Buffer->IsBuffered(SlotAction));
    }
    TestTrue(TEXT("The pump fake is buffered"), Buffer->IsBuffered(Passing->GetTuning().PumpFakeAction));
    TestFalse(TEXT("Confirm (the hike) is not"), Buffer->IsBuffered(TEXT("Confirm")));

    FInputBufferTuningRow Broken = Buffer->GetTuning();
    Broken.MaxQueued = 0;
    if (Broken.Actions.Num() > 0)
    {
        const FPSInputBufferDef Copy = Broken.Actions[0];
        Broken.Actions.Add(Copy);
    }
    FPSInputBufferDef Stick;
    Stick.ActionId = TEXT("Move");
    Broken.Actions.Add(Stick);
    TestTrue(TEXT("Validation catches no queue, a repeated action and a non-button action"),
        UPSInputBufferComponent::ValidateTuning(Broken, &Input->Catalog).Num() >= 3);

    // What a key means where it was pressed decides whether a press may be carried.
    const FKey FaceBottom(TEXT("Gamepad_FaceButton_Bottom"));
    TestEqual(TEXT("A on the field confirms (hikes)"), Input->FindActionForKey(FaceBottom, TEXT("OnField")), FName(TEXT("Confirm")));
    TestEqual(TEXT("...and means nothing in PreSnap"), Input->FindActionForKey(FaceBottom, TEXT("PreSnap")), FName(NAME_None));
    TestEqual(TEXT("...and throws to slot 5 in Passing"), Input->FindActionForKey(FaceBottom, TEXT("Passing")), FName(TEXT("PassTarget5")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Carrier moves wait out commitment
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputBufferCommitmentTest,
    "PlaySports.Input.BufferWaitsOutCommitment",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputBufferCommitmentTest::RunTest(const FString& Parameters)
{
    using namespace PSInputBufferTests;

    UWorld* World = CreateTestWorld();
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    APSPlayerPawn* Carrier = World ? SpawnPlayer(World, EPlayerRole::RunningBack, TEXT("RB"), FVector(0.f, 0.f, 100.f), 80.f, 60.f) : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Carrier"), Carrier))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Carrier->GainPossession();
    TestTrue(TEXT("The human takes the carrier"), Controller->TakeControlOf(Carrier));
    Controller->GetCarrierInputComponent()->BindToController();
    Controller->SetDepthContext(TEXT("BallCarrier"));

    UPSCarrierMoveComponent* Moves = Carrier->GetCarrierMoveComponent();
    UPSInputBufferComponent* Buffer = Controller->GetInputBufferComponent();
    const FPSCarrierMoveDef* Truck = FindMove(Moves->GetCatalog(), EPSCarrierMove::Truck);
    const FPSCarrierMoveDef* Juke = FindMove(Moves->GetCatalog(), EPSCarrierMove::Juke);
    const FPSCarrierMoveDef* Spin = FindMove(Moves->GetCatalog(), EPSCarrierMove::Spin);
    const FPSCarrierMoveDef* StiffArm = FindMove(Moves->GetCatalog(), EPSCarrierMove::StiffArm);
    if (!TestTrue(TEXT("Truck, juke, spin and stiff-arm are defined"), Truck && Juke && Spin && StiffArm))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FInputBufferTuningRow& Tuning = Buffer->GetTuning();
    const float JukeHalf = FindBufferSeconds(Tuning, Juke->ActionId) * 0.5f;
    const float StiffArmHalf = FindBufferSeconds(Tuning, StiffArm->ActionId) * 0.5f;
    const float LateHalf = FMath::Min(FindBufferSeconds(Tuning, Spin->ActionId), FindBufferSeconds(Tuning, StiffArm->ActionId)) * 0.5f;
    // The scenarios below press inside a buffer window before a commitment or cooldown ends,
    // and expect one press to wait at a time.
    if (!TestTrue(TEXT("The data fits the scenarios (one press waits, windows open, commitments longer than half a window, cooldowns longer than commitment plus a window)"),
        Tuning.MaxQueued == 1 && JukeHalf > 0.f && LateHalf > 0.f && Truck->CommitSeconds > JukeHalf && Juke->CommitSeconds > LateHalf
        && StiffArm->CooldownSeconds > StiffArm->CommitSeconds + StiffArmHalf * 2.f + 0.02f))
    {
        DestroyTestWorld(World);
        return false;
    }

    auto Step = [Moves, Buffer](float Seconds)
    {
        Moves->AdvanceTime(Seconds);
        Buffer->AdvanceTime(Seconds);
    };

    // A juke pressed late in the truck's commitment waits for it, then goes at once.
    Tap(Controller, Truck->ActionId);
    TestEqual(TEXT("The carrier trucks"), Moves->GetActiveMove(), EPSCarrierMove::Truck);
    TestTrue(TEXT("...committed to it"), Moves->IsCommitted());
    Step(Truck->CommitSeconds - JukeHalf);
    Tap(Controller, Juke->ActionId);
    TestEqual(TEXT("A juke pressed during the commitment doesn't cut the truck short"), Moves->GetActiveMove(), EPSCarrierMove::Truck);
    TestTrue(TEXT("...it waits in the buffer"), Buffer->IsQueued(Juke->ActionId));
    Step(JukeHalf + 0.01f);
    TestEqual(TEXT("...and fires the moment the commitment ends"), Moves->GetActiveMove(), EPSCarrierMove::Juke);
    TestFalse(TEXT("...leaving the buffer"), Buffer->IsQueued(Juke->ActionId));

    // Two presses during the juke's commitment: the newest wins.
    Step(Juke->CommitSeconds - LateHalf);
    Tap(Controller, Spin->ActionId);
    Tap(Controller, StiffArm->ActionId);
    TestTrue(TEXT("The newest press waits"), Buffer->IsQueued(StiffArm->ActionId));
    TestFalse(TEXT("...pushing out the older one"), Buffer->IsQueued(Spin->ActionId));
    TestEqual(TEXT("...so one press waits"), Buffer->GetQueuedCount(), 1);
    Step(LateHalf + 0.01f);
    TestEqual(TEXT("...and it goes when the juke's commitment ends"), Moves->GetActiveMove(), EPSCarrierMove::StiffArm);

    // A press near the end of the stiff-arm's cooldown fires as it ends.
    Step(StiffArm->CooldownSeconds - StiffArmHalf);
    Tap(Controller, StiffArm->ActionId);
    TestTrue(TEXT("A stiff-arm pressed as its cooldown ends waits"), Buffer->IsQueued(StiffArm->ActionId));
    Step(StiffArmHalf + 0.01f);
    TestEqual(TEXT("...and fires when it's over"), Moves->GetActiveMove(), EPSCarrierMove::StiffArm);

    // A press too early in the cooldown is dropped when its window passes, and never fires.
    Tap(Controller, StiffArm->ActionId);
    TestTrue(TEXT("A stiff-arm pressed straight after another waits"), Buffer->IsQueued(StiffArm->ActionId));
    Step(StiffArmHalf * 2.f + 0.01f);
    TestFalse(TEXT("...until its window passes"), Buffer->IsQueued(StiffArm->ActionId));
    Step(StiffArm->CooldownSeconds);
    TestEqual(TEXT("...and it never fires"), Moves->GetActiveMove(), EPSCarrierMove::None);

    // Waiting can't help a carrier with no stamina: the press goes straight through and fails.
    Carrier->CurrentStamina = 0.f;
    Tap(Controller, Truck->ActionId);
    TestFalse(TEXT("A tired carrier's press doesn't wait"), Buffer->IsQueued(Truck->ActionId));
    TestEqual(TEXT("...and does nothing"), Moves->GetActiveMove(), EPSCarrierMove::None);
    Carrier->ResetFatigue();

    // Letting the player go drops what waits for him.
    Tap(Controller, Truck->ActionId);
    Tap(Controller, Juke->ActionId);
    TestTrue(TEXT("A juke waits on the truck again"), Buffer->IsQueued(Juke->ActionId));
    Controller->ReleaseControl();
    TestEqual(TEXT("Letting the carrier go empties the buffer"), Buffer->GetQueuedCount(), 0);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Pass buttons wait for the ball
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputBufferPassTest,
    "PlaySports.Input.BufferHoldsPassForTheBall",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputBufferPassTest::RunTest(const FString& Parameters)
{
    using namespace PSInputBufferTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const float Strength = 50.f;
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f), 50.f, Strength);
    APSPlayerPawn* LeftWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_L"), FVector(1000.f, -900.f, 100.f));
    APSPlayerPawn* RightWR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_R"), FVector(1000.f, 900.f, 100.f));
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR_L"), LeftWR) || !TestNotNull(TEXT("WR_R"), RightWR))
    {
        DestroyTestWorld(World);
        return false;
    }

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    UPSPassingComponent* Passing = Controller->GetPassingComponent();
    UPSInputBufferComponent* Buffer = Controller->GetInputBufferComponent();
    const FPassingInputTuningRow& PassTuning = Passing->GetTuning();
    if (!TestTrue(TEXT("Two pass slots"), PassTuning.SlotActions.Num() >= 2))
    {
        Bus->OnThrowMC.Remove(Handle);
        DestroyTestWorld(World);
        return false;
    }
    const FName SlotOne = PassTuning.SlotActions[0];
    const FName SlotTwo = PassTuning.SlotActions[1];
    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    Passing->BindToController();
    // The Passing context is on while the snap is still in the air to him.
    Controller->SetDepthContext(TEXT("Passing"));

    // Slot 1 tapped before the ball arrives: it waits, then throws a touch pass.
    Tap(Controller, SlotOne);
    TestTrue(TEXT("Without the ball, the slot press waits"), Buffer->IsQueued(SlotOne));
    TestEqual(TEXT("...nothing is thrown"), Throws.Num(), 0);
    GiveBall(World, QB);
    Buffer->AdvanceTime(0.02f);
    if (TestEqual(TEXT("The ball arrives and the throw goes"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to slot 1, the left receiver"), Throws[0].TargetReceiverName, FString(TEXT("WR_L")));
        TestTrue(TEXT("...a touch pass, as tapped"), FMath::IsNearlyEqual(Throws[0].LaunchSpeed, FullArm(Strength) * PassTuning.TouchSpeedScale, 1.f));
    }

    // A press older than its window is dropped, and its release throws nothing.
    Controller->OnCatalogActionStarted.Broadcast(SlotTwo);
    TestTrue(TEXT("Ball gone, slot 2 waits"), Buffer->IsQueued(SlotTwo));
    Buffer->AdvanceTime(FindBufferSeconds(Buffer->GetTuning(), SlotTwo) + 0.01f);
    TestFalse(TEXT("...until its window passes"), Buffer->IsQueued(SlotTwo));
    GiveBall(World, QB);
    Buffer->AdvanceTime(0.02f);
    Controller->OnCatalogActionCompleted.Broadcast(SlotTwo);
    TestEqual(TEXT("...and neither the ball's arrival nor the release throws"), Throws.Num(), 1);

    // A hold is timed by the buffer from the press: past BulletHoldSeconds it's a bullet.
    Controller->OnCatalogActionStarted.Broadcast(SlotTwo);
    TestFalse(TEXT("With the ball, the press goes straight through"), Buffer->IsQueued(SlotTwo));
    Buffer->AdvanceTime(PassTuning.BulletHoldSeconds + 0.05f);
    Controller->OnCatalogActionCompleted.Broadcast(SlotTwo);
    if (TestEqual(TEXT("Releasing throws"), Throws.Num(), 2))
    {
        TestEqual(TEXT("...to slot 2"), Throws[1].TargetReceiverName, FString(TEXT("WR_R")));
        TestTrue(TEXT("...a bullet after a hold"), FMath::IsNearlyEqual(Throws[1].LaunchSpeed, FullArm(Strength), 1.f));
    }

    // A press whose context goes away while it waits is dropped.
    Tap(Controller, SlotOne);
    TestTrue(TEXT("Ball gone again, slot 1 waits"), Buffer->IsQueued(SlotOne));
    Controller->SetDepthContext(TEXT("BallCarrier"));
    GiveBall(World, QB);
    Buffer->AdvanceTime(0.02f);
    TestFalse(TEXT("Out of the Passing context the waiting press is dropped"), Buffer->IsQueued(SlotOne));
    TestEqual(TEXT("...and throws nothing"), Throws.Num(), 2);

    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- A press just before its context comes on
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputBufferCarryTest,
    "PlaySports.Input.BufferCarriesPressIntoNewContext",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputBufferCarryTest::RunTest(const FString& Parameters)
{
    using namespace PSInputBufferTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const float Strength = 50.f;
    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-200.f, 0.f, 100.f), 50.f, Strength);
    if (!TestNotNull(TEXT("QB"), QB))
    {
        DestroyTestWorld(World);
        return false;
    }
    // A receiver for every slot, left to right.
    const TArray<FString> ReceiverNames = { TEXT("WR_1"), TEXT("WR_2"), TEXT("WR_3"), TEXT("WR_4"), TEXT("WR_5") };
    for (int32 Index = 0; Index < ReceiverNames.Num(); ++Index)
    {
        SpawnPlayer(World, EPlayerRole::WideReceiver, *ReceiverNames[Index], FVector(1000.f, -1000.f + 500.f * Index, 100.f));
    }

    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });

    UPSPassingComponent* Passing = Controller->GetPassingComponent();
    UPSInputBufferComponent* Buffer = Controller->GetInputBufferComponent();
    UPSPlayContextComponent* Context = Controller->GetPlayContextComponent();
    UPSInputConfig* Config = Controller->GetInputConfig();
    const FPassingInputTuningRow& PassTuning = Passing->GetTuning();
    TestTrue(TEXT("The human takes the QB"), Controller->TakeControlOf(QB));
    GiveBall(World, QB);
    Passing->BindToController();
    Context->BindToBus();
    Context->Refresh();
    TestEqual(TEXT("Before the snap: PreSnap"), Controller->GetDepthContext(), FName(TEXT("PreSnap")));

    // From the catalog: slot keys that mean nothing before the snap (a fresh and a stale one),
    // and the hike key that also throws in Passing.
    const TArray<FName> PreSnapStack = { Controller->GameplayContextId, Context->PreSnapContextId };
    auto MeansNothingPreSnap = [Config, &PreSnapStack](const FKey& Key)
    {
        for (const FName& ContextId : PreSnapStack)
        {
            if (!Config->FindActionForKey(Key, ContextId).IsNone())
            {
                return false;
            }
        }
        return true;
    };
    int32 FreshSlot = INDEX_NONE;
    int32 StaleSlot = INDEX_NONE;
    FKey FreshKey;
    FKey StaleKey;
    for (int32 Slot = 0; Slot < PassTuning.SlotActions.Num() && StaleSlot == INDEX_NONE; ++Slot)
    {
        for (const FKey& Key : Config->GetKeysFor(PassTuning.SlotActions[Slot], Context->PassingContextId))
        {
            if (!MeansNothingPreSnap(Key))
            {
                continue;
            }
            if (FreshSlot == INDEX_NONE)
            {
                FreshSlot = Slot;
                FreshKey = Key;
            }
            else
            {
                StaleSlot = Slot;
                StaleKey = Key;
            }
            break;
        }
    }
    FKey HikeKey;
    FName HikeSlotAction;
    for (const FKey& Key : Config->GetKeysFor(TEXT("Confirm"), Controller->GameplayContextId))
    {
        const FName InPassing = Config->FindActionForKey(Key, Context->PassingContextId);
        if (PassTuning.SlotActions.Contains(InPassing))
        {
            HikeKey = Key;
            HikeSlotAction = InPassing;
            break;
        }
    }
    if (!TestTrue(TEXT("Two pass slots have keys that mean nothing before the snap"), FreshSlot != INDEX_NONE && StaleSlot != INDEX_NONE))
    {
        Bus->OnThrowMC.Remove(Handle);
        DestroyTestWorld(World);
        return false;
    }
    const FName FreshAction = PassTuning.SlotActions[FreshSlot];
    const FName StaleAction = PassTuning.SlotActions[StaleSlot];

    // The fresh slot key went down a frame before the snap; the stale one a second before; the
    // hike key too, if one throws in Passing.
    TMap<FKey, float> KeysDown;
    KeysDown.Add(FreshKey, 0.02f);
    KeysDown.Add(StaleKey, 1.f);
    if (HikeKey.IsValid())
    {
        KeysDown.Add(HikeKey, 0.02f);
    }
    else
    {
        AddInfo(TEXT("No hike key throws in Passing; the hike case is not exercised."));
    }
    Buffer->KeyStateQuery.BindLambda([&KeysDown](const FKey& Key)
    {
        const float* SecondsDown = KeysDown.Find(Key);
        return SecondsDown ? *SecondsDown : -1.f;
    });

    FPSTelemetrySnapEvent Snap;
    Snap.LineOfScrimmage = FVector::ZeroVector;
    Bus->PublishSnap(Snap);
    Context->Refresh();
    TestEqual(TEXT("Snapped with the ball behind the line: Passing"), Controller->GetDepthContext(), Context->PassingContextId);
    TestTrue(TEXT("The slot pressed a frame before the snap counts in Passing"), Buffer->IsActionDown(FreshAction));
    TestFalse(TEXT("A press older than its window doesn't"), Buffer->IsActionDown(StaleAction));
    if (HikeKey.IsValid())
    {
        TestFalse(TEXT("The hike press is never replayed as a throw"), Buffer->IsActionDown(HikeSlotAction));
    }
    TestEqual(TEXT("Nothing is thrown while the button is held"), Throws.Num(), 0);

    // Releasing the fresh key throws to its slot, timed from the physical press: a tap, so touch.
    KeysDown.Add(FreshKey, -1.f);
    Buffer->AdvanceTime(0.05f);
    if (TestEqual(TEXT("Releasing the carried key throws"), Throws.Num(), 1))
    {
        TestEqual(TEXT("...to its slot"), Throws[0].TargetReceiverName, ReceiverNames[FreshSlot]);
        TestTrue(TEXT("...a touch pass after a tap"), FMath::IsNearlyEqual(Throws[0].LaunchSpeed, FullArm(Strength) * PassTuning.TouchSpeedScale, 1.f));
    }
    TestFalse(TEXT("...and the press is over"), Buffer->IsActionDown(FreshAction));

    // Letting go of the other keys throws nothing more.
    GiveBall(World, QB);
    KeysDown.Add(StaleKey, -1.f);
    if (HikeKey.IsValid())
    {
        KeysDown.Add(HikeKey, -1.f);
    }
    Buffer->AdvanceTime(0.05f);
    TestEqual(TEXT("The stale and hike keys' releases throw nothing"), Throws.Num(), 1);

    Buffer->KeyStateQuery.Unbind();
    Bus->OnThrowMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
