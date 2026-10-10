#include "PSQuickSimRunner.h"
#include "PSReplayRecorder.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Class.h"

FPSQuickSimResult UPSQuickSimRunner::SimulateGame(const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster, int32 Seed)
{
    UPSPlaySimulation* Sim = MakeGameSimulation(HomeRoster, AwayRoster);
    if (Seed != 0)
    {
        Sim->SeedRolls(Seed);
    }
    return RunGame(*Sim, SecondsPerPlayAdvance, nullptr);
}

FPSQuickSimResult UPSQuickSimRunner::RecordGame(UObject* WorldContextObject, const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster,
    int32 Seed, FPSReplayRecording& OutRecording)
{
    OutRecording = FPSReplayRecording();
    FString Failure;
    UWorld* World = GetRecordingWorld(WorldContextObject, Failure);
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSQuickSimRunner: Can't record a game: %s"), *Failure);
        return FPSQuickSimResult();
    }

    UPSPlaySimulation* Sim = MakeGameSimulation(HomeRoster, AwayRoster);
    if (Seed != 0)
    {
        Sim->SeedRolls(Seed);
    }
    FPSReplayRecording Start = UPSReplayFormat::MakeRecording(Sim->GetPlayState(), HomeRoster, AwayRoster);
    Start.Header.RandomSeed = Seed;
    Start.Header.FixedDeltaSeconds = SecondsPerPlayAdvance;
    return RecordRun(*World, *Sim, Start, OutRecording);
}

bool UPSQuickSimRunner::ReplayGame(UObject* WorldContextObject, const FPSReplayRecording& Recording, FPSReplayRecording& OutReplay,
    FPSQuickSimResult& OutResult, FString& OutFailure)
{
    OutReplay = FPSReplayRecording();
    OutResult = FPSQuickSimResult();
    OutFailure.Reset();

    if (Recording.Header.RandomSeed == 0)
    {
        OutFailure = TEXT("the recording has no random seed: it supports event playback only");
        return false;
    }
    if (Recording.Header.FixedDeltaSeconds <= 0.f)
    {
        OutFailure = TEXT("the recording has no fixed step: it was recorded at a variable frame delta");
        return false;
    }
    UWorld* World = GetRecordingWorld(WorldContextObject, OutFailure);
    if (!World)
    {
        return false;
    }

    UPSPlaySimulation* Sim = MakeGameSimulation(Recording.InitialState.OffenseRoster, Recording.InitialState.DefenseRoster);
    Sim->SeedRolls(Recording.Header.RandomSeed);
    FPlayState Opening = Sim->GetPlayState();
    if (!FPlayState::StaticStruct()->CompareScriptStruct(&Opening, &Recording.InitialState.PlayState, PPF_None))
    {
        OutFailure = FString::Printf(TEXT("the recording starts at Q%d, down %d at the %d, not at a quick-sim game's opening (Q%d, down %d at the %d)"),
            Recording.InitialState.PlayState.Quarter, Recording.InitialState.PlayState.Down, Recording.InitialState.PlayState.YardLine,
            Opening.Quarter, Opening.Down, Opening.YardLine);
        return false;
    }

    FPSReplayRecording Start = UPSReplayFormat::MakeRecording(Opening, Recording.InitialState.OffenseRoster, Recording.InitialState.DefenseRoster);
    Start.Header.RandomSeed = Recording.Header.RandomSeed;
    Start.Header.FixedDeltaSeconds = Recording.Header.FixedDeltaSeconds;
    Start.Header.GameBuildVersion = Recording.Header.GameBuildVersion;
    OutResult = RecordRun(*World, *Sim, Start, OutReplay);
    return true;
}

UPSPlaySimulation* UPSQuickSimRunner::MakeGameSimulation(const TArray<FPlayerAttributes>& HomeRoster, const TArray<FPlayerAttributes>& AwayRoster)
{
    // In the transient package, never under the runner: a simulation inside a live world finds
    // that world's game mode and resets its pawns after every play.
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->bQuickSimMode = true;
    Sim->InitializePlay(HomeRoster, AwayRoster);
    Sim->OnPlayResolved.AddLambda([this](const FPSTelemetryPlayResultEvent& Event)
    {
        OnPlayResolved.Broadcast(Event);
    });
    Sim->OnPenaltyRuled.AddLambda([this](const FPSTelemetryPenaltyEvent& Event)
    {
        OnPenaltyRuled.Broadcast(Event);
    });
    return Sim;
}

FPSQuickSimResult UPSQuickSimRunner::RunGame(UPSPlaySimulation& Sim, float StepSeconds, UPSReplayRecorder* Recorder) const
{
    int32 TickCount = 0;
    while (TickCount < MaxPlaysPerGame)
    {
        const FPlayState State = Sim.GetPlayState();
        if (State.Quarter > 4)
        {
            break;
        }

        if (Recorder)
        {
            Recorder->SetTick(TickCount + 1);
        }
        if (State.Phase == EPlayPhase::PreSnap)
        {
            Sim.TriggerSnap();
        }

        Sim.AdvancePlay(StepSeconds);
        ++TickCount;
    }

    const FPlayState FinalState = Sim.GetPlayState();
    FPSQuickSimResult Result;
    Result.HomeScore = FinalState.HomeScore;
    Result.AwayScore = FinalState.AwayScore;
    return Result;
}

FPSQuickSimResult UPSQuickSimRunner::RecordRun(UWorld& World, UPSPlaySimulation& Sim, const FPSReplayRecording& Start, FPSReplayRecording& OutRecording)
{
    UPSReplayRecorder* Recorder = NewObject<UPSReplayRecorder>(this);
    Recorder->BeginRecording(World.GetSubsystem<UPSTelemetryBus>(), Start);

    // On the bus from here: its opening game state is the recording's first event, at step 0.
    Sim.InitializeWithWorld(&World);
    const FPSQuickSimResult Result = RunGame(Sim, Start.Header.FixedDeltaSeconds, Recorder);

    // Off the bus again, so nothing it still hears there makes it publish.
    Sim.InitializeWithWorld(nullptr);
    OutRecording = Recorder->EndRecording();
    return Result;
}

UWorld* UPSQuickSimRunner::GetRecordingWorld(UObject* WorldContextObject, FString& OutFailure)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World)
    {
        OutFailure = TEXT("no world to run in");
        return nullptr;
    }
    if (World->GetAuthGameMode())
    {
        OutFailure = TEXT("the world has a game mode: a live game, whose bus and pawns the run would drive");
        return nullptr;
    }
    if (!World->GetSubsystem<UPSTelemetryBus>())
    {
        OutFailure = TEXT("the world has no telemetry bus to record from");
        return nullptr;
    }
    return World;
}
