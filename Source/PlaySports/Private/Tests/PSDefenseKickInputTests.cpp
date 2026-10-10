// PSDefenseKickInputTests.cpp -- Epic 104.5 (kick meter and defensive interaction inputs)
//
// Tests covered:
//   1. The data: the technique and kick meter tuning load and validate, their buttons are in the
//      catalog's PreSnap, Defense and Kicking contexts with gamepad glyphs, and the meter's roll
//      and the extracted fumble formula give the expected numbers.
//   2. Jump-snap: a press just before the snap bursts the human defender off the line; an
//      earlier one (or a second press after it) is offside and flagged by the play simulation;
//      nothing after the snap or for an offensive player.
//   3. Strip: the button opens a strip attempt that trades tackle odds for fumble odds, wears
//      off, waits out its cooldown through the input buffer, and needs a defender without the
//      ball.
//   4. The kick meter: a kick phase lines the human kicker up (the Kicking context comes on and
//      the play simulation waits), the meter's hold-release-press makes the kick, and the
//      simulation takes its roll into the special-teams model (Epic 75): a perfect field goal is
//      good, a botched one misses, a perfect punt goes the model's longest and a shank its
//      shortest, a perfect kickoff is a touchback; without a human the CPU kicks on time.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBallResolutionHelpers.h"
#include "PSDefenderTechniqueComponent.h"
#include "PSDefenseController.h"
#include "PSDefenseInputComponent.h"
#include "PSInputBufferComponent.h"
#include "PSInputConfig.h"
#include "PSKickMeterComponent.h"
#include "PSOffenseController.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSPlaySimulation.h"
#include "PSSpecialTeamsModel.h"
#include "PSTelemetryBus.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDefenseKickInputTests
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

    /** A pawn at Location under its side's AI controller. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Strength = 80.f)
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
        Attributes.Strength = Strength;
        Pawn->InitializePlayer(Attributes);
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
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

    static void PublishPhase(UPSTelemetryBus* Bus, const TCHAR* OldPhase, const TCHAR* NewPhase)
    {
        FPSTelemetryPhaseChangeEvent Event;
        Event.OldPhase = OldPhase;
        Event.NewPhase = NewPhase;
        Bus->PublishPhaseChange(Event);
    }

    static void PublishSnap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenseKickDataTest,
    "PlaySports.Input.DefenseAndKickTuningValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenseKickDataTest::RunTest(const FString& Parameters)
{
    UPSDefenderTechniqueComponent* Technique = NewObject<UPSDefenderTechniqueComponent>();
    TestTrue(TEXT("Data/defensive_techniques.json loads"), Technique->LoadTuningFromJson(UPSDefenderTechniqueComponent::GetDefaultTuningPath()));
    for (const FString& Problem : UPSDefenderTechniqueComponent::ValidateTuning(Technique->GetTuning()))
    {
        AddError(FString::Printf(TEXT("defensive_techniques.json: %s"), *Problem));
    }
    UPSKickMeterComponent* Meter = NewObject<UPSKickMeterComponent>();
    TestTrue(TEXT("Data/kick_meter.json loads"), Meter->LoadTuningFromJson(UPSKickMeterComponent::GetDefaultTuningPath()));
    for (const FString& Problem : UPSKickMeterComponent::ValidateTuning(Meter->GetTuning()))
    {
        AddError(FString::Printf(TEXT("kick_meter.json: %s"), *Problem));
    }

    // Each button is a Boolean in its context, with a gamepad glyph.
    UPSInputConfig* Input = NewObject<UPSInputConfig>();
    Input->LoadDefaults();
    struct FButtonCase
    {
        FName ActionId;
        FName ContextId;
    };
    const FButtonCase Buttons[] = {
        { Technique->GetTuning().JumpSnapAction, FName(TEXT("DefensePreSnap")) },
        { Technique->GetTuning().StripAction, FName(TEXT("Defense")) },
        { Meter->GetTuning().KickAction, FName(TEXT("Kicking")) } };
    for (const FButtonCase& Button : Buttons)
    {
        const FPSInputActionDef* Action = Input->Catalog.Actions.FindByPredicate(
            [&Button](const FPSInputActionDef& Candidate) { return Candidate.ActionId == Button.ActionId; });
        TestTrue(*FString::Printf(TEXT("%s is a Boolean action in %s"), *Button.ActionId.ToString(), *Button.ContextId.ToString()),
            Action && Action->ValueType == EInputActionValueType::Boolean && Action->Contexts.Contains(Button.ContextId));
        FPSInputGlyph Glyph;
        TestTrue(*FString::Printf(TEXT("%s has a gamepad glyph"), *Button.ActionId.ToString()),
            Input->GetGlyphForAction(Button.ActionId, Button.ContextId, EPSInputDevice::Gamepad, Glyph));
    }
    TestTrue(TEXT("Kicking outranks OnField"), Input->GetContextPriority(TEXT("Kicking")) > Input->GetContextPriority(TEXT("OnField")));

    // The meter's roll: 0 for a full, straight kick; worse for missing power or a needle off center.
    const FKickMeterTuningRow& Kick = Meter->GetTuning();
    TestTrue(TEXT("A full, straight kick rolls 0"), FMath::IsNearlyZero(UPSKickMeterComponent::ComputeRoll(1.f, 0.f, Kick)));
    TestTrue(TEXT("Half power costs half the power weight"), FMath::IsNearlyEqual(UPSKickMeterComponent::ComputeRoll(0.5f, 0.f, Kick), Kick.PowerWeight * 0.5f));
    TestTrue(TEXT("A needle at either end costs the accuracy weight"),
        FMath::IsNearlyEqual(UPSKickMeterComponent::ComputeRoll(1.f, -1.f, Kick), FMath::Min(1.f, Kick.AccuracyWeight))
        && FMath::IsNearlyEqual(UPSKickMeterComponent::ComputeRoll(1.f, 1.f, Kick), FMath::Min(1.f, Kick.AccuracyWeight)));
    TestTrue(TEXT("The kick phases are Kickoff, Punt and FieldGoal"),
        UPSKickMeterComponent::IsKickPhase(TEXT("Kickoff")) && UPSKickMeterComponent::IsKickPhase(TEXT("Punt"))
        && UPSKickMeterComponent::IsKickPhase(TEXT("FieldGoal")) && !UPSKickMeterComponent::IsKickPhase(TEXT("PreSnap")));

    // The fumble formula ResolveTackle used inline, unchanged by the extraction, plus a strip.
    TestTrue(TEXT("Fumble chance at 500 cm/s matches the old inline formula"), FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeFumbleChance(500.f), 0.02f + 500.f * 0.0001f));
    TestTrue(TEXT("...standing still it is the base"), FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeFumbleChance(0.f), 0.02f));
    TestTrue(TEXT("...and it tops out at the old cap"), FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeFumbleChance(10000.f), 0.25f));
    TestTrue(TEXT("A strip adds on top of the cap"), FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeFumbleChance(10000.f, 0.3f), 0.55f));
    TestTrue(TEXT("...never past certainty"), PSBallResolutionHelpers::ComputeFumbleChance(10000.f, 5.f) <= 1.f);

    FDefensiveTechniqueTuningRow BrokenTechnique = Technique->GetTuning();
    BrokenTechnique.StripAction = BrokenTechnique.JumpSnapAction;
    BrokenTechnique.StripTackleScale = 2.f;
    TestTrue(TEXT("Technique validation catches a shared action and a scale above 1"), UPSDefenderTechniqueComponent::ValidateTuning(BrokenTechnique).Num() >= 2);
    FKickMeterTuningRow BrokenKick = Kick;
    BrokenKick.PowerFillSeconds = 0.f;
    BrokenKick.PowerWeight = 0.f;
    BrokenKick.AccuracyWeight = 0.f;
    TestTrue(TEXT("Kick meter validation catches a zero fill time and no weights"), UPSKickMeterComponent::ValidateTuning(BrokenKick).Num() >= 2);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Jump-snap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSJumpSnapTest,
    "PlaySports.Input.JumpSnapTimedAtTheSnap",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSJumpSnapTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenseKickInputTests;

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
    APSPlayerPawn* Lineman = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL"), FVector(100.f, 0.f, 100.f));
    APSPlayerPawn* Center = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL"), FVector(-100.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("DL"), Lineman) || !TestNotNull(TEXT("OL"), Center))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The play simulation hears the jump on the bus and flags an offside one.
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>(World);
    Sim->InitializeWithWorld(World);
    TArray<FPSTelemetryJumpSnapEvent> Jumps;
    const FDelegateHandle Handle = Bus->OnJumpSnapMC.AddLambda([&Jumps](const FPSTelemetryJumpSnapEvent& Event) { Jumps.Add(Event); });

    UPSDefenseInputComponent* Defense = Controller->GetDefenseInputComponent();
    Defense->BindToController();
    Defense->BindToBus();
    TestTrue(TEXT("The human takes the defensive lineman"), Controller->TakeControlOf(Lineman));
    const FDefensiveTechniqueTuningRow& Tuning = Lineman->GetDefenderTechniqueComponent()->GetTuning();
    UFloatingPawnMovement* Movement = Lineman->GetFloatingMovementComponent();
    Movement->Velocity = FVector::ZeroVector;

    // Pressed just before the snap: a clean jump, bursting him at the line.
    Controller->OnCatalogActionStarted.Broadcast(Tuning.JumpSnapAction);
    TestTrue(TEXT("The jump button times his move"), Defense->HasJumped());
    Defense->AdvanceTime(Tuning.JumpWindowSeconds * 0.5f);
    PublishSnap(Bus);
    if (TestEqual(TEXT("The jump goes on the bus at the snap"), Jumps.Num(), 1))
    {
        TestFalse(TEXT("...onside"), Jumps[0].bOffside);
        TestTrue(TEXT("...half a window early"), FMath::IsNearlyEqual(Jumps[0].LeadSeconds, Tuning.JumpWindowSeconds * 0.5f, 0.001f));
    }
    TestTrue(TEXT("He bursts toward the line of scrimmage"), FMath::IsNearlyEqual(Movement->Velocity.X, -Tuning.GetOffSpeed, 1.f));
    TestEqual(TEXT("No flag"), Sim->ActivePenalty, EPSPenaltyType::None);
    TestFalse(TEXT("After the snap the button does nothing"), Defense->PressJumpSnap());

    // The next play: pressed too early, and pressing again doesn't take it back. Offside.
    PublishPhase(Bus, TEXT("Scoring"), TEXT("PreSnap"));
    Movement->Velocity = FVector::ZeroVector;
    TestTrue(TEXT("A new play, a new jump"), Defense->PressJumpSnap());
    Defense->AdvanceTime(Tuning.JumpWindowSeconds + 0.1f);
    TestFalse(TEXT("A second press doesn't reset the first"), Defense->PressJumpSnap());
    PublishSnap(Bus);
    if (TestEqual(TEXT("The early jump goes on the bus"), Jumps.Num(), 2))
    {
        TestTrue(TEXT("...offside"), Jumps[1].bOffside);
    }
    TestTrue(TEXT("An offside jump gets no burst"), Movement->Velocity.IsNearlyZero());
    TestEqual(TEXT("The play simulation flags it"), Sim->ActivePenalty, EPSPenaltyType::Offsides);

    // An offensive player has no jump to time.
    PublishPhase(Bus, TEXT("Scoring"), TEXT("PreSnap"));
    TestTrue(TEXT("The human takes the center"), Controller->TakeControlOf(Center));
    TestFalse(TEXT("On offense the button does nothing"), Defense->PressJumpSnap());
    PublishSnap(Bus);
    TestEqual(TEXT("...and nothing goes on the bus"), Jumps.Num(), 2);

    Bus->OnJumpSnapMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Strip
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStripAttemptTest,
    "PlaySports.Input.StripTradesTackleForFumble",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStripAttemptTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenseKickInputTests;

    UWorld* World = CreateTestWorld();
    APSPlayerController* Controller = World ? SpawnPlayerController(World) : nullptr;
    const float Strength = 80.f;
    APSPlayerPawn* Safety = World ? SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("DB"), FVector(500.f, 0.f, 100.f), Strength) : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("DB"), Safety))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSDefenseInputComponent* Defense = Controller->GetDefenseInputComponent();
    UPSInputBufferComponent* Buffer = Controller->GetInputBufferComponent();
    UPSDefenderTechniqueComponent* Technique = Safety->GetDefenderTechniqueComponent();
    const FDefensiveTechniqueTuningRow& Tuning = Technique->GetTuning();
    TestTrue(TEXT("The human takes the safety"), Controller->TakeControlOf(Safety));
    Defense->BindToController();
    Controller->SetDepthContext(TEXT("Defense"));

    TestEqual(TEXT("Not stripping: tackles as usual"), Technique->GetTackleChanceScale(), 1.f);
    TestEqual(TEXT("...and no extra fumbles"), Technique->GetFumbleChanceBonus(), 0.f);

    Controller->OnCatalogActionStarted.Broadcast(Tuning.StripAction);
    TestTrue(TEXT("The strip button rips at the ball"), Technique->IsStripping());
    TestTrue(TEXT("...his tackles land less often"), FMath::IsNearlyEqual(Technique->GetTackleChanceScale(), Tuning.StripTackleScale));
    const float Bonus = Tuning.StripFumbleChance * Strength / 100.f;
    TestTrue(TEXT("...but force more fumbles, in proportion to his Strength"), FMath::IsNearlyEqual(Technique->GetFumbleChanceBonus(), Bonus));

    // What a tackle makes of it (UPSBallActionComponent::ResolveTackle's formulas).
    FPlayerAttributes Runner;
    Runner.Strength = 50.f;
    Runner.Agility = 50.f;
    FPlayerAttributes Tackler;
    Tackler.Strength = Strength;
    const float Plain = PSBallResolutionHelpers::ComputeTackleChance(Runner, Tackler, 500.f, 500.f);
    TestTrue(TEXT("A stripping tackle succeeds StripTackleScale as often"),
        FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeTackleChance(Runner, Tackler, 500.f, 500.f, Technique->GetTackleChanceScale()), Plain * Tuning.StripTackleScale));
    TestTrue(TEXT("...and fumbles that much more often"),
        FMath::IsNearlyEqual(PSBallResolutionHelpers::ComputeFumbleChance(500.f, Technique->GetFumbleChanceBonus()), PSBallResolutionHelpers::ComputeFumbleChance(500.f) + Bonus));

    // It wears off; a press during the cooldown waits in the buffer and fires as it ends.
    Technique->AdvanceTime(Tuning.StripWindowSeconds + 0.01f);
    TestFalse(TEXT("The attempt wears off"), Technique->IsStripping());
    TestEqual(TEXT("...and his tackles are back to normal"), Technique->GetTackleChanceScale(), 1.f);
    const float StripBuffer = Buffer->IsBuffered(Tuning.StripAction) ? FMath::Max(0.02f, Buffer->GetTuning().Actions.FindByPredicate(
        [&Tuning](const FPSInputBufferDef& Def) { return Def.ActionId == Tuning.StripAction; })->BufferSeconds) : 0.f;
    if (TestTrue(TEXT("The strip is buffered, with a window shorter than its cooldown"), StripBuffer > 0.f && StripBuffer < Tuning.StripCooldownSeconds - Tuning.StripWindowSeconds))
    {
        Technique->AdvanceTime(Tuning.StripCooldownSeconds - Tuning.StripWindowSeconds - 0.01f - StripBuffer * 0.5f);
        Controller->OnCatalogActionStarted.Broadcast(Tuning.StripAction);
        TestFalse(TEXT("Pressed as the cooldown ends, he doesn't strip yet"), Technique->IsStripping());
        TestTrue(TEXT("...the press waits"), Buffer->IsQueued(Tuning.StripAction));
        Technique->AdvanceTime(StripBuffer * 0.5f + 0.01f);
        Buffer->AdvanceTime(StripBuffer * 0.5f + 0.01f);
        TestTrue(TEXT("...and strips the moment it's over"), Technique->IsStripping());
    }

    // Only a defender without the ball strips.
    Technique->ResetTechniques();
    Safety->GainPossession();
    TestFalse(TEXT("With the ball (an interception) there's nothing to strip"), Defense->PressStrip());
    Safety->LosePossession();
    TestTrue(TEXT("Without it he can"), Defense->PressStrip());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The kick meter
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSKickMeterTest,
    "PlaySports.Input.KickMeterDrivesTheKick",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSKickMeterTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenseKickInputTests;

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
    APSPlayerPawn* Kicker = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("K"), FVector(-700.f, 0.f, 100.f));
    APSPlayerPawn* Returner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("PR"), FVector(3000.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("Kicker"), Kicker) || !TestNotNull(TEXT("Returner"), Returner))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The play simulation starts at its own 20: a 97-yard field goal. Its kicks resolve through
    // the special-teams model (Epic 75), here with no blocks and a make chance out to 100 yards,
    // so the kicker's roll alone decides the kick.
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>(World);
    Sim->InitializeWithWorld(World);
    FPSSpecialTeamsTuning KickTuning = Sim->GetSpecialTeams()->GetTuning();
    KickTuning.PuntBlockChance = 0.f;
    KickTuning.FieldGoalBlockChance = 0.f;
    FPSFieldGoalRangeDef LongRange;
    LongRange.MaxYards = 100.f;
    LongRange.MakeChance = 0.3f;
    KickTuning.FieldGoalRanges = { LongRange };
    Sim->GetSpecialTeams()->SetTuning(KickTuning);
    TArray<FPSTelemetryKickEvent> Kicks;
    const FDelegateHandle Handle = Bus->OnKickMC.AddLambda([&Kicks](const FPSTelemetryKickEvent& Event) { Kicks.Add(Event); });

    UPSKickMeterComponent* Meter = Controller->GetKickMeterComponent();
    UPSPlayContextComponent* Context = Controller->GetPlayContextComponent();
    const FKickMeterTuningRow& Tuning = Meter->GetTuning();
    Meter->BindToController();
    Meter->BindToBus();
    Context->BindToBus();
    TestTrue(TEXT("The human takes the kicker"), Controller->TakeControlOf(Kicker));

    // The field goal unit runs on: the meter lines up and the play waits for it.
    Sim->SetPlayPhase(EPlayPhase::FieldGoal);
    PublishPhase(Bus, TEXT("Scoring"), TEXT("FieldGoal"));
    if (TestEqual(TEXT("The kicker lines up on the bus"), Kicks.Num(), 1))
    {
        TestTrue(TEXT("...lining up"), Kicks[0].bLiningUp);
        TestEqual(TEXT("...for the field goal"), Kicks[0].KickType, FString(TEXT("FieldGoal")));
        TestTrue(TEXT("...asking the play to wait LineUpSeconds"), FMath::IsNearlyEqual(Kicks[0].HoldSeconds, Tuning.LineUpSeconds));
    }
    TestEqual(TEXT("The meter waits for the button"), Meter->GetStage(), EPSKickMeterStage::LiningUp);
    Context->Refresh();
    TestEqual(TEXT("The Kicking context is on"), Controller->GetDepthContext(), Context->KickingContextId);
    Sim->AdvancePlay(2.5f);
    TestEqual(TEXT("Past the CPU kicker's time, the play still waits for the human"), Sim->GetPlayState().Phase, EPlayPhase::FieldGoal);

    // Hold to full power, release, stop the needle dead center: a perfect kick.
    Controller->OnCatalogActionStarted.Broadcast(Tuning.KickAction);
    TestEqual(TEXT("Holding the button fills the power"), Meter->GetStage(), EPSKickMeterStage::Power);
    Meter->AdvanceTime(Tuning.PowerFillSeconds * 0.5f);
    TestTrue(TEXT("...half way after half the fill time"), FMath::IsNearlyEqual(Meter->GetPower(), 0.5f, 0.01f));
    Meter->AdvanceTime(Tuning.PowerFillSeconds * 0.5f);
    Controller->OnCatalogActionCompleted.Broadcast(Tuning.KickAction);
    TestEqual(TEXT("Releasing locks the power"), Meter->GetStage(), EPSKickMeterStage::Accuracy);
    TestTrue(TEXT("...full"), FMath::IsNearlyEqual(Meter->GetPower(), 1.f, 0.01f));
    TestTrue(TEXT("The needle starts hooked left"), FMath::IsNearlyEqual(Meter->GetNeedle(), -1.f, 0.01f));
    Meter->AdvanceTime(Tuning.AccuracySweepSeconds * 0.5f);
    Controller->OnCatalogActionStarted.Broadcast(Tuning.KickAction);
    Controller->OnCatalogActionCompleted.Broadcast(Tuning.KickAction);
    if (TestEqual(TEXT("Stopping the needle kicks"), Kicks.Num(), 2))
    {
        TestFalse(TEXT("...the kick itself"), Kicks[1].bLiningUp);
        TestTrue(TEXT("...full power"), FMath::IsNearlyEqual(Kicks[1].Power, 1.f, 0.01f));
        TestTrue(TEXT("...straight"), FMath::IsNearlyZero(Kicks[1].Accuracy, 0.02f));
        TestTrue(TEXT("...a near-perfect roll"), Kicks[1].Roll < 0.02f);
    }
    TestEqual(TEXT("The meter is done"), Meter->GetStage(), EPSKickMeterStage::Idle);
    Sim->AdvancePlay(0.1f);
    TestEqual(TEXT("The play takes the kick at once"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("...and even from 97 yards, with a 0.3 make chance, a perfect kick is good"), Sim->GetPlayResult().ResultType, EPlayResultType::FieldGoalGood);

    // Held twice as long the bar comes back down; left alone the needle ends pushed right.
    Sim->SetPlayPhase(EPlayPhase::FieldGoal);
    PublishPhase(Bus, TEXT("Scoring"), TEXT("FieldGoal"));
    Controller->OnCatalogActionStarted.Broadcast(Tuning.KickAction);
    Meter->AdvanceTime(Tuning.PowerFillSeconds * 2.f);
    Controller->OnCatalogActionCompleted.Broadcast(Tuning.KickAction);
    TestTrue(TEXT("Held past full, the power drained away"), Meter->GetPower() < 0.01f);
    Meter->AdvanceTime(Tuning.AccuracySweepSeconds + 0.01f);
    const float WorstRoll = UPSKickMeterComponent::ComputeRoll(0.f, 1.f, Tuning);
    if (TestEqual(TEXT("The needle runs out and kicks"), Kicks.Num(), 4))
    {
        TestTrue(TEXT("...pushed right"), FMath::IsNearlyEqual(Kicks[3].Accuracy, 1.f));
        TestTrue(TEXT("...the worst roll"), FMath::IsNearlyEqual(Kicks[3].Roll, WorstRoll, 0.01f));
    }
    Sim->AdvancePlay(0.1f);
    if (WorstRoll >= LongRange.MakeChance)
    {
        TestEqual(TEXT("A botched 97-yarder misses"), Sim->GetPlayResult().ResultType, EPlayResultType::FieldGoalMissed);
    }

    // Punts and kickoffs follow the roll too.
    FPSTelemetryKickEvent Perfect;
    Perfect.KickType = TEXT("Punt");
    Perfect.Roll = 0.f;
    Sim->SetPlayPhase(EPlayPhase::Punt);
    Sim->OnBusKickEvent(Perfect);
    Sim->AdvancePlay(0.1f);
    TestEqual(TEXT("A perfect punt"), Sim->GetPlayResult().ResultType, EPlayResultType::PuntResult);
    TestEqual(TEXT("...goes the longest"), Sim->GetLastSpecialTeamsOutcome().Yards, KickTuning.PuntGrossYardsMax);
    FPSTelemetryKickEvent Shank = Perfect;
    Shank.Roll = 1.f;
    Sim->SetPlayPhase(EPlayPhase::Punt);
    Sim->OnBusKickEvent(Shank);
    Sim->AdvancePlay(0.1f);
    TestEqual(TEXT("A shanked punt goes the shortest"), Sim->GetLastSpecialTeamsOutcome().Yards, KickTuning.PuntGrossYardsMin);
    Perfect.KickType = TEXT("Kickoff");
    Sim->SetPlayPhase(EPlayPhase::Kickoff);
    Sim->OnBusKickEvent(Perfect);
    Sim->AdvancePlay(0.1f);
    TestEqual(TEXT("A perfect kickoff"), Sim->GetPlayResult().ResultType, EPlayResultType::KickoffResult);
    TestEqual(TEXT("...is a touchback"), Sim->GetLastSpecialTeamsOutcome().Result, EPSSpecialTeamsResult::Touchback);

    // Without a human the CPU kicks on time; a lined-up human who never kicks is waited for, then
    // kicked for.
    Sim->SetPlayPhase(EPlayPhase::FieldGoal);
    Sim->AdvancePlay(2.1f);
    TestEqual(TEXT("Nobody lined up: the CPU kicks after two seconds"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    Sim->SetPlayPhase(EPlayPhase::FieldGoal);
    PublishPhase(Bus, TEXT("Scoring"), TEXT("FieldGoal"));
    Sim->AdvancePlay(2.1f);
    TestEqual(TEXT("A lined-up human is waited for"), Sim->GetPlayState().Phase, EPlayPhase::FieldGoal);
    Sim->AdvancePlay(Tuning.LineUpSeconds);
    TestEqual(TEXT("...until his time runs out"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    Meter->AdvanceTime(Tuning.LineUpSeconds);
    TestEqual(TEXT("...and the meter stands down"), Meter->GetStage(), EPSKickMeterStage::Idle);

    // The other side doesn't kick.
    TestTrue(TEXT("The human takes the returner"), Controller->TakeControlOf(Returner));
    const int32 KicksBefore = Kicks.Num();
    PublishPhase(Bus, TEXT("Scoring"), TEXT("Punt"));
    TestEqual(TEXT("A returner doesn't line up"), Kicks.Num(), KicksBefore);
    TestEqual(TEXT("...the meter stays idle"), Meter->GetStage(), EPSKickMeterStage::Idle);
    Context->Refresh();
    TestEqual(TEXT("...and he has no depth context"), Controller->GetDepthContext(), FName(NAME_None));

    Bus->OnKickMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
