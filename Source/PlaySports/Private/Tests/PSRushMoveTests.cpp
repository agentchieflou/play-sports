// PSRushMoveTests.cpp -- the pass-rush move system (Epic 70)
//
// Tests covered:
//   1. The move library: the shipped catalog loads and is sound, each move's chance follows its
//      ratings and is gated by them, the counter bonus and the double-team scale apply, and the
//      rush plan's score follows the matchup history.
//   2. The counter chain: a rusher's bull is anchored, he spins off the anchor and wins, which
//      ends the engagement and bursts him toward the passer; both moves go on the bus.
//   3. The rush plan learns each matchup: a move that beat a blocker is picked against him
//      again next play, while a different blocker still sees the default plan.
//   4. Double teams: a second blocker on the rusher makes every move harder and brings out
//      the split; only a free or engaged-on-him lineman counts; only rushers work moves.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "PSRushMoveComponent.h"
#include "PSTelemetryBus.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSRushMoveTests
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

    /** A pawn at Location under its side's AI controller, rated 70 unless given, with full stamina. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location,
        float Strength = 70.f, float Agility = 70.f, float Speed = 70.f, float Awareness = 70.f)
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
        Attributes.Agility = Agility;
        Attributes.Speed = Speed;
        Attributes.Awareness = Awareness;
        Attributes.Stamina = 100.f;
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
        }
        return Pawn;
    }

    static APSDefenseController* ControllerOf(APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    }

    static UPSRushMoveComponent* RushOf(APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = ControllerOf(Pawn);
        return Controller ? Controller->GetRushMoves() : nullptr;
    }

    /** Snaps, then gives Defender his assignment and lets his AI take it up (a snap outside a
     *  call window hands out CPU plays first, so the assignment comes after it). */
    static void SnapAndAssign(UPSTelemetryBus* Bus, APSPlayerPawn* Defender, EPSDefensiveAssignmentType Assignment)
    {
        FPSTelemetrySnapEvent Event;
        Bus->PublishSnap(Event);
        if (APSDefenseController* Controller = ControllerOf(Defender))
        {
            Controller->SetAssignment(Assignment);
            Controller->GetDefenderAI()->TickAI(0.05f);
        }
    }

    static void Engage(APSPlayerPawn* Rusher, APSPlayerPawn* Blocker)
    {
        Rusher->bIsEngaged = true;
        Rusher->EngagedOpponent = Blocker;
        Blocker->bIsEngaged = true;
        Blocker->EngagedOpponent = Rusher;
    }

    static void Disengage(APSPlayerPawn* Pawn)
    {
        Pawn->bIsEngaged = false;
        Pawn->EngagedOpponent = nullptr;
    }

    static FPSRushMoveDef MakeMove(EPSRushMove Move, FName Attribute, float BaseWinChance, EPSBlockResponse Response, EPSBlockResponse Counters,
        bool bDoubleTeamOnly = false, float DoubleTeamWinScale = 0.3f)
    {
        FPSRushMoveDef Def;
        Def.Move = Move;
        Def.Attribute = Attribute;
        Def.BlockerAttribute = Attribute;
        Def.MinAttribute = 0.f;
        Def.BaseWinChance = BaseWinChance;
        Def.RatingScalar = 0.005f;
        Def.MoveSeconds = 0.5f;
        Def.StaminaCost = 5.f;
        Def.WinBurstSpeed = 200.f;
        Def.DoubleTeamWinScale = DoubleTeamWinScale;
        Def.bDoubleTeamOnly = bDoubleTeamOnly;
        Def.Response = Response;
        Def.Counters = Counters;
        return Def;
    }

    static int32 CountRushEvents(const UPSTelemetryBus* Bus)
    {
        int32 Count = 0;
        for (const FPSTelemetryEvent& Event : Bus->GetEventHistory())
        {
            Count += Event.EventType == EPSTelemetryEventType::PassRushMove ? 1 : 0;
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The move library
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRushMoveLibraryTest,
    "PlaySports.AI.PassRush.MoveLibrary",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRushMoveLibraryTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSRushMoveCatalog Catalog;
    if (!TestTrue(TEXT("The shipped move library loads"), Ingestion->LoadRushMovesFromJson(UPSRushMoveComponent::GetDefaultCatalogPath(), Catalog)))
    {
        return false;
    }
    const TArray<FString> Problems = PSRushMoves::ValidateCatalog(Catalog);
    TestEqual(TEXT("...and is sound"), Problems.Num(), 0);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    for (const EPSRushMove Move : { EPSRushMove::Swim, EPSRushMove::Rip, EPSRushMove::Bull, EPSRushMove::Spin, EPSRushMove::Club })
    {
        TestTrue(FString::Printf(TEXT("The library has move %d"), static_cast<int32>(Move)),
            Catalog.RushMoves.ContainsByPredicate([Move](const FPSRushMoveDef& Def) { return Def.Move == Move; }));
    }

    const FPSRushMoveDef* Bull = Catalog.RushMoves.FindByPredicate([](const FPSRushMoveDef& Def) { return Def.Move == EPSRushMove::Bull; });
    const FPSRushMoveDef* Split = Catalog.RushMoves.FindByPredicate([](const FPSRushMoveDef& Def) { return Def.Move == EPSRushMove::Split; });
    if (!TestNotNull(TEXT("Bull"), Bull) || !TestNotNull(TEXT("Split"), Split))
    {
        return false;
    }

    // The success curve: the rusher's rating against the blocker's.
    FPlayerAttributes Strong;
    Strong.Strength = 90.f;
    Strong.Agility = 80.f;
    FPlayerAttributes Average;
    Average.Strength = 70.f;
    Average.Agility = 70.f;
    FPlayerAttributes Weak;
    Weak.Strength = FMath::Max(0.f, Bull->MinAttribute - 10.f);
    const EPSBlockResponse NoResponse = EPSBlockResponse::None;
    const float StrongOnAverage = PSRushMoves::ComputeWinChance(*Bull, Strong, Average, NoResponse, false, Catalog);
    const float AverageOnAverage = PSRushMoves::ComputeWinChance(*Bull, Average, Average, NoResponse, false, Catalog);
    const float AverageOnStrong = PSRushMoves::ComputeWinChance(*Bull, Average, Strong, NoResponse, false, Catalog);
    TestTrue(TEXT("A stronger rusher bulls more often"), StrongOnAverage > AverageOnAverage);
    TestTrue(TEXT("...a stronger blocker stops it more often"), AverageOnStrong < AverageOnAverage);
    TestTrue(TEXT("...always within the catalog's bounds"), AverageOnStrong >= Catalog.WinChanceMin && StrongOnAverage <= Catalog.WinChanceMax);
    TestEqual(TEXT("Below the move's rating gate, no chance at all"), PSRushMoves::ComputeWinChance(*Bull, Weak, Average, NoResponse, false, Catalog), 0.f);

    // The counter bonus, against the response a move counters.
    FPSRushMoveCatalog Wide = Catalog;
    Wide.WinChanceMin = 0.f;
    Wide.WinChanceMax = 1.f;
    const FPSRushMoveDef Spin = PSRushMoveTests::MakeMove(EPSRushMove::Spin, TEXT("Agility"), 0.2f, EPSBlockResponse::Mirror, EPSBlockResponse::Anchor);
    const float Fresh = PSRushMoves::ComputeWinChance(Spin, Average, Average, NoResponse, false, Wide);
    TestTrue(TEXT("A move gets the counter bonus right after the response it counters"),
        FMath::IsNearlyEqual(PSRushMoves::ComputeWinChance(Spin, Average, Average, EPSBlockResponse::Anchor, false, Wide), Fresh + Wide.CounterBonus));
    TestTrue(TEXT("...and not after another"),
        FMath::IsNearlyEqual(PSRushMoves::ComputeWinChance(Spin, Average, Average, EPSBlockResponse::Punch, false, Wide), Fresh));

    // Double teams.
    TestEqual(TEXT("The split is for double teams only"), PSRushMoves::ComputeWinChance(*Split, Average, Average, NoResponse, false, Catalog), 0.f);
    TestTrue(TEXT("...where it's available"), PSRushMoves::ComputeWinChance(*Split, Average, Average, NoResponse, true, Catalog) > 0.f);
    TestTrue(TEXT("A double team scales every other move down"),
        FMath::IsNearlyEqual(PSRushMoves::ComputeWinChance(*Bull, Strong, Average, NoResponse, true, Catalog), StrongOnAverage * Bull->DoubleTeamWinScale));

    // The rush plan's score: the chance until there is history, then pulled toward it.
    FPSRushMoveRecord NoHistory;
    FPSRushMoveRecord Stopped;
    Stopped.Attempts = 3;
    FPSRushMoveRecord Winning;
    Winning.Attempts = 3;
    Winning.Wins = 3;
    TestTrue(TEXT("With no history, the score is the chance"), FMath::IsNearlyEqual(PSRushMoves::ScoreMove(0.3f, NoHistory, 3.f), 0.3f));
    TestTrue(TEXT("...a move this blocker keeps stopping scores lower"), PSRushMoves::ScoreMove(0.3f, Stopped, 3.f) < 0.3f);
    TestTrue(TEXT("...one that keeps beating him higher"), PSRushMoves::ScoreMove(0.3f, Winning, 3.f) > 0.3f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The counter chain
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRushCounterChainTest,
    "PlaySports.AI.PassRush.CounterChain",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRushCounterChainTest::RunTest(const FString& Parameters)
{
    using namespace PSRushMoveTests;

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

    APSPlayerPawn* QB = SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-400.f, 0.f, 100.f));
    APSPlayerPawn* Tackle = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_T"), FVector(0.f, 0.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_E"), FVector(100.f, 0.f, 100.f), 80.f, 80.f);
    UPSRushMoveComponent* Rush = RushOf(Rusher);
    if (!TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("Tackle"), Tackle) || !TestNotNull(TEXT("Rusher"), Rusher) || !TestNotNull(TEXT("Rush moves"), Rush))
    {
        DestroyTestWorld(World);
        return false;
    }

    // A bull (the better move on paper) that the anchor stops, and a spin that beats the anchor.
    FPSRushMoveCatalog Catalog;
    Catalog.CounterBonus = 0.2f;
    Catalog.RushMoves.Add(MakeMove(EPSRushMove::Bull, TEXT("Strength"), 0.3f, EPSBlockResponse::Anchor, EPSBlockResponse::Mirror));
    Catalog.RushMoves.Add(MakeMove(EPSRushMove::Spin, TEXT("Agility"), 0.2f, EPSBlockResponse::Mirror, EPSBlockResponse::Anchor));
    Rush->SetCatalog(Catalog);
    Rush->SeedRolls(7);

    SnapAndAssign(Bus, Rusher, EPSDefensiveAssignmentType::PassRush);
    Rush->TickRush(0.1f);
    TestEqual(TEXT("Unblocked, a rusher has no moves to work"), Rush->GetActiveMove(), EPSRushMove::None);

    Engage(Rusher, Tackle);
    const int32 EventsBefore = CountRushEvents(Bus);
    Rush->TickRush(0.1f);
    TestEqual(TEXT("Just engaged, he sets up first"), Rush->GetActiveMove(), EPSRushMove::None);
    const float StaminaBefore = Rusher->CurrentStamina;
    Rush->TickRush(Catalog.FirstMoveSeconds + 0.01f);
    TestEqual(TEXT("Then he goes to his best move: the bull"), Rush->GetActiveMove(), EPSRushMove::Bull);
    TestTrue(TEXT("...on the curve: base plus his strength edge"), FMath::IsNearlyEqual(Rush->GetActiveWinChance(), 0.3f + 10.f * 0.005f));
    TestTrue(TEXT("...and it costs stamina"), FMath::IsNearlyEqual(Rusher->CurrentStamina, StaminaBefore - 5.f));

    TestFalse(TEXT("The tackle anchors: the bull is stopped"), Rush->ResolveActiveMove(0.99f));
    TestEqual(TEXT("...the response is recorded"), Rush->GetLastResponse(), EPSBlockResponse::Anchor);
    TestTrue(TEXT("...the rusher is still blocked"), Rusher->bIsEngaged && Tackle->bIsEngaged);
    TestEqual(TEXT("...and the matchup remembers it"), Rush->GetMatchupRecord(TEXT("OL_T"), EPSRushMove::Bull).Attempts, 1);

    Rush->TickRush(Catalog.RecoverySeconds + 0.05f);
    TestEqual(TEXT("He counters the anchor: a spin"), Rush->GetActiveMove(), EPSRushMove::Spin);
    TestTrue(TEXT("...with the counter bonus"), FMath::IsNearlyEqual(Rush->GetActiveWinChance(), 0.2f + 10.f * 0.005f + Catalog.CounterBonus));

    Rusher->GetFloatingMovementComponent()->Velocity = FVector::ZeroVector;
    TestTrue(TEXT("The spin wins"), Rush->ResolveActiveMove(0.f));
    TestTrue(TEXT("...ending the engagement on both sides"), !Rusher->bIsEngaged && !Rusher->EngagedOpponent && !Tackle->bIsEngaged && !Tackle->EngagedOpponent);
    const FVector Burst = Rusher->GetFloatingMovementComponent()->Velocity;
    TestTrue(TEXT("...and bursting him at the passer"), Burst.X < 0.f && FMath::IsNearlyEqual(Burst.Size(), 200.f, 1.f));
    TestEqual(TEXT("A win clears the response"), Rush->GetLastResponse(), EPSBlockResponse::None);
    TestEqual(TEXT("The spin's win is remembered"), Rush->GetMatchupRecord(TEXT("OL_T"), EPSRushMove::Spin).Wins, 1);
    TestEqual(TEXT("Both moves went on the bus"), CountRushEvents(Bus) - EventsBefore, 2);

    Rush->TickRush(1.f);
    TestEqual(TEXT("Free, he works no more moves"), Rush->GetActiveMove(), EPSRushMove::None);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The rush plan learns each matchup
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRushPlanTest,
    "PlaySports.AI.PassRush.RushPlanLearnsMatchups",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRushPlanTest::RunTest(const FString& Parameters)
{
    using namespace PSRushMoveTests;

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

    APSPlayerPawn* Guard = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_G"), FVector(0.f, 0.f, 100.f));
    APSPlayerPawn* Tackle = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_T"), FVector(0.f, 1000.f, 100.f));
    APSPlayerPawn* Rusher = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_T"), FVector(100.f, 0.f, 100.f));
    UPSRushMoveComponent* Rush = RushOf(Rusher);
    if (!TestNotNull(TEXT("Guard"), Guard) || !TestNotNull(TEXT("Tackle"), Tackle) || !TestNotNull(TEXT("Rusher"), Rusher) || !TestNotNull(TEXT("Rush moves"), Rush))
    {
        DestroyTestWorld(World);
        return false;
    }

    // Two moves of nearly equal promise, neither countering the other's response.
    FPSRushMoveCatalog Catalog;
    Catalog.RushMoves.Add(MakeMove(EPSRushMove::Bull, TEXT("Strength"), 0.3f, EPSBlockResponse::Anchor, EPSBlockResponse::None));
    Catalog.RushMoves.Add(MakeMove(EPSRushMove::Club, TEXT("Strength"), 0.28f, EPSBlockResponse::Mirror, EPSBlockResponse::None));
    Rush->SetCatalog(Catalog);

    // Play 1 against the guard: the bull is stopped, the club wins.
    SnapAndAssign(Bus, Rusher, EPSDefensiveAssignmentType::PassRush);
    Engage(Rusher, Guard);
    Rush->TickRush(0.1f);
    Rush->TickRush(Catalog.FirstMoveSeconds + 0.01f);
    TestEqual(TEXT("Play 1 opens with the bull"), Rush->GetActiveMove(), EPSRushMove::Bull);
    Rush->ResolveActiveMove(0.99f);
    Rush->TickRush(Catalog.RecoverySeconds + 0.05f);
    TestEqual(TEXT("...stopped, he goes to the club"), Rush->GetActiveMove(), EPSRushMove::Club);
    TestTrue(TEXT("...which wins"), Rush->ResolveActiveMove(0.f));
    Rush->TickRush(0.1f);

    // Play 2: the guard again. The club beat him, so the plan opens with it.
    SnapAndAssign(Bus, Rusher, EPSDefensiveAssignmentType::PassRush);
    Engage(Rusher, Guard);
    TestEqual(TEXT("Next play the plan opens with what beat this guard"), Rush->ChooseMove(), EPSRushMove::Club);
    TestEqual(TEXT("...the bull's record against him"), Rush->GetMatchupRecord(TEXT("OL_G"), EPSRushMove::Bull).Attempts, 1);
    TestEqual(TEXT("...the club's"), Rush->GetMatchupRecord(TEXT("OL_G"), EPSRushMove::Club).Wins, 1);

    // The tackle hasn't seen either move: the default plan.
    Disengage(Guard);
    Rusher->SetActorLocation(FVector(100.f, 1000.f, 100.f));
    Engage(Rusher, Tackle);
    TestEqual(TEXT("A blocker he hasn't faced gets the default plan"), Rush->ChooseMove(), EPSRushMove::Bull);

    // A new game forgets.
    Disengage(Tackle);
    Rusher->SetActorLocation(FVector(100.f, 0.f, 100.f));
    Engage(Rusher, Guard);
    Rush->ResetMatchupHistory();
    TestEqual(TEXT("With the history reset, the guard gets the default plan too"), Rush->ChooseMove(), EPSRushMove::Bull);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Double teams, and only rushers work moves
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRushDoubleTeamTest,
    "PlaySports.AI.PassRush.DoubleTeam",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRushDoubleTeamTest::RunTest(const FString& Parameters)
{
    using namespace PSRushMoveTests;

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

    APSPlayerPawn* Center = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_C"), FVector(0.f, 0.f, 100.f));
    APSPlayerPawn* Guard = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_G"), FVector(0.f, 1000.f, 100.f));
    APSPlayerPawn* Nose = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_N"), FVector(100.f, 0.f, 100.f));
    APSPlayerPawn* End = SpawnPlayer(World, EPlayerRole::DefensiveLineman, TEXT("DL_E"), FVector(100.f, 2000.f, 100.f));
    UPSRushMoveComponent* Rush = RushOf(Nose);
    if (!TestNotNull(TEXT("Center"), Center) || !TestNotNull(TEXT("Guard"), Guard) || !TestNotNull(TEXT("Nose"), Nose) || !TestNotNull(TEXT("End"), End)
        || !TestNotNull(TEXT("Rush moves"), Rush))
    {
        DestroyTestWorld(World);
        return false;
    }

    FPSRushMoveCatalog Catalog;
    Catalog.DoubleTeamRadius = 160.f;
    Catalog.RushMoves.Add(MakeMove(EPSRushMove::Bull, TEXT("Strength"), 0.3f, EPSBlockResponse::Anchor, EPSBlockResponse::None));
    Catalog.RushMoves.Add(MakeMove(EPSRushMove::Split, TEXT("Agility"), 0.2f, EPSBlockResponse::Anchor, EPSBlockResponse::None, true, 0.8f));
    Rush->SetCatalog(Catalog);

    SnapAndAssign(Bus, Nose, EPSDefensiveAssignmentType::PassRush);
    Engage(Nose, Center);
    TestEqual(TEXT("One-on-one, the nose bulls"), Rush->ChooseMove(), EPSRushMove::Bull);
    TestFalse(TEXT("...no double team"), Rush->IsDoubleTeamed());

    // The guard, free, steps over to help.
    Guard->SetActorLocation(FVector(0.f, 120.f, 100.f));
    TestEqual(TEXT("Doubled, he splits the double team"), Rush->ChooseMove(), EPSRushMove::Split);
    TestTrue(TEXT("...recognizing it"), Rush->IsDoubleTeamed());

    // The guard is busy with his own man: no help, however close.
    Engage(End, Guard);
    TestEqual(TEXT("A lineman blocking someone else isn't a second blocker"), Rush->ChooseMove(), EPSRushMove::Bull);
    TestFalse(TEXT("...no double team"), Rush->IsDoubleTeamed());

    // Called double team: the guard is on the nose from wherever he is.
    Disengage(End);
    Guard->SetActorLocation(FVector(0.f, 400.f, 100.f));
    Guard->bIsEngaged = true;
    Guard->EngagedOpponent = Nose;
    TestEqual(TEXT("A lineman engaged on him is a second blocker"), Rush->ChooseMove(), EPSRushMove::Split);
    Rush->TickRush(0.1f);
    Rush->TickRush(Catalog.FirstMoveSeconds + 0.01f);
    TestTrue(TEXT("The split is started at its double-team chance"),
        Rush->GetActiveMove() == EPSRushMove::Split && FMath::IsNearlyEqual(Rush->GetActiveWinChance(), 0.2f * 0.8f));

    // A run-fit defender held by a blocker plays the run, not rush moves.
    Disengage(Guard);
    SnapAndAssign(Bus, Nose, EPSDefensiveAssignmentType::RunFit);
    Engage(Nose, Center);
    Rush->TickRush(0.1f);
    Rush->TickRush(1.f);
    TestEqual(TEXT("A run-fit defender works no rush moves"), Rush->GetActiveMove(), EPSRushMove::None);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
