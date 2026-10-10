// PSPlayDemoRunner.cpp - live-play demos: real plays run end to end in a ticking world, recorded
#include "PSPlayDemoRunner.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSGameMode.h"
#include "PSLeagueData.h"
#include "PSMatchSetup.h"
#include "PSNetRandomStreams.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaybookData.h"
#include "PSPlaySimulation.h"
#include "PSPlayerPawn.h"
#include "PSPreSnapSubsystem.h"
#include "PSReplayRecorder.h"
#include "PSReplaySubsystem.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "PSUITeamCatalog.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace PSPlayDemoRunnerPrivate
{
    /** What the bus has said about the play so far, timed on the world's clock (the bus's
     *  timestamps and the sampler's frames run on it too). */
    struct FPlayWatch
    {
        const UWorld* World = nullptr;
        bool bSnapped = false;
        float SnapTime = 0.f;
        FPSTelemetrySnapEvent Snap;
        bool bWhistle = false;
        float WhistleTime = 0.f;
        bool bResult = false;
        float ResultTime = 0.f;
        FPSTelemetryPlayResultEvent Result;
        /** The last event between the snap and the whistle that ends a play, and when. */
        FString LastEnding;
        float LastEndingTime = 0.f;
        /** Once the demo has called its plays: the offense's last call before the snap (its own,
         *  or the quarterback's audible), and every flag until the result. */
        bool bCalled = false;
        FName RunPlayId;
        FString RunPlayName;
        TArray<FString> Penalties;

        float Now() const
        {
            return World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
        }

        void NoteEnding(const TCHAR* EventName)
        {
            if (bSnapped && !bWhistle)
            {
                LastEnding = EventName;
                LastEndingTime = Now();
            }
        }
    };

    /** Takes every scheduled frame the sampler captured since the last one taken. Keyframes are
     *  left out: one is taken mid-step at the instant of its event, with the same time as the
     *  step's own frame, and the events themselves are in the recording. */
    void CollectFrames(const UPSTelemetrySamplingSubsystem& Sampler, int32& InOutLastIndex, float& InOutLastTime, TArray<FPSSnapshotFrame>& OutFrames)
    {
        for (const FPSSnapshotFrame& Frame : Sampler.GetFramesBetween(InOutLastTime, TNumericLimits<float>::Max()))
        {
            if (!Frame.bKeyframe && Frame.FrameIndex > InOutLastIndex)
            {
                OutFrames.Add(Frame);
                InOutLastIndex = Frame.FrameIndex;
                InOutLastTime = Frame.Time;
            }
        }
    }

    /** Destroys the world and its context, as the headless tests do. Its subsystems, which
     *  every actor's bus subscription lives on, go with it. */
    void DestroyDemoWorld(UWorld* World)
    {
        if (!World)
        {
            return;
        }
        if (GEngine)
        {
            GEngine->DestroyWorldContext(World);
        }
        World->DestroyWorld(false);
    }

    bool IsFiniteVector(const FVector& Vector)
    {
        return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
    }

    /** The first frame at or after Time (half a step early counts), or INDEX_NONE. */
    int32 FindFrameAt(const TArray<FPSSnapshotFrame>& Frames, float Time, float Step)
    {
        for (int32 Index = 0; Index < Frames.Num(); ++Index)
        {
            if (Frames[Index].Time + Step * 0.5f >= Time)
            {
                return Index;
            }
        }
        return INDEX_NONE;
    }

    /** Run, Completion, Incompletion, Sack, Interception or Touchdown, from the play's result. */
    FString OutcomeOf(const FPSTelemetryPlayResultEvent& Result)
    {
        if (Result.bSack)
        {
            return TEXT("Sack");
        }
        if (Result.bInterception)
        {
            return TEXT("Interception");
        }
        if (Result.Result == TEXT("Touchdown"))
        {
            return TEXT("Touchdown");
        }
        if (Result.bPass)
        {
            return Result.bComplete ? TEXT("Completion") : TEXT("Incompletion");
        }
        return Result.Result == TEXT("Tackle") ? FString(TEXT("Run")) : Result.Result;
    }

    FString NameOf(const TArray<FPSReplayParticipant>& Participants, FName PlayerId)
    {
        const FPSReplayParticipant* Found = Participants.FindByPredicate([PlayerId](const FPSReplayParticipant& Participant) { return Participant.PlayerId == PlayerId; });
        return Found ? Found->DisplayName : PlayerId.ToString();
    }

    /** "6 yards", "no gain", "a loss of 3 yards". */
    FString DescribeGain(int32 Yards)
    {
        const int32 Size = FMath::Abs(Yards);
        const FString Unit = Size == 1 ? TEXT("yard") : TEXT("yards");
        if (Yards > 0)
        {
            return FString::Printf(TEXT("%d %s"), Size, *Unit);
        }
        return Yards == 0 ? FString(TEXT("no gain")) : FString::Printf(TEXT("a loss of %d %s"), Size, *Unit);
    }

    /** What happened, in words, from the result and who took part. */
    FString MakeTitle(const FPSPlayDemoSummary& Summary, const TArray<FPSReplayParticipant>& Participants)
    {
        const FString Called = Summary.OffensePlayName.IsEmpty() ? Summary.OffensePlayId.ToString() : Summary.OffensePlayName;
        const FString Ran = Summary.RunPlayName.IsEmpty() ? Summary.RunPlayId.ToString() : Summary.RunPlayName;
        const FString Play = Summary.bAudible ? FString::Printf(TEXT("%s, audibled to %s"), *Called, *Ran) : Called;
        const FName CarrierId = !Summary.RusherId.IsNone() ? Summary.RusherId : Summary.BallCarrierId;
        const FString Passer = NameOf(Participants, Summary.PasserId);
        const FString Receiver = NameOf(Participants, Summary.ReceiverId);
        const FString Carrier = NameOf(Participants, CarrierId);
        FString What;
        if (Summary.Outcome == TEXT("Sack"))
        {
            What = FString::Printf(TEXT("%s sacks %s for %s"), *NameOf(Participants, Summary.TacklerId), *Passer, *DescribeGain(Summary.YardsGained));
        }
        else if (Summary.Outcome == TEXT("Interception"))
        {
            What = FString::Printf(TEXT("%s intercepts %s's pass for %s"), *NameOf(Participants, Summary.InterceptorId), *Passer, *Receiver);
        }
        else if (Summary.Outcome == TEXT("Touchdown"))
        {
            What = !Summary.ReceiverId.IsNone()
                ? FString::Printf(TEXT("%s to %s, a %d-yard touchdown"), *Passer, *Receiver, Summary.YardsGained)
                : FString::Printf(TEXT("%s runs %d yards for a touchdown"), *Carrier, Summary.YardsGained);
        }
        else if (Summary.Outcome == TEXT("Completion"))
        {
            What = FString::Printf(TEXT("%s to %s for %s"), *Passer, *Receiver, *DescribeGain(Summary.YardsGained));
        }
        else if (Summary.Outcome == TEXT("Incompletion"))
        {
            What = Summary.ReceiverId.IsNone()
                ? FString::Printf(TEXT("%s's pass falls incomplete"), *Passer)
                : FString::Printf(TEXT("%s's pass for %s falls incomplete"), *Passer, *Receiver);
        }
        else if (Summary.Outcome == TEXT("Run"))
        {
            What = FString::Printf(TEXT("%s runs for %s"), *Carrier, *DescribeGain(Summary.YardsGained));
        }
        else
        {
            What = Summary.Result.IsEmpty() ? FString(TEXT("no result")) : Summary.Result;
        }
        if ((Summary.Outcome == TEXT("Run") || Summary.Outcome == TEXT("Completion")) && !Summary.TacklerId.IsNone())
        {
            What += FString::Printf(TEXT(", tackled by %s"), *NameOf(Participants, Summary.TacklerId));
        }
        return FString::Printf(TEXT("%s: %s"), *Play, *What);
    }

    /** The teams (home first) and every player the frames name, labelled from the league's data. */
    void AddTeamsAndParticipants(FPSReplayRecording& Recording, FName HomeTeamId, FName AwayTeamId)
    {
        const FString TeamsPath = UPSUITeamCatalog::GetDefaultTeamsPath();
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        UDataTable* Teams = NewObject<UDataTable>();
        Teams->RowStruct = FPSTeamInfo::StaticStruct();
        TArray<FPSTeamInfo*> TeamRows;
        if (Ingestion->LoadTeamsFromJson(TeamsPath, Teams))
        {
            Teams->GetAllRows<FPSTeamInfo>(TEXT("UPSPlayDemoRunner"), TeamRows);
        }

        TMap<FName, TPair<FPlayerAttributes, FName>> Players;
        for (const TPair<FName, bool>& Side : { TPair<FName, bool>(HomeTeamId, true), TPair<FName, bool>(AwayTeamId, false) })
        {
            FPSReplayTeam& Team = Recording.Teams.AddDefaulted_GetRef();
            Team.TeamId = Side.Key;
            Team.bHome = Side.Value;
            for (const FPSTeamInfo* Row : TeamRows)
            {
                if (Row && Row->TeamId == Side.Key)
                {
                    Team.DisplayName = Row->DisplayName;
                    Team.Abbreviation = Row->Abbreviation;
                    Team.PrimaryColor = Row->PrimaryColor;
                    Team.SecondaryColor = Row->SecondaryColor;
                }
            }
            TArray<FPlayerAttributes> TeamPlayers;
            UPSMatchSetup::LoadTeamPlayers(TeamsPath, Side.Key, TeamPlayers);
            for (const FPlayerAttributes& Player : TeamPlayers)
            {
                Players.Add(Player.PlayerId, TPair<FPlayerAttributes, FName>(Player, Side.Key));
            }
        }

        TSet<FName> Seen;
        for (const FPSSnapshotFrame& Frame : Recording.Frames)
        {
            for (const FPSPawnSnapshot& Snapshot : Frame.Pawns)
            {
                if (Seen.Contains(Snapshot.PlayerId))
                {
                    continue;
                }
                Seen.Add(Snapshot.PlayerId);
                FPSReplayParticipant& Participant = Recording.Participants.AddDefaulted_GetRef();
                Participant.PlayerId = Snapshot.PlayerId;
                Participant.TeamSide = Snapshot.TeamSide;
                Participant.Role = Snapshot.Role;
                Participant.DisplayName = Snapshot.PlayerId.ToString();
                if (const TPair<FPlayerAttributes, FName>* Row = Players.Find(Snapshot.PlayerId))
                {
                    Participant.DisplayName = Row->Key.DisplayName;
                    Participant.Role = Row->Key.Role;
                    Participant.JerseyNumber = Row->Key.JerseyNumber;
                    Participant.TeamId = Row->Value;
                }
            }
        }
    }

    /** The fastest player and ball, how far the ball went from the snap, and who had it at the
     *  whistle. */
    void MeasureRun(FPSPlayDemoRun& Run, float Step)
    {
        FPSPlayDemoSummary& Summary = Run.Summary;
        const TArray<FPSSnapshotFrame>& Frames = Run.Recording.Frames;
        const int32 SnapFrame = FindFrameAt(Frames, Summary.SnapTime, Step);
        FVector SnapBall = FVector::ZeroVector;
        bool bHasSnapBall = false;
        if (Frames.IsValidIndex(SnapFrame) && Frames[SnapFrame].bBallSampled)
        {
            SnapBall = Frames[SnapFrame].BallLocation;
            bHasSnapBall = true;
        }
        for (int32 Index = 0; Index < Frames.Num(); ++Index)
        {
            const FPSSnapshotFrame& Frame = Frames[Index];
            FName Carrier;
            for (const FPSPawnSnapshot& Snapshot : Frame.Pawns)
            {
                const float Speed = static_cast<float>(Snapshot.Velocity.Size2D());
                if (Speed > Summary.MaxPlayerSpeedCmPerSec)
                {
                    Summary.MaxPlayerSpeedCmPerSec = Speed;
                    Summary.FastestPlayerId = Snapshot.PlayerId;
                }
                if (Snapshot.bHasBall)
                {
                    Carrier = Snapshot.PlayerId;
                }
            }
            if (Frame.Time <= Summary.WhistleTime + Step * 0.5f)
            {
                Summary.BallCarrierId = Carrier;
            }
            if (Frame.bBallSampled)
            {
                Summary.MaxBallSpeedCmPerSec = FMath::Max(Summary.MaxBallSpeedCmPerSec, static_cast<float>(Frame.BallVelocity.Size()));
                if (bHasSnapBall && Index > SnapFrame)
                {
                    Summary.BallTravelCm = FMath::Max(Summary.BallTravelCm, static_cast<float>(FVector::Dist(Frame.BallLocation, SnapBall)));
                }
            }
        }
    }

    const TCHAR* const DemoMethod =
        TEXT("Each play runs in a game world of its own with APSGameMode as its game mode (UWorld::SetGameMode, ")
        TEXT("InitializeActorsForPlay, BeginPlay), ticked with UWorld::Tick at a fixed step. The demo names both calls ")
        TEXT("through UPSPlayCallSubsystem::CallPlay; everything after that is the game's own systems: the snap, the ")
        TEXT("orchestrator's assignments, the AI, movement, the ball's flight, contact, catches and tackles from the physics ")
        TEXT("scene, and the play simulation's whistle and result (a tackle, the ball grounded or out of bounds, a score; ")
        TEXT("its backstop only if none comes). Frames are the telemetry sampler's, one per step; events ")
        TEXT("are every bus event (UPSReplayRecorder). Nothing is scripted, teleported or interpolated.");
}

