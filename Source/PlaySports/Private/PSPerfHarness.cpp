#include "PSPerfHarness.h"
#include "PSAIFieldSnapshot.h"
#include "PSAudioSubsystem.h"
#include "PSBall.h"
#include "PSCrowdExcitementSubsystem.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenderGapSubsystem.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSOffenseController.h"
#include "PSOverlayBallFlightSubsystem.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSPerfBudget.h"
#include "PSPlaySimulation.h"
#include "PSPlayerPawn.h"
#include "PSRushMoveComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

const TCHAR* UPSPerfHarness::StandardPlayScenario = TEXT("StandardPlay");

namespace PSPerfHarnessPrivate
{
    /** The eleven on each side of the standard play, as the game's default personnel lines up. */
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

    /** A pawn at Location under its side's AI controller, listening to the bus. */
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
        Attributes.PlayerId = FName(*FString::Printf(TEXT("PERF_%02d"), Number));
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

    void HandBall(APSBall* Ball, APSPlayerPawn* From, APSPlayerPawn* To)
    {
        if (From)
        {
            From->LosePossession();
        }
        if (To)
        {
            To->GainPossession();
            if (Ball)
            {
                Ball->AttachToCarrier(To);
            }
        }
    }

    /** `PS.Perf.RunHarness`: the standard play on this machine, at its tier. */
    void RunHarnessCommand(const TArray<FString>& Args)
    {
        TStrongObjectPtr<UPSPerfHarness> Harness(NewObject<UPSPerfHarness>());
        const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();
        const FPSPerfReport Report = Harness->RunStandardPlayInScratchWorld(Tier);
        const FString Path = PSPerf::GetReportPath(UPSPerfHarness::StandardPlayScenario, Tier.TierId);
        UPSPerfHarness::LogReport(Report);
        UE_LOG(LogTemp, Display, TEXT("PS.Perf.RunHarness: %s %s"), PSPerf::WriteReport(Report, Path) ? TEXT("wrote") : TEXT("could not write"), *Path);
    }

    /** `PS.Perf.Capture [seconds]`: profiles whatever is running for that long (30 s by
     *  default), one frame per engine tick, then writes Saved/Profiling/Live_<tier>.json. */
    void CaptureCommand(const TArray<FString>& Args)
    {
        if (PSPerf::IsCapturing())
        {
            UE_LOG(LogTemp, Warning, TEXT("PS.Perf.Capture: a capture is already running."));
            return;
        }
        const float Seconds = Args.Num() > 0 ? FMath::Max(FCString::Atof(*Args[0]), 1.f) : 30.f;
        TStrongObjectPtr<UPSPerfHarness> Settings(NewObject<UPSPerfHarness>());
        const FPSPerfHarnessTuning& Active = Settings->GetTuning();
        PSPerf::BeginCapture(Active.HistogramBucketMs, Active.HistogramBucketCount);
        const double EndsAt = FPlatformTime::Seconds() + Seconds;
        UE_LOG(LogTemp, Display, TEXT("PS.Perf.Capture: profiling for %.0f s."), Seconds);

        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([EndsAt](float DeltaTime) -> bool
        {
            if (!PSPerf::IsCapturing())
            {
                return false;
            }
            PSPerf::EndFrame();
            if (FPlatformTime::Seconds() < EndsAt)
            {
                return true;
            }
            PSPerf::EndCapture();
            const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();
            const FPSPerfReport Report = PSPerf::BuildReport(Tier, TEXT("Live"));
            const FString Path = PSPerf::GetReportPath(TEXT("Live"), Tier.TierId);
            UPSPerfHarness::LogReport(Report);
            UE_LOG(LogTemp, Display, TEXT("PS.Perf.Capture: %s %s"), PSPerf::WriteReport(Report, Path) ? TEXT("wrote") : TEXT("could not write"), *Path);
            return false;
        }));
    }

    FAutoConsoleCommand RunHarnessConsoleCommand(
        TEXT("PS.Perf.RunHarness"),
        TEXT("Runs the standard play headless and writes Saved/Profiling/StandardPlay_<tier>.json (Epic 114)."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&RunHarnessCommand));

    FAutoConsoleCommand CaptureConsoleCommand(
        TEXT("PS.Perf.Capture"),
        TEXT("PS.Perf.Capture [seconds]: profiles the game's systems while it runs and writes Saved/Profiling/Live_<tier>.json (Epic 114)."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&CaptureCommand));
}

