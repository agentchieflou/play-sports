// PSOpponentModelTests.cpp -- adaptive opponent learning (Epic 78)
//
// Tests covered:
//   1. The tendency tracker: the human's calls are counted by side, down, distance and
//      personnel once each play is snapped -- the call he ran, not one he changed, never the
//      CPU's or an untracked one -- and read from the narrowest situation with enough calls.
//   2. Never psychic: the human's call for a play is unknown to the model until its snap.
//   3. Counter-selection: a run-heavy human gets the defense in base and out of prevent, a
//      blitz-heavy one gets screens; an even mix, no read or no strength changes nothing; the
//      multipliers stay in their bounds, and the coaching AI weighs and explains them.
//   4. Halftime adjustments: the CPU leans harder from the half, and its first call that does
//      announces it once on the bus; the dial at 0 never adapts.
//   5. Across games: a game's calls join the earlier games' at their weight.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSOpponentModel.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOpponentModelTests
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

    template <typename ControllerType>
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location)
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
        Pawn->InitializePlayer(Attributes);
        if (ControllerType* AI = World->SpawnActor<ControllerType>(ControllerType::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    /** A world where a human has the offense's quarterback against a CPU defense. */
    struct FHumanGame
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSPlayCallSubsystem* PlayCall = nullptr;
        UPSOpponentModel* Model = nullptr;

        bool Start(FAutomationTestBase& Test, bool bHumanOffense = true)
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
            Model = World ? World->GetSubsystem<UPSOpponentModel>() : nullptr;
            APSPlayerPawn* QB = World ? SpawnPlayer<APSOffenseController>(World, EPlayerRole::Quarterback, TEXT("QB_HUMAN"), FVector::ZeroVector) : nullptr;
            APSPlayerPawn* LB = World ? SpawnPlayer<APSDefenseController>(World, EPlayerRole::Linebacker, TEXT("LB_HUMAN"), FVector(300.f, 0.f, 0.f)) : nullptr;
            if (!Test.TestNotNull(TEXT("Bus"), Bus) || !Test.TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !Test.TestNotNull(TEXT("Opponent model"), Model)
                || !Test.TestNotNull(TEXT("QB"), QB) || !Test.TestNotNull(TEXT("LB"), LB))
            {
                return false;
            }
            FPSTelemetryControlChangeEvent Take;
            Take.PlayerId = bHumanOffense ? TEXT("QB_HUMAN") : TEXT("LB_HUMAN");
            Take.PlayerName = Take.PlayerId.ToString();
            Take.bHumanControlled = true;
            Bus->PublishControlChange(Take);
            return Test.TestTrue(TEXT("A human has a side"), PlayCall->IsHumanSide(bHumanOffense) && !PlayCall->IsHumanSide(!bHumanOffense));
        }

        void End()
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
        }

        /** One down: the window opens, the human calls PlayId (then FinalPlayId, if given, before
         *  the snap), the CPU calls the other side, and the ball is snapped. */
        bool PlayDown(int32 Down, int32 Distance, const TCHAR* PlayId, int32 Quarter = 1, const TCHAR* FinalPlayId = nullptr)
        {
            FPSSituationContext Situation;
            Situation.Down = Down;
            Situation.Distance = Distance;
            Situation.YardLine = 40;
            Situation.Quarter = Quarter;
            PlayCall->OpenPlayCall(Situation);
            bool bCalled = PlayCall->CallPlay(PlayId, EPSPlayCaller::Human);
            PlayCall->PollReadyToSnap(0.f);
            if (FinalPlayId)
            {
                bCalled = PlayCall->CallPlay(FinalPlayId, EPSPlayCaller::Human) && bCalled;
            }
            FPSTelemetrySnapEvent Snap;
            Snap.Down = Down;
            Snap.Distance = Distance;
            Snap.YardLine = 40;
            Bus->PublishSnap(Snap);
            return bCalled;
        }
    };

    static FPSTendencyCell MakeCell(bool bOffense, int32 Down, int32 Bucket, const TCHAR* Category, int32 Count, FName Personnel = NAME_None)
    {
        FPSTendencyCell Cell;
        Cell.bOffense = bOffense;
        Cell.Down = Down;
        Cell.DistanceBucket = Bucket;
        Cell.Personnel = Personnel;
        Cell.Category = Category;
        Cell.Count = Count;
        return Cell;
    }

    static FPSOpponentModelTuning LoadShippedTuning()
    {
        FPSOpponentModelTuning Tuning;
        NewObject<UPSDataIngestion>()->LoadOpponentModelTuningFromJson(UPSOpponentModel::GetDefaultTuningPath(), Tuning);
        return Tuning;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tendency tracker
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSOpponentTrackerTest,
    "PlaySports.AI.OpponentModel.TendencyTracker",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSOpponentTrackerTest::RunTest(const FString& Parameters)
{
    using namespace PSOpponentModelTests;

    const FPSOpponentModelTuning Shipped = LoadShippedTuning();
    const TArray<FString> Problems = PSOpponentModel::ValidateTuning(Shipped);
    TestEqual(TEXT("The shipped tuning is sound"), Problems.Num(), 0);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestTrue(TEXT("(It reads with four calls)"), FMath::IsNearlyEqual(Shipped.MinSamples, 4.f));
    TestEqual(TEXT("Third and 2 is short"), PSOpponentModel::GetDistanceBucket(2, Shipped), 0);
    TestEqual(TEXT("Second and 6 is medium"), PSOpponentModel::GetDistanceBucket(6, Shipped), 1);
    TestEqual(TEXT("First and 10 is long"), PSOpponentModel::GetDistanceBucket(10, Shipped), 2);

    FHumanGame Game;
    if (!Game.Start(*this))
    {
        Game.End();
        return false;
    }
    Game.Model->SetTuning(Shipped);

    // Four runs on 1st and 10 (one of them a deep shot he changed to a run before the snap).
    TestTrue(TEXT("He calls his plays"), Game.PlayDown(1, 10, TEXT("Offense_InsideZone")) && Game.PlayDown(1, 10, TEXT("Offense_PowerOSweep"))
        && Game.PlayDown(1, 10, TEXT("Offense_ShotgunDraw")) && Game.PlayDown(1, 10, TEXT("Offense_FourVerts"), 1, TEXT("Offense_InsideZone")));
    // A spike isn't a tendency.
    Game.PlayDown(2, 10, TEXT("Offense_Spike"));

    FPSTendencyRead Read = Game.Model->ReadTendency(true, 1, 10, NAME_None);
    TestEqual(TEXT("On 1st and 10 he has shown four calls"), Read.Samples, 4.f);
    TestTrue(TEXT("...read for that down and distance"), Read.Basis == EPSTendencyBasis::DownDistance);
    TestTrue(TEXT("...all runs: the call he changed counts as the run he ran"), FMath::IsNearlyEqual(Read.Shares.FindRef(TEXT("Run")), 1.f) && Read.TopCategory == TEXT("Run"));
    TestEqual(TEXT("...and none of his defense (he has none)"), Game.Model->ReadTendency(false, 1, 10, NAME_None).Samples, 0.f);
    int32 Counted = 0;
    for (const FPSTendencyCell& Cell : Game.Model->GetGameCells())
    {
        Counted += Cell.Count;
        TestTrue(TEXT("Only his offensive calls are counted, never the CPU's defense"), Cell.bOffense);
        TestNotEqual(TEXT("...and never the spike"), Cell.Category, FString(TEXT("Spike")));
    }
    TestEqual(TEXT("Four calls in all"), Counted, 4);

    // A short pass on 3rd and 8 in 12 personnel: too few calls there or on 3rd down, so the
    // read falls back to everything he has called.
    FPSTelemetryPersonnelEvent Personnel;
    Personnel.bOffense = true;
    Personnel.PackageId = TEXT("P12");
    Game.Bus->PublishPersonnel(Personnel);
    TestEqual(TEXT("The model sees the offense's personnel"), Game.Model->GetOffensePersonnel(), FName(TEXT("P12")));
    Game.PlayDown(3, 8, TEXT("Offense_SlantFlat"));
    Read = Game.Model->ReadTendency(true, 3, 8, TEXT("P12"));
    TestTrue(TEXT("3rd and 8 falls back to everything"), Read.Basis == EPSTendencyBasis::Overall);
    TestTrue(TEXT("...four runs and a short pass"), FMath::IsNearlyEqual(Read.Samples, 5.f) && FMath::IsNearlyEqual(Read.Shares.FindRef(TEXT("ShortPass")), 0.2f));

    // Three more from 12 personnel on 3rd and 8: now that exact situation has enough.
    Game.PlayDown(3, 8, TEXT("Offense_OutCurl"));
    Game.PlayDown(3, 8, TEXT("Offense_IFormSlants"));
    Game.PlayDown(3, 8, TEXT("Offense_ScreenLeft"));
    Read = Game.Model->ReadTendency(true, 3, 8, TEXT("P12"));
    TestTrue(TEXT("3rd and 8 in 12 personnel is read exactly"), Read.Basis == EPSTendencyBasis::Exact);
    TestTrue(TEXT("...three short passes and a screen"), FMath::IsNearlyEqual(Read.Shares.FindRef(TEXT("ShortPass")), 0.75f)
        && FMath::IsNearlyEqual(Read.Shares.FindRef(TEXT("Screen")), 0.25f) && FMath::IsNearlyEqual(Read.Shares.FindRef(TEXT("Run")), 0.f));
    Read = Game.Model->ReadTendency(true, 3, 8, TEXT("P11"));
    TestTrue(TEXT("In other personnel the same down and distance is read across personnel"), Read.Basis == EPSTendencyBasis::DownDistance);

    Game.End();
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Never psychic
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSOpponentNotPsychicTest,
    "PlaySports.AI.OpponentModel.NeverPsychic",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSOpponentNotPsychicTest::RunTest(const FString& Parameters)
{
    using namespace PSOpponentModelTests;

    FHumanGame Game;
    if (!Game.Start(*this))
    {
        Game.End();
        return false;
    }
    Game.Model->SetTuning(LoadShippedTuning());
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Game.PlayDown(2, 5, TEXT("Offense_InsideZone"));
    }
    FPSSituationContext Situation;
    Situation.Down = 2;
    Situation.Distance = 5;
    Situation.Quarter = 3;
    const FPSTendencyProfile Before = Game.Model->CounterTendency(false, Situation, FPSTendencyProfile());

    // He calls a deep shot: until it is snapped, the CPU's read is what it was.
    Situation.YardLine = 40;
    Game.PlayCall->OpenPlayCall(Situation);
    TestTrue(TEXT("He calls a deep shot"), Game.PlayCall->CallPlay(TEXT("Offense_FourVerts"), EPSPlayCaller::Human));
    const FPSTendencyRead Pending = Game.Model->ReadTendency(true, 2, 5, NAME_None);
    TestTrue(TEXT("The call isn't known before the snap"), FMath::IsNearlyEqual(Pending.Samples, 4.f) && FMath::IsNearlyEqual(Pending.Shares.FindRef(TEXT("DeepPass")), 0.f));
    const FPSTendencyProfile During = Game.Model->CounterTendency(false, Situation, FPSTendencyProfile());
    TestTrue(TEXT("...so the CPU's counter to it is unchanged"), During.CounterWeights.OrderIndependentCompareEqual(Before.CounterWeights));
    Game.PlayCall->PollReadyToSnap(0.f);

    FPSTelemetrySnapEvent Snap;
    Snap.Down = 2;
    Snap.Distance = 5;
    Game.Bus->PublishSnap(Snap);
    const FPSTendencyRead Shown = Game.Model->ReadTendency(true, 2, 5, NAME_None);
    TestTrue(TEXT("Once snapped it has been shown"), FMath::IsNearlyEqual(Shown.Samples, 5.f) && FMath::IsNearlyEqual(Shown.Shares.FindRef(TEXT("DeepPass")), 0.2f));

    Game.End();
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Counter-selection
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSOpponentCounterTest,
    "PlaySports.AI.OpponentModel.CounterSelection",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSOpponentCounterTest::RunTest(const FString& Parameters)
{
    using namespace PSOpponentModelTests;

    const FPSOpponentModelTuning Tuning = LoadShippedTuning();

    // A human who runs on 1st and 10.
    const TArray<FPSTendencyCell> Runner = { MakeCell(true, 1, 2, TEXT("Run"), 7), MakeCell(true, 1, 2, TEXT("ShortPass"), 1) };
    const FPSTendencyRead RunRead = PSOpponentModel::ReadTendency(Runner, {}, true, 1, 10, NAME_None, Tuning);
    TestTrue(TEXT("He runs seven times in eight"), FMath::IsNearlyEqual(RunRead.Shares.FindRef(TEXT("Run")), 0.875f));
    const TMap<FString, float> AgainstRun = PSOpponentModel::ComputeCounterWeights(RunRead, true, 1.f, Tuning);
    TestTrue(TEXT("The defense leans to base against the run"), AgainstRun.FindRef(TEXT("Base")) > 1.5f);
    TestTrue(TEXT("...and away from prevent"), AgainstRun.Contains(TEXT("Prevent")) && AgainstRun.FindRef(TEXT("Prevent")) < 1.f);
    for (const TPair<FString, float>& Entry : AgainstRun)
    {
        TestTrue(FString::Printf(TEXT("%s stays within the guardrails"), *Entry.Key), Entry.Value >= Tuning.MinMultiplier && Entry.Value <= Tuning.MaxMultiplier);
    }
    const TMap<FString, float> Gentle = PSOpponentModel::ComputeCounterWeights(RunRead, true, 0.4f, Tuning);
    TestTrue(TEXT("At less strength it leans less"), Gentle.FindRef(TEXT("Base")) > 1.f && Gentle.FindRef(TEXT("Base")) < AgainstRun.FindRef(TEXT("Base")));

    // No read, no strength, or an even mix: nothing changes.
    TestEqual(TEXT("No read, no counters"), PSOpponentModel::ComputeCounterWeights(FPSTendencyRead(), true, 1.f, Tuning).Num(), 0);
    TestEqual(TEXT("No strength, no counters"), PSOpponentModel::ComputeCounterWeights(RunRead, true, 0.f, Tuning).Num(), 0);
    const TArray<FPSTendencyCell> Balanced = { MakeCell(true, 1, 2, TEXT("Run"), 2), MakeCell(true, 1, 2, TEXT("ShortPass"), 2), MakeCell(true, 1, 2, TEXT("DeepPass"), 2),
        MakeCell(true, 1, 2, TEXT("PlayAction"), 2), MakeCell(true, 1, 2, TEXT("Screen"), 2) };
    for (const TPair<FString, float>& Entry : PSOpponentModel::ComputeCounterWeights(PSOpponentModel::ReadTendency(Balanced, {}, true, 1, 10, NAME_None, Tuning), true, 1.f, Tuning))
    {
        TestTrue(FString::Printf(TEXT("An even mix leaves %s alone"), *Entry.Key), FMath::IsNearlyEqual(Entry.Value, 1.f, 0.001f));
    }

    // A human defense that blitzes: the CPU offense screens.
    const TArray<FPSTendencyCell> Blitzer = { MakeCell(false, 3, 1, TEXT("Blitz"), 5), MakeCell(false, 3, 1, TEXT("Base"), 1) };
    const TMap<FString, float> AgainstBlitz = PSOpponentModel::ComputeCounterWeights(PSOpponentModel::ReadTendency(Blitzer, {}, false, 3, 6, NAME_None, Tuning), false, 1.f, Tuning);
    TestTrue(TEXT("Against the blitz the offense screens"), AgainstBlitz.FindRef(TEXT("Screen")) > 1.5f);
    TestTrue(TEXT("...and doesn't hold the ball for a deep shot"), AgainstBlitz.FindRef(TEXT("DeepPass")) < 1.f);

    // The coaching AI weighs the counters and says why.
    UWorld* World = CreateTestWorld();
    UPSOpponentModel* Model = World ? World->GetSubsystem<UPSOpponentModel>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (TestNotNull(TEXT("Model"), Model) && TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        Model->SetTuning(Tuning);
        // The runs from an earlier game: the same shares as RunRead, at their weight.
        Model->SetCareerCells(Runner);
        TestTrue(TEXT("(Enough to read at their weight)"), 8.f * Tuning.PriorGameWeight >= Tuning.MinSamples);
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.Quarter = 3;
        const FPSTendencyProfile Countered = Model->CounterTendency(false, Situation, FPSTendencyProfile());
        TestTrue(TEXT("The CPU defense's tendency carries the counters"), Countered.CounterWeights.FindRef(TEXT("Base")) > 1.5f);

        const TArray<FPSPlayDefinition> Plays = PlayCall->GetPlays(false);
        UPSCoachingAI* Coach = PlayCall->GetCoachingAI();
        const TArray<FPSPlaySuggestion> Plain = Coach->RankPlays(Situation, FPSTendencyProfile(), Plays, false);
        const TArray<FPSPlaySuggestion> Adapted = Coach->RankPlays(Situation, Countered, Plays, false);
        const FPSPlaySuggestion* PlainBase = Plain.FindByPredicate([](const FPSPlaySuggestion& Entry) { return Entry.PlayId == TEXT("Defense_43Cover2"); });
        const FPSPlaySuggestion* AdaptedBase = Adapted.FindByPredicate([](const FPSPlaySuggestion& Entry) { return Entry.PlayId == TEXT("Defense_43Cover2"); });
        if (TestNotNull(TEXT("A base call"), PlainBase) && TestNotNull(TEXT("...adapted"), AdaptedBase))
        {
            TestTrue(TEXT("Its weight is scaled by the counter"), FMath::IsNearlyEqual(AdaptedBase->Weight, PlainBase->Weight * Countered.CounterWeights.FindRef(TEXT("Base")), 0.01f));
            TestTrue(TEXT("...and the reason says so"), AdaptedBase->Reasons.ContainsByPredicate([](const FString& Reason) { return Reason.StartsWith(TEXT("Countering your tendencies")); }));
        }
    }
    if (World)
    {
        DestroyTestWorld(World);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Halftime adjustments and the dial
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSOpponentHalftimeTest,
    "PlaySports.AI.OpponentModel.HalftimeAdjustment",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSOpponentHalftimeTest::RunTest(const FString& Parameters)
{
    using namespace PSOpponentModelTests;

    FHumanGame Game;
    if (!Game.Start(*this))
    {
        Game.End();
        return false;
    }
    const FPSOpponentModelTuning Tuning = LoadShippedTuning();
    Game.Model->SetTuning(Tuning);
    TestTrue(TEXT("(The second half leans harder)"), Tuning.SecondHalfStrength > Tuning.FirstHalfStrength);
    TestTrue(TEXT("The first half leans at its strength"), FMath::IsNearlyEqual(Game.Model->GetAdaptationStrength(1), Tuning.DefaultAdaptationDial * Tuning.FirstHalfStrength));
    TestTrue(TEXT("...the second at its own"), FMath::IsNearlyEqual(Game.Model->GetAdaptationStrength(Tuning.HalftimeQuarter), Tuning.DefaultAdaptationDial * Tuning.SecondHalfStrength));

    TArray<FPSTelemetryOpponentAdjustmentEvent> Adjustments;
    const FDelegateHandle Handle = Game.Bus->OnOpponentAdjustmentMC.AddLambda([&Adjustments](const FPSTelemetryOpponentAdjustmentEvent& Event) { Adjustments.Add(Event); });

    // He runs through the first half; the CPU defense calls against him every down.
    for (int32 Index = 0; Index < 5; ++Index)
    {
        Game.PlayDown(1, 10, TEXT("Offense_InsideZone"), 2);
    }
    TestEqual(TEXT("No adjustment before the half"), Adjustments.Num(), 0);

    // The first call after the half leans harder, and says so once.
    Game.PlayDown(1, 10, TEXT("Offense_InsideZone"), Tuning.HalftimeQuarter);
    if (TestEqual(TEXT("The CPU defense adjusts at the half"), Adjustments.Num(), 1))
    {
        TestFalse(TEXT("...on defense"), Adjustments[0].bCpuOffense);
        TestEqual(TEXT("...in the third quarter"), Adjustments[0].Quarter, Tuning.HalftimeQuarter);
        TestTrue(TEXT("...leaning harder"), Adjustments[0].Strength > Adjustments[0].PreviousStrength);
        TestEqual(TEXT("...on his runs"), Adjustments[0].TopCategory, FString(TEXT("Run")));
        TestTrue(TEXT("...every one of them"), FMath::IsNearlyEqual(Adjustments[0].TopShare, 1.f) && Adjustments[0].Samples >= Tuning.MinSamples);
    }
    Game.PlayDown(1, 10, TEXT("Offense_InsideZone"), Tuning.HalftimeQuarter);
    TestEqual(TEXT("...only once"), Adjustments.Num(), 1);

    // The dial at 0 never adapts, however much it has seen.
    Game.Model->SetAdaptationDial(0.f);
    FPSSituationContext Situation;
    Situation.Down = 1;
    Situation.Distance = 10;
    Situation.Quarter = 4;
    TestEqual(TEXT("At dial 0 the CPU doesn't lean at all"), Game.Model->GetAdaptationStrength(4), 0.f);
    TestEqual(TEXT("...and counters nothing"), Game.Model->CounterTendency(false, Situation, FPSTendencyProfile()).CounterWeights.Num(), 0);
    Game.Model->SetAdaptationDial(0.5f);
    TestTrue(TEXT("At half dial, half the strength"), FMath::IsNearlyEqual(Game.Model->GetAdaptationStrength(4), 0.5f * Tuning.SecondHalfStrength));

    Game.Bus->OnOpponentAdjustmentMC.Remove(Handle);
    Game.End();
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Across games
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSOpponentAcrossGamesTest,
    "PlaySports.AI.OpponentModel.AcrossGames",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSOpponentAcrossGamesTest::RunTest(const FString& Parameters)
{
    using namespace PSOpponentModelTests;

    TArray<FPSTendencyCell> Merged = { MakeCell(true, 1, 2, TEXT("Run"), 2) };
    PSOpponentModel::MergeCells(Merged, { MakeCell(true, 1, 2, TEXT("Run"), 3), MakeCell(true, 1, 2, TEXT("Run"), 1, TEXT("P12")) });
    TestTrue(TEXT("Merging adds up the same cell and keeps a new one"), Merged.Num() == 2 && Merged[0].Count == 5 && Merged[1].Count == 1);

    FHumanGame Game;
    if (!Game.Start(*this))
    {
        Game.End();
        return false;
    }
    const FPSOpponentModelTuning Tuning = LoadShippedTuning();
    Game.Model->SetTuning(Tuning);
    Game.Model->SetCareerCells({});
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Game.PlayDown(1, 10, TEXT("Offense_InsideZone"));
    }
    TestTrue(TEXT("This game, four runs make a read"), Game.Model->ReadTendency(true, 1, 10, NAME_None).Basis != EPSTendencyBasis::None);

    // The game ends: its runs are an earlier game's now, worth PriorGameWeight each.
    Game.Model->EndGame();
    TestEqual(TEXT("A new game has no calls of its own"), Game.Model->GetGameCells().Num(), 0);
    TestEqual(TEXT("...the last game's are remembered"), Game.Model->GetCareerCells().Num(), 1);
    const FPSTendencyRead Remembered = Game.Model->ReadTendency(true, 1, 10, NAME_None);
    const float Weighted = 4.f * Tuning.PriorGameWeight;
    TestTrue(TEXT("(Remembered calls count for less)"), Weighted < Tuning.MinSamples);
    TestTrue(TEXT("Alone they are too few to act on"), Remembered.Basis == EPSTendencyBasis::None);

    // Two more runs this game, with the old ones, make a read again.
    Game.PlayDown(1, 10, TEXT("Offense_InsideZone"));
    Game.PlayDown(1, 10, TEXT("Offense_PowerOSweep"));
    const FPSTendencyRead Combined = Game.Model->ReadTendency(true, 1, 10, NAME_None);
    TestTrue(TEXT("This game's and the last game's runs together are read"), Combined.Basis != EPSTendencyBasis::None);
    TestTrue(TEXT("...counting the old ones at their weight"), FMath::IsNearlyEqual(Combined.Samples, 2.f + Weighted));

    Game.End();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