FString UPSPlayDemoRunner::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/play_demos.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FString UPSPlayDemoRunner::GetDefaultOutputDirectory()
{
    FString Path = FPaths::ProjectSavedDir() / TEXT("PlayDemos");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSPlayDemoRunner::LoadCatalogFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPlayDemoCatalog Loaded;
    if (!Ingestion->LoadPlayDemoCatalogFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayDemoRunner: Could not read %s."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateCatalog(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayDemoRunner: %s: %s"), *JsonFilePath, *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Catalog = Loaded;
    return true;
}

const TArray<FString>& UPSPlayDemoRunner::GetOutcomes()
{
    static const TArray<FString> Outcomes = { TEXT("Run"), TEXT("Completion"), TEXT("Incompletion"), TEXT("Sack"), TEXT("Interception"), TEXT("Touchdown") };
    return Outcomes;
}

TArray<FString> UPSPlayDemoRunner::ValidateCatalog(const FPSPlayDemoCatalog& InCatalog)
{
    TArray<FString> Problems;
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("FrameRateHz"), InCatalog.FrameRateHz),
        TPair<const TCHAR*, float>(TEXT("MaxPreSnapSeconds"), InCatalog.MaxPreSnapSeconds),
        TPair<const TCHAR*, float>(TEXT("MaxPlaySeconds"), InCatalog.MaxPlaySeconds),
        TPair<const TCHAR*, float>(TEXT("MaxResultWaitSeconds"), InCatalog.MaxResultWaitSeconds) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s (%g) must be above 0"), Positive.Key, Positive.Value));
        }
    }
    for (const TPair<const TCHAR*, float>& NotNegative : {
        TPair<const TCHAR*, float>(TEXT("PostWhistleSeconds"), InCatalog.PostWhistleSeconds),
        TPair<const TCHAR*, float>(TEXT("MinPlayerMoveCm"), InCatalog.MinPlayerMoveCm),
        TPair<const TCHAR*, float>(TEXT("MinLinemanMoveCm"), InCatalog.MinLinemanMoveCm),
        TPair<const TCHAR*, float>(TEXT("SpeedAllowanceCmPerSec"), InCatalog.SpeedAllowanceCmPerSec),
        TPair<const TCHAR*, float>(TEXT("GroundToleranceCm"), InCatalog.GroundToleranceCm) })
    {
        if (!(NotNegative.Value >= 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s (%g) must be 0 or more"), NotNegative.Key, NotNegative.Value));
        }
    }
    if (!(InCatalog.PostWhistleSeconds < InCatalog.MaxResultWaitSeconds))
    {
        Problems.Add(TEXT("PostWhistleSeconds must be shorter than MaxResultWaitSeconds"));
    }
    if (InCatalog.PlayDemos.Num() == 0)
    {
        Problems.Add(TEXT("PlayDemos lists no demo"));
    }
    TSet<FName> Ids;
    for (int32 Index = 0; Index < InCatalog.PlayDemos.Num(); ++Index)
    {
        const FPSPlayDemoDef& Demo = InCatalog.PlayDemos[Index];
        const FString Where = FString::Printf(TEXT("PlayDemos[%d] '%s'"), Index, *Demo.DemoId.ToString());
        if (Demo.DemoId.IsNone())
        {
            Problems.Add(FString::Printf(TEXT("%s: no DemoId"), *Where));
        }
        else if (Ids.Contains(Demo.DemoId))
        {
            Problems.Add(FString::Printf(TEXT("%s: DemoId used twice"), *Where));
        }
        Ids.Add(Demo.DemoId);
        if (Demo.HomeTeamId.IsNone() || Demo.AwayTeamId.IsNone() || Demo.HomeTeamId == Demo.AwayTeamId)
        {
            Problems.Add(FString::Printf(TEXT("%s: needs two different teams"), *Where));
        }
        if (Demo.OffensePlayId.IsNone() || Demo.DefensePlayId.IsNone())
        {
            Problems.Add(FString::Printf(TEXT("%s: needs both calls"), *Where));
        }
        if (Demo.SeedTries < 1)
        {
            Problems.Add(FString::Printf(TEXT("%s: SeedTries (%d) must be 1 or more"), *Where, Demo.SeedTries));
        }
        if (!Demo.WantedOutcome.IsEmpty() && !GetOutcomes().Contains(Demo.WantedOutcome))
        {
            Problems.Add(FString::Printf(TEXT("%s: WantedOutcome '%s' is not one of %s"), *Where, *Demo.WantedOutcome, *FString::Join(GetOutcomes(), TEXT(", "))));
        }
    }
    return Problems;
}

