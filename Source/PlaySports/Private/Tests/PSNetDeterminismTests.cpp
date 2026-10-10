// PSNetDeterminismTests.cpp -- Epic 108.1: the determinism audit's automated checks
//
// The feasibility study for online play (Specs/Determinism_Audit.md, "Epic 108") runs the same
// seeded game twice and compares what came of it, event for event and frame for frame.
//
// Tests covered:
//   1. A seeded quick-sim game is the same game whatever the engine's global random stream holds:
//      it rolls on the simulation's own stream, and the unrecorded game for the seed agrees. The
//      recording's fingerprint is logged for comparison with a run on another platform.
//   2. A version 1 recording, whose seed seeded the global stream, loads as version 2 without
//      its seed: its events still play back, and re-simulating it is refused.
//   3. A live play, headless: 22 players under their AI, the CPU's calls, the snap, the routes,
//      the rush and the coverage, run twice in fresh worlds with the same match seed (and the
//      global stream seeded differently) at a fixed step in a fixed order, is the same play: the
//      same bus events and the same position and velocity of every player and the ball, frame
//      for frame. A fixed-step integrator stands in for UFloatingPawnMovement, which a world
//      that never ticks does not drive.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "PSAIFieldSnapshot.h"
#include "PSBall.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderGapSubsystem.h"
#include "PSDefenseController.h"
#include "PSDeterminism.h"
#include "PSFieldGrid.h"
#include "PSNetRandomStreams.h"
#include "PSOffenseController.h"
#include "PSPlaySimulation.h"
#include "PSPlayerPawn.h"
#include "PSQuickSimRunner.h"
#include "PSReplayFormat.h"
#include "PSReplayRecorder.h"
#include "PSRushMoveComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSNetDeterminismTests
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

    void UnseedRandom()
    {
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    }

    /** A side of eleven with every rating at Rating. */
    TArray<FPlayerAttributes> MakeRoster(const TCHAR* Prefix, float Rating)
    {
        static const TArray<EPlayerRole> Roles = {
            EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::WideReceiver,
            EPlayerRole::TightEnd, EPlayerRole::OffensiveLineman, EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman,
            EPlayerRole::Linebacker, EPlayerRole::DefensiveBack, EPlayerRole::DefensiveBack };

        TArray<FPlayerAttributes> Roster;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            FPlayerAttributes& Player = Roster.AddDefaulted_GetRef();
            Player.PlayerId = FName(*FString::Printf(TEXT("%s_%02d"), Prefix, Index));
            Player.DisplayName = Player.PlayerId.ToString();
            Player.Role = Roles[Index];
            Player.WeightKg = 100.f;
            Player.HeightCm = 188.f;
            Player.Speed = Rating;
            Player.Agility = Rating;
            Player.Strength = Rating;
            Player.Acceleration = Rating;
            Player.Awareness = Rating;
            Player.Stamina = Rating;
        }
        return Roster;
    }

    /** One number for a recording's events (their ticks, types and payloads), to compare runs
     *  on different machines by eye. */
    uint32 Fingerprint(const FPSReplayRecording& Recording)
    {
        uint32 Crc = 0;
        for (const FPSReplayEventRecord& Event : Recording.Events)
        {
            const FString Line = FString::Printf(TEXT("%d|%s|%s"), Event.TickIndex, *Event.EventType, *Event.PayloadJson);
            Crc = FCrc::StrCrc32(*Line, Crc);
        }
        return Crc;
    }

    // --- The live play ----------------------------------------------------------------------

    constexpr float FrameSeconds = 1.f / 30.f;
    constexpr int32 PlayFrames = 150;
    constexpr float ScrimmageX = 2000.f;

    /** The eleven on each side, as the game's default personnel lines up. */
    TArray<EPlayerRole> StandardRoles()
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
        return Roles;
    }

    /** A player at Location under his side's AI, listening to the bus. */
    APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, int32 Number, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(*FString::Printf(TEXT("DET_%02d"), Number));
        Attributes.DisplayName = Attributes.PlayerId.ToString();
        Attributes.Role = Role;
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

    APSPlayerPawn* FindFirst(const TArray<APSPlayerPawn*>& Players, EPlayerRole Role)
    {
        APSPlayerPawn* const* Found = Players.FindByPredicate([Role](const APSPlayerPawn* Player) { return Player->GetAttributes().Role == Role; });
        return Found ? *Found : nullptr;
    }

    /** A float's exact bits, so frames compare exactly. */
    FString Bits(double Value)
    {
        const float Narrow = static_cast<float>(Value);
        uint32 Raw = 0;
        FMemory::Memcpy(&Raw, &Narrow, sizeof(Raw));
        return FString::Printf(TEXT("%08x"), Raw);
    }

    /** One frame of what the game ticks, at a fixed step in a fixed order (the players in
     *  PlayerId order): the field read, the simulation, each player's AI (deciding every frame)
     *  and pass rush, the gap fits; then each player moves by his movement input at his top
     *  speed (the stand-in for UFloatingPawnMovement), and a ball in the air flies. */
    void StepFrame(UWorld* World, const TArray<APSPlayerPawn*>& Players, UPSPlaySimulation* Simulation, APSBall* Ball)
    {
        if (UPSAIFieldSnapshot* Field = World->GetSubsystem<UPSAIFieldSnapshot>())
        {
            Field->Invalidate();
        }
        Simulation->AdvancePlay(FrameSeconds);
        for (APSPlayerPawn* Player : Players)
        {
            if (const APSDefenseController* Defense = Cast<APSDefenseController>(Player->GetController()))
            {
                Defense->GetDefenderAI()->UpdateAI(FrameSeconds, 0.f);
                Defense->GetRushMoves()->TickRush(FrameSeconds);
            }
            else if (const APSOffenseController* Offense = Cast<APSOffenseController>(Player->GetController()))
            {
                Offense->GetSkillAI()->UpdateAI(FrameSeconds, 0.f);
            }
        }
        if (UPSDefenderGapSubsystem* Gaps = World->GetSubsystem<UPSDefenderGapSubsystem>())
        {
            Gaps->UpdateFits(FrameSeconds);
        }
        for (APSPlayerPawn* Player : Players)
        {
            const FVector Input = Player->ConsumeMovementInputVector();
            UFloatingPawnMovement* Movement = Player->GetFloatingMovementComponent();
            const float TopSpeed = Movement ? Movement->GetMaxSpeed() : 600.f;
            FVector Velocity = Input.GetClampedToMaxSize(1.f) * TopSpeed;
            Velocity.Z = 0.f;
            if (Movement)
            {
                Movement->Velocity = Velocity;
            }
            Player->SetActorLocation(Player->GetActorLocation() + Velocity * FrameSeconds);
        }
        if (Ball && !Ball->GetAttachParentActor())
        {
            UProjectileMovementComponent* Flight = Ball->GetProjectileMovement();
            if (Flight && Flight->IsActive() && Flight->UpdatedComponent)
            {
                Flight->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
            }
        }
    }

    /** What one run of the live play came to. */
    struct FLivePlayRun
    {
        bool bRan = false;
        FPSReplayRecording Recording;
        /** Per frame: the play's phase, then every player's spot and velocity and the ball's spot,
         *  as exact bits. */
        TArray<FString> Frames;
        FPlayState Final;
        int32 Throws = 0;
        float LongestMove = 0.f;
    };

    /** The live play in a fresh world with MatchSeed set, the global stream seeded with
     *  GlobalSeed. */
    FLivePlayRun RunLivePlay(int32 MatchSeed, int32 GlobalSeed)
    {
        FLivePlayRun Run;
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        UPSNetRandomStreams* Streams = World ? World->GetSubsystem<UPSNetRandomStreams>() : nullptr;
        if (!Bus || !Streams)
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return Run;
        }
        Streams->SetMatchSeed(MatchSeed);
        FMath::RandInit(GlobalSeed);

        // Two elevens lined up from the 20, in PlayerId order, and the ball in the center's hands.
        const TArray<EPlayerRole> Roles = StandardRoles();
        const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, ScrimmageX);
        TArray<APSPlayerPawn*> Players;
        TArray<FVector> LinedUp;
        TArray<FPlayerAttributes> OffenseRoster;
        TArray<FPlayerAttributes> DefenseRoster;
        for (int32 Index = 0; Index < Roles.Num() && Index < Lineup.Num(); ++Index)
        {
            if (APSPlayerPawn* Player = SpawnPlayer(World, Roles[Index], Index, Lineup[Index]))
            {
                Players.Add(Player);
                LinedUp.Add(Player->GetActorLocation());
                (Player->TeamSide == EPSTeamSide::Offense ? OffenseRoster : DefenseRoster).Add(Player->GetAttributes());
            }
        }
        APSPlayerPawn* Center = FindFirst(Players, EPlayerRole::OffensiveLineman);
        APSPlayerPawn* Quarterback = FindFirst(Players, EPlayerRole::Quarterback);
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(ScrimmageX, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
        if (Players.Num() != Roles.Num() || !Center || !Quarterback || !Ball)
        {
            DestroyTestWorld(World);
            UnseedRandom();
            return Run;
        }

        // The live simulation, seeded from the match as the game mode seeds it, recorded from its
        // first announcement.
        UPSPlaySimulation* Simulation = NewObject<UPSPlaySimulation>();
        Simulation->InitializePlay(OffenseRoster, DefenseRoster);
        Simulation->SeedRolls(Streams->MakeMatchSeed(TEXT("PlaySimulation")));
        UPSReplayRecorder* Recorder = NewObject<UPSReplayRecorder>();
        FPSReplayRecording Start = UPSReplayFormat::MakeRecording(Simulation->GetPlayState(), OffenseRoster, DefenseRoster);
        Start.Header.RandomSeed = MatchSeed;
        Start.Header.FixedDeltaSeconds = FrameSeconds;
        Recorder->BeginRecording(Bus, Start);
        Simulation->InitializeWithWorld(World);
        const FDelegateHandle ThrowHandle = Bus->OnThrowMC.AddLambda([&Run](const FPSTelemetryThrowEvent&) { ++Run.Throws; });

        // The snap: the quarterback takes the ball, and the CPU calls both sides.
        Ball->AttachToCarrier(Quarterback, TEXT("HandSocket"));
        Quarterback->GainPossession();
        FPSTelemetrySnapEvent Snap;
        Snap.Down = 1;
        Snap.Distance = 10;
        Snap.YardLine = 20;
        Snap.GameClockSeconds = 900.f;
        Snap.LineOfScrimmage = FVector(ScrimmageX, 0.f, 0.f);
        Recorder->SetTick(0);
        Bus->PublishSnap(Snap);
        Simulation->TriggerSnap();

        for (int32 Frame = 1; Frame <= PlayFrames; ++Frame)
        {
            Recorder->SetTick(Frame);
            StepFrame(World, Players, Simulation, Ball);

            FString Line = FString::Printf(TEXT("%d"), static_cast<int32>(Simulation->GetPlayState().Phase));
            for (const APSPlayerPawn* Player : Players)
            {
                const FVector Spot = Player->GetActorLocation();
                const FVector Velocity = Player->GetVelocity();
                Line += FString::Printf(TEXT(" %s:%s,%s,%s,%s"), *Player->GetAttributes().PlayerId.ToString(),
                    *Bits(Spot.X), *Bits(Spot.Y), *Bits(Velocity.X), *Bits(Velocity.Y));
            }
            const FVector BallSpot = Ball->GetActorLocation();
            Line += FString::Printf(TEXT(" Ball:%s,%s,%s"), *Bits(BallSpot.X), *Bits(BallSpot.Y), *Bits(BallSpot.Z));
            Run.Frames.Add(Line);
        }

        for (int32 Index = 0; Index < Players.Num(); ++Index)
        {
            Run.LongestMove = FMath::Max(Run.LongestMove, static_cast<float>(FVector::Dist2D(Players[Index]->GetActorLocation(), LinedUp[Index])));
        }
        Bus->OnThrowMC.Remove(ThrowHandle);
        Run.Recording = Recorder->EndRecording();
        Simulation->InitializeWithWorld(nullptr);
        Run.Final = Simulation->GetPlayState();
        Run.bRan = true;

        DestroyTestWorld(World);
        UnseedRandom();
        return Run;
    }

    /** The first frame two runs part ways at, and the first player (or the ball) that differs
     *  there; empty when every frame matches. */
    FString FindFirstFrameDifference(const FLivePlayRun& Expected, const FLivePlayRun& Actual)
    {
        const int32 Shared = FMath::Min(Expected.Frames.Num(), Actual.Frames.Num());
        for (int32 Frame = 0; Frame < Shared; ++Frame)
        {
            if (Expected.Frames[Frame] == Actual.Frames[Frame])
            {
                continue;
            }
            TArray<FString> Want;
            TArray<FString> Got;
            Expected.Frames[Frame].ParseIntoArray(Want, TEXT(" "));
            Actual.Frames[Frame].ParseIntoArray(Got, TEXT(" "));
            for (int32 Index = 0; Index < FMath::Min(Want.Num(), Got.Num()); ++Index)
            {
                if (Want[Index] != Got[Index])
                {
                    return FString::Printf(TEXT("frame %d: expected %s, got %s"), Frame + 1, *Want[Index], *Got[Index]);
                }
            }
            return FString::Printf(TEXT("frame %d"), Frame + 1);
        }
        return Expected.Frames.Num() == Actual.Frames.Num() ? FString() : TEXT("frame count");
    }
}

