// PSSituationClockTests.cpp -- Epic 76 (situational football intelligence), the clock side
//
// Tests covered:
//   1. Tempo at the snap: a running game clock runs down to the call's play-clock mark when the
//      offense snaps; a stopped clock or the defense's call doesn't; a played game no longer
//      runs off a flat 30 seconds at the whistle, quick sim still does.
//   2. Spike and kneel: resolved by the simulation at the snap -- a kneel is down for a loss
//      with the clock running, a spike an incompletion that stops it -- a late hit after the
//      whistle changes nothing, and a call made after the snap isn't for that snap.
//   3. Out of bounds stops the clock only late in a half; timeouts on the bus are charged to
//      the side that called them.
//   4. The play caller: a CPU offense in the two-minute drill calls its timeout when the window
//      opens and plays no-huddle to the sideline; a winning CPU offense kneels and the
//      simulation resolves it; the defense facing a four-minute offense calls its timeout.
//   5. A human's tempo: the Tempo action cycles it, hurry-up reruns the last real play with no
//      call screen, a new tempo re-announces the call, and the Timeout action calls one.
//   6. The ball carrier stays in bounds or heads for the sideline as the call says.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSCoachingAI.h"
#include "PSMenuComponent.h"
#include "PSOffenseController.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaySimulation.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSRulesConfig.h"
#include "PSSituationAI.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSituationClockTests
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

    static UPSPlaySimulation* MakeSim(bool bQuickSim)
    {
        UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
        Sim->bQuickSimMode = bQuickSim;
        TArray<FPlayerAttributes> Offense, Defense;
        Sim->InitializePlay(Offense, Defense);
        return Sim;
    }

    /** Runs the game clock to Quarter / Seconds with live-play ticks, then stands before the snap. */
    static void RunClockTo(UPSPlaySimulation* Sim, int32 Quarter, float Seconds)
    {
        while (Sim->GetPlayState().Quarter < Quarter)
        {
            Sim->SetPlayPhase(EPlayPhase::PassRush);
            Sim->AdvancePlay(Sim->GetPlayState().GameClockSeconds);
        }
        const float ToRun = Sim->GetPlayState().GameClockSeconds - Seconds;
        if (ToRun > 0.f)
        {
            Sim->SetPlayPhase(EPlayPhase::PassRush);
            Sim->AdvancePlay(ToRun);
        }
        Sim->SetPlayPhase(EPlayPhase::PreSnap);
        Sim->ActivePenalty = EPSPenaltyType::None;
    }

    /** A play that ends with the carrier down in bounds (or out of bounds), whistle to next down. */
    static void EndPlayWithCarrierDown(UPSPlaySimulation* Sim, int32 Yards, bool bOutOfBounds)
    {
        Sim->SetPlayPhase(EPlayPhase::BallCarrierMovement);
        if (bOutOfBounds)
        {
            Sim->RecordOutOfBounds(Yards);
        }
        else
        {
            Sim->RecordTackle(Yards);
        }
        Sim->ActivePenalty = EPSPenaltyType::None;
        Sim->EndPlayAndPrepareNext();
    }

    static FPSTelemetryPlayCallEvent MakeOffenseCall(const TCHAR* Category, float SnapAtPlayClock)
    {
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = Category;
        Call.SnapAtPlayClockSeconds = SnapAtPlayClock;
        return Call;
    }

    /** Snaps (with no offside flag) and runs the first tick of the play. */
    static void SnapAndTick(UPSPlaySimulation* Sim, float DeltaSeconds)
    {
        Sim->TriggerSnap();
        Sim->ActivePenalty = EPSPenaltyType::None;
        Sim->AdvancePlay(DeltaSeconds);
    }

    static FPSSituationContext MakeContext(int32 Quarter, float Clock, int32 ScoreDifferential, int32 Down, int32 Distance, int32 YardLine,
        int32 Timeouts, int32 OpponentTimeouts, bool bClockRunning)
    {
        FPSSituationContext Context;
        Context.Quarter = Quarter;
        Context.GameClockSeconds = Clock;
        Context.ScoreDifferential = ScoreDifferential;
        Context.Down = Down;
        Context.Distance = Distance;
        Context.YardLine = YardLine;
        Context.TimeoutsRemaining = Timeouts;
        Context.OpponentTimeoutsRemaining = OpponentTimeouts;
        Context.bClockRunning = bClockRunning;
        return Context;
    }

    /** A controller holding a QB, bound to the play-call authority as the game binds it. */
    static APSPlayerController* SpawnHumanQuarterback(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* QB = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (!QB || !Controller)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = TEXT("QB_SITUATION");
        Attributes.DisplayName = TEXT("QB_SITUATION");
        Attributes.Role = EPlayerRole::Quarterback;
        QB->InitializePlayer(Attributes);

        Controller->GetPlayCallComponent()->BindToPlayCall();
        Controller->TakeControlOf(QB);
        return Controller;
    }

    /** Has the human call PlayId in an open window and snaps it, so it joins the call history. */
    static void RunHumanCall(UPSPlayCallSubsystem* PlayCall, UPSTelemetryBus* Bus, UPSMenuComponent* Menu, FName PlayId)
    {
        PlayCall->OpenPlayCall(MakeContext(1, 600.f, 0, 1, 10, 25, 3, 3, false));
        PlayCall->CallPlay(PlayId, EPSPlayCaller::Human);
        if (Menu->IsMenuOpen())
        {
            Menu->Resume();
        }
        FPSTelemetrySnapEvent Snap;
        Snap.LineOfScrimmage = FVector(2500.f, 0.f, 0.f);
        Bus->PublishSnap(Snap);
    }

    /** A receiver with the ball under the offense AI, LineOfScrimmage behind him. */
    static APSPlayerPawn* SpawnCarrier(UWorld* World, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = TEXT("WR_SITUATION");
        Attributes.DisplayName = TEXT("WR_SITUATION");
        Attributes.Role = EPlayerRole::WideReceiver;
        Pawn->InitializePlayer(Attributes);
        if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        if (APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            Ball->AttachToCarrier(Pawn, TEXT("HandSocket"));
            Pawn->GainPossession();
        }
        return Pawn;
    }

    /** The snap, then the offense's call with Intent (the snap outside a window has the CPU
     *  call and hand out routes first, so those are cleared). */
    static void StartPlayWithIntent(UWorld* World, UPSTelemetryBus* Bus, EPSBoundaryIntent Intent)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.LineOfScrimmage = FVector(2000.f, 0.f, 0.f);
        Bus->PublishSnap(Snap);
        for (TActorIterator<APSOffenseController> It(World); It; ++It)
        {
            It->SetAssignedRoute(TArray<FVector>());
        }
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = TEXT("ShortPass");
        Call.BoundaryIntent = Intent;
        Bus->PublishPlayCall(Call);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tempo's mark at the snap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationTempoClockTest,
    "PlaySports.Situation.TempoRunsTheClockAtTheSnap",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationTempoClockTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationClockTests;

    UPSPlaySimulation* Sim = MakeSim(false);
    RunClockTo(Sim, 1, 600.f);
    EndPlayWithCarrierDown(Sim, 3, false);
    TestTrue(TEXT("A tackle in bounds keeps the clock running"), Sim->GetPlayState().bIsClockRunning);
    TestEqual(TEXT("...and a played game runs off nothing at the whistle"), Sim->GetPlayState().GameClockSeconds, 600.f);

    // A huddle snaps with 12 on the play clock: the other 28 seconds of a fresh 40 run off.
    Sim->OnBusPlayCallEvent(MakeOffenseCall(TEXT("ShortPass"), 12.f));
    Sim->TriggerSnap();
    TestEqual(TEXT("The game clock runs down to the tempo's mark at the snap"), Sim->GetPlayState().GameClockSeconds, 572.f);
    TestEqual(TEXT("...and the play clock reads the mark"), Sim->GetPlayState().PlayClockSeconds, 12.f);

    // The defense's call carries no mark; a stopped clock runs off nothing.
    UPSPlaySimulation* Stopped = MakeSim(false);
    RunClockTo(Stopped, 1, 600.f);
    Stopped->SetPlayPhase(EPlayPhase::Scoring);
    Stopped->ActivePenalty = EPSPenaltyType::None;
    Stopped->EndPlayAndPrepareNext();
    TestFalse(TEXT("An incompletion stops the clock"), Stopped->GetPlayState().bIsClockRunning);
    Stopped->OnBusPlayCallEvent(MakeOffenseCall(TEXT("ShortPass"), 12.f));
    Stopped->TriggerSnap();
    TestEqual(TEXT("A stopped clock runs off nothing before the snap"), Stopped->GetPlayState().GameClockSeconds, 600.f);

    UPSPlaySimulation* DefenseCall = MakeSim(false);
    RunClockTo(DefenseCall, 1, 600.f);
    EndPlayWithCarrierDown(DefenseCall, 3, false);
    FPSTelemetryPlayCallEvent Defense = MakeOffenseCall(TEXT("Base"), 2.f);
    Defense.bOffense = false;
    DefenseCall->OnBusPlayCallEvent(Defense);
    DefenseCall->TriggerSnap();
    TestEqual(TEXT("The defense's call sets no mark"), DefenseCall->GetPlayState().GameClockSeconds, 600.f);

    // Quick sim has no time before the snap, so a tackle still runs off its fixed amount.
    UPSPlaySimulation* QuickSim = MakeSim(true);
    EndPlayWithCarrierDown(QuickSim, 3, false);
    TestEqual(TEXT("Quick sim runs off the rules' fixed amount"), QuickSim->GetPlayState().GameClockSeconds, 900.f - GetDefault<UPSRulesConfig>()->QuickSimTackleRunoffSeconds);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Spike and kneel at the snap
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationSpikeKneelTest,
    "PlaySports.Situation.SpikeAndKneelResolveAtTheSnap",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationSpikeKneelTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationClockTests;

    const UPSRulesConfig* Rules = GetDefault<UPSRulesConfig>();
    UPSPlaySimulation* Sim = MakeSim(false);
    RunClockTo(Sim, 4, 60.f);
    EndPlayWithCarrierDown(Sim, 3, false);
    const int32 YardLineBefore = Sim->GetPlayState().YardLine;
    const int32 DownBefore = Sim->GetPlayState().Down;

    // Kneel, milking the play clock to 2.
    Sim->OnBusPlayCallEvent(MakeOffenseCall(TEXT("Kneel"), 2.f));
    SnapAndTick(Sim, 0.1f);
    TestEqual(TEXT("The kneel is over at the snap"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("...down where he knelt"), Sim->GetPlayResult().ResultType, EPlayResultType::Tackle);
    TestEqual(TEXT("...for the rules' loss"), Sim->GetPlayResult().YardsGained, Rules->KneelYardage);
    TestEqual(TEXT("The milked play clock and the kneel ran the game clock"), Sim->GetPlayState().GameClockSeconds, 60.f - 38.f - 0.1f - Rules->KneelPlaySeconds, 0.01f);

    // A late hit after the whistle changes nothing.
    FPSTelemetryTackleEvent LateHit;
    LateHit.YardsGained = 12;
    Sim->OnBusTackleEvent(LateHit);
    Sim->RecordTackle(12);
    TestEqual(TEXT("A tackle after the whistle doesn't change the result"), Sim->GetPlayResult().YardsGained, Rules->KneelYardage);

    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->EndPlayAndPrepareNext();
    TestEqual(TEXT("The kneel costs a down"), Sim->GetPlayState().Down, DownBefore + 1);
    TestEqual(TEXT("...and the yardage"), Sim->GetPlayState().YardLine, YardLineBefore + Rules->KneelYardage);
    TestTrue(TEXT("...and the clock keeps running"), Sim->GetPlayState().bIsClockRunning);

    // Spike at hurry-up: nothing above the mark to run off, then an incompletion.
    const float ClockBeforeSpike = Sim->GetPlayState().GameClockSeconds;
    Sim->OnBusPlayCallEvent(MakeOffenseCall(TEXT("Spike"), 40.f));
    SnapAndTick(Sim, 0.1f);
    TestEqual(TEXT("The spike is an incompletion"), Sim->GetPlayResult().ResultType, EPlayResultType::Incomplete);
    TestEqual(TEXT("...over at the snap"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("It takes the spike's time off the clock"), Sim->GetPlayState().GameClockSeconds, ClockBeforeSpike - 0.1f - Rules->SpikePlaySeconds, 0.01f);
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->EndPlayAndPrepareNext();
    TestFalse(TEXT("...and stops it"), Sim->GetPlayState().bIsClockRunning);

    // A kneel called after the snap is not for that snap.
    UPSPlaySimulation* Late = MakeSim(false);
    Late->TriggerSnap();
    Late->ActivePenalty = EPSPenaltyType::None;
    Late->OnBusPlayCallEvent(MakeOffenseCall(TEXT("Kneel"), 2.f));
    Late->AdvancePlay(0.6f);
    TestEqual(TEXT("A call made after the snap doesn't end the play"), Late->GetPlayState().Phase, EPlayPhase::PassRush);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Out of bounds and timeouts
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationOutOfBoundsTest,
    "PlaySports.Situation.OutOfBoundsAndTimeouts",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationOutOfBoundsTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationClockTests;

    UPSPlaySimulation* Late4th = MakeSim(false);
    RunClockTo(Late4th, 4, 200.f);
    EndPlayWithCarrierDown(Late4th, 6, true);
    TestFalse(TEXT("Out of bounds inside the last five minutes stops the clock"), Late4th->GetPlayState().bIsClockRunning);

    UPSPlaySimulation* Early = MakeSim(false);
    RunClockTo(Early, 1, 600.f);
    EndPlayWithCarrierDown(Early, 6, true);
    TestTrue(TEXT("Out of bounds in the 1st quarter: the clock runs again"), Early->GetPlayState().bIsClockRunning);

    UPSPlaySimulation* Half = MakeSim(false);
    RunClockTo(Half, 2, 200.f);
    EndPlayWithCarrierDown(Half, 6, true);
    TestTrue(TEXT("Out of bounds in the 2nd quarter before the two-minute mark: the clock runs again"), Half->GetPlayState().bIsClockRunning);
    RunClockTo(Half, 2, 100.f);
    EndPlayWithCarrierDown(Half, 6, true);
    TestFalse(TEXT("...inside the last two minutes it stays stopped"), Half->GetPlayState().bIsClockRunning);

    TestTrue(TEXT("The rule itself: late 4th"), DoesOutOfBoundsStopClock(4, 299.f, nullptr));
    TestFalse(TEXT("...not the 3rd"), DoesOutOfBoundsStopClock(3, 30.f, nullptr));

    // Timeouts on the bus go to the side that called them.
    UPSPlaySimulation* Sim = MakeSim(false);
    EndPlayWithCarrierDown(Sim, 3, false);
    const bool bHomeOnOffense = Sim->GetPlayState().bHomeHasPossession;
    FPSTelemetryTimeoutEvent Timeout;
    Timeout.bOffense = true;
    Sim->OnBusTimeoutEvent(Timeout);
    TestEqual(TEXT("The offense's timeout is charged to the offense"), bHomeOnOffense ? Sim->GetPlayState().HomeTimeoutsRemaining : Sim->GetPlayState().AwayTimeoutsRemaining, 2);
    TestFalse(TEXT("...and stops the clock"), Sim->GetPlayState().bIsClockRunning);
    Timeout.bOffense = false;
    Sim->OnBusTimeoutEvent(Timeout);
    TestEqual(TEXT("The defense's to the defense"), bHomeOnOffense ? Sim->GetPlayState().AwayTimeoutsRemaining : Sim->GetPlayState().HomeTimeoutsRemaining, 2);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The CPU play caller runs the clock
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationCpuClockTest,
    "PlaySports.Situation.CpuPlayCallerRunsTheClock",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationCpuClockTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationClockTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Situational read"), PlayCall->GetCoachingAI() ? PlayCall->GetCoachingAI()->GetSituationAI() : nullptr))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const UPSSituationAI* Situation = PlayCall->GetCoachingAI()->GetSituationAI();

    // The clock's authority listens on the same bus.
    UPSPlaySimulation* Sim = MakeSim(false);
    Sim->InitializeWithWorld(World);
    const bool bHomeOnOffense = Sim->GetPlayState().bHomeHasPossession;

    TArray<FPSTelemetryTimeoutEvent> Timeouts;
    TArray<FPSTelemetryPlayCallEvent> Calls;
    const FDelegateHandle TimeoutHandle = Bus->OnTimeoutMC.AddLambda([&Timeouts](const FPSTelemetryTimeoutEvent& Event) { Timeouts.Add(Event); });
    const FDelegateHandle CallHandle = Bus->OnPlayCallMC.AddLambda([&Calls](const FPSTelemetryPlayCallEvent& Event) { Calls.Add(Event); });

    // Down 4 at 0:45 with the clock running and two timeouts: stop it now.
    PlayCall->OpenPlayCall(MakeContext(4, 45.f, -4, 2, 6, 60, 2, 3, true));
    if (TestEqual(TEXT("The CPU offense calls a timeout as the window opens"), Timeouts.Num(), 1))
    {
        TestTrue(TEXT("...the offense's, the CPU's"), Timeouts[0].bOffense && !Timeouts[0].bHumanCall);
    }
    TestEqual(TEXT("The window's situation spends it"), PlayCall->GetSituation().TimeoutsRemaining, 1);
    TestFalse(TEXT("...and stops the clock"), PlayCall->GetSituation().bClockRunning);
    TestEqual(TEXT("The simulation charges it to the offense"), bHomeOnOffense ? Sim->GetPlayState().HomeTimeoutsRemaining : Sim->GetPlayState().AwayTimeoutsRemaining, 2);

    Calls.Reset();
    PlayCall->PollReadyToSnap(0.f);
    const FPSTelemetryPlayCallEvent* OffenseCall = Calls.FindByPredicate([](const FPSTelemetryPlayCallEvent& Event) { return Event.bOffense; });
    const FPSTelemetryPlayCallEvent* DefenseCall = Calls.FindByPredicate([](const FPSTelemetryPlayCallEvent& Event) { return !Event.bOffense; });
    if (TestNotNull(TEXT("The CPU offense calls"), OffenseCall) && TestNotNull(TEXT("The CPU defense calls"), DefenseCall))
    {
        TestEqual(TEXT("With the clock stopped the two-minute offense goes no-huddle"), OffenseCall->Tempo, EPSTempo::NoHuddle);
        TestEqual(TEXT("...announcing its snap mark"), OffenseCall->SnapAtPlayClockSeconds, Situation->GetSnapPlayClock(EPSTempo::NoHuddle));
        TestEqual(TEXT("...and that the carrier gets out of bounds"), OffenseCall->BoundaryIntent, EPSBoundaryIntent::GetOutOfBounds);
        TestEqual(TEXT("A timeout in hand: no spike"), PSSituation::ClockPlayFromCategory(OffenseCall->PlayCategory), EPSClockPlay::None);
        TestTrue(TEXT("The defense's call sets no snap mark"), DefenseCall->SnapAtPlayClockSeconds < 0.f);
        TestEqual(TEXT("The offense's call holds its tempo"), PlayCall->GetCall(true).Tempo, EPSTempo::NoHuddle);
    }

    // Up 3 at 0:40, the defense out of timeouts: kneel, and the simulation ends it at the snap.
    Calls.Reset();
    PlayCall->OpenPlayCall(MakeContext(4, 40.f, 3, 1, 10, 40, 3, 0, true));
    PlayCall->PollReadyToSnap(0.f);
    TestEqual(TEXT("A winning CPU offense kneels"), PlayCall->GetCall(true).PlayId, FName(TEXT("Offense_Kneel")));
    TestEqual(TEXT("...milking the clock"), PlayCall->GetCall(true).Tempo, EPSTempo::MilkClock);
    OffenseCall = Calls.FindByPredicate([](const FPSTelemetryPlayCallEvent& Event) { return Event.bOffense; });
    TestTrue(TEXT("...staying in bounds"), OffenseCall && OffenseCall->BoundaryIntent == EPSBoundaryIntent::StayInbounds);
    TestEqual(TEXT("Nobody calls a timeout in victory formation"), Timeouts.Num(), 1);
    SnapAndTick(Sim, 0.1f);
    TestEqual(TEXT("The simulation resolves the CPU's kneel at the snap"), Sim->GetPlayResult().ResultType, EPlayResultType::Tackle);
    TestEqual(TEXT("...for the kneel's loss"), Sim->GetPlayResult().YardsGained, GetDefault<UPSRulesConfig>()->KneelYardage);

    // Up 3 at 2:30 against two timeouts: the defense stops the clock.
    PlayCall->OpenPlayCall(MakeContext(4, 150.f, 3, 2, 5, 50, 3, 2, true));
    if (TestEqual(TEXT("The CPU defense calls a timeout against the four-minute offense"), Timeouts.Num(), 2))
    {
        TestFalse(TEXT("...the defense's"), Timeouts[1].bOffense);
    }
    TestEqual(TEXT("It spends the defense's timeout"), PlayCall->GetSituation().OpponentTimeoutsRemaining, 1);
    PlayCall->PollReadyToSnap(0.f);
    TestEqual(TEXT("With the clock stopped the four-minute offense huddles"), PlayCall->GetCall(true).Tempo, EPSTempo::Huddle);
    TestTrue(TEXT("The call screen names the moment"), PlayCall->BuildCallScreenBody(true).Contains(TEXT("Four-minute offense")));

    Bus->OnTimeoutMC.Remove(TimeoutHandle);
    Bus->OnPlayCallMC.Remove(CallHandle);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- A human's tempo and timeouts
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationHumanTempoTest,
    "PlaySports.Situation.HumanTempoAndTimeout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationHumanTempoTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationClockTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    APSPlayerController* Controller = World ? SpawnHumanQuarterback(World) : nullptr;
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    UPSPlayCallComponent* Caller = Controller ? Controller->GetPlayCallComponent() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Human QB"), Controller)
        || !TestNotNull(TEXT("Menu"), Menu) || !TestNotNull(TEXT("Play-call component"), Caller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    TArray<FPSTelemetryTimeoutEvent> Timeouts;
    TArray<FPSTelemetryPlayCallEvent> Calls;
    const FDelegateHandle TimeoutHandle = Bus->OnTimeoutMC.AddLambda([&Timeouts](const FPSTelemetryTimeoutEvent& Event) { Timeouts.Add(Event); });
    const FDelegateHandle CallHandle = Bus->OnPlayCallMC.AddLambda([&Calls](const FPSTelemetryPlayCallEvent& Event) { Calls.Add(Event); });

    TestEqual(TEXT("A human starts in the huddle"), PlayCall->GetHumanTempo(), EPSTempo::Huddle);
    Caller->HandleCatalogAction(Caller->TempoActionId);
    TestEqual(TEXT("The Tempo action steps to no-huddle"), PlayCall->GetHumanTempo(), EPSTempo::NoHuddle);
    Caller->HandleCatalogAction(Caller->TempoActionId);
    TestEqual(TEXT("...then hurry-up"), PlayCall->GetHumanTempo(), EPSTempo::HurryUp);

    // The human's call carries the human's tempo.
    Calls.Reset();
    RunHumanCall(PlayCall, Bus, Menu, TEXT("Offense_SlantFlat"));
    const FPSTelemetryPlayCallEvent* HumanCall = Calls.FindByPredicate([](const FPSTelemetryPlayCallEvent& Event) { return Event.bOffense && Event.bHumanCall; });
    TestTrue(TEXT("The human's call is at hurry-up"), HumanCall && HumanCall->Tempo == EPSTempo::HurryUp);
    RunHumanCall(PlayCall, Bus, Menu, TEXT("Offense_Spike"));

    // Hurry-up: the next down reruns the last real play (not the spike), with no call screen.
    PlayCall->OpenPlayCall(MakeContext(4, 50.f, -4, 2, 6, 60, 2, 3, true));
    TestTrue(TEXT("Hurry-up calls the last play again for the human"),
        PlayCall->GetCall(true).Caller == EPSPlayCaller::Human && PlayCall->GetCall(true).PlayId == FName(TEXT("Offense_SlantFlat")));
    TestEqual(TEXT("...at hurry-up"), PlayCall->GetCall(true).Tempo, EPSTempo::HurryUp);
    TestFalse(TEXT("...with no call screen"), Menu->IsPlayCallScreenOpen());
    const FString Body = PlayCall->BuildCallScreenBody(true);
    TestTrue(TEXT("The call screen shows the tempo"), Body.Contains(TEXT("Tempo: Hurry-up")));
    TestTrue(TEXT("...and the moment"), Body.Contains(TEXT("Two-minute drill")));

    // A new tempo re-announces the call already in.
    Calls.Reset();
    PlayCall->SetHumanTempo(EPSTempo::Huddle);
    if (TestEqual(TEXT("Changing tempo re-announces the call"), Calls.Num(), 1))
    {
        TestTrue(TEXT("...the same play at the new tempo"), Calls[0].PlayId == FName(TEXT("Offense_SlantFlat")) && Calls[0].Tempo == EPSTempo::Huddle && Calls[0].bHumanCall);
    }

    // The Timeout action calls the human side's timeout.
    Caller->HandleCatalogAction(Caller->TimeoutActionId);
    if (TestEqual(TEXT("The Timeout action calls a timeout"), Timeouts.Num(), 1))
    {
        TestTrue(TEXT("...the human offense's"), Timeouts[0].bOffense && Timeouts[0].bHumanCall);
    }
    TestEqual(TEXT("...spending one"), PlayCall->GetSituation().TimeoutsRemaining, 1);
    PlayCall->OpenPlayCall(MakeContext(4, 50.f, -4, 2, 6, 60, 0, 3, true));
    Caller->HandleCatalogAction(Caller->TimeoutActionId);
    TestEqual(TEXT("None left: no timeout"), Timeouts.Num(), 1);

    Bus->OnTimeoutMC.Remove(TimeoutHandle);
    Bus->OnPlayCallMC.Remove(CallHandle);
    Caller->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- The ball carrier and the sideline
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSituationSidelineIntentTest,
    "PlaySports.Situation.CarrierSidelineIntent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSituationSidelineIntentTest::RunTest(const FString& Parameters)
{
    using namespace PSSituationClockTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // Past the line, a step from the right sideline.
    const FSkillPlayerAITuningRow Tuning;
    APSPlayerPawn* NearSideline = SpawnCarrier(World, FVector(3000.f, Tuning.FieldHalfWidth - Tuning.SidelineCushion * 0.5f, 100.f));
    const APSOffenseController* AI = NearSideline ? Cast<APSOffenseController>(NearSideline->GetController()) : nullptr;
    UPSSkillPlayerAIComponent* SkillAI = AI ? AI->GetSkillAI() : nullptr;
    if (!TestNotNull(TEXT("The carrier has an offense AI"), SkillAI))
    {
        DestroyTestWorld(World);
        return false;
    }

    StartPlayWithIntent(World, Bus, EPSBoundaryIntent::StayInbounds);
    SkillAI->TickAI(0.1f);
    TestEqual(TEXT("He carries the ball"), SkillAI->GetAction(), EPSSkillPlayerAction::CarryBall);
    TestTrue(TEXT("Told to stay in bounds, he turns back inside"), SkillAI->GetDesiredDirection().Y < 0.f);
    TestTrue(TEXT("...still going upfield"), SkillAI->GetDesiredDirection().X > 0.f);

    StartPlayWithIntent(World, Bus, EPSBoundaryIntent::None);
    SkillAI->TickAI(0.1f);
    TestTrue(TEXT("With no intent he runs straight upfield"), FMath::IsNearlyZero(SkillAI->GetDesiredDirection().Y, 0.01f));

    // Mid-field and told to get out of bounds: he heads for the nearer sideline.
    NearSideline->SetActorLocation(FVector(3000.f, 800.f, 100.f));
    StartPlayWithIntent(World, Bus, EPSBoundaryIntent::GetOutOfBounds);
    SkillAI->TickAI(0.1f);
    TestTrue(TEXT("Told to get out of bounds, he heads for the sideline"), SkillAI->GetDesiredDirection().Y > 0.f);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
