#include "PSGameMode.h"
#include "PSPlayCallSubsystem.h"
#include "PSPreSnapSubsystem.h"
#include "PSFieldReads.h"
#include "PSDataIngestion.h"
#include "PSPlaySimulation.h"
#include "Misc/Paths.h"
#include "PSHUD.h"
#include "PSPlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "PSBall.h"
#include "PSPlayerPawn.h"
#include "PSFieldGrid.h"
#include "PSBroadcastCamera.h"
#include "PSRoster.h"
#include "PSPersonnelManager.h"
#include "PSHealthComponent.h"
#include "PSRulesConfig.h"
#include "PSPlayerLeveling.h"
#include "PSMatchSetup.h"
#include "PSStaffManager.h"
#include "PSStatsEngine.h"
#include "PSUITeamCatalog.h"
#include "PSVersusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/FloatingPawnMovement.h"

static bool LoadMovementTuningFromJson(const FString& JsonFilePath, FMovementTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> ParsedArray;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedArray) || ParsedArray.Num() == 0)
    {
        TSharedPtr<FJsonObject> ParsedObject;
        TSharedRef<TJsonReader<>> ReaderObj = TJsonReaderFactory<>::Create(JsonString);
        if (FJsonSerializer::Deserialize(ReaderObj, ParsedObject) && ParsedObject.IsValid())
        {
            return FJsonObjectConverter::JsonObjectToUStruct(ParsedObject.ToSharedRef(), &OutTuning, 0, 0);
        }
        return false;
    }

    TSharedPtr<FJsonObject> RowObject = ParsedArray[0]->AsObject();
    if (RowObject.IsValid())
    {
        return FJsonObjectConverter::JsonObjectToUStruct(RowObject.ToSharedRef(), &OutTuning, 0, 0);
    }
    return false;
}

APSGameMode::APSGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    RosterJsonPath = TEXT("Data/sample_players.json");
    PlayerRosterTable = nullptr;
    PlaySimulation = nullptr;
    BroadcastCamera = nullptr;
    ActiveRoster = nullptr;
    PersonnelManager = nullptr;
    CurrentPlayIndex = 0;
    ExtraDefenderPawn = nullptr;
    PlayerLeveling = nullptr;
    MatchSetup = nullptr;
    StaffManager = nullptr;
    MatchStats = nullptr;

    HUDClass = APSHUD::StaticClass();
    PlayerControllerClass = APSPlayerController::StaticClass();
    HomeScore = 0;
    AwayScore = 0;

    MovementTuningTable = nullptr;
    MovementTuningJsonPath = TEXT("Data/movement_tuning.json");
    MovementTuningSettings = FMovementTuningRow();
}