FString UPSPerfHarness::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/perf_harness.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSPerfHarness::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPerfHarnessTuning Loaded;
    if (!Ingestion || !Ingestion->LoadPerfHarnessTuningFromJson(JsonFilePath, Loaded))
    {
        return false;
    }
    const TArray<FString> Problems = ValidateTuning(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPerfHarness: %s: %s"), *JsonFilePath, *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = Loaded;
    bTuningLoaded = true;
    return true;
}

const FPSPerfHarnessTuning& UPSPerfHarness::GetTuning()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        if (!LoadTuningFromJson(GetDefaultTuningPath()))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPerfHarness: Using the default harness tuning."));
        }
    }
    return Tuning;
}

TArray<FString> UPSPerfHarness::ValidateTuning(const FPSPerfHarnessTuning& Candidate)
{
    TArray<FString> Problems;
    if (!(Candidate.FrameSeconds > 0.f) || !(Candidate.HistogramBucketMs > 0.f))
    {
        Problems.Add(TEXT("FrameSeconds and HistogramBucketMs must be above 0"));
    }
    if (Candidate.WarmupFrames < 0 || Candidate.PreSnapFrames < 0 || Candidate.PassFrames < 1 || Candidate.PursuitFrames < 1)
    {
        Problems.Add(TEXT("WarmupFrames and PreSnapFrames must be 0 or more, PassFrames and PursuitFrames 1 or more"));
    }
    if (Candidate.HistogramBucketCount < 1 || Candidate.MaxBusEventsPerPlay < 1 || Candidate.TrendWindow < 1)
    {
        Problems.Add(TEXT("HistogramBucketCount, MaxBusEventsPerPlay and TrendWindow must be 1 or more"));
    }
    if (Candidate.HardFailMultiplier < 1.f)
    {
        Problems.Add(TEXT("HardFailMultiplier must be 1 or more"));
    }
    if (Candidate.RegressionTolerance < 0.f || Candidate.MinRegressionMs < 0.f)
    {
        Problems.Add(TEXT("RegressionTolerance and MinRegressionMs must be 0 or more"));
    }
    return Problems;
}

int32 UPSPerfHarness::GetCapturedFrameCount(const FPSPerfHarnessTuning& InTuning)
{
    return InTuning.PassFrames + InTuning.PursuitFrames + InTuning.PreSnapFrames;
}

void UPSPerfHarness::StepFrame(UWorld* World, const TArray<APSPlayerPawn*>& Players, UPSPlaySimulation* Simulation, float DeltaSeconds, float DecisionInterval)
{
    // A new frame for the shared field read (Epic 17.5), then the game's systems in tick order.
    if (UPSAIFieldSnapshot* Field = World->GetSubsystem<UPSAIFieldSnapshot>())
    {
        Field->Invalidate();
    }
    if (Simulation)
    {
        Simulation->AdvancePlay(DeltaSeconds);
    }
    for (APSPlayerPawn* Player : Players)
    {
        if (const APSDefenseController* Defense = Cast<APSDefenseController>(Player->GetController()))
        {
            Defense->GetDefenderAI()->UpdateAI(DeltaSeconds, DecisionInterval);
            Defense->GetRushMoves()->TickRush(DeltaSeconds);
        }
        else if (const APSOffenseController* Offense = Cast<APSOffenseController>(Player->GetController()))
        {
            Offense->GetSkillAI()->UpdateAI(DeltaSeconds, DecisionInterval);
        }
    }
    if (UPSDefenderGapSubsystem* Gaps = World->GetSubsystem<UPSDefenderGapSubsystem>())
    {
        Gaps->UpdateFits(DeltaSeconds);
    }
    if (UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>())
    {
        Sampler->AdvanceTime(DeltaSeconds);
    }
    if (UPSOverlayBallFlightSubsystem* BallFlight = World->GetSubsystem<UPSOverlayBallFlightSubsystem>())
    {
        BallFlight->AdvanceTime(DeltaSeconds);
    }
    if (UPSOverlayBroadcastSubsystem* Broadcast = World->GetSubsystem<UPSOverlayBroadcastSubsystem>())
    {
        Broadcast->AdvanceTime(DeltaSeconds);
    }
    if (UPSCrowdExcitementSubsystem* Crowd = World->GetSubsystem<UPSCrowdExcitementSubsystem>())
    {
        Crowd->AdvanceTime(DeltaSeconds);
    }
    if (UPSAudioSubsystem* Audio = World->GetSubsystem<UPSAudioSubsystem>())
    {
        Audio->AdvanceTime(DeltaSeconds);
    }
    PSPerf::EndFrame();
}