FPSPlayDemoRun UPSPlayDemoRunner::RunDemo(const FPSPlayDemoDef& Demo)
{
    const int32 Tries = Demo.WantedOutcome.IsEmpty() ? 1 : FMath::Max(1, Demo.SeedTries);
    FPSPlayDemoRun Run;
    for (int32 Try = 0; Try < Tries; ++Try)
    {
        Run = RunPlay(Demo, Demo.Seed + Try);
        Run.Summary.SeedsTried = Try + 1;
        // Ended that way on the field, not by the simulation's backstop.
        if (Demo.WantedOutcome.IsEmpty() || (Run.Summary.Outcome == Demo.WantedOutcome && Run.Summary.EndedBy != TEXT("PhaseClock")))
        {
            break;
        }
    }
    return Run;
}

TArray<FPSPlayDemoRun> UPSPlayDemoRunner::RunAll()
{
    TArray<FPSPlayDemoRun> Runs;
    for (const FPSPlayDemoDef& Demo : Catalog.PlayDemos)
    {
        Runs.Add(RunDemo(Demo));
    }
    // The demos seeded the engine's global stream; a run after them draws as if they hadn't.
    FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    return Runs;
}

FPSPlayDemoRun UPSPlayDemoRunner::RunPlay(const FPSPlayDemoDef& Demo, int32 Seed)
{
    using namespace PSPlayDemoRunnerPrivate;

    FPSPlayDemoRun Run;
    FPSPlayDemoSummary& Summary = Run.Summary;
    Summary.DemoId = Demo.DemoId;
    Summary.Intent = Demo.Intent;
    Summary.Seed = Seed;
    Summary.SeedsTried = 1;
    Summary.WantedOutcome = Demo.WantedOutcome;
    Summary.HomeTeamId = Demo.HomeTeamId;
    Summary.AwayTeamId = Demo.AwayTeamId;
    Summary.OffensePlayId = Demo.OffensePlayId;
    Summary.DefensePlayId = Demo.DefensePlayId;
    Summary.CalledBy = TEXT("Demo");
    Summary.FrameRateHz = Catalog.FrameRateHz;
    Summary.Outcome = TEXT("None");

    const float Step = 1.f / FMath::Max(Catalog.FrameRateHz, 1.f);
    UWorld* World = GEngine ? UWorld::CreateWorld(EWorldType::Game, false) : nullptr;
    if (!World)
    {
        Summary.Problems.Add(TEXT("Could not make a game world."));
        return Run;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    // As a map load makes a match: the travel options name the teams, the match is the world's
    // game mode (through a game instance, which makes it), and the world's actors are initialized
    // for play before play begins.
    FURL URL;
    URL.AddOption(*FString::Printf(TEXT("home=%s"), *Demo.HomeTeamId.ToString()));
    URL.AddOption(*FString::Printf(TEXT("away=%s"), *Demo.AwayTeamId.ToString()));
    World->URL = URL;
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode = APSGameMode::StaticClass();
    World->SetGameMode(URL);
    APSGameMode* GameMode = Cast<APSGameMode>(World->GetAuthGameMode());
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>();
    UPSNetRandomStreams* Streams = World->GetSubsystem<UPSNetRandomStreams>();
    UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
    if (!GameMode || !Bus || !Sampler || !Streams || !PlayCall)
    {
        Summary.Problems.Add(FString::Printf(TEXT("The world has no %s."), !GameMode ? TEXT("APSGameMode") : TEXT("telemetry bus, sampler, random streams or play-call subsystem")));
        DestroyDemoWorld(World);
        return Run;
    }

    // The match on the demo's seed (every contest's roll and the simulation's flags), recorded
    // at the demo's rate whatever this machine's platform tier samples at, never thinned.
    Streams->SetMatchSeed(Seed);
    FMath::RandInit(Seed);
    FPSTelemetrySamplingTuning SampleTuning = Sampler->GetTuning();
    SampleTuning.SampleRateHz = Catalog.FrameRateHz;
    SampleTuning.MaxDegradeLevel = 0;
    Sampler->SetTuning(SampleTuning);

    FPlayWatch Watch;
    Watch.World = World;
    const FDelegateHandle SnapHandle = Bus->OnSnapMC.AddLambda([&Watch](const FPSTelemetrySnapEvent& Event)
    {
        if (!Watch.bSnapped)
        {
            Watch.bSnapped = true;
            Watch.SnapTime = Watch.Now();
            Watch.Snap = Event;
        }
    });
    const FDelegateHandle PhaseHandle = Bus->OnPhaseChangeMC.AddLambda([&Watch](const FPSTelemetryPhaseChangeEvent& Event)
    {
        if (Watch.bSnapped && !Watch.bWhistle && Event.NewPhase == TEXT("Scoring"))
        {
            Watch.bWhistle = true;
            Watch.WhistleTime = Watch.Now();
        }
    });
    const FDelegateHandle ResultHandle = Bus->OnPlayResultMC.AddLambda([&Watch](const FPSTelemetryPlayResultEvent& Event)
    {
        if (Watch.bSnapped && !Watch.bResult)
        {
            Watch.bResult = true;
            Watch.ResultTime = Watch.Now();
            Watch.Result = Event;
        }
    });
    const FDelegateHandle TackleHandle = Bus->OnTackleMC.AddLambda([&Watch](const FPSTelemetryTackleEvent&) { Watch.NoteEnding(TEXT("Tackle")); });
    const FDelegateHandle GroundedHandle = Bus->OnBallGroundedMC.AddLambda([&Watch](const FPSTelemetryBallGroundedEvent&) { Watch.NoteEnding(TEXT("BallGrounded")); });
    const FDelegateHandle BoundaryHandle = Bus->OnBoundaryCrossedMC.AddLambda([&Watch](const FPSTelemetryBoundaryCrossedEvent&) { Watch.NoteEnding(TEXT("BoundaryCrossed")); });
    const FDelegateHandle LooseHandle = Bus->OnLooseBallMC.AddLambda([&Watch](const FPSTelemetryLooseBallEvent&) { Watch.NoteEnding(TEXT("LooseBall")); });
    const FDelegateHandle ScoreHandle = Bus->OnScoreMC.AddLambda([&Watch](const FPSTelemetryScoreEvent&) { Watch.NoteEnding(TEXT("Score")); });
    const FDelegateHandle CallHandle = Bus->OnPlayCallMC.AddLambda([&Watch](const FPSTelemetryPlayCallEvent& Event)
    {
        if (Watch.bCalled && !Watch.bSnapped && Event.bOffense)
        {
            Watch.RunPlayId = Event.PlayId;
            Watch.RunPlayName = Event.DisplayName;
        }
    });
    const FDelegateHandle PenaltyHandle = Bus->OnPenaltyMC.AddLambda([&Watch](const FPSTelemetryPenaltyEvent& Event)
    {
        if (Watch.bCalled && !Watch.bResult)
        {
            const FString Kind = StaticEnum<EPSPenaltyEventKind>()->GetNameStringByValue(static_cast<int64>(Event.Kind));
            FString Line = FString::Printf(TEXT("%s: %s on the %s"), *Kind, *Event.Penalty, Event.bOnDefense ? TEXT("defense") : TEXT("offense"));
            if (Event.Kind == EPSPenaltyEventKind::Accepted)
            {
                Line += FString::Printf(TEXT(", %d yards"), Event.Yards);
            }
            Watch.Penalties.Add(Line);
        }
    });
    const auto Unwatch = [Bus, SnapHandle, PhaseHandle, ResultHandle, TackleHandle, GroundedHandle, BoundaryHandle, LooseHandle, ScoreHandle, CallHandle, PenaltyHandle]()
    {
        Bus->OnSnapMC.Remove(SnapHandle);
        Bus->OnPhaseChangeMC.Remove(PhaseHandle);
        Bus->OnPlayResultMC.Remove(ResultHandle);
        Bus->OnTackleMC.Remove(TackleHandle);
        Bus->OnBallGroundedMC.Remove(GroundedHandle);
        Bus->OnBoundaryCrossedMC.Remove(BoundaryHandle);
        Bus->OnLooseBallMC.Remove(LooseHandle);
        Bus->OnScoreMC.Remove(ScoreHandle);
        Bus->OnPlayCallMC.Remove(CallHandle);
        Bus->OnPenaltyMC.Remove(PenaltyHandle);
    };

    // Every bus event from kickoff on: the call window, the calls, the snap, the play, the result.
    UPSReplayRecorder* Recorder = NewObject<UPSReplayRecorder>(this);
    Recorder->BeginRecording(Bus, FPSReplayRecording());

    World->InitializeActorsForPlay(URL);
    World->BeginPlay();

    APSBall* Ball = GameMode->ActiveBall;
    UPSPlaySimulation* Simulation = GameMode->PlaySimulation;
    const APSPlayerPawn* AnyPawn = GameMode->CachedPawns.Num() > 0 ? GameMode->CachedPawns[0] : nullptr;
    if (!Ball || !Simulation || !AnyPawn || !GameMode->MatchSetup)
    {
        Summary.Problems.Add(FString::Printf(TEXT("The match didn't start: %d players, %s ball, %s simulation."), GameMode->CachedPawns.Num(),
            Ball ? TEXT("a") : TEXT("no"), Simulation ? TEXT("a") : TEXT("no")));
        Recorder->EndRecording();
        Unwatch();
        DestroyDemoWorld(World);
        return Run;
    }
    Summary.HomeTeamId = GameMode->MatchSetup->GetHomeTeamId();
    Summary.AwayTeamId = GameMode->MatchSetup->GetAwayTeamId();
    Summary.OffenseTeamId = Simulation->GetPlayState().bHomeHasPossession ? Summary.HomeTeamId : Summary.AwayTeamId;
    Summary.DefenseTeamId = Simulation->GetPlayState().bHomeHasPossession ? Summary.AwayTeamId : Summary.HomeTeamId;

    // What the checks hold the play to.
    const UProjectileMovementComponent* Flight = Ball->GetProjectileMovement();
    Summary.PlayerSpeedLimitCmPerSec = GameMode->MovementTuningSettings.BaseMaxSpeedMax + Catalog.SpeedAllowanceCmPerSec;
    Summary.BallSpeedLimitCmPerSec = Flight ? Flight->MaxSpeed : 0.f;
    Summary.GroundZ = static_cast<float>(PSField::YardLineToWorld(0.f).Z);
    Summary.PawnHalfHeightCm = AnyPawn->GetSimpleCollisionHalfHeight();
    Summary.BallRadiusCm = Ball->GetCollisionComponent() ? Ball->GetCollisionComponent()->GetScaledSphereRadius() : 0.f;

    // The kickoff's state and who took the field, as the format's initial state.
    TArray<FPlayerAttributes> OffenseOnField;
    TArray<FPlayerAttributes> DefenseOnField;
    for (const APSPlayerPawn* Pawn : GameMode->CachedPawns)
    {
        if (Pawn)
        {
            (Pawn->TeamSide == EPSTeamSide::Offense ? OffenseOnField : DefenseOnField).Add(Pawn->GetAttributes());
        }
    }
    const FPSReplayRecording Start = UPSReplayFormat::MakeRecording(Simulation->GetPlayState(), OffenseOnField, DefenseOnField);

    // Both calls, through the play-call authority, while the kickoff's call window is open.
    FPSPlayDefinition OffensePlay;
    if (PlayCall->FindPlay(Demo.OffensePlayId, OffensePlay))
    {
        Summary.OffensePlayName = OffensePlay.DisplayName;
        Summary.OffenseFormation = OffensePlay.Formation;
        Summary.OffenseCategory = OffensePlay.PlayCategory;
    }
    FPSPlayDefinition DefensePlay;
    if (PlayCall->FindPlay(Demo.DefensePlayId, DefensePlay))
    {
        Summary.DefensePlayName = DefensePlay.DisplayName;
        Summary.DefenseFormation = DefensePlay.Formation;
    }
    // The CPU quarterback reads the defense at the line as in any game; a demo that must show its
    // call keeps him to it.
    if (UPSPreSnapSubsystem* PreSnap = World->GetSubsystem<UPSPreSnapSubsystem>())
    {
        PreSnap->SetCpuAudiblesAllowed(Demo.bAllowAudibles);
    }
    Summary.bAudiblesAllowed = Demo.bAllowAudibles;
    Watch.bCalled = true;
    const bool bOffenseCalled = PlayCall->CallPlay(Demo.OffensePlayId, EPSPlayCaller::CPU);
    const bool bDefenseCalled = PlayCall->CallPlay(Demo.DefensePlayId, EPSPlayCaller::CPU);
    if (!bOffenseCalled || !bDefenseCalled)
    {
        Summary.Problems.Add(FString::Printf(TEXT("The play-call subsystem refused the call %s: not in the playbook or the team's scheme, or no call window."),
            *(!bOffenseCalled ? Demo.OffensePlayId : Demo.DefensePlayId).ToString()));
        Recorder->EndRecording();
        Unwatch();
        DestroyDemoWorld(World);
        return Run;
    }

    // The world ticks, one engine frame per step, until the play's result is on the bus and the
    // frames have run PostWhistleSeconds past the whistle.
    TArray<FPSSnapshotFrame> Frames;
    int32 LastFrameIndex = INDEX_NONE;
    float LastFrameTime = -1.f;
    bool bFramesOpen = true;
    const float StartTime = Watch.Now();
    const int32 MaxSteps = FMath::CeilToInt((Catalog.MaxPreSnapSeconds + Catalog.MaxPlaySeconds + Catalog.MaxResultWaitSeconds) * Catalog.FrameRateHz) + 1;
    for (int32 StepIndex = 1; StepIndex <= MaxSteps; ++StepIndex)
    {
        Recorder->SetTick(StepIndex);
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, Step);
        if (bFramesOpen)
        {
            CollectFrames(*Sampler, LastFrameIndex, LastFrameTime, Frames);
        }

        const float Now = Watch.Now();
        if (!Watch.bSnapped)
        {
            if (Now - StartTime > Catalog.MaxPreSnapSeconds)
            {
                Summary.Problems.Add(FString::Printf(TEXT("No snap %.1f s after kickoff."), Catalog.MaxPreSnapSeconds));
                break;
            }
            continue;
        }
        if (!Watch.bWhistle)
        {
            if (Now - Watch.SnapTime > Catalog.MaxPlaySeconds)
            {
                Summary.Problems.Add(FString::Printf(TEXT("No whistle %.1f s after the snap."), Catalog.MaxPlaySeconds));
                break;
            }
            continue;
        }
        if (bFramesOpen && Now - Watch.WhistleTime + Step * 0.5f >= Catalog.PostWhistleSeconds)
        {
            bFramesOpen = false;
        }
        if (!bFramesOpen && Watch.bResult)
        {
            break;
        }
        if (Now - Watch.WhistleTime > Catalog.MaxResultWaitSeconds)
        {
            Summary.Problems.Add(FString::Printf(TEXT("No play result on the bus %.1f s after the whistle."), Catalog.MaxResultWaitSeconds));
            break;
        }
    }

    Run.Recording = Recorder->EndRecording();
    Unwatch();
    DestroyDemoWorld(World);

    // The recording: the format's header and initial state, the frames, and who is in them. The
    // header's RandomSeed stays 0: it is the simulation stream's seed for re-simulation (Mode 2),
    // which a live play isn't; the demo's match seed is in the index.
    FPSReplayRecording& Recording = Run.Recording;
    Recording.Header = Start.Header;
    Recording.Header.GameBuildVersion = FApp::GetBuildVersion();
    Recording.Header.RandomSeed = 0;
    Recording.Header.FixedDeltaSeconds = Step;
    Recording.InitialState = Start.InitialState;
    Recording.Frames = MoveTemp(Frames);
    AddTeamsAndParticipants(Recording, Summary.HomeTeamId, Summary.AwayTeamId);
    UPSReplaySubsystem::AssignEventTicks(Recording);

    Summary.EventCount = Recording.Events.Num();
    Summary.FrameCount = Recording.Frames.Num();
    Summary.RunPlayId = Watch.RunPlayId.IsNone() ? Demo.OffensePlayId : Watch.RunPlayId;
    Summary.RunPlayName = Watch.RunPlayId.IsNone() ? Summary.OffensePlayName : Watch.RunPlayName;
    Summary.bAudible = Summary.RunPlayId != Demo.OffensePlayId;
    Summary.Penalties = Watch.Penalties;
    if (Watch.bSnapped)
    {
        Summary.SnapTime = Watch.SnapTime;
        Summary.Down = Watch.Snap.Down;
        Summary.Distance = Watch.Snap.Distance;
        Summary.YardLine = Watch.Snap.YardLine;
    }
    if (Watch.bWhistle)
    {
        Summary.WhistleTime = Watch.WhistleTime;
        Summary.PlaySeconds = Watch.WhistleTime - Watch.SnapTime;
        // The game mode announces the whistle on the step after the event that ended the play; an
        // ending event further back was one the simulation didn't end the play on.
        const bool bEndedByEvent = !Watch.LastEnding.IsEmpty() && Watch.WhistleTime - Watch.LastEndingTime <= Step * 2.5f;
        Summary.EndedBy = bEndedByEvent ? Watch.LastEnding : FString(TEXT("PhaseClock"));
    }
    if (Watch.bResult)
    {
        const FPSTelemetryPlayResultEvent& Result = Watch.Result;
        Summary.ResultTime = Watch.ResultTime;
        Summary.Result = Result.Result;
        Summary.Outcome = OutcomeOf(Result);
        Summary.YardsGained = Result.YardsGained;
        Summary.bFirstDown = Result.bFirstDown;
        Summary.PasserId = Result.PasserId;
        Summary.ReceiverId = Result.ReceiverId;
        Summary.RusherId = Result.RusherId;
        Summary.TacklerId = Result.TacklerId;
        Summary.InterceptorId = Result.InterceptorId;
    }
    Run.bRan = Watch.bSnapped && Watch.bWhistle && Watch.bResult;
    MeasureRun(Run, Step);
    Summary.Title = MakeTitle(Summary, Recording.Participants);
    Summary.Problems.Append(CheckRun(Run, Catalog));
    UE_LOG(LogTemp, Display, TEXT("UPSPlayDemoRunner: %s (seed %d): %s [%s, %d yards, ended by %s, %.2f s, %d frames, %d events, %d problem(s)]"),
        *Demo.DemoId.ToString(), Seed, *Summary.Title, *Summary.Outcome, Summary.YardsGained, *Summary.EndedBy, Summary.PlaySeconds,
        Summary.FrameCount, Summary.EventCount, Summary.Problems.Num());
    return Run;
}

