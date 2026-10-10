// PSNetRandomStreamsTests.cpp -- Epic 108.1: the match's seeded random streams
//
// Tests covered:
//   1. The same match seed and the same snaps roll the same numbers in two worlds, whatever the
//      engine's global stream holds; one domain's or one player's draws never shift another's;
//      each snap begins new streams; another match seed rolls otherwise. The seed mixing is
//      integer arithmetic pinned to values worked out independently, so a run on another
//      platform checks it is the same there.
//   2. A pass's scatter rolls on the play's seeded stream: the same match seed and snap throw
//      the same ball to the same spot, whatever the global stream holds; another seed misses
//      elsewhere; every miss is within the passer's inaccuracy.
//   3. The tackle contest (the tackle, the strip and the hit's damage) rolls on the carrier's
//      seeded streams: the same seed and snap resolve the same series of tackles, hit for hit;
//      each hit draws its own damage spread.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PSBall.h"
#include "PSBallActionComponent.h"
#include "PSDifficultySubsystem.h"
#include "PSNetRandomStreams.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSNetRandomStreamsTests
{
    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    FPSTelemetrySnapEvent MakeSnap(int32 Down, int32 Distance, int32 YardLine, float GameClockSeconds)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.Down = Down;
        Snap.Distance = Distance;
        Snap.YardLine = YardLine;
        Snap.GameClockSeconds = GameClockSeconds;
        Snap.LineOfScrimmage = FVector(YardLine * 100.f, 0.f, 0.f);
        return Snap;
    }

    APSPlayerPawn* SpawnPlayer(UWorld* World, const TCHAR* PlayerId, EPlayerRole Role, float Awareness, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Player;
            Player.PlayerId = FName(PlayerId);
            Player.DisplayName = PlayerId;
            Player.Role = Role;
            Player.Speed = 70.f;
            Player.Agility = 70.f;
            Player.Strength = 70.f;
            Player.Acceleration = 70.f;
            Player.Awareness = Awareness;
            Pawn->InitializePlayer(Player);
        }
        return Pawn;
    }

    /** Where one pass came down, against where it was aimed. */
    struct FThrowOutcome
    {
        bool bThrown = false;
        FVector Target = FVector::ZeroVector;
        FVector Landing = FVector::ZeroVector;
        float MaxMiss = 0.f;
    };

    /** A fresh world, MatchSeed set, 1st and 10 snapped, then a 40-Awareness quarterback throws
     *  to his receiver with the global stream seeded with GlobalSeed. */
    FThrowOutcome ThrowOnce(int32 MatchSeed, int32 GlobalSeed)
    {
        FThrowOutcome Outcome;
        UWorld* World = CreateTestWorld();
        UPSNetRandomStreams* Streams = World ? World->GetSubsystem<UPSNetRandomStreams>() : nullptr;
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        if (Streams && Bus)
        {
            Streams->SetMatchSeed(MatchSeed);
            // Snapped before anyone is on the field: nobody's AI is bound to hear it.
            Bus->PublishSnap(MakeSnap(1, 10, 20, 900.f));

            APSPlayerPawn* Passer = SpawnPlayer(World, TEXT("QB_01"), EPlayerRole::Quarterback, 40.f, FVector(2000.f, 0.f, 100.f));
            APSPlayerPawn* Receiver = SpawnPlayer(World, TEXT("WR_01"), EPlayerRole::WideReceiver, 70.f, FVector(3500.f, 600.f, 100.f));
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(2000.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
            if (Passer && Receiver && Ball)
            {
                Ball->AttachToCarrier(Passer, TEXT("HandSocket"));
                Passer->GainPossession();
                const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Outcome](const FPSTelemetryThrowEvent& Event)
                {
                    Outcome.Target = Event.TargetLocation;
                    Outcome.Landing = Event.LandingLocation;
                });
                FMath::RandInit(GlobalSeed);
                Outcome.bThrown = Passer->ThrowPass(Ball, Receiver->GetActorLocation(), false, Receiver);
                Bus->OnThrowMC.Remove(Handle);

                // ThrowPass's inaccuracy: up to 2 cm per point of Awareness short of 100, scaled
                // for a CPU passer by the difficulty.
                UPSDifficultySubsystem* Difficulty = World->GetSubsystem<UPSDifficultySubsystem>();
                Outcome.MaxMiss = (100.f - 40.f) * 2.f * (Difficulty ? Difficulty->GetThrowScatterScale(Passer) : 1.f);
            }
        }
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
        if (World)
        {
            DestroyTestWorld(World);
        }
        return Outcome;
    }

    /** What a series of tackle attempts on one carrier came to: each attempt's result and the
     *  damage of each hit that landed. */
    struct FTackleSeries
    {
        TArray<bool> Results;
        TArray<float> Damage;
    };

    /** A fresh world, MatchSeed set, 1st and 10 snapped, then a strong linebacker tries Attempts
     *  tackles on a weak back, with the global stream seeded with GlobalSeed. */
    FTackleSeries TackleSeries(int32 MatchSeed, int32 GlobalSeed, int32 Attempts)
    {
        FTackleSeries Series;
        UWorld* World = CreateTestWorld();
        UPSNetRandomStreams* Streams = World ? World->GetSubsystem<UPSNetRandomStreams>() : nullptr;
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        if (Streams && Bus)
        {
            Streams->SetMatchSeed(MatchSeed);
            Bus->PublishSnap(MakeSnap(1, 10, 20, 900.f));
            APSPlayerPawn* Carrier = SpawnPlayer(World, TEXT("RB_01"), EPlayerRole::RunningBack, 50.f, FVector(2800.f, 0.f, 100.f));
            APSPlayerPawn* Tackler = SpawnPlayer(World, TEXT("LB_01"), EPlayerRole::Linebacker, 70.f, FVector(2850.f, 0.f, 100.f));
            if (Carrier && Tackler)
            {
                FPlayerAttributes Strong = Tackler->GetAttributes();
                Strong.Strength = 99.f;
                Tackler->InitializePlayer(Strong);
                FPlayerAttributes Weak = Carrier->GetAttributes();
                Weak.Strength = 40.f;
                Weak.Agility = 40.f;
                Carrier->InitializePlayer(Weak);
                Carrier->GainPossession();

                const FDelegateHandle Handle = Bus->OnDamageMC.AddLambda([&Series](const FPSTelemetryDamageEvent& Event)
                {
                    Series.Damage.Add(Event.Amount);
                });
                FMath::RandInit(GlobalSeed);
                for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
                {
                    Series.Results.Add(Carrier->GetBallActionComponent()->ResolveTackle(Tackler));
                }
                Bus->OnDamageMC.Remove(Handle);
            }
        }
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
        if (World)
        {
            DestroyTestWorld(World);
        }
        return Series;
    }

    /** Count draws from one domain's stream for Key. */
    TArray<float> Draw(UPSNetRandomStreams& Streams, const TCHAR* Domain, FName Key, int32 Count)
    {
        TArray<float> Rolls;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Rolls.Add(Streams.Roll(Domain, Key));
        }
        return Rolls;
    }
}

