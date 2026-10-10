// PSDefenderPreSnapTests.cpp -- the defense's pre-snap play (Epic 67)
//
// Tests covered:
//   1. Shell disguise: a single-high call lines up single-high; disguised, it shows two-high to
//      the offense while still calling one deep safety, and the safeties rotate to their real
//      zones at the snap. A raw secondary leaks the disguise.
//   2. Show blitz and creep: real blitzers walk up; creeping, they show coverage and walk up
//      late, then blitz at the snap; a shown blitz walks up linebackers who drop.
//   3. Audibles and shadows: an audible stays in the front; a shadow lines up over his man and
//      plays man on him at the snap whatever the call, until cleared.
//   4. The CPU: disguises as often as its coach's aggression and its defenders' Awareness say;
//      its best back shadows the best receiver on a man call.
//   5. The human's buttons on defense: their own actions in the DefensePreSnap context, with
//      glyphs and keys of their own, live only while he controls a defender before the snap,
//      pressed through the catalog.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderPreSnapInputComponent.h"
#include "PSDefenderPreSnapSubsystem.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSInputConfig.h"
#include "PSOffenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSPreSnapSubsystem.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDefenderPreSnapTests
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

    /** Both elevens as the game lines them up on the ball at the origin, by role in order:
     *  OL x5, QB, RB, WR x3, TE, DL x4, LB x3, DB x4. Every defender rated DefenseAwareness. */
    struct FPreSnapField
    {
        TArray<APSPlayerPawn*> Line;
        TArray<APSPlayerPawn*> Receivers;
        TArray<APSPlayerPawn*> Linemen;
        TArray<APSPlayerPawn*> Linebackers;
        TArray<APSPlayerPawn*> Backs;
        APSPlayerPawn* Quarterback = nullptr;
        APSPlayerPawn* RunningBack = nullptr;
        APSPlayerPawn* TightEnd = nullptr;

        bool IsComplete() const
        {
            return Line.Num() == 5 && Receivers.Num() == 3 && Linemen.Num() == 4 && Linebackers.Num() == 3 && Backs.Num() == 4
                && Quarterback && RunningBack && TightEnd;
        }
    };

    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FString& PlayerId, const FVector& Location, float Awareness)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(*PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
        Attributes.Awareness = Awareness;
        Pawn->InitializePlayer(Attributes);

        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
                AI->GetDefenderAI()->BindToBus();
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static FPreSnapField SpawnField(UWorld* World, float DefenseAwareness)
    {
        const TArray<EPlayerRole> Personnel = {
            EPlayerRole::OffensiveLineman, EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
            EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack };
        const TArray<int32> Counts = { 5, 1, 1, 3, 1, 4, 3, 4 };
        TArray<EPlayerRole> Roles;
        for (int32 Group = 0; Group < Personnel.Num(); ++Group)
        {
            for (int32 Count = 0; Count < Counts[Group]; ++Count)
            {
                Roles.Add(Personnel[Group]);
            }
        }
        const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, 0.f);
        FPreSnapField Field;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            const EPlayerRole Role = Roles[Index];
            const bool bDefense = APSFieldGrid::GetSideForRole(Role) == EPSTeamSide::Defense;
            APSPlayerPawn* Player = SpawnPlayer(World, Role, FString::Printf(TEXT("P%02d"), Index), Lineup[Index], bDefense ? DefenseAwareness : 50.f);
            if (!Player)
            {
                continue;
            }
            switch (Role)
            {
            case EPlayerRole::OffensiveLineman: Field.Line.Add(Player); break;
            case EPlayerRole::Quarterback: Field.Quarterback = Player; break;
            case EPlayerRole::RunningBack: Field.RunningBack = Player; break;
            case EPlayerRole::WideReceiver: Field.Receivers.Add(Player); break;
            case EPlayerRole::TightEnd: Field.TightEnd = Player; break;
            case EPlayerRole::DefensiveLineman: Field.Linemen.Add(Player); break;
            case EPlayerRole::Linebacker: Field.Linebackers.Add(Player); break;
            default: Field.Backs.Add(Player); break;
            }
        }
        return Field;
    }

    static FPSSituationContext FirstAndTen()
    {
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.YardLine = 20;
        return Situation;
    }

    /** A new down: the whistle, then the call window. */
    static void NewDown(UPSTelemetryBus* Bus, UPSPlayCallSubsystem* PlayCall)
    {
        FPSTelemetryPhaseChangeEvent Whistle;
        Whistle.NewPhase = TEXT("PreSnap");
        Bus->PublishPhaseChange(Whistle);
        PlayCall->OpenPlayCall(FirstAndTen());
    }

    static void Snap(UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Event;
        Event.Down = 1;
        Event.Distance = 10;
        Event.YardLine = 20;
        Event.LineOfScrimmage = FVector::ZeroVector;
        Bus->PublishSnap(Event);
    }

    static void SetAwareness(APSPlayerPawn* Pawn, float Awareness)
    {
        FPlayerAttributes Attributes = Pawn->GetAttributes();
        Attributes.Awareness = Awareness;
        Pawn->InitializePlayer(Attributes);
    }

    static APSDefenseController* ControllerOf(APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    }

    static const FPSInputActionDef* FindAction(const UPSInputConfig* InputConfig, FName ActionId)
    {
        return InputConfig ? InputConfig->Catalog.Actions.FindByPredicate(
            [ActionId](const FPSInputActionDef& Candidate) { return Candidate.ActionId == ActionId; }) : nullptr;
    }

    /** Presses ActionId as a button would: the controller raises it only while a context the
     *  catalog binds it in is on its stack. False when none is, so the press never arrives. */
    static bool PressButton(APSPlayerController* Controller, const UPSInputConfig* InputConfig, FName ActionId)
    {
        const FPSInputActionDef* Action = FindAction(InputConfig, ActionId);
        if (!Action || !Action->Contexts.ContainsByPredicate([Controller](const FName& ContextId) { return Controller->IsInputContextActive(ContextId); }))
        {
            return false;
        }
        Controller->OnCatalogActionStarted.Broadcast(ActionId);
        return true;
    }

    static int32 CountDefensiveEvents(const UPSTelemetryBus* Bus, FName Action)
    {
        int32 Count = 0;
        for (const FPSTelemetryEvent& Event : Bus->GetEventHistory())
        {
            Count += Event.EventType == EPSTelemetryEventType::DefensivePreSnap && Event.Description.Contains(Action.ToString()) ? 1 : 0;
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Shell disguise
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderShellDisguiseTest,
    "PlaySports.PreSnap.Defense.ShellDisguise",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderShellDisguiseTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* OffensePreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Offense pre-snap"), OffensePreSnap)
        || !TestNotNull(TEXT("Defense pre-snap"), DefensePreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPreSnapField Field = SpawnField(World, 100.f);
    if (!TestTrue(TEXT("Both elevens are on the field"), Field.IsComplete()))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSDefensivePreSnapTuning& Tuning = DefensePreSnap->GetTuning();
    // The safeties are the two inside backs (+900 and -900 across the field); the left one
    // takes the middle when one is deep. The line's front is at X -50.
    APSPlayerPawn* RightSafety = Field.Backs[0];
    APSPlayerPawn* LeftSafety = Field.Backs[1];
    const float LineX = -50.f;

    // Cover 3: one deep safety, the other rolled down into the box.
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The offense calls"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("The defense calls Cover 3"), PlayCall->CallPlay(TEXT("Defense_34Cover3"), EPSPlayCaller::Human));
    TestEqual(TEXT("Cover 3 plays one deep safety"), DefensePreSnap->GetCalledDeepSafeties(), 1);
    TestTrue(TEXT("The left safety lines up deep in the middle"),
        LeftSafety->GetActorLocation().Equals(FVector(LineX + Tuning.SingleHighDepth, 0.f, LeftSafety->GetActorLocation().Z), 1.f));
    TestTrue(TEXT("...the right one rolled down on his side"),
        RightSafety->GetActorLocation().Equals(FVector(LineX + Tuning.RobberDepth, Tuning.RobberWidth, RightSafety->GetActorLocation().Z), 1.f));
    FPSDefensiveLook Look = OffensePreSnap->GetDefensiveLook();
    TestEqual(TEXT("The offense sees single-high"), Look.CoverageShell, FString(TEXT("SingleHigh")));
    TestEqual(TEXT("...and the rolled-down safety in the box"), Look.BoxCount, 8);

    // Disguised: two-high shown, one deep called.
    TestTrue(TEXT("The defense disguises its shell"), DefensePreSnap->ToggleShellDisguise(true));
    TestTrue(TEXT("Both safeties line up two-high"),
        LeftSafety->GetActorLocation().Equals(FVector(LineX + Tuning.TwoHighDepth, -Tuning.TwoHighWidth, LeftSafety->GetActorLocation().Z), 1.f)
        && RightSafety->GetActorLocation().Equals(FVector(LineX + Tuning.TwoHighDepth, Tuning.TwoHighWidth, RightSafety->GetActorLocation().Z), 1.f));
    Look = OffensePreSnap->GetDefensiveLook();
    TestEqual(TEXT("The offense sees two-high"), Look.CoverageShell, FString(TEXT("TwoHigh")));
    TestEqual(TEXT("...while the call still plays one deep"), DefensePreSnap->GetCalledDeepSafeties(), 1);
    TestEqual(TEXT("The disguise is announced"), CountDefensiveEvents(Bus, TEXT("DisguiseShell")), 1);

    // The snap: each safety plays his real zone from where he stands.
    Snap(Bus);
    UPSDefenderAIComponent* RightAI = ControllerOf(RightSafety)->GetDefenderAI();
    RightAI->TickAI(0.05f);
    const FVector Zone = RightAI->GetZoneSpot();
    TestEqual(TEXT("At the snap the right safety plays his zone"), RightAI->GetAction(), EPSDefenderAction::Zone);
    TestTrue(TEXT("...rotating to it from the disguise"), !RightAI->GetDesiredDirection().IsNearlyZero() && !Zone.Equals(RightSafety->GetActorLocation(), 1.f));

    // Next down, a raw secondary: the right safety cheats toward his real spot and gives it away.
    NewDown(Bus, PlayCall);
    SetAwareness(LeftSafety, 0.f);
    SetAwareness(RightSafety, 0.f);
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_34Cover3"), EPSPlayCaller::Human);
    TestTrue(TEXT("A raw secondary disguises too"), DefensePreSnap->ToggleShellDisguise(true));
    TestEqual(TEXT("...but the offense still sees single-high"), OffensePreSnap->GetDefensiveLook().CoverageShell, FString(TEXT("SingleHigh")));
    TestTrue(TEXT("...because the right safety cheated down"), RightSafety->GetActorLocation().X - LineX < Tuning.DeepSafetyDepth);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Show blitz and creep
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderShowBlitzTest,
    "PlaySports.PreSnap.Defense.ShowBlitzAndCreep",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderShowBlitzTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPreSnapSubsystem* OffensePreSnap = World ? World->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Offense pre-snap"), OffensePreSnap)
        || !TestNotNull(TEXT("Defense pre-snap"), DefensePreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPreSnapField Field = SpawnField(World, 100.f);
    if (!TestTrue(TEXT("Both elevens are on the field"), Field.IsComplete()))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSDefensivePreSnapTuning& Tuning = DefensePreSnap->GetTuning();
    APSPlayerPawn* Mike = Field.Linebackers[1];
    const float BaseX = Mike->GetActorLocation().X;
    const float LineX = -50.f;

    // The double-A blitz, shown: the linebackers walk up.
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_DoubleABlitz"), EPSPlayCaller::Human);
    TestTrue(TEXT("Blitzing linebackers walk up"), FMath::IsNearlyEqual(Mike->GetActorLocation().X, LineX + Tuning.ShowBlitzDepth, 1.f));
    TestTrue(TEXT("...and the offense sees the blitz"), OffensePreSnap->GetDefensiveLook().bShowsBlitz);

    // Creep: they hold a coverage look, then walk up late.
    TestTrue(TEXT("The defense creeps its blitz"), DefensePreSnap->ToggleCreep(true));
    TestTrue(TEXT("The blitzers line up in coverage"), FMath::IsNearlyEqual(Mike->GetActorLocation().X, BaseX, 1.f) && DefensePreSnap->IsCreeper(Mike));
    TestFalse(TEXT("...so the offense sees no blitz"), OffensePreSnap->GetDefensiveLook().bShowsBlitz);
    DefensePreSnap->TickPreSnap(Tuning.CreepDelaySeconds * 0.5f);
    TestTrue(TEXT("They hold it at first"), Mike->ConsumeMovementInputVector().IsNearlyZero());
    DefensePreSnap->TickPreSnap(Tuning.CreepDelaySeconds);
    const FVector Creep = Mike->ConsumeMovementInputVector();
    TestTrue(TEXT("...then creep up toward the line"), Creep.X < 0.f && FMath::IsNearlyEqual(Creep.Size(), Tuning.CreepSpeedScale, 0.01f));
    Snap(Bus);
    TestTrue(TEXT("At the snap the creeper blitzes"), ControllerOf(Mike)->GetAssignment() == EPSDefensiveAssignmentType::PassRush);
    DefensePreSnap->TickPreSnap(1.f);
    TestTrue(TEXT("...and creeping is over"), Mike->ConsumeMovementInputVector().IsNearlyZero() && !DefensePreSnap->IsCreeper(Mike));

    // Cover 2, with a blitz shown that isn't coming.
    NewDown(Bus, PlayCall);
    Mike->SetActorLocation(FVector(BaseX, Mike->GetActorLocation().Y, Mike->GetActorLocation().Z));
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::Human);
    TestFalse(TEXT("Cover 2 shows no blitz"), OffensePreSnap->GetDefensiveLook().bShowsBlitz);
    TestTrue(TEXT("The defense shows one"), DefensePreSnap->ToggleShowBlitz(true));
    TestTrue(TEXT("...walking up the middle linebacker"), FMath::IsNearlyEqual(Mike->GetActorLocation().X, LineX + Tuning.ShowBlitzDepth, 1.f));
    TestTrue(TEXT("...which the offense reads as a blitz"), OffensePreSnap->GetDefensiveLook().bShowsBlitz);
    Snap(Bus);
    TestTrue(TEXT("At the snap he plays his real job: no rush"), ControllerOf(Mike)->GetAssignment() != EPSDefensiveAssignmentType::PassRush);
    TestTrue(TEXT("Each disguise was announced"), CountDefensiveEvents(Bus, TEXT("Creep")) == 1 && CountDefensiveEvents(Bus, TEXT("ShowBlitz")) == 1);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Audibles and shadows
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderAudibleShadowTest,
    "PlaySports.PreSnap.Defense.AudibleAndShadow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderAudibleShadowTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Defense pre-snap"), DefensePreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPreSnapField Field = SpawnField(World, 100.f);
    if (!TestTrue(TEXT("Both elevens are on the field"), Field.IsComplete()))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Audibles stay in the front.
    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
    PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::Human);
    const TArray<FPSPlayDefinition> Audibles = DefensePreSnap->GetAudibles();
    TestTrue(TEXT("The 4-3 front has another play"), Audibles.Num() > 0 && !Audibles.ContainsByPredicate([](const FPSPlayDefinition& Play) { return Play.Front != TEXT("4-3"); }));
    TestFalse(TEXT("A play of another front is no audible"), DefensePreSnap->Audible(TEXT("Defense_NickelManFree"), true));
    TestTrue(TEXT("The defense checks to Cover 4"), DefensePreSnap->Audible(TEXT("Defense_Cover4Quarters"), true));
    TestEqual(TEXT("...a new call"), PlayCall->GetCall(false).PlayId, FName(TEXT("Defense_Cover4Quarters")));
    TestEqual(TEXT("...that lines up two-high"), DefensePreSnap->GetShownDeepSafeties(), 2);
    TestTrue(TEXT("The next audible stays in the front"), DefensePreSnap->AudibleToNext(true));
    FPSPlayDefinition Called;
    TestTrue(TEXT("...a 4-3 play"), PlayCall->GetDefensivePlayToRun(Called) && Called.Front == TEXT("4-3") && Called.PlayId != FName(TEXT("Defense_Cover4Quarters")));

    // A shadow: the far corner takes the right receiver.
    APSPlayerPawn* Corner = Field.Backs[3];
    APSPlayerPawn* Receiver = Field.Receivers[0];
    TestTrue(TEXT("The far corner shadows the right receiver"), DefensePreSnap->SetShadow(Corner, Receiver, true));
    TestTrue(TEXT("...lining up over him"), FMath::IsNearlyEqual(Corner->GetActorLocation().Y, Receiver->GetActorLocation().Y, 1.f));
    TestTrue(TEXT("...the matchup is known both ways"), DefensePreSnap->GetShadow(Corner) == Receiver && DefensePreSnap->GetShadowingDefender(Receiver) == Corner);
    TestFalse(TEXT("A receiver can't shadow"), DefensePreSnap->SetShadow(Receiver, Corner, true));
    Snap(Bus);
    APSDefenseController* CornerAI = ControllerOf(Corner);
    TestTrue(TEXT("At the snap he plays man on him, whatever the zone call gave him"),
        CornerAI->GetAssignment() == EPSDefensiveAssignmentType::ManCoverage && CornerAI->GetCoverageTarget() == Receiver);

    // The matchup holds into the next down, until cleared.
    NewDown(Bus, PlayCall);
    TestTrue(TEXT("The shadow holds into the next down"), DefensePreSnap->GetShadow(Corner) == Receiver);
    DefensePreSnap->ClearShadow(Corner);
    TestNull(TEXT("...until cleared"), DefensePreSnap->GetShadow(Corner));
    TestTrue(TEXT("Audibles and the shadow were announced"), CountDefensiveEvents(Bus, TEXT("Audible")) == 2 && CountDefensiveEvents(Bus, TEXT("Shadow")) == 1);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The CPU's disguises
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderCpuDisguiseTest,
    "PlaySports.PreSnap.Defense.CpuDisguise",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderCpuDisguiseTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Defense pre-snap"), DefensePreSnap))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPreSnapField Field = SpawnField(World, 100.f);
    if (!TestTrue(TEXT("Both elevens are on the field"), Field.IsComplete()))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Shell disguise only, certain for an aggressive coach, never for a conservative one.
    FPSDefensivePreSnapTuning Tuning = DefensePreSnap->GetTuning();
    Tuning.DisguiseChanceConservative = 0.f;
    Tuning.DisguiseChanceAggressive = 1.f;
    Tuning.ShowBlitzChanceConservative = 0.f;
    Tuning.ShowBlitzChanceAggressive = 0.f;
    Tuning.CreepChanceConservative = 0.f;
    Tuning.CreepChanceAggressive = 1.f;
    Tuning.bCpuShadowsTopReceiver = false;
    DefensePreSnap->SetTuning(Tuning);
    FPSTendencyProfile Aggressive;
    Aggressive.AggressionScore = 1.f;
    FPSTendencyProfile Conservative;
    Conservative.AggressionScore = 0.f;
    auto CpuCalls = [&](FName PlayId)
    {
        NewDown(Bus, PlayCall);
        PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human);
        PlayCall->CallPlay(PlayId, EPSPlayCaller::CPU);
    };

    DefensePreSnap->SetTendency(Aggressive);
    CpuCalls(TEXT("Defense_34Cover3"));
    TestTrue(TEXT("An aggressive coach with a sharp secondary disguises"), DefensePreSnap->GetDisguise().bDisguiseShell);
    TestEqual(TEXT("...showing two-high on a single-high call"), DefensePreSnap->GetShownDeepSafeties(), 2);

    DefensePreSnap->SetTendency(Conservative);
    CpuCalls(TEXT("Defense_34Cover3"));
    TestFalse(TEXT("A conservative coach doesn't"), DefensePreSnap->GetDisguise().bDisguiseShell);

    DefensePreSnap->SetTendency(Aggressive);
    for (APSPlayerPawn* Back : Field.Backs)
    {
        SetAwareness(Back, 0.f);
    }
    CpuCalls(TEXT("Defense_34Cover3"));
    TestFalse(TEXT("Nor does a raw secondary, however aggressive the coach"), DefensePreSnap->GetDisguise().bDisguiseShell);

    // A sharp blitzing front creeps; the same call by a raw one shows its hand.
    CpuCalls(TEXT("Defense_DoubleABlitz"));
    TestTrue(TEXT("Sharp blitzers creep"), DefensePreSnap->GetDisguise().bCreep && DefensePreSnap->IsCreeper(Field.Linebackers[0]));
    for (APSPlayerPawn* Linebacker : Field.Linebackers)
    {
        SetAwareness(Linebacker, 0.f);
    }
    CpuCalls(TEXT("Defense_DoubleABlitz"));
    TestFalse(TEXT("Raw ones walk up and show it"), DefensePreSnap->GetDisguise().bCreep);

    // On a man call the best back shadows the best receiver.
    Tuning.bCpuShadowsTopReceiver = true;
    DefensePreSnap->SetTuning(Tuning);
    FPlayerAttributes Fast = Field.Backs[2]->GetAttributes();
    Fast.Speed = 95.f;
    Fast.Agility = 90.f;
    Field.Backs[2]->InitializePlayer(Fast);
    FPlayerAttributes Star = Field.Receivers[1]->GetAttributes();
    Star.Speed = 98.f;
    Star.Agility = 92.f;
    Field.Receivers[1]->InitializePlayer(Star);
    CpuCalls(TEXT("Defense_NickelManFree"));
    TestTrue(TEXT("On a man call the CPU's best back shadows the best receiver"), DefensePreSnap->GetShadow(Field.Backs[2]) == Field.Receivers[1]);
    CpuCalls(TEXT("Defense_43Cover2"));
    TestNull(TEXT("...for that call only"), DefensePreSnap->GetShadow(Field.Backs[2]));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The human's buttons on defense
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefenderPreSnapButtonsTest,
    "PlaySports.PreSnap.Defense.HumanButtons",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefenderPreSnapButtonsTest::RunTest(const FString& Parameters)
{
    using namespace PSDefenderPreSnapTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSDefenderPreSnapSubsystem* DefensePreSnap = World ? World->GetSubsystem<UPSDefenderPreSnapSubsystem>() : nullptr;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Defense pre-snap"), DefensePreSnap) || !TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    FPreSnapField Field = SpawnField(World, 100.f);
    UPSDefenderPreSnapInputComponent* Input = Controller->GetDefenderPreSnapInputComponent();
    UPSPlayContextComponent* Context = Controller->GetPlayContextComponent();
    const UPSInputConfig* InputConfig = Controller->GetInputConfig();
    if (!TestTrue(TEXT("Both elevens are on the field"), Field.IsComplete()) || !TestNotNull(TEXT("The controller has defensive pre-snap buttons"), Input)
        || !TestNotNull(TEXT("...a play context"), Context) || !TestNotNull(TEXT("...and an input catalog"), InputConfig))
    {
        DestroyTestWorld(World);
        return false;
    }
    Input->BindToController();
    Context->BindToBus();
    const FPSDefensivePreSnapTuning& Tuning = DefensePreSnap->GetTuning();
    const FName DefensePreSnapContext(TEXT("DefensePreSnap"));
    const FName OffensePreSnapContext(TEXT("PreSnap"));
    for (const FString& Problem : UPSDefenderPreSnapSubsystem::ValidateTuning(Tuning))
    {
        AddError(FString::Printf(TEXT("defensive_presnap.json: %s"), *Problem));
    }
    FPSDefensivePreSnapTuning SharedButton = Tuning;
    SharedButton.CreepAction = SharedButton.ShowBlitzAction;
    TestEqual(TEXT("Validation catches two calls on one button"), UPSDefenderPreSnapSubsystem::ValidateTuning(SharedButton).Num(), 1);

    // Each call is its own button in the DefensePreSnap context, with a gamepad glyph, on keys
    // nothing else uses there or in the gameplay context under it.
    const FName Buttons[] = { Tuning.AudibleAction, Tuning.SelectAction, Tuning.ShadowAction, Tuning.ShowBlitzAction, Tuning.DisguiseAction, Tuning.CreepAction };
    for (const FName ButtonId : Buttons)
    {
        const FPSInputActionDef* Action = FindAction(InputConfig, ButtonId);
        if (!TestTrue(*FString::Printf(TEXT("%s is a Boolean action in DefensePreSnap, not the offense's PreSnap"), *ButtonId.ToString()),
            Action && Action->ValueType == EInputActionValueType::Boolean && Action->Contexts.Contains(DefensePreSnapContext)
            && !Action->Contexts.Contains(OffensePreSnapContext)))
        {
            continue;
        }
        FPSInputGlyph Glyph;
        TestTrue(*FString::Printf(TEXT("%s has a gamepad glyph"), *ButtonId.ToString()),
            InputConfig->GetGlyphForAction(ButtonId, DefensePreSnapContext, EPSInputDevice::Gamepad, Glyph));
        FString Clash;
        for (const FPSInputActionDef& Other : InputConfig->Catalog.Actions)
        {
            if (Other.ActionId == ButtonId || !(Other.Contexts.Contains(DefensePreSnapContext) || Other.Contexts.Contains(FName(TEXT("OnField")))))
            {
                continue;
            }
            for (const FPSInputKeyBinding& Binding : Action->Bindings)
            {
                if (Other.Bindings.ContainsByPredicate([&Binding](const FPSInputKeyBinding& Candidate) { return Candidate.Key == Binding.Key; }))
                {
                    Clash = FString::Printf(TEXT("%s (%s)"), *Binding.Key.ToString(), *Other.ActionId.ToString());
                }
            }
        }
        TestTrue(*FString::Printf(TEXT("%s's keys are its own%s%s"), *ButtonId.ToString(), Clash.IsEmpty() ? TEXT("") : TEXT(", but it shares "), *Clash),
            Clash.IsEmpty());
    }

    // On offense the defense's buttons aren't on the stack, and have nothing to act on.
    TestTrue(TEXT("The human takes the quarterback"), Controller->TakeControlOf(Field.Quarterback));
    Context->Refresh();
    TestTrue(TEXT("Before the snap on offense: PreSnap"), Controller->IsInputContextActive(OffensePreSnapContext));
    TestFalse(TEXT("...not DefensePreSnap"), Controller->IsInputContextActive(DefensePreSnapContext));
    TestFalse(TEXT("The disguise button isn't live"), PressButton(Controller, InputConfig, Tuning.DisguiseAction));
    TestEqual(TEXT("...and would have nothing to act on"), Input->GetSelectableReceivers().Num(), 0);

    TestTrue(TEXT("The human takes the middle linebacker"), Controller->TakeControlOf(Field.Linebackers[1]));
    Context->Refresh();
    TestTrue(TEXT("Before the snap on defense: DefensePreSnap"), Controller->IsInputContextActive(DefensePreSnapContext));
    TestFalse(TEXT("...not the offense's PreSnap"), Controller->IsInputContextActive(OffensePreSnapContext));

    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::CPU);
    TestTrue(TEXT("The human calls Cover 2"), PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::Human));

    const TArray<APSPlayerPawn*> Receivers = Input->GetSelectableReceivers();
    TestEqual(TEXT("Five receivers to pick from"), Receivers.Num(), 5);
    TestTrue(TEXT("...left to right"), Receivers.Num() == 5 && Receivers[0]->GetActorLocation().Y < Receivers[4]->GetActorLocation().Y);

    // Every press below arrives as a button does: through the catalog, its context on the stack.
    TestTrue(TEXT("The disguise button is live"), PressButton(Controller, InputConfig, Tuning.DisguiseAction));
    TestTrue(TEXT("...and disguises the shell"), DefensePreSnap->GetDisguise().bDisguiseShell);
    TestTrue(TEXT("The show-blitz button is live"), PressButton(Controller, InputConfig, Tuning.ShowBlitzAction));
    TestTrue(TEXT("...and shows a blitz"), DefensePreSnap->GetDisguise().bShowBlitz);
    PressButton(Controller, InputConfig, Tuning.CreepAction);
    TestTrue(TEXT("The creep button sends the blitzers in late"), DefensePreSnap->GetDisguise().bCreep);
    PressButton(Controller, InputConfig, Tuning.CreepAction);
    TestFalse(TEXT("...and pressed again, lines them up honestly"), DefensePreSnap->GetDisguise().bCreep);

    // Select the rightmost receiver and shadow him with the nearest back.
    for (int32 Press = 0; Press < 4; ++Press)
    {
        PressButton(Controller, InputConfig, Tuning.SelectAction);
    }
    APSPlayerPawn* Selected = Input->GetSelectedReceiver();
    TestTrue(TEXT("Select walks across to the rightmost receiver"), Selected == Receivers.Last());
    PressButton(Controller, InputConfig, Tuning.ShadowAction);
    const APSPlayerPawn* Shadow = DefensePreSnap->GetShadowingDefender(Selected);
    TestTrue(TEXT("The shadow button puts an AI defensive back on him"), Shadow && Shadow->GetAttributes().Role == EPlayerRole::DefensiveBack && !Shadow->IsUserControlled());
    PressButton(Controller, InputConfig, Tuning.ShadowAction);
    TestNull(TEXT("Pressed again, it lets him go"), DefensePreSnap->GetShadowingDefender(Selected));

    PressButton(Controller, InputConfig, Tuning.AudibleAction);
    TestEqual(TEXT("The audible button checks to the front's next play"), PlayCall->GetCall(false).PlayId, FName(TEXT("Defense_Cover4Quarters")));
    TestFalse(TEXT("The offense's audible isn't live on defense"), PressButton(Controller, InputConfig, TEXT("Audible")));
    TestFalse(TEXT("...nor does the defense's component take it"), Input->PressAction(TEXT("Audible")));
    TestEqual(TEXT("...so the call stands"), PlayCall->GetCall(false).PlayId, FName(TEXT("Defense_Cover4Quarters")));

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