void APSGameMode::StartPlay()
{
    Super::StartPlay();

    // Who plays: the travel options name the teams (UPSMatchSetup reads them).
    MatchSetup = NewObject<UPSMatchSetup>(this);
    MatchSetup->InitializeFromOptions(OptionsString, UPSMatchSetup::LoadLeagueTeamIds(UPSUITeamCatalog::GetDefaultTeamsPath()));
    // A head-to-head game's seats play these teams (Epic 107): the versus subsystem reads them here.
    if (UPSVersusSubsystem* Versus = GetWorld()->GetSubsystem<UPSVersusSubsystem>())
    {
        Versus->SetMatchSetup(MatchSetup);
    }

    // Load movement tuning from DataTable or JSON
    if (MovementTuningTable)
    {
        static const FString ContextString(TEXT("MovementTuningContext"));
        TArray<FMovementTuningRow*> TuningRows;
        MovementTuningTable->GetAllRows<FMovementTuningRow>(ContextString, TuningRows);
        if (TuningRows.Num() > 0)
        {
            MovementTuningSettings = *TuningRows[0];
            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Loaded movement tuning settings from DataTable."));
        }
    }
    else
    {
        FString FullTuningPath = FPaths::ProjectDir() + MovementTuningJsonPath;
        FPaths::CollapseRelativeDirectories(FullTuningPath);
        if (FPaths::FileExists(FullTuningPath))
        {
            FMovementTuningRow LoadedTuning;
            if (LoadMovementTuningFromJson(FullTuningPath, LoadedTuning))
            {
                MovementTuningSettings = LoadedTuning;
                UE_LOG(LogTemp, Display, TEXT("PSGameMode: Loaded movement tuning settings from JSON (%s)."), *FullTuningPath);
            }
        }
    }

    // Instantiate transient DataTable if none configured
    if (!PlayerRosterTable)
    {
        PlayerRosterTable = NewObject<UDataTable>(this, TEXT("DynamicPlayerRosterTable"));
        PlayerRosterTable->RowStruct = FPlayerAttributes::StaticStruct();
        UE_LOG(LogTemp, Display, TEXT("PSGameMode: No PlayerRosterTable configured. Created a transient table."));
    }

    // Resolve JSON path relative to project directory
    FString FullJsonPath = FPaths::ProjectDir() + RosterJsonPath;
    FPaths::CollapseRelativeDirectories(FullJsonPath);

    UE_LOG(LogTemp, Display, TEXT("PSGameMode: Attempting to load roster from %s"), *FullJsonPath);

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    if (Ingestion)
    {
        if (Ingestion->LoadPlayerAttributesFromJson(FullJsonPath, PlayerRosterTable))
        {
            int32 RowCount = PlayerRosterTable->GetRowMap().Num();
            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Successfully ingested roster. Loaded %d players."), RowCount);

            // Epic 19.5: the roster holds every player and the depth chart; the personnel
            // manager picks who takes the field from it, starting with the default packages.
            TArray<FPlayerAttributes*> AllPlayers;
            PlayerRosterTable->GetAllRows<FPlayerAttributes>(TEXT("PSGameMode Roster Ingestion"), AllPlayers);
            TArray<FPlayerAttributes> RosterRows;
            for (const FPlayerAttributes* Player : AllPlayers)
            {
                if (Player)
                {
                    RosterRows.Add(*Player);
                }
            }
            // The match's teams take the field (UPSMatchSetup): the home offense against the away
            // defense, each from its own team's roster; RosterJsonPath's players only without
            // them. Kickoff (Epic 89): both staffs' plans go to the play-call authority and every
            // player plays at his team's scheme fit, in the rows the pawns point at -- the
            // simulation's copies below are the same players at the same ratings.
            MatchSetup->LoadFieldPlayers(UPSUITeamCatalog::GetDefaultTeamsPath(), RosterRows);
            StaffManager = NewObject<UPSStaffManager>(this);
            StaffManager->LoadFromJson(UPSStaffManager::GetDefaultDataPath());
            MatchSetup->ApplyStaffsToField(StaffManager, GetWorld()->GetSubsystem<UPSPlayCallSubsystem>(), RosterRows);
            ActiveRoster = NewObject<UPSRoster>(this);
            ActiveRoster->InitializeRoster(RosterRows);
            ActiveRoster->BuildDefaultDepthChart();
            PersonnelManager = NewObject<UPSPersonnelManager>(this);
            PersonnelManager->Initialize(ActiveRoster);
            PersonnelManager->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath());
            const TArray<const FPlayerAttributes*> Starters = PersonnelManager->GetStartingLineup();

            TArray<FPlayerAttributes> OffenseRoster;
            TArray<FPlayerAttributes> DefenseRoster;
            for (const FPlayerAttributes* Player : Starters)
            {
                TArray<FPlayerAttributes>& SideRoster = APSFieldGrid::GetSideForRole(Player->Role) == EPSTeamSide::Offense ? OffenseRoster : DefenseRoster;
                SideRoster.Add(*Player);
            }

            PlaySimulation = NewObject<UPSPlaySimulation>(this);
            if (PlaySimulation)
            {
                PlaySimulation->InitializePlay(OffenseRoster, DefenseRoster);
                UE_LOG(LogTemp, Display, TEXT("PSGameMode: Initialized PlaySimulation with %d Offense and %d Defense players."), OffenseRoster.Num(), DefenseRoster.Num());

                // Subscribe GameMode scoring to the telemetry bus
                UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
                if (Bus)
                {
                    Bus->OnScore.AddDynamic(this, &APSGameMode::OnBusScoreEvent);
                    Bus->OnCatch.AddDynamic(this, &APSGameMode::OnBusCatchEvent);
                    Bus->OnDeath.AddDynamic(this, &APSGameMode::OnBusDeathEvent);
                    UE_LOG(LogTemp, Display, TEXT("PSGameMode: Subscribed scoring/catch/death handlers to TelemetryBus."));
                }

                // The match's box score, from the plays the simulation announces (Epic 92).
                MatchStats = NewObject<UPSStatsEngine>(this);
                MatchStats->BindToBus(Bus);
                MatchStats->BeginGame(MatchSetup->GetSeasonWeek(), MatchSetup->GetHomeTeamId(), MatchSetup->GetAwayTeamId());

                // Give the simulation its world ref so it can subscribe to bus events (C2)
                PlaySimulation->InitializeWithWorld(GetWorld());
            }

            PlayerLeveling = NewObject<UPSPlayerLeveling>(this);

            // Find the broadcast camera in the level so bus-driven catch events can
            // retarget it (fixes the orphaned TargetActor, Epic C3).
            BroadcastCamera = Cast<APSBroadcastCamera>(UGameplayStatics::GetActorOfClass(GetWorld(), APSBroadcastCamera::StaticClass()));
            if (BroadcastCamera)
            {
                UE_LOG(LogTemp, Display, TEXT("PSGameMode: Found BroadcastCamera %s in level."), *BroadcastCamera->GetName());
            }

            CachedPawns.Reset();
            TArray<AActor*> ExistingPawns;
            UGameplayStatics::GetAllActorsOfClass(GetWorld(), APSPlayerPawn::StaticClass(), ExistingPawns);
            if (ExistingPawns.Num() == 0)
            {
                // The pawns point at the roster's own rows (one authority, Epic C3/19.5).
                const float ScrimmageX = PlaySimulation ? PlaySimulation->GetPlayState().YardLine * 100.f : 2000.f;
                CachedPawns = APSFieldGrid::SpawnPlayersFromRoster(Starters, ScrimmageX, GetWorld());
                PersonnelManager->BindPawns(CachedPawns);
                PersonnelManager->BindToBus(GetWorld()->GetSubsystem<UPSTelemetryBus>());
                PersonnelManager->BeginNewPlay(ScrimmageX, CurrentPlayIndex);
            }
            else
            {
                for (AActor* Actor : ExistingPawns)
                {
                    if (APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(Actor))
                    {
                        CachedPawns.Add(Pawn);
                    }
                }
            }

            FActorSpawnParameters BallSpawnParams;
            BallSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ActiveBall = GetWorld()->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(100.f, 0.f, 100.f), FRotator::ZeroRotator, BallSpawnParams);

            APSPlayerPawn* Center = FindPlayerPawnByRole(EPlayerRole::OffensiveLineman);
            if (ActiveBall && Center)
            {
                ActiveBall->AttachToCarrier(Center, TEXT("HandSocket"));
                Center->GainPossession();
                UE_LOG(LogTemp, Display, TEXT("PSGameMode: Spawned ActiveBall and attached it to Center (%s) pre-snap."), *Center->GetName());
            }

            // The first down's call window (Epic 102); later downs open on entering PreSnap.
            OpenPlayCallWindow();
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("PSGameMode: Failed to load player roster from JSON."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("PSGameMode: Failed to instantiate UPSDataIngestion."));
    }
}

void APSGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (PlaySimulation)
    {
        EPlayPhase PreviousPhase = PlaySimulation->GetPlayState().Phase;
        PlaySimulation->AdvancePlay(DeltaSeconds);
        EPlayPhase CurrentPhase = PlaySimulation->GetPlayState().Phase;

        if (PreviousPhase != CurrentPhase)
        {
            FString OldPhaseStr;
            FString NewPhaseStr;
            auto PhaseToString = [](EPlayPhase P) -> FString
            {
                switch (P)
                {
                case EPlayPhase::PreSnap:             return TEXT("PreSnap");
                case EPlayPhase::Snap:                return TEXT("Snap");
                case EPlayPhase::PassRush:            return TEXT("PassRush");
                case EPlayPhase::BallCarrierMovement: return TEXT("BallCarrierMovement");
                case EPlayPhase::Scoring:             return TEXT("Scoring");
                case EPlayPhase::Kickoff:             return TEXT("Kickoff");
                case EPlayPhase::Punt:                return TEXT("Punt");
                case EPlayPhase::FieldGoal:           return TEXT("FieldGoal");
                default:                              return TEXT("Unknown");
                }
            };
            OldPhaseStr = PhaseToString(PreviousPhase);
            NewPhaseStr = PhaseToString(CurrentPhase);

            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Play Phase Transitioned to %s at simulation time %f"), *NewPhaseStr, PlaySimulation->GetPlayState().GameTimeSeconds);

            // Publish phase-change event on bus
            UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
            if (Bus)
            {
                FPSTelemetryPhaseChangeEvent PhaseEvt;
                PhaseEvt.OldPhase         = OldPhaseStr;
                PhaseEvt.NewPhase         = NewPhaseStr;
                PhaseEvt.GameClockSeconds = PlaySimulation->GetPlayState().GameClockSeconds;
                PhaseEvt.PlayClockSeconds = PlaySimulation->GetPlayState().PlayClockSeconds;
                Bus->PublishPhaseChange(PhaseEvt);
            }

            if (CurrentPhase == EPlayPhase::PreSnap)
            {
                OpenPlayCallWindow();
            }
        }

        // Epic 102: the play-call authority decides when the offense snaps -- after both
        // calls are in, on the human's hike or after the CPU's delay. It sees the live play
        // clock so a human who runs low gets the suggestion called for them.
        UPSPlayCallSubsystem* PlayCall = GetWorld()->GetSubsystem<UPSPlayCallSubsystem>();
        if (CurrentPhase == EPlayPhase::PreSnap && PlayCall)
        {
            PlayCall->SetPlayClock(PlaySimulation->GetPlayState().PlayClockSeconds);
            if (PlayCall->PollReadyToSnap(DeltaSeconds))
            {
                ExecuteSnap();
            }
        }
    }
}