TArray<FString> UPSPlayDemoRunner::CheckRun(const FPSPlayDemoRun& Run, const FPSPlayDemoCatalog& InCatalog)
{
    using namespace PSPlayDemoRunnerPrivate;

    TArray<FString> Problems;
    const FPSPlayDemoSummary& Summary = Run.Summary;
    const TArray<FPSSnapshotFrame>& Frames = Run.Recording.Frames;
    if (Frames.Num() < 2)
    {
        Problems.Add(FString::Printf(TEXT("Only %d frame(s) were recorded."), Frames.Num()));
        return Problems;
    }

    // Every step's frame, at the catalog's rate.
    const float Step = 1.f / FMath::Max(InCatalog.FrameRateHz, 1.f);
    int32 OffRate = 0;
    FString FirstOffRate;
    for (int32 Index = 1; Index < Frames.Num(); ++Index)
    {
        const float Gap = Frames[Index].Time - Frames[Index - 1].Time;
        if (FMath::Abs(Gap - Step) > Step * 0.05f)
        {
            if (OffRate++ == 0)
            {
                FirstOffRate = FString::Printf(TEXT("frame %d comes %.4f s after the one before"), Index, Gap);
            }
        }
    }
    if (OffRate > 0)
    {
        Problems.Add(FString::Printf(TEXT("%d frame(s) off the %.0f Hz rate; the first: %s."), OffRate, InCatalog.FrameRateHz, *FirstOffRate));
    }

    // From the snap: all 22 in every frame, each moving, and the ball moving.
    const int32 SnapFrame = Summary.SnapTime > 0.f ? FindFrameAt(Frames, Summary.SnapTime, Step) : INDEX_NONE;
    if (SnapFrame == INDEX_NONE)
    {
        Problems.Add(TEXT("No frame at or after the snap."));
    }
    else
    {
        int32 ShortFrames = 0;
        for (int32 Index = SnapFrame; Index < Frames.Num(); ++Index)
        {
            ShortFrames += Frames[Index].Pawns.Num() != 22 ? 1 : 0;
        }
        if (ShortFrames > 0)
        {
            Problems.Add(FString::Printf(TEXT("%d frame(s) from the snap don't have 22 players (the snap's has %d)."), ShortFrames, Frames[SnapFrame].Pawns.Num()));
        }

        // A lineman held up in his block moves little; everyone else has somewhere to go: the
        // quarterback his drop or hand-off, the others their routes, blocks and reads.
        TMap<FName, FVector> SnapSpots;
        TMap<FName, float> Moved;
        TMap<FName, float> MustMove;
        for (const FPSPawnSnapshot& Snapshot : Frames[SnapFrame].Pawns)
        {
            SnapSpots.Add(Snapshot.PlayerId, Snapshot.Location);
            Moved.Add(Snapshot.PlayerId, 0.f);
            const bool bLineman = Snapshot.Role == EPlayerRole::OffensiveLineman || Snapshot.Role == EPlayerRole::DefensiveLineman;
            MustMove.Add(Snapshot.PlayerId, bLineman ? InCatalog.MinLinemanMoveCm : InCatalog.MinPlayerMoveCm);
        }
        float BallTravel = 0.f;
        for (int32 Index = SnapFrame + 1; Index < Frames.Num(); ++Index)
        {
            for (const FPSPawnSnapshot& Snapshot : Frames[Index].Pawns)
            {
                if (const FVector* Spot = SnapSpots.Find(Snapshot.PlayerId))
                {
                    float& Farthest = Moved.FindChecked(Snapshot.PlayerId);
                    Farthest = FMath::Max(Farthest, static_cast<float>(FVector::Dist2D(Snapshot.Location, *Spot)));
                }
            }
            if (Frames[Index].bBallSampled && Frames[SnapFrame].bBallSampled)
            {
                BallTravel = FMath::Max(BallTravel, static_cast<float>(FVector::Dist(Frames[Index].BallLocation, Frames[SnapFrame].BallLocation)));
            }
        }
        TArray<FString> Still;
        for (const TPair<FName, float>& Entry : Moved)
        {
            const float Needed = MustMove.FindChecked(Entry.Key);
            if (Entry.Value < Needed)
            {
                Still.Add(FString::Printf(TEXT("%s (%.0f of %.0f cm)"), *Entry.Key.ToString(), Entry.Value, Needed));
            }
        }
        if (Still.Num() > 0)
        {
            Problems.Add(FString::Printf(TEXT("%d player(s) moved less than they must from their spot at the snap (%.0f cm, a lineman %.0f): %s."),
                Still.Num(), InCatalog.MinPlayerMoveCm, InCatalog.MinLinemanMoveCm, *FString::Join(Still, TEXT(", "))));
        }
        if (!Frames[SnapFrame].bBallSampled)
        {
            Problems.Add(TEXT("No ball in the snap's frame."));
        }
        else if (BallTravel < InCatalog.MinPlayerMoveCm)
        {
            Problems.Add(FString::Printf(TEXT("The ball moved only %.0f cm from the snap."), BallTravel));
        }
    }

    // The play's outcome on the bus.
    const bool bResult = Run.Recording.Events.ContainsByPredicate([&Summary](const FPSReplayEventRecord& Event)
    {
        return Event.EventType == TEXT("PlayResult") && Event.TimestampSeconds >= Summary.SnapTime;
    });
    if (SnapFrame != INDEX_NONE && !bResult)
    {
        Problems.Add(TEXT("No PlayResult on the bus after the snap."));
    }

    // Every frame: real numbers, speeds within the limits, nobody through the ground.
    int32 NonFinite = 0;
    int32 TooFast = 0;
    float Fastest = 0.f;
    FString FastestWho;
    int32 BelowGround = 0;
    FString FirstBelow;
    int32 BallTooFast = 0;
    const float Floor = Summary.GroundZ - InCatalog.GroundToleranceCm;
    for (const FPSSnapshotFrame& Frame : Frames)
    {
        for (const FPSPawnSnapshot& Snapshot : Frame.Pawns)
        {
            if (!IsFiniteVector(Snapshot.Location) || !IsFiniteVector(Snapshot.Velocity) || !IsFiniteVector(Snapshot.Acceleration) || !FMath::IsFinite(Snapshot.FacingYaw))
            {
                ++NonFinite;
                continue;
            }
            const float Speed = static_cast<float>(Snapshot.Velocity.Size2D());
            if (Speed > Summary.PlayerSpeedLimitCmPerSec)
            {
                ++TooFast;
                if (Speed > Fastest)
                {
                    Fastest = Speed;
                    FastestWho = FString::Printf(TEXT("%s at %.0f cm/s, %.2f s"), *Snapshot.PlayerId.ToString(), Speed, Frame.Time);
                }
            }
            if (Snapshot.Location.Z - Summary.PawnHalfHeightCm < Floor)
            {
                if (BelowGround++ == 0)
                {
                    FirstBelow = FString::Printf(TEXT("%s at Z %.1f, %.2f s"), *Snapshot.PlayerId.ToString(), Snapshot.Location.Z, Frame.Time);
                }
            }
        }
        if (Frame.bBallSampled)
        {
            if (!IsFiniteVector(Frame.BallLocation) || !IsFiniteVector(Frame.BallVelocity))
            {
                ++NonFinite;
                continue;
            }
            if (Frame.BallVelocity.Size() > FMath::Max(Summary.BallSpeedLimitCmPerSec, Summary.PlayerSpeedLimitCmPerSec) + KINDA_SMALL_NUMBER)
            {
                ++BallTooFast;
            }
            if (Frame.BallLocation.Z - Summary.BallRadiusCm < Floor)
            {
                if (BelowGround++ == 0)
                {
                    FirstBelow = FString::Printf(TEXT("the ball at Z %.1f, %.2f s"), Frame.BallLocation.Z, Frame.Time);
                }
            }
        }
    }
    if (NonFinite > 0)
    {
        Problems.Add(FString::Printf(TEXT("%d snapshot(s) hold a NaN or infinite number."), NonFinite));
    }
    if (TooFast > 0)
    {
        Problems.Add(FString::Printf(TEXT("%d snapshot(s) faster than the movement data's %.0f cm/s (the fastest: %s)."), TooFast, Summary.PlayerSpeedLimitCmPerSec, *FastestWho));
    }
    if (BallTooFast > 0)
    {
        Problems.Add(FString::Printf(TEXT("%d frame(s) with the ball faster than %.0f cm/s."), BallTooFast, Summary.BallSpeedLimitCmPerSec));
    }
    if (BelowGround > 0)
    {
        Problems.Add(FString::Printf(TEXT("%d time(s) a capsule or the ball went below the ground (the first: %s)."), BelowGround, *FirstBelow));
    }
    return Problems;
}

