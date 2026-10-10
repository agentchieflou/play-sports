// PSPlayDemoTests.cpp - live-play demos: real plays recorded from the running game
//
// Tests covered:
//   1. PlaySports.Demo.Catalog: Data/play_demos.json loads and is sound; ValidateCatalog catches
//      a bad rate, a reused ID, one team twice, a missing call, no seed and an unknown outcome.
//   2. PlaySports.Demo.ReplayParticipants: the replay format's optional Teams and Participants
//      survive a round trip; a recording written before them still loads, with neither.
//   3. PlaySports.Demo.BallAndSampler: the ball overlaps players (so a catch can resolve) and
//      blocks the world (so it lands); a pass flies on past an offensive lineman and the passer
//      and is caught by a receiver; the sampler names a pawn's new player after a substitution.
//   4. PlaySports.Demo.SanityChecks: CheckRun passes a sound synthetic play and catches each kind
//      of failure: a frame gap, a player who stands still, no result, a NaN, a player too fast,
//      a capsule through the ground.
//   5. PlaySports.Demo.LivePlays: every demo in Data/play_demos.json, run end to end in a ticking
//      world under APSGameMode, recorded to Saved/PlayDemos, each held to the sanity checks.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PSBall.h"
#include "PSPlayDemoRunner.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSReplayFormat.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayDemoTests
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

    FPlayerAttributes MakePlayer(const TCHAR* PlayerId, EPlayerRole Role, int32 JerseyNumber = 0)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.Speed = 80.f;
        Player.Agility = 80.f;
        Player.Strength = 80.f;
        Player.Acceleration = 80.f;
        Player.Awareness = 80.f;
        Player.Stamina = 100.f;
        Player.JerseyNumber = JerseyNumber;
        return Player;
    }

    APSPlayerPawn* SpawnPlayer(UWorld* World, const FPlayerAttributes& Player, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            Pawn->InitializePlayer(Player);
        }
        return Pawn;
    }

    FPSPlayDemoDef MakeDemo(const TCHAR* DemoId)
    {
        FPSPlayDemoDef Demo;
        Demo.DemoId = FName(DemoId);
        Demo.Intent = TEXT("A test");
        Demo.HomeTeamId = TEXT("Hawks");
        Demo.AwayTeamId = TEXT("Wolves");
        Demo.OffensePlayId = TEXT("Offense_SlantFlat");
        Demo.DefensePlayId = TEXT("Defense_34Cover3");
        Demo.Seed = 7;
        return Demo;
    }

    /** A sound synthetic play at 30 Hz: 22 players running upfield from the snap at 3 m/s, the
     *  ball carried by the first, the snap at frame 10 and the result on the bus after it. */
    FPSPlayDemoRun MakeSoundRun(const FPSPlayDemoCatalog& Catalog)
    {
        FPSPlayDemoRun Run;
        Run.bRan = true;
        FPSPlayDemoSummary& Summary = Run.Summary;
        const float Step = 1.f / Catalog.FrameRateHz;
        Summary.SnapTime = 10.f * Step;
        Summary.WhistleTime = 50.f * Step;
        Summary.PlayerSpeedLimitCmPerSec = 1050.f;
        Summary.BallSpeedLimitCmPerSec = 5000.f;
        Summary.GroundZ = 0.f;
        Summary.PawnHalfHeightCm = 88.f;
        Summary.BallRadiusCm = 15.f;
        for (int32 Index = 0; Index < 60; ++Index)
        {
            FPSSnapshotFrame& Frame = Run.Recording.Frames.AddDefaulted_GetRef();
            Frame.FrameIndex = Index;
            Frame.Time = Index * Step;
            const float Run3ms = Index > 10 ? (Index - 10) * Step * 300.f : 0.f;
            for (int32 Player = 0; Player < 22; ++Player)
            {
                FPSPawnSnapshot& Snapshot = Frame.Pawns.AddDefaulted_GetRef();
                Snapshot.PlayerId = FName(*FString::Printf(TEXT("P%02d"), Player));
                Snapshot.TeamSide = Player < 11 ? EPSTeamSide::Offense : EPSTeamSide::Defense;
                Snapshot.Location = FVector(2000.f + Run3ms, Player * 200.f - 2100.f, 100.f);
                Snapshot.Velocity = Index > 10 ? FVector(300.f, 0.f, 0.f) : FVector::ZeroVector;
                Snapshot.bHasBall = Player == 0;
            }
            Frame.bBallSampled = true;
            Frame.BallLocation = Frame.Pawns[0].Location;
            Frame.BallVelocity = Frame.Pawns[0].Velocity;
        }
        FPSReplayEventRecord Result;
        Result.EventType = TEXT("PlayResult");
        Result.TimestampSeconds = 55.f * Step;
        Run.Recording.Events.Add(Result);
        return Run;
    }

    bool HasProblem(const TArray<FString>& Problems, const TCHAR* Needle)
    {
        return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
    }
}