void APSGameMode::OpenPlayCallWindow()
{
    UPSPlayCallSubsystem* PlayCall = GetWorld() ? GetWorld()->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (PlayCall && PlaySimulation && PlaySimulation->GetPlayState().Phase == EPlayPhase::PreSnap)
    {
        PlayCall->OpenPlayCall(UPSPlayCallSubsystem::MakeSituation(PlaySimulation->GetPlayState()));
    }
}

void APSGameMode::ExecuteSnap()
{
    APSPlayerPawn* Center = FindPlayerPawnByRole(EPlayerRole::OffensiveLineman);
    APSPlayerPawn* QB = FindPlayerPawnByRole(EPlayerRole::Quarterback);

    if (Center && QB && ActiveBall)
    {
        if (Center->TransferPossessionTo(QB))
        {
            ActiveBall->AttachToCarrier(QB, TEXT("HandSocket"));
            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Executed snap. Transferred ball from Center to QB."));
        }
    }

    if (PlaySimulation)
    {
        PlaySimulation->TriggerSnap();
    }

    // Publish snap event on bus
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (Bus && PlaySimulation)
    {
        FPSTelemetrySnapEvent SnapEvt;
        SnapEvt.YardLine          = PlaySimulation->GetPlayState().YardLine;
        SnapEvt.Down              = PlaySimulation->GetPlayState().Down;
        SnapEvt.Distance          = PlaySimulation->GetPlayState().Distance;
        SnapEvt.GameClockSeconds  = PlaySimulation->GetPlayState().GameClockSeconds;
        // The same yard-line-to-world mapping ResetPawnPositions places the pawns with.
        SnapEvt.LineOfScrimmage   = FVector(PlaySimulation->GetPlayState().YardLine * 100.f, 0.f, 0.f);
        Bus->PublishSnap(SnapEvt);
    }

    PairLinemen();
}

void APSGameMode::PairLinemen()
{
    TArray<APSPlayerPawn*> OffensiveLinemen;
    TArray<APSPlayerPawn*> Defenders;

    for (APSPlayerPawn* Pawn : CachedPawns)
    {
        if (Pawn)
        {
            Pawn->EngagedOpponent = nullptr;
            Pawn->bIsEngaged = false;

            if (Pawn->GetAttributes().Role == EPlayerRole::OffensiveLineman)
            {
                OffensiveLinemen.Add(Pawn);
            }
            else if (Pawn->TeamSide == EPSTeamSide::Defense &&
                     (Pawn->GetAttributes().Role == EPlayerRole::DefensiveLineman ||
                      Pawn->GetAttributes().Role == EPlayerRole::Linebacker))
            {
                Defenders.Add(Pawn);
            }
        }
    }

    // Each lineman takes the nearest free rusher, looking toward the offense's slide call
    // (Epic 66: UPSPreSnapSubsystem owns protection).
    UPSPreSnapSubsystem* PreSnap = GetWorld() ? GetWorld()->GetSubsystem<UPSPreSnapSubsystem>() : nullptr;
    const FVector SlideAim = PreSnap ? PreSnap->GetSlideAimOffset() : FVector::ZeroVector;
    for (const TPair<APSPlayerPawn*, APSPlayerPawn*>& Pair : UPSPreSnapSubsystem::ComputeBlockingPairs(OffensiveLinemen, Defenders, SlideAim))
    {
        APSPlayerPawn* OL = Pair.Key;
        APSPlayerPawn* BestDefender = Pair.Value;
        OL->EngagedOpponent = BestDefender;
        OL->bIsEngaged = true;
        BestDefender->EngagedOpponent = OL;
        BestDefender->bIsEngaged = true;

        UE_LOG(LogTemp, Display, TEXT("PSGameMode: Paired OL %s with Defender %s for engagement."),
            *OL->GetAttributes().DisplayName, *BestDefender->GetAttributes().DisplayName);
    }
}