FPSPerfReport UPSPerfHarness::RunStandardPlay(UWorld* World, const FPSPlatformTier& Tier)
{
    const FPSPerfHarnessTuning& Active = GetTuning();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        FPSPerfReport Empty;
        Empty.Scenario = StandardPlayScenario;
        Empty.TierId = Tier.TierId;
        return Empty;
    }

    // Two elevens, lined up from the 20-yard line, and the ball in the center's hands.
    const float ScrimmageX = 2000.f;
    const TArray<EPlayerRole> Roles = PSPerfHarnessPrivate::StandardRoles();
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, ScrimmageX);
    TArray<APSPlayerPawn*> Players;
    TArray<FPlayerAttributes> OffenseRoster;
    TArray<FPlayerAttributes> DefenseRoster;
    for (int32 Index = 0; Index < Roles.Num() && Index < Lineup.Num(); ++Index)
    {
        if (APSPlayerPawn* Player = PSPerfHarnessPrivate::SpawnPlayer(World, Roles[Index], Index, Lineup[Index]))
        {
            Players.Add(Player);
            (Player->TeamSide == EPSTeamSide::Offense ? OffenseRoster : DefenseRoster).Add(Player->GetAttributes());
        }
    }
    APSPlayerPawn* Center = PSPerfHarnessPrivate::FindFirst(Players, EPlayerRole::OffensiveLineman);
    APSPlayerPawn* Quarterback = PSPerfHarnessPrivate::FindFirst(Players, EPlayerRole::Quarterback);
    APSPlayerPawn* Receiver = PSPerfHarnessPrivate::FindFirst(Players, EPlayerRole::WideReceiver);
    APSPlayerPawn* Tackler = PSPerfHarnessPrivate::FindFirst(Players, EPlayerRole::DefensiveBack);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(ScrimmageX, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
    PSPerfHarnessPrivate::HandBall(Ball, nullptr, Center);

    UPSPlaySimulation* Simulation = NewObject<UPSPlaySimulation>(this);
    Simulation->InitializePlay(OffenseRoster, DefenseRoster);
    Simulation->InitializeWithWorld(World);
    if (UPSTelemetrySamplingSubsystem* Sampler = World->GetSubsystem<UPSTelemetrySamplingSubsystem>())
    {
        Sampler->ApplyPlatformTier(Tier);
        Sampler->RefreshRoster();
    }
    // The audio and the crowd hear the play as they do in a match (Epic 23), at the tier's rates.
    if (UPSAudioSubsystem* Audio = World->GetSubsystem<UPSAudioSubsystem>())
    {
        Audio->BindToBus(Bus);
        Audio->ApplyPlatformTier(Tier);
    }
    if (UPSCrowdExcitementSubsystem* Crowd = World->GetSubsystem<UPSCrowdExcitementSubsystem>())
    {
        Crowd->BindToBus(Bus);
        Crowd->ApplyPlatformTier(Tier);
    }

    const float Frame = Active.FrameSeconds;
    const float Interval = Tier.AIDecisionInterval;
    for (int32 Warmup = 0; Warmup < Active.WarmupFrames; ++Warmup)
    {
        StepFrame(World, Players, Simulation, Frame, Interval);
    }

    PSPerf::BeginCapture(Active.HistogramBucketMs, Active.HistogramBucketCount);

    // The snap: the center gives the QB the ball, and the CPU calls both sides.
    PSPerfHarnessPrivate::HandBall(Ball, Center, Quarterback);
    FPSTelemetrySnapEvent Snap;
    Snap.Down = 1;
    Snap.Distance = 10;
    Snap.YardLine = 20;
    Snap.LineOfScrimmage = FVector(ScrimmageX, 0.f, 0.f);
    Bus->PublishSnap(Snap);
    Simulation->TriggerSnap();

    // The rush, the routes and the coverage; two thirds in, the throw.
    const int32 ThrowFrame = Active.PassFrames * 2 / 3;
    for (int32 Index = 0; Index < Active.PassFrames; ++Index)
    {
        if (Index == ThrowFrame && Quarterback && Receiver)
        {
            FPSTelemetryThrowEvent Throw;
            Throw.PasserName = Quarterback->GetAttributes().DisplayName;
            Throw.TargetReceiverName = Receiver->GetAttributes().DisplayName;
            Throw.StartLocation = Quarterback->GetActorLocation();
            Throw.TargetLocation = Receiver->GetActorLocation();
            Throw.LandingLocation = Throw.TargetLocation;
            Throw.LaunchSpeed = 2000.f;
            Bus->PublishThrow(Throw);
        }
        StepFrame(World, Players, Simulation, Frame, Interval);
    }

    // The catch, then pursuit of the receiver.
    PSPerfHarnessPrivate::HandBall(Ball, Quarterback, Receiver);
    if (Receiver)
    {
        FPSTelemetryCatchEvent Catch;
        Catch.ReceiverName = Receiver->GetAttributes().DisplayName;
        Catch.CatchLocation = Receiver->GetActorLocation();
        Catch.YardsGained = 12;
        Bus->PublishCatch(Catch);
    }
    for (int32 Index = 0; Index < Active.PursuitFrames; ++Index)
    {
        StepFrame(World, Players, Simulation, Frame, Interval);
    }

    // The tackle and the whistle, then the next down's pre-snap.
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = Tackler ? Tackler->GetAttributes().DisplayName : FString();
    Tackle.BallCarrierName = Receiver ? Receiver->GetAttributes().DisplayName : FString();
    Tackle.YardLine = 38;
    Bus->PublishTackle(Tackle);
    FPSTelemetryPhaseChangeEvent Whistle;
    Whistle.OldPhase = TEXT("BallCarrierMovement");
    Whistle.NewPhase = TEXT("PreSnap");
    Bus->PublishPhaseChange(Whistle);
    for (int32 Index = 0; Index < Active.PreSnapFrames; ++Index)
    {
        StepFrame(World, Players, Simulation, Frame, Interval);
    }

    PSPerf::EndCapture();
    return PSPerf::BuildReport(Tier, StandardPlayScenario);
}