// ---------------------------------------------------------------------------
// 1. Same seed, same rolls; independent streams; new plays, new streams
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNetRandomStreamsSameSeedTest,
    "PlaySports.Net.RandomStreams.SameSeedSameRolls",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNetRandomStreamsSameSeedTest::RunTest(const FString& Parameters)
{
    using namespace PSNetRandomStreamsTests;

    UWorld* FirstWorld = CreateTestWorld();
    UWorld* SecondWorld = CreateTestWorld();
    UPSNetRandomStreams* First = FirstWorld ? FirstWorld->GetSubsystem<UPSNetRandomStreams>() : nullptr;
    UPSNetRandomStreams* Second = SecondWorld ? SecondWorld->GetSubsystem<UPSNetRandomStreams>() : nullptr;
    UPSTelemetryBus* FirstBus = FirstWorld ? FirstWorld->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSTelemetryBus* SecondBus = SecondWorld ? SecondWorld->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestTrue(TEXT("Two worlds, each with its streams and its bus"), First && Second && FirstBus && SecondBus))
    {
        if (FirstWorld)
        {
            DestroyTestWorld(FirstWorld);
        }
        if (SecondWorld)
        {
            DestroyTestWorld(SecondWorld);
        }
        return false;
    }
    TestTrue(TEXT("Get finds a world's streams from anything in it"), UPSNetRandomStreams::Get(FirstWorld) == First);

    // The mixing is integer arithmetic: these values were worked out outside the engine.
    TestEqual(TEXT("MixSeed(12345, 678)"), UPSNetRandomStreams::MixSeed(12345, 678u), -226495199);
    TestEqual(TEXT("MixSeed(0, 0)"), UPSNetRandomStreams::MixSeed(0, 0u), -1832243442);
    TestEqual(TEXT("MixSeed(-1, 0xFFFFFFFF)"), UPSNetRandomStreams::MixSeed(-1, 0xFFFFFFFFu), 693533081);
    TestEqual(TEXT("1st and 10 at the 20 with 15:00 to play"), static_cast<int32>(UPSNetRandomStreams::HashSnap(MakeSnap(1, 10, 20, 900.f))), 795187700);
    TestEqual(TEXT("3rd and 7 at the 45 with 6:52.5 to play"), static_cast<int32>(UPSNetRandomStreams::HashSnap(MakeSnap(3, 7, 45, 412.5f))), 181039852);
    TestTrue(TEXT("Text hashes ignore case (an FName's case is the process's first spelling)"),
        UPSNetRandomStreams::HashText(TEXT("QB_01")) == UPSNetRandomStreams::HashText(TEXT("qb_01")));

    const FName Quarterback(TEXT("QB_01"));
    const FName Runner(TEXT("RB_01"));
    const FName Linebacker(TEXT("LB_01"));

    // The same match seed and the same snap in both worlds; the global stream seeded differently
    // before each world's draws.
    First->SetMatchSeed(2026);
    Second->SetMatchSeed(2026);
    TestEqual(TEXT("A match starts at play 0"), First->GetPlayIndex(), 0);
    const FPSTelemetrySnapEvent Opening = MakeSnap(1, 10, 25, 900.f);
    FirstBus->PublishSnap(Opening);
    SecondBus->PublishSnap(Opening);
    TestEqual(TEXT("The bus's snap begins play 1"), First->GetPlayIndex(), 1);
    TestEqual(TEXT("Both worlds have the same play seed"), First->GetPlaySeed(), Second->GetPlaySeed());

    // First world: other domains and players draw before the quarterback's scatter.
    FMath::RandInit(1);
    First->Roll(TEXT("Tackle"), Runner);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        First->Roll(TEXT("Tackle"), Linebacker);
    }
    const TArray<float> FirstScatter = Draw(*First, TEXT("ThrowScatter"), Quarterback, 4);
    const float FirstRunnerSecondRoll = First->Roll(TEXT("Tackle"), Runner);

    // Second world: the scatter first, nobody else, and a different global stream.
    FMath::RandInit(987654);
    const TArray<float> SecondScatter = Draw(*Second, TEXT("ThrowScatter"), Quarterback, 4);
    const float SecondRunnerFirstRoll = Second->Roll(TEXT("Tackle"), Runner);
    const float SecondRunnerSecondRoll = Second->Roll(TEXT("Tackle"), Runner);
    FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));

    TestTrue(TEXT("The same seed and snap roll the same scatter, whatever else drew first and whatever the global stream held"),
        FirstScatter == SecondScatter);
    TestTrue(TEXT("A player's stream is his own: the linebacker's five draws didn't shift the runner's"),
        FirstRunnerSecondRoll == SecondRunnerSecondRoll);
    TestTrue(TEXT("...and his draws follow one another"), SecondRunnerFirstRoll != SecondRunnerSecondRoll);
    TestTrue(TEXT("Different domains are different streams"), FirstScatter.Num() > 0 && FirstScatter[0] != SecondRunnerFirstRoll);
    for (const float Roll : FirstScatter)
    {
        TestTrue(TEXT("A roll is in [0, 1)"), Roll >= 0.f && Roll < 1.f);
    }
    const float Spread = First->RollRange(TEXT("Spread"), 0.8f, 1.2f);
    TestTrue(TEXT("A ranged roll is in its range"), Spread >= 0.8f && Spread < 1.2f);
    const FVector Bounce = First->RollUnitVector(TEXT("Bounce"));
    TestTrue(TEXT("A unit vector is unit length"), FMath::Abs(Bounce.Size() - 1.0) < 1e-3);

    // The same situation snapped again is a new play: new streams.
    const int32 FirstPlaySeed = First->GetPlaySeed();
    FirstBus->PublishSnap(Opening);
    TestEqual(TEXT("Play 2"), First->GetPlayIndex(), 2);
    TestTrue(TEXT("A repeated situation is still a new play seed"), First->GetPlaySeed() != FirstPlaySeed);
    const TArray<float> NextScatter = Draw(*First, TEXT("ThrowScatter"), Quarterback, 4);
    TestTrue(TEXT("The next play's scatter is not the last one's"), NextScatter != FirstScatter);

    // Another match seed rolls another game.
    Second->SetMatchSeed(2027);
    TestEqual(TEXT("A new match seed starts over at play 0"), Second->GetPlayIndex(), 0);
    SecondBus->PublishSnap(Opening);
    TestTrue(TEXT("Another match seed, another play seed"), Second->GetPlaySeed() != FirstPlaySeed);
    TestTrue(TEXT("...and another scatter"), Draw(*Second, TEXT("ThrowScatter"), Quarterback, 4) != FirstScatter);

    // Starting the match over from the first seed replays it.
    Second->SetMatchSeed(2026);
    SecondBus->PublishSnap(Opening);
    TestTrue(TEXT("The match seed set again replays the first play's scatter"), Draw(*Second, TEXT("ThrowScatter"), Quarterback, 4) == FirstScatter);
    TestEqual(TEXT("A match seed is the same all match for the simulation"), Second->MakeMatchSeed(TEXT("PlaySimulation")), First->MakeMatchSeed(TEXT("PlaySimulation")));
    TestTrue(TEXT("...and differs by domain"), First->MakeMatchSeed(TEXT("PlaySimulation")) != First->MakeMatchSeed(TEXT("Other")));

    DestroyTestWorld(FirstWorld);
    DestroyTestWorld(SecondWorld);
    return true;
}

