#include "PSGameMode.h"
#include "PSDataIngestion.h"
#include "PSPlaySimulation.h"
#include "Misc/Paths.h"
#include "PSHUD.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "PSBall.h"
#include "PSPlayerPawn.h"
#include "Kismet/GameplayStatics.h"

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

    HUDClass = APSHUD::StaticClass();
    HomeScore = 0;
    AwayScore = 0;

    MovementTuningTable = nullptr;
    MovementTuningJsonPath = TEXT("Data/movement_tuning.json");
    MovementTuningSettings = FMovementTuningRow();
}

void APSGameMode::StartPlay()
{
    Super::StartPlay();

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

            // Separate players into Offense and Defense
            TArray<FPlayerAttributes> OffenseRoster;
            TArray<FPlayerAttributes> DefenseRoster;
            TArray<FPlayerAttributes*> AllPlayers;
            PlayerRosterTable->GetAllRows<FPlayerAttributes>(TEXT("PSGameMode Roster Ingestion"), AllPlayers);

            for (FPlayerAttributes* Player : AllPlayers)
            {
                if (Player)
                {
                    if (Player->Role == EPlayerRole::Quarterback ||
                        Player->Role == EPlayerRole::RunningBack ||
                        Player->Role == EPlayerRole::WideReceiver ||
                        Player->Role == EPlayerRole::TightEnd ||
                        Player->Role == EPlayerRole::OffensiveLineman)
                    {
                        OffenseRoster.Add(*Player);
                    }
                    else
                    {
                        DefenseRoster.Add(*Player);
                    }
                }
            }

            PlaySimulation = NewObject<UPSPlaySimulation>(this);
            if (PlaySimulation)
            {
                PlaySimulation->InitializePlay(OffenseRoster, DefenseRoster);
                UE_LOG(LogTemp, Display, TEXT("PSGameMode: Initialized PlaySimulation with %d Offense and %d Defense players."), OffenseRoster.Num(), DefenseRoster.Num());
            }

            TArray<AActor*> ExistingPawns;
            UGameplayStatics::GetAllActorsOfClass(GetWorld(), APSPlayerPawn::StaticClass(), ExistingPawns);
            if (ExistingPawns.Num() == 0)
            {
                TArray<FPlayerAttributes*> AllPlayers;
                PlayerRosterTable->GetAllRows<FPlayerAttributes>(TEXT("PSGameMode Spawning"), AllPlayers);
                
                float XOffset = 0.f;
                for (FPlayerAttributes* Player : AllPlayers)
                {
                    if (Player)
                    {
                        FActorSpawnParameters SpawnParams;
                        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                        
                        float SpawnX = (Player->Role == EPlayerRole::Quarterback) ? 0.f : 100.f;
                        FVector SpawnLocation(SpawnX, XOffset, 100.f);
                        XOffset += 150.f;
                        
                        APSPlayerPawn* NewPawn = GetWorld()->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);
                        if (NewPawn)
                        {
                            NewPawn->InitializePlayer(*Player);
                        }
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
            FString PhaseStr;
            switch (CurrentPhase)
            {
            case EPlayPhase::PreSnap: PhaseStr = TEXT("PreSnap"); break;
            case EPlayPhase::Snap: PhaseStr = TEXT("Snap"); break;
            case EPlayPhase::PassRush: PhaseStr = TEXT("PassRush"); break;
            case EPlayPhase::BallCarrierMovement: PhaseStr = TEXT("BallCarrierMovement"); break;
            case EPlayPhase::Scoring: PhaseStr = TEXT("Scoring"); break;
            default: PhaseStr = TEXT("Unknown"); break;
            }
            UE_LOG(LogTemp, Display, TEXT("PSGameMode: Play Phase Transitioned to %s at simulation time %f"), *PhaseStr, PlaySimulation->GetPlayState().GameTimeSeconds);

            if (CurrentPhase == EPlayPhase::Scoring)
            {
                FPlayResult PlayResult = PlaySimulation->GetPlayResult();
                if (PlayResult.ResultType == EPlayResultType::Touchdown)
                {
                    HomeScore += 6;
                    UE_LOG(LogTemp, Display, TEXT("PSGameMode: TOUCHDOWN! Offense scores 6 points. HomeScore: %d, AwayScore: %d"), HomeScore, AwayScore);
                }
            }
        }
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
}

APSPlayerPawn* APSGameMode::FindPlayerPawnByRole(EPlayerRole Role) const
{
    if (!GetWorld())
    {
        return nullptr;
    }

    TArray<AActor*> PlayerActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APSPlayerPawn::StaticClass(), PlayerActors);
    for (AActor* Actor : PlayerActors)
    {
        if (APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(Actor))
        {
            if (Pawn->GetAttributes().Role == Role)
            {
                return Pawn;
            }
        }
    }
    return nullptr;
}