APSPlayerPawn* APSGameMode::FindPlayerPawnByRole(EPlayerRole PlayerRole) const
{
    for (APSPlayerPawn* Pawn : CachedPawns)
    {
        if (Pawn && Pawn->GetAttributes().Role == PlayerRole)
        {
            return Pawn;
        }
    }
    return nullptr;
}

FVector APSGameMode::GetLargestRunLaneGap() const
{
    const FVector LargestGapCenter = PSFieldReads::LargestRunLaneGap(CachedPawns);
    UE_LOG(LogTemp, Display, TEXT("PSGameMode: Largest run lane gap found at %s."), *LargestGapCenter.ToString());
    return LargestGapCenter;
}

void APSGameMode::ResetPawnPositions()
{
    if (!PlaySimulation)
    {
        return;
    }

    CurrentPlayIndex++;

    int32 YardLine = PlaySimulation->GetPlayState().YardLine;
    float ScrimmageX = YardLine * 100.f;

    // Epic 140: no punting means no safety valve to discourage 4th-down attempts, so
    // the defense fields extra defenders on 4th down instead.
    const int32 CurrentDown = PlaySimulation->GetPlayState().Down;
    const int32 DesiredDefensiveCount = GetDefensivePersonnelCount(CurrentDown, PlaySimulation->RulesConfig);
    if (DesiredDefensiveCount > 11 && !ExtraDefenderPawn && ActiveRoster)
    {
        const FName BackupLinebackerId = ActiveRoster->GetNextBackup(ActiveRoster->GetStarterId(EPlayerRole::Linebacker));
        if (!BackupLinebackerId.IsNone() && ActiveRoster->FindPlayerById(BackupLinebackerId, ExtraDefenderAttributes))
        {
            TArray<const FPlayerAttributes*> ExtraRoster;
            ExtraRoster.Add(&ExtraDefenderAttributes);
            TArray<APSPlayerPawn*> Spawned = APSFieldGrid::SpawnPlayersFromRoster(ExtraRoster, ScrimmageX, GetWorld());
            if (Spawned.Num() > 0 && Spawned[0])
            {
                ExtraDefenderPawn = Spawned[0];
                ExtraDefenderPawn->TeamSide = EPSTeamSide::Defense;
                CachedPawns.Add(ExtraDefenderPawn);
                UE_LOG(LogTemp, Display, TEXT("PSGameMode: 4th-down defensive overload -- fielded extra defender %s."), *ExtraDefenderAttributes.DisplayName);
            }
        }
    }
    else if (DesiredDefensiveCount <= 11 && ExtraDefenderPawn)
    {
        CachedPawns.Remove(ExtraDefenderPawn);
        ExtraDefenderPawn->Destroy();
        ExtraDefenderPawn = nullptr;
    }

    // Every down starts from the same lineup (APSFieldGrid::ComputeLineup).
    TArray<APSPlayerPawn*> FieldPawns;
    TArray<EPlayerRole> Roles;
    for (APSPlayerPawn* Pawn : CachedPawns)
    {
        if (Pawn)
        {
            FieldPawns.Add(Pawn);
            Roles.Add(Pawn->GetAttributes().Role);
        }
    }
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, ScrimmageX);

    for (int32 PawnIndex = 0; PawnIndex < FieldPawns.Num(); ++PawnIndex)
    {
        APSPlayerPawn* Pawn = FieldPawns[PawnIndex];

        // Epic 139: heal every on-field pawn's live HP pool back to full for the
        // new play, and mirror that into the authoritative roster live-state. A ball
        // carrier due to sit this play out keeps his sit-out for the personnel manager.
        if (UPSHealthComponent* Health = Pawn->GetHealthComponent())
        {
            Health->Respawn();
            if (ActiveRoster && ActiveRoster->IsAvailableForPlay(Pawn->GetAttributes().PlayerId, CurrentPlayIndex))
            {
                ActiveRoster->RespawnForNewPlay(Pawn->GetAttributes().PlayerId, Health->GetMaxHitPoints());
            }
        }

        // Epic 141: award participation XP for the play just completed (and a
        // bonus to whoever is still holding the ball if it ended in a touchdown).
        if (ActiveRoster && PlayerLeveling)
        {
            const bool bTouchdown = PlaySimulation && PlaySimulation->GetPlayResult().ResultType == EPlayResultType::Touchdown && Pawn->HasPossession();
            const float XpAmount = PlayerLeveling->ComputeXpForPlay(bTouchdown, LevelingTuningSettings);
            PlayerLeveling->AwardXpForPlay(ActiveRoster, Pawn->GetAttributes().PlayerId, XpAmount, LevelingTuningSettings);
        }

        // Reset velocities
        if (Pawn->GetFloatingMovementComponent())
        {
            Pawn->GetFloatingMovementComponent()->Velocity = FVector::ZeroVector;
            Pawn->GetFloatingMovementComponent()->StopActiveMovement();
        }

        // Clear possession and block engagement
        Pawn->LosePossession();
        Pawn->bIsEngaged = false;
        Pawn->EngagedOpponent = nullptr;

        const FVector TargetLoc = Lineup[PawnIndex];
        Pawn->SetActorLocation(TargetLoc, false, nullptr, ETeleportType::TeleportPhysics);
        Pawn->SetStartingLocation(TargetLoc);
    }

    // Re-attach ball to Center
    APSPlayerPawn* Center = FindPlayerPawnByRole(EPlayerRole::OffensiveLineman);
    if (ActiveBall && Center)
    {
        ActiveBall->DetachFromCarrier();
        ActiveBall->bIsFumbled = false;
        ActiveBall->SetActorLocation(FVector(ScrimmageX, 0.f, 100.f));
        ActiveBall->AttachToCarrier(Center, TEXT("HandSocket"));
        Center->GainPossession();
        UE_LOG(LogTemp, Display, TEXT("PSGameMode: Reset play cycle. Re-attached ActiveBall to Center at scrimmage line."));
    }

    // Epic 19.5: sit-outs and tired players come off now; the play call brings its package on.
    if (PersonnelManager)
    {
        PersonnelManager->BeginNewPlay(ScrimmageX, CurrentPlayIndex);
    }
}