// ---------------------------------------------------------------------------
// 1. A seeded quick sim is the same game whatever the global stream holds
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNetDeterminismQuickSimTest,
    "PlaySports.Net.Determinism.QuickSimIgnoresGlobalStream",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNetDeterminismQuickSimTest::RunTest(const FString& Parameters)
{
    using namespace PSNetDeterminismTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    const TArray<FPlayerAttributes> Home = MakeRoster(TEXT("HOM"), 74.f);
    const TArray<FPlayerAttributes> Away = MakeRoster(TEXT("AWY"), 70.f);
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();

    FMath::RandInit(1);
    FPSReplayRecording First;
    const FPSQuickSimResult FirstScore = Runner->RecordGame(World, Home, Away, 2024, First);

    // Something else draws from the global stream, seeded otherwise, between the two games.
    FMath::RandInit(2);
    for (int32 Draw = 0; Draw < 37; ++Draw)
    {
        FMath::FRand();
    }
    FPSReplayRecording Again;
    const FPSQuickSimResult AgainScore = Runner->RecordGame(World, Home, Away, 2024, Again);

    FPSReplayRecording Other;
    Runner->RecordGame(World, Home, Away, 2025, Other);
    const FPSQuickSimResult Unrecorded = Runner->SimulateGame(Home, Away, 2024);
    UnseedRandom();

    AddInfo(FString::Printf(TEXT("Seed 2024: %d events, final %d-%d, fingerprint %08x (compare with a run on another platform)."),
        First.Events.Num(), FirstScore.HomeScore, FirstScore.AwayScore, Fingerprint(First)));
    TestTrue(TEXT("A real game was recorded"), First.Events.Num() > 40);
    const FPSReplayDivergence Divergence = UPSDeterminism::FindFirstDivergence(First, Again);
    TestFalse(*FString::Printf(TEXT("The same seed plays the same game whatever the global stream holds (%s)"), *UPSDeterminism::DescribeDivergence(Divergence)),
        Divergence.bDiverged);
    TestEqual(TEXT("...to the same home score"), AgainScore.HomeScore, FirstScore.HomeScore);
    TestEqual(TEXT("...and away score"), AgainScore.AwayScore, FirstScore.AwayScore);
    TestEqual(TEXT("The unrecorded game for the seed agrees at home"), Unrecorded.HomeScore, FirstScore.HomeScore);
    TestEqual(TEXT("...and away"), Unrecorded.AwayScore, FirstScore.AwayScore);

    // Another seed: compare the events alone (the headers differ by their seeds).
    FPSReplayRecording OtherEvents = Other;
    OtherEvents.Header = First.Header;
    const FPSReplayDivergence Elsewhere = UPSDeterminism::FindFirstDivergence(First, OtherEvents);
    AddInfo(FString::Printf(TEXT("Seed 2025 against 2024: %s"), *UPSDeterminism::DescribeDivergence(Elsewhere)));
    TestTrue(TEXT("Another seed plays another game"), Elsewhere.bDiverged && Elsewhere.EventIndex > 0);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// 2. A version 1 recording's seed is dropped on load
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNetDeterminismVersionOneTest,
    "PlaySports.Net.Determinism.VersionOneSeedsMigrateToPlaybackOnly",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNetDeterminismVersionOneTest::RunTest(const FString& Parameters)
{
    using namespace PSNetDeterminismTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();
    FPSReplayRecording Recording;
    Runner->RecordGame(World, MakeRoster(TEXT("HOM"), 72.f), MakeRoster(TEXT("AWY"), 72.f), 4321, Recording);
    TestEqual(TEXT("New recordings are version 2"), Recording.Header.FormatVersion, 2);
    TestEqual(TEXT("...the current version"), UPSReplayFormat::CurrentFormatVersion, 2);

    // The same recording as a version 1 build wrote it.
    FPSReplayRecording VersionOne = Recording;
    VersionOne.Header.FormatVersion = 1;
    FPSReplayRecording Loaded;
    if (!TestTrue(TEXT("A version 1 recording loads"), UPSReplayFormat::DeserializeFromJson(UPSReplayFormat::SerializeToJson(VersionOne), Loaded)))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("...migrated to version 2"), Loaded.Header.FormatVersion, 2);
    TestEqual(TEXT("...without its seed, which seeded the global stream"), Loaded.Header.RandomSeed, 0);
    TestEqual(TEXT("...with every event kept for playback"), Loaded.Events.Num(), Recording.Events.Num());
    TestEqual(TEXT("...and its step"), Loaded.Header.FixedDeltaSeconds, Recording.Header.FixedDeltaSeconds);

    FPSReplayRecording Replay;
    FPSQuickSimResult Result;
    FString Failure;
    TestFalse(TEXT("Re-simulating it is refused"), Runner->ReplayGame(World, Loaded, Replay, Result, Failure));
    TestTrue(*FString::Printf(TEXT("...for want of a seed (%s)"), *Failure), Failure.Contains(TEXT("seed")));

    // A version 2 recording keeps its seed through a save and load.
    FPSReplayRecording Current;
    TestTrue(TEXT("A version 2 recording loads"), UPSReplayFormat::DeserializeFromJson(UPSReplayFormat::SerializeToJson(Recording), Current));
    TestEqual(TEXT("...with its seed"), Current.Header.RandomSeed, 4321);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// 3. A live play, run twice, is the same play
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSNetDeterminismLivePlayTest,
    "PlaySports.Net.Determinism.LivePlaySameSeedSamePlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSNetDeterminismLivePlayTest::RunTest(const FString& Parameters)
{
    using namespace PSNetDeterminismTests;

    const FLivePlayRun First = RunLivePlay(1108, 1);
    const FLivePlayRun Again = RunLivePlay(1108, 8675309);
    const FLivePlayRun Other = RunLivePlay(1109, 1);
    if (!TestTrue(TEXT("All three plays ran"), First.bRan && Again.bRan && Other.bRan))
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("Seed 1108: %d bus events, %d throws, a player moved up to %.0f cm, fingerprint %08x."),
        First.Recording.Events.Num(), First.Throws, First.LongestMove, Fingerprint(First.Recording)));

    TestTrue(TEXT("The snap was recorded"), First.Recording.Events.ContainsByPredicate([](const FPSReplayEventRecord& Event) { return Event.EventType == TEXT("Snap"); }));
    TestTrue(TEXT("...and the CPU's calls"), First.Recording.Events.ContainsByPredicate([](const FPSReplayEventRecord& Event) { return Event.EventType == TEXT("PlayCall"); }));
    TestEqual(TEXT("Every frame was stepped"), First.Frames.Num(), PlayFrames);

    const FPSReplayDivergence Divergence = UPSDeterminism::FindFirstDivergence(First.Recording, Again.Recording);
    TestFalse(*FString::Printf(TEXT("The same match seed plays the same bus events (%s)"), *UPSDeterminism::DescribeDivergence(Divergence)),
        Divergence.bDiverged);
    const FString FrameDifference = FindFirstFrameDifference(First, Again);
    TestTrue(*FString::Printf(TEXT("...and moves every player and the ball the same, frame for frame (%s)"), *FrameDifference), FrameDifference.IsEmpty());
    TestTrue(TEXT("...to the same game state"), FPlayState::StaticStruct()->CompareScriptStruct(&First.Final, &Again.Final, PPF_None));
    TestEqual(TEXT("...with the same throws"), Again.Throws, First.Throws);

    // Another match seed may or may not change a play this short (a flag, a throw's miss).
    FPSReplayRecording OtherEvents = Other.Recording;
    OtherEvents.Header = First.Recording.Header;
    FString OtherFrames = FindFirstFrameDifference(First, Other);
    if (OtherFrames.IsEmpty())
    {
        OtherFrames = TEXT("identical");
    }
    AddInfo(FString::Printf(TEXT("Seed 1109 against 1108: events %s; frames %s."),
        *UPSDeterminism::DescribeDivergence(UPSDeterminism::FindFirstDivergence(First.Recording, OtherEvents)), *OtherFrames));
    return true;
}

#endif