// ---------------------------------------------------------------------------
// 1. The demo set's data
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPlayDemoCatalogTest,
    "PlaySports.Demo.Catalog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDemoCatalogTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDemoTests;

    UPSPlayDemoRunner* Runner = NewObject<UPSPlayDemoRunner>();
    if (!TestTrue(TEXT("Data/play_demos.json loads and is sound"), Runner->LoadCatalogFromJson(UPSPlayDemoRunner::GetDefaultCatalogPath())))
    {
        return false;
    }
    const FPSPlayDemoCatalog& Catalog = Runner->GetCatalog();
    TestEqual(TEXT("It records at 30 Hz"), Catalog.FrameRateHz, 30.f);
    TestTrue(TEXT("It has the brief's four plays and more"), Catalog.PlayDemos.Num() >= 4);

    FPSPlayDemoCatalog Bad;
    Bad.FrameRateHz = 0.f;
    Bad.PlayDemos.Add(MakeDemo(TEXT("Twice")));
    FPSPlayDemoDef Reused = MakeDemo(TEXT("Twice"));
    Reused.AwayTeamId = Reused.HomeTeamId;
    Reused.DefensePlayId = NAME_None;
    Reused.SeedTries = 0;
    Reused.WantedOutcome = TEXT("Safety");
    Bad.PlayDemos.Add(Reused);
    const TArray<FString> Problems = UPSPlayDemoRunner::ValidateCatalog(Bad);
    TestTrue(TEXT("A rate of 0 is caught"), HasProblem(Problems, TEXT("FrameRateHz")));
    TestTrue(TEXT("...a reused ID"), HasProblem(Problems, TEXT("used twice")));
    TestTrue(TEXT("...one team on both sides"), HasProblem(Problems, TEXT("two different teams")));
    TestTrue(TEXT("...a missing call"), HasProblem(Problems, TEXT("both calls")));
    TestTrue(TEXT("...no seed to try"), HasProblem(Problems, TEXT("SeedTries")));
    TestTrue(TEXT("...and an outcome that isn't one"), HasProblem(Problems, TEXT("WantedOutcome")));
    TestEqual(TEXT("A sound catalog has no problem"), UPSPlayDemoRunner::ValidateCatalog(Catalog).Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// 2. The replay format's teams and participants
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPlayDemoReplayParticipantsTest,
    "PlaySports.Demo.ReplayParticipants",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDemoReplayParticipantsTest::RunTest(const FString& Parameters)
{
    FPSReplayRecording Recording = UPSReplayFormat::MakeRecording(FPlayState(), {}, {});
    FPSReplayTeam& Home = Recording.Teams.AddDefaulted_GetRef();
    Home.TeamId = TEXT("Hawks");
    Home.DisplayName = TEXT("Harbor Hawks");
    Home.Abbreviation = TEXT("HAW");
    Home.PrimaryColor = TEXT("#0B5E8A");
    Home.SecondaryColor = TEXT("#F2B705");
    Home.bHome = true;
    FPSReplayParticipant& Receiver = Recording.Participants.AddDefaulted_GetRef();
    Receiver.PlayerId = TEXT("HAW_WR_001");
    Receiver.DisplayName = TEXT("J. Swoop");
    Receiver.TeamId = TEXT("Hawks");
    Receiver.TeamSide = EPSTeamSide::Offense;
    Receiver.Role = EPlayerRole::WideReceiver;
    Receiver.JerseyNumber = 13;

    const FString Json = UPSReplayFormat::SerializeToJson(Recording);
    FPSReplayRecording Loaded;
    if (!TestTrue(TEXT("A recording with teams and participants loads"), UPSReplayFormat::DeserializeFromJson(Json, Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...at the current version: the fields are optional (policy rule 1)"), Loaded.Header.FormatVersion, UPSReplayFormat::CurrentFormatVersion);
    if (TestEqual(TEXT("...with its team"), Loaded.Teams.Num(), 1))
    {
        TestEqual(TEXT("...named"), Loaded.Teams[0].DisplayName, FString(TEXT("Harbor Hawks")));
        TestEqual(TEXT("...coloured"), Loaded.Teams[0].PrimaryColor, FString(TEXT("#0B5E8A")));
        TestTrue(TEXT("...at home"), Loaded.Teams[0].bHome);
    }
    if (TestEqual(TEXT("...and its player"), Loaded.Participants.Num(), 1))
    {
        const FPSReplayParticipant& Back = Loaded.Participants[0];
        TestEqual(TEXT("...by PlayerId"), Back.PlayerId, FName(TEXT("HAW_WR_001")));
        TestEqual(TEXT("...with his name"), Back.DisplayName, FString(TEXT("J. Swoop")));
        TestEqual(TEXT("...team"), Back.TeamId, FName(TEXT("Hawks")));
        TestEqual(TEXT("...side"), Back.TeamSide, EPSTeamSide::Offense);
        TestEqual(TEXT("...role"), Back.Role, EPlayerRole::WideReceiver);
        TestEqual(TEXT("...and number"), Back.JerseyNumber, 13);
    }
    TestTrue(TEXT("Enums are written by name"), Json.Contains(TEXT("WideReceiver")));

    // A recording written before the fields existed.
    FPSReplayRecording Before = UPSReplayFormat::MakeRecording(FPlayState(), {}, {});
    FString OldJson = UPSReplayFormat::SerializeToJson(Before);
    OldJson.ReplaceInline(TEXT("\"teams\""), TEXT("\"retiredTeams\""));
    OldJson.ReplaceInline(TEXT("\"participants\""), TEXT("\"retiredParticipants\""));
    FPSReplayRecording OldLoaded;
    TestTrue(TEXT("A recording without them still loads"), UPSReplayFormat::DeserializeFromJson(OldJson, OldLoaded));
    TestEqual(TEXT("...with no teams"), OldLoaded.Teams.Num(), 0);
    TestEqual(TEXT("...and no participants"), OldLoaded.Participants.Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// 3. The ball's collision, eligibility, and the sampler after a substitution
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPlayDemoBallAndSamplerTest,
    "PlaySports.Demo.BallAndSampler",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDemoBallAndSamplerTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDemoTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSTelemetrySamplingSubsystem* Sampler = World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
    if (!TestTrue(TEXT("A world with the bus and the sampler"), Bus && Sampler))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(2000.f, 0.f, 150.f), FRotator::ZeroRotator, SpawnParams);
    APSPlayerPawn* Passer = SpawnPlayer(World, MakePlayer(TEXT("DEMO_QB"), EPlayerRole::Quarterback, 12), FVector(1900.f, 0.f, 100.f));
    APSPlayerPawn* Guard = SpawnPlayer(World, MakePlayer(TEXT("DEMO_OL"), EPlayerRole::OffensiveLineman, 66), FVector(1950.f, 150.f, 100.f));
    APSPlayerPawn* Receiver = SpawnPlayer(World, MakePlayer(TEXT("DEMO_WR"), EPlayerRole::WideReceiver, 81), FVector(2600.f, 300.f, 100.f));
    if (!TestTrue(TEXT("The ball and three players"), Ball && Ball->GetCollisionComponent() && Passer && Guard && Receiver))
    {
        DestroyTestWorld(World);
        return false;
    }

    // The ball passes through players, telling them it touched them, and lands on the world.
    const USphereComponent* Collision = Ball->GetCollisionComponent();
    TestTrue(TEXT("The ball overlaps players, so a catch can resolve"), Collision->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);
    TestTrue(TEXT("...and blocks the ground, so it lands"), Collision->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
    TestTrue(TEXT("...and reports its overlaps"), Collision->GetGenerateOverlapEvents());

    // A pass in the air: the lineman and the passer are ineligible, the receiver takes it.
    Ball->CatchTuningSettings.CatchChanceMin = 1.f;
    Ball->CatchTuningSettings.CatchChanceMax = 1.f;
    Ball->Launch(FVector(1500.f, 0.f, 400.f));
    TestFalse(TEXT("A pass flies on past an offensive lineman"), Ball->ResolveTouch(Guard));
    TestFalse(TEXT("...and past the passer"), Ball->ResolveTouch(Passer));
    TestFalse(TEXT("...neither of whom has it"), Guard->HasPossession() || Passer->HasPossession());
    TestTrue(TEXT("A receiver catches it"), Ball->ResolveTouch(Receiver));
    TestTrue(TEXT("...and has the ball"), Receiver->HasPossession() && Ball->GetAttachParentActor() == Receiver);

    // The sampler names the receiver's pawn; a substitution points it at another player.
    Sampler->AdvanceTime(1.f);
    FPSSnapshotFrame Frame;
    TestTrue(TEXT("The sampler took a frame"), Sampler->GetLatestFrame(Frame) && Frame.FindPawn(TEXT("DEMO_WR")) != nullptr);
    Receiver->InitializePlayer(MakePlayer(TEXT("DEMO_TE"), EPlayerRole::TightEnd, 87));
    FPSTelemetryPersonnelEvent Substitution;
    Substitution.bOffense = true;
    Bus->PublishPersonnel(Substitution);
    Sampler->AdvanceTime(1.f);
    TestTrue(TEXT("After the substitution's Personnel event, frames name the player who came on"),
        Sampler->GetLatestFrame(Frame) && Frame.FindPawn(TEXT("DEMO_TE")) != nullptr && Frame.FindPawn(TEXT("DEMO_WR")) == nullptr);
    if (const FPSPawnSnapshot* Incoming = Frame.FindPawn(TEXT("DEMO_TE")))
    {
        TestEqual(TEXT("...at his role"), Incoming->Role, EPlayerRole::TightEnd);
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// 4. The sanity checks themselves
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPlayDemoSanityChecksTest,
    "PlaySports.Demo.SanityChecks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDemoSanityChecksTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayDemoTests;

    const FPSPlayDemoCatalog Catalog;
    const FPSPlayDemoRun Sound = MakeSoundRun(Catalog);
    const TArray<FString> None = UPSPlayDemoRunner::CheckRun(Sound, Catalog);
    TestEqual(*FString::Printf(TEXT("A sound play passes (%s)"), *FString::Join(None, TEXT(" | "))), None.Num(), 0);

    FPSPlayDemoRun Gap = Sound;
    Gap.Recording.Frames.RemoveAt(30);
    TestTrue(TEXT("A missing frame is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(Gap, Catalog), TEXT("off the 30 Hz rate")));

    FPSPlayDemoRun Still = Sound;
    for (FPSSnapshotFrame& Frame : Still.Recording.Frames)
    {
        Frame.Pawns[5].Location = Sound.Recording.Frames[10].Pawns[5].Location;
    }
    TestTrue(TEXT("A player who never moves is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(Still, Catalog), TEXT("P05")));

    // A lineman held up in his block need only move MinLinemanMoveCm; a quarterback who moves as
    // little has neither dropped nor handed off.
    FPSPlayDemoRun Blocked = Sound;
    for (int32 Index = 0; Index < Blocked.Recording.Frames.Num(); ++Index)
    {
        for (const int32 Lineman : { 4, 15 })
        {
            FPSPawnSnapshot& Snapshot = Blocked.Recording.Frames[Index].Pawns[Lineman];
            Snapshot.Role = Lineman < 11 ? EPlayerRole::OffensiveLineman : EPlayerRole::DefensiveLineman;
            Snapshot.Location = Sound.Recording.Frames[10].Pawns[Lineman].Location + FVector(Index > 10 ? 20.f : 0.f, 0.f, 0.f);
        }
    }
    const TArray<FString> HeldUp = UPSPlayDemoRunner::CheckRun(Blocked, Catalog);
    TestEqual(*FString::Printf(TEXT("Linemen held up 20 cm from their spots pass (%s)"), *FString::Join(HeldUp, TEXT(" | "))), HeldUp.Num(), 0);
    FPSPlayDemoRun StaticPasser = Blocked;
    for (FPSSnapshotFrame& Frame : StaticPasser.Recording.Frames)
    {
        Frame.Pawns[4].Role = EPlayerRole::Quarterback;
    }
    TestTrue(TEXT("...a quarterback who moves as little is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(StaticPasser, Catalog), TEXT("P04")));

    FPSPlayDemoRun NoResult = Sound;
    NoResult.Recording.Events.Reset();
    TestTrue(TEXT("A play with no result on the bus is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(NoResult, Catalog), TEXT("PlayResult")));

    FPSPlayDemoRun NotANumber = Sound;
    NotANumber.Recording.Frames[20].Pawns[3].Velocity.X = std::numeric_limits<double>::quiet_NaN();
    TestTrue(TEXT("A NaN is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(NotANumber, Catalog), TEXT("NaN")));

    FPSPlayDemoRun TooFast = Sound;
    TooFast.Recording.Frames[25].Pawns[7].Velocity = FVector(2000.f, 0.f, 0.f);
    TestTrue(TEXT("A player faster than the movement data allows is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(TooFast, Catalog), TEXT("P07")));

    FPSPlayDemoRun Sunk = Sound;
    Sunk.Recording.Frames[40].Pawns[9].Location.Z = 50.f;
    TestTrue(TEXT("A capsule through the ground is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(Sunk, Catalog), TEXT("below the ground")));

    FPSPlayDemoRun Short = Sound;
    Short.Recording.Frames[45].Pawns.RemoveAt(21);
    TestTrue(TEXT("A frame without all 22 is caught"), HasProblem(UPSPlayDemoRunner::CheckRun(Short, Catalog), TEXT("22 players")));
    return true;
}

// ---------------------------------------------------------------------------
// 5. The live plays
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSPlayDemoLivePlaysTest,
    "PlaySports.Demo.LivePlays",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayDemoLivePlaysTest::RunTest(const FString& Parameters)
{
    UPSPlayDemoRunner* Runner = NewObject<UPSPlayDemoRunner>();
    if (!TestTrue(TEXT("Data/play_demos.json loads"), Runner->LoadCatalogFromJson(UPSPlayDemoRunner::GetDefaultCatalogPath())))
    {
        return false;
    }

    TArray<FPSPlayDemoRun> Runs = Runner->RunAll();
    FString IndexPath;
    TestTrue(TEXT("Every play and index.json are written to Saved/PlayDemos"), Runner->WriteDemos(Runs, FString(), IndexPath));
    TestEqual(TEXT("Every demo ran"), Runs.Num(), Runner->GetCatalog().PlayDemos.Num());
    const FString Directory = FPaths::GetPath(IndexPath);
    for (const FPSPlayDemoRun& Run : Runs)
    {
        const FPSPlayDemoSummary& Summary = Run.Summary;
        const FString Id = Summary.DemoId.ToString();
        AddInfo(FString::Printf(TEXT("%s (seed %d of %d tried): %s | %s: %s, %d yards, ended by %s, %.2f s snap to whistle | %d frames, %d events | fastest %s at %.0f cm/s, ball up to %.0f cm/s, travelled %.0f cm"),
            *Id, Summary.Seed, Summary.SeedsTried, *Summary.Title, *Summary.Outcome, *Summary.Result, Summary.YardsGained, *Summary.EndedBy,
            Summary.PlaySeconds, Summary.FrameCount, Summary.EventCount, *Summary.FastestPlayerId.ToString(), Summary.MaxPlayerSpeedCmPerSec,
            Summary.MaxBallSpeedCmPerSec, Summary.BallTravelCm));
        if (!Summary.WantedOutcome.IsEmpty() && Summary.Outcome != Summary.WantedOutcome)
        {
            AddInfo(FString::Printf(TEXT("%s: no %s in %d seed(s); kept the last, a %s."), *Id, *Summary.WantedOutcome, Summary.SeedsTried, *Summary.Outcome));
        }
        if (Summary.EndedBy == TEXT("PhaseClock"))
        {
            AddWarning(FString::Printf(TEXT("%s: the whistle came from the play simulation's phase timer, not from the play (no tackle, landing or boundary before it)."), *Id));
        }
        TestTrue(*FString::Printf(TEXT("%s: the play snapped, reached its whistle and announced its result"), *Id), Run.bRan);
        for (const FString& Problem : Summary.Problems)
        {
            AddError(FString::Printf(TEXT("%s: %s"), *Id, *Problem));
        }

        // The file on disk is the format's, with everyone labelled.
        FString Json;
        FPSReplayRecording Loaded;
        if (TestTrue(*FString::Printf(TEXT("%s: %s was written"), *Id, *Summary.File), FFileHelper::LoadFileToString(Json, *(Directory / Summary.File)))
            && TestTrue(*FString::Printf(TEXT("%s: it loads through the replay format"), *Id), UPSReplayFormat::DeserializeFromJson(Json, Loaded)))
        {
            TestEqual(*FString::Printf(TEXT("%s: with every frame"), *Id), Loaded.Frames.Num(), Summary.FrameCount);
            TestEqual(*FString::Printf(TEXT("%s: and both teams"), *Id), Loaded.Teams.Num(), 2);
            TestTrue(*FString::Printf(TEXT("%s: and at least the 22 on the field"), *Id), Loaded.Participants.Num() >= 22);
            TestTrue(*FString::Printf(TEXT("%s: each with a team, a name and a number"), *Id), !Loaded.Participants.ContainsByPredicate([](const FPSReplayParticipant& Participant)
            {
                return Participant.TeamId.IsNone() || Participant.DisplayName.IsEmpty() || Participant.JerseyNumber <= 0;
            }));
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