void APSGameMode::OnBusScoreEvent(const FPSTelemetryScoreEvent& Event)
{
    if (Event.bHomeScored)
    {
        HomeScore += Event.Points;
    }
    else
    {
        AwayScore += Event.Points;
    }
    UE_LOG(LogTemp, Display, TEXT("PSGameMode: Score event via bus — %s (%d pts). Home: %d, Away: %d"),
        *Event.ScoreType, Event.Points, HomeScore, AwayScore);
}

void APSGameMode::OnBusCatchEvent(const FPSTelemetryCatchEvent& Event)
{
    if (!BroadcastCamera)
    {
        return;
    }

    for (APSPlayerPawn* Pawn : CachedPawns)
    {
        if (Pawn && Pawn->GetAttributes().DisplayName == Event.ReceiverName)
        {
            BroadcastCamera->SetTargetActor(Pawn);
            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Catch event routed BroadcastCamera to %s."), *Event.ReceiverName);
            break;
        }
    }
}

void APSGameMode::OnBusDeathEvent(const FPSTelemetryDeathEvent& Event)
{
    if (!ActiveRoster)
    {
        return;
    }

    for (APSPlayerPawn* Pawn : CachedPawns)
    {
        if (Pawn && Pawn->GetAttributes().DisplayName == Event.PlayerName)
        {
            // TackleDamage deaths only ever happen to the ball carrier (only carriers
            // get tackled) -- they sit out exactly the next play. InterceptionPunishment
            // deaths only ever happen to a non-carrier (the intended receiver never had
            // the ball) -- no sit-out, full respawn next play (Epic 139/140).
            if (Event.Cause == EPSDeathCause::TackleDamage)
            {
                ActiveRoster->MarkDownedForNextPlay(Pawn->GetAttributes().PlayerId, CurrentPlayIndex);
            }
            else
            {
                ActiveRoster->MarkDownedForCurrentPlayOnly(Pawn->GetAttributes().PlayerId);
            }
            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Death event routed to roster live-state for %s (Cause=%s)."),
                *Event.PlayerName, *UEnum::GetValueAsString(Event.Cause));
            break;
        }
    }
}