bool UPSPlayDemoRunner::WriteDemos(TArray<FPSPlayDemoRun>& Runs, const FString& Directory, FString& OutIndexPath)
{
    using namespace PSPlayDemoRunnerPrivate;

    const FString OutputDirectory = Directory.IsEmpty() ? GetDefaultOutputDirectory() : Directory;
    IFileManager& Files = IFileManager::Get();
    // The CI runner's workspace persists between runs: an earlier run's plays must not linger.
    TArray<FString> Earlier;
    Files.FindFiles(Earlier, *(OutputDirectory / TEXT("*.json")), true, false);
    for (const FString& Name : Earlier)
    {
        Files.Delete(*(OutputDirectory / Name));
    }
    Files.MakeDirectory(*OutputDirectory, true);

    bool bWritten = true;
    FPSPlayDemoIndex Index;
    Index.GeneratedAtUtc = FDateTime::UtcNow();
    Index.GameBuildVersion = FApp::GetBuildVersion();
    Index.FrameRateHz = Catalog.FrameRateHz;
    Index.Method = DemoMethod;
    for (int32 RunIndex = 0; RunIndex < Runs.Num(); ++RunIndex)
    {
        FPSPlayDemoRun& Run = Runs[RunIndex];
        Run.Summary.File = FString::Printf(TEXT("%02d_%s.json"), RunIndex + 1, *Run.Summary.DemoId.ToString());
        const FString Json = UPSReplayFormat::SerializeToJson(Run.Recording);
        if (Json.IsEmpty() || !FFileHelper::SaveStringToFile(Json, *(OutputDirectory / Run.Summary.File), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPlayDemoRunner: Could not write %s."), *(OutputDirectory / Run.Summary.File));
            bWritten = false;
        }
        Index.Plays.Add(Run.Summary);
    }

    OutIndexPath = OutputDirectory / TEXT("index.json");
    FString IndexJson;
    if (!FJsonObjectConverter::UStructToJsonObjectString(Index, IndexJson)
        || !FFileHelper::SaveStringToFile(IndexJson, *OutIndexPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayDemoRunner: Could not write %s."), *OutIndexPath);
        bWritten = false;
    }
    return bWritten;
}
