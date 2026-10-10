// PSNetRandomStreamsTests.cpp -- Epic 108.1: the match's seeded random streams
//
// Tests covered:
//   1. The same match seed and the same snaps roll the same numbers in two worlds, whatever the
//      engine's global stream holds; one domain's or one player's draws never shift another's;
//      each snap begins new streams; another match seed rolls otherwise. The seed mixing is
//      integer arithmetic pinned to values worked out independently, so a run on another
//      platform checks it is the same there.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PSNetRandomStreams.h"
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

#endif