FPSPerfReport UPSPerfHarness::RunStandardPlayInScratchWorld(const FPSPlatformTier& Tier)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!World || !GEngine)
    {
        return FPSPerfReport();
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);
    const FPSPerfReport Report = RunStandardPlay(World, Tier);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return Report;
}

void UPSPerfHarness::LogReport(const FPSPerfReport& Report)
{
    UE_LOG(LogTemp, Display, TEXT("PSPerf: %s on %s (%s), %d frames, %.0f fps frame %.2f ms; %d bus events, %d AI decisions, %d field scans."),
        *Report.Scenario, *Report.TierId.ToString(), *Report.Platform, Report.Frames, Report.TargetFrameRate, Report.FrameBudgetMs,
        Report.BusEvents, Report.AIDecisions, Report.FieldScans);
    for (const FPSPerfSystemResult& Result : Report.Systems)
    {
        if (!Result.bMeasured)
        {
            UE_LOG(LogTemp, Display, TEXT("PSPerf:   %-10s not measured (budget %.2f ms)"), *UEnum::GetValueAsString(Result.System), Result.BudgetMs);
            continue;
        }
        UE_LOG(LogTemp, Display, TEXT("PSPerf:   %-10s p50 %.3f  p95 %.3f  max %.3f ms of %.2f%s"), *UEnum::GetValueAsString(Result.System),
            Result.P50Ms, Result.P95Ms, Result.MaxMs, Result.BudgetMs, Result.bOverBudget ? TEXT("  OVER BUDGET") : TEXT(""));
    }
}