// ---------------------------------------------------------------------------
// 2. The pass's scatter is the play's seeded roll
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNetRandomStreamsThrowScatterTest,
    "PlaySports.Net.RandomStreams.ThrowScatterIsSeeded",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNetRandomStreamsThrowScatterTest::RunTest(const FString& Parameters)
{
    using namespace PSNetRandomStreamsTests;

    const FThrowOutcome First = ThrowOnce(77, 1);
    const FThrowOutcome Again = ThrowOnce(77, 424242);
    const FThrowOutcome Other = ThrowOnce(78, 1);
    if (!TestTrue(TEXT("All three passes were thrown and announced"), First.bThrown && Again.bThrown && Other.bThrown))
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("Seed 77 lands at %s, seed 78 at %s, aimed at %s (misses up to %.1f cm)."),
        *First.Landing.ToString(), *Other.Landing.ToString(), *First.Target.ToString(), First.MaxMiss));

    TestTrue(TEXT("The same match seed and snap throw the same ball, whatever the global stream held"), First.Landing == Again.Landing);
    TestTrue(TEXT("...aimed at the same target"), First.Target == Again.Target);
    TestTrue(TEXT("Another match seed misses elsewhere"), !(Other.Landing == First.Landing));
    TestTrue(TEXT("A 40-Awareness passer misses at all"), FVector::Dist2D(First.Landing, First.Target) > 0.0);
    for (const FThrowOutcome* Outcome : { &First, &Again, &Other })
    {
        TestTrue(TEXT("The miss is on the ground plane"), FMath::Abs(Outcome->Landing.Z - Outcome->Target.Z) < 1e-3);
        TestTrue(TEXT("...and within the passer's inaccuracy"), FVector::Dist2D(Outcome->Landing, Outcome->Target) <= Outcome->MaxMiss + 0.01);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 3. The tackle contest's rolls are the play's seeded rolls
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNetRandomStreamsTackleTest,
    "PlaySports.Net.RandomStreams.TackleContestIsSeeded",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNetRandomStreamsTackleTest::RunTest(const FString& Parameters)
{
    using namespace PSNetRandomStreamsTests;

    const int32 Attempts = 10;
    const FTackleSeries First = TackleSeries(31, 5, Attempts);
    const FTackleSeries Again = TackleSeries(31, 55555, Attempts);
    const FTackleSeries Other = TackleSeries(32, 5, Attempts);
    if (!TestEqual(TEXT("Every attempt was resolved"), First.Results.Num(), Attempts))
    {
        return false;
    }
    FString Hits;
    for (const float Amount : First.Damage)
    {
        Hits += FString::Printf(TEXT(" %.3f"), Amount);
    }
    AddInfo(FString::Printf(TEXT("Seed 31: %d hits landed, damage%s."), First.Damage.Num(), *Hits));

    TestTrue(TEXT("The same match seed and snap resolve the same tackles, whatever the global stream held"), First.Results == Again.Results);
    TestTrue(TEXT("...with the same damage, hit for hit"), First.Damage == Again.Damage);
    TestTrue(TEXT("A strong tackler lands hits"), First.Damage.Num() >= 2);
    bool bSpreadVaries = false;
    for (int32 Index = 1; Index < First.Damage.Num(); ++Index)
    {
        bSpreadVaries |= First.Damage[Index] != First.Damage[0];
    }
    TestTrue(TEXT("Each hit draws its own damage spread (an unseeded model drew the same one every time)"), bSpreadVaries);
    TestTrue(TEXT("Another match seed resolves them otherwise"), !(Other.Results == First.Results && Other.Damage == First.Damage));
    return true;
}

#endif
