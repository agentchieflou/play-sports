#include "PSBall.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "PSPlayerPawn.h"
#include "PSGameMode.h"
#include "PSPlaySimulation.h"
#include "PSTelemetryBus.h"
#include "PSHealthComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

static bool LoadCatchTuningFromJson(const FString& JsonFilePath, FCatchTuningRow& OutTuning)
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
        return false;
    }

    TSharedPtr<FJsonObject> RowObject = ParsedArray[0]->AsObject();
    if (RowObject.IsValid())
    {
        return FJsonObjectConverter::JsonObjectToUStruct(RowObject.ToSharedRef(), &OutTuning, 0, 0);
    }
    return false;
}

APSBall::APSBall()
{
    PrimaryActorTick.bCanEverTick = true;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    CollisionComponent->InitSphereRadius(15.f); // regulation size approx 30cm long, 15cm radius
    CollisionComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
    RootComponent = CollisionComponent;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComponent->SetupAttachment(RootComponent);
    MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileComp"));
    ProjectileMovement->UpdatedComponent = CollisionComponent;
    ProjectileMovement->InitialSpeed = 0.f;
    ProjectileMovement->MaxSpeed = 5000.f; // ~50 m/s max throw speed
    ProjectileMovement->bRotationFollowsVelocity = false; // We handle rotation customly for spiral Roll
    ProjectileMovement->bShouldBounce = true;
    ProjectileMovement->Bounciness = 0.4f;
    ProjectileMovement->Friction = 0.2f;
    ProjectileMovement->ProjectileGravityScale = 1.0f;
    ProjectileMovement->bInitialVelocityInLocalSpace = false;
    ProjectileMovement->Deactivate(); // Starts stationary

    SpiralSpinRate = 720.f;
    CurrentRollSpin = 0.f;
    bIsFumbled = false;

    CatchTuningTable = nullptr;
    CatchTuningJsonPath = TEXT("Data/catch_tuning.json");
    CatchTuningSettings = FCatchTuningRow();
}

void APSBall::BeginPlay()
{
    Super::BeginPlay();

    if (CollisionComponent)
    {
        CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &APSBall::OnBallOverlap);
    }

    if (ProjectileMovement)
    {
        ProjectileMovement->OnProjectileBounce.AddDynamic(this, &APSBall::OnBallBounce);
    }

    if (UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr)
    {
        Bus->OnThrow.AddDynamic(this, &APSBall::OnBusThrowEvent);
    }

    if (CatchTuningTable)
    {
        static const FString ContextString(TEXT("CatchTuningContext"));
        TArray<FCatchTuningRow*> TuningRows;
        CatchTuningTable->GetAllRows<FCatchTuningRow>(ContextString, TuningRows);
        if (TuningRows.Num() > 0)
        {
            CatchTuningSettings = *TuningRows[0];
            UE_LOG(LogTemp, Display, TEXT("APSBall: Loaded catch tuning settings from DataTable."));
        }
    }
    else
    {
        FString FullTuningPath = FPaths::ProjectDir() + CatchTuningJsonPath;
        FPaths::CollapseRelativeDirectories(FullTuningPath);
        if (FPaths::FileExists(FullTuningPath))
        {
            FCatchTuningRow LoadedTuning;
            if (LoadCatchTuningFromJson(FullTuningPath, LoadedTuning))
            {
                CatchTuningSettings = LoadedTuning;
                UE_LOG(LogTemp, Display, TEXT("APSBall: Loaded catch tuning settings from JSON (%s)."), *FullTuningPath);
            }
        }
    }
}

void APSBall::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (ProjectileMovement && ProjectileMovement->IsActive() && !ProjectileMovement->Velocity.IsNearlyZero())
    {
        FVector VelocityDir = ProjectileMovement->Velocity;
        VelocityDir.Normalize();

        FRotator FlightRotation = VelocityDir.Rotation();
        
        CurrentRollSpin += SpiralSpinRate * DeltaTime;
        CurrentRollSpin = FMath::Fmod(CurrentRollSpin, 360.0f);

        FlightRotation.Roll = CurrentRollSpin;
        SetActorRotation(FlightRotation);
    }
}

void APSBall::Launch(const FVector& Velocity)
{
    DetachFromCarrier();

    if (ProjectileMovement)
    {
        ProjectileMovement->Velocity = Velocity;
        ProjectileMovement->Activate();
        UE_LOG(LogTemp, Display, TEXT("APSBall: Launched with velocity %s (speed: %.1f cm/s)"), *Velocity.ToString(), Velocity.Size());
    }
}

void APSBall::AttachToCarrier(APawn* Carrier, FName SocketName)
{
    if (!Carrier)
    {
        return;
    }

    if (ProjectileMovement)
    {
        ProjectileMovement->Deactivate();
        ProjectileMovement->Velocity = FVector::ZeroVector;
    }

    if (CollisionComponent)
    {
        CollisionComponent->SetSimulatePhysics(false);
        CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    AttachToActor(Carrier, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
    UE_LOG(LogTemp, Display, TEXT("APSBall: Attached ball to carrier %s at socket %s"), *Carrier->GetName(), *SocketName.ToString());
}

void APSBall::DetachFromCarrier()
{
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (CollisionComponent)
    {
        CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    if (ProjectileMovement)
    {
        ProjectileMovement->Deactivate();
        ProjectileMovement->Velocity = FVector::ZeroVector;
    }
    UE_LOG(LogTemp, Display, TEXT("APSBall: Detached ball from carrier."));
}

void APSBall::Fumble(const FVector& LaunchVelocity)
{
    DetachFromCarrier();
    bIsFumbled = true;
    Launch(LaunchVelocity);
    UE_LOG(LogTemp, Display, TEXT("APSBall: Fumble executed with velocity %s (speed: %.1f cm/s)"), *LaunchVelocity.ToString(), LaunchVelocity.Size());
}

void APSBall::OnBallOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (GetAttachParentActor() != nullptr)
    {
        return;
    }

    if (!bIsFumbled)
    {
        if (!ProjectileMovement || !ProjectileMovement->IsActive() || ProjectileMovement->Velocity.IsNearlyZero())
        {
            return;
        }
    }

    if (APSPlayerPawn* PlayerPawn = Cast<APSPlayerPawn>(OtherActor))
    {
        APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
        if (!GM)
        {
            return;
        }

        UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();

        if (bIsFumbled)
        {
            FPlayerAttributes Attr = PlayerPawn->GetAttributes();
            float RecoveryChance = PSBallResolutionHelpers::ComputeFumbleRecoveryChance(Attr, CatchTuningSettings);

            float Roll = FMath::FRand();
            if (Roll <= RecoveryChance)
            {
                AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
                PlayerPawn->GainPossession();
                bIsFumbled = false;

                // Publish fumble-recovery event on bus
                if (Bus)
                {
                    FPSTelemetryFumbleEvent FumbleEvt;
                    FumbleEvt.FumblerName  = TEXT("Unknown");
                    FumbleEvt.RecoveryName = Attr.DisplayName;
                    FumbleEvt.YardLine     = 0;
                    FumbleEvt.bIsTurnover  = false;
                    Bus->PublishFumble(FumbleEvt);
                }

                // Keep direct phase set until C2 routes control through bus
                if (GM->PlaySimulation)
                {
                    GM->PlaySimulation->SetPlayPhase(EPlayPhase::BallCarrierMovement);
                }
                UE_LOG(LogTemp, Display, TEXT("APSBall: Fumble RECOVERED by %s (Roll: %.2f <= Chance: %.2f)"), *Attr.DisplayName, Roll, RecoveryChance);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("APSBall: %s failed to recover fumble (Roll: %.2f > Chance: %.2f)"), *Attr.DisplayName, Roll, RecoveryChance);
            }
            return;
        }

        if (PlayerPawn->TeamSide == EPSTeamSide::Offense)
        {
            FPlayerAttributes Attr = PlayerPawn->GetAttributes();
            float CatchChance = PSBallResolutionHelpers::ComputeCatchChance(Attr, CatchTuningSettings);

            float Roll = FMath::FRand();
            if (PSBallResolutionHelpers::ResolveCatch(Attr, Roll, CatchTuningSettings))
            {
                AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
                PlayerPawn->GainPossession();

                // Publish catch event on bus
                if (Bus)
                {
                    FPSTelemetryCatchEvent CatchEvt;
                    CatchEvt.ReceiverName   = Attr.DisplayName;
                    CatchEvt.CatchLocation  = GetActorLocation();
                    CatchEvt.YardsGained    = 0; // updated by sim when play ends
                    CatchEvt.bIsInterception = false;
                    Bus->PublishCatch(CatchEvt);
                }
                LastThrowTargetName.Empty();

                // Keep direct phase set until C2 routes control through bus
                if (GM->PlaySimulation)
                {
                    GM->PlaySimulation->SetPlayPhase(EPlayPhase::BallCarrierMovement);
                }
                UE_LOG(LogTemp, Display, TEXT("APSBall: Pass CAUGHT by %s (Roll: %.2f <= Chance: %.2f)"), *Attr.DisplayName, Roll, CatchChance);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("APSBall: Pass DROPPED by %s (Roll: %.2f > Chance: %.2f)"), *Attr.DisplayName, Roll, CatchChance);
            }
        }
        else if (PlayerPawn->TeamSide == EPSTeamSide::Defense)
        {
            FPlayerAttributes Attr = PlayerPawn->GetAttributes();
            float InterceptChance = PSBallResolutionHelpers::ComputeInterceptionChance(Attr, CatchTuningSettings);

            float Roll = FMath::FRand();
            if (Roll <= InterceptChance)
            {
                AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
                PlayerPawn->GainPossession();

                // Publish interception as a catch event on bus (bIsInterception = true)
                if (Bus)
                {
                    FPSTelemetryCatchEvent IntEvt;
                    IntEvt.ReceiverName    = Attr.DisplayName;
                    IntEvt.CatchLocation   = GetActorLocation();
                    IntEvt.YardsGained     = 0;
                    IntEvt.bIsInterception = true;
                    Bus->PublishCatch(IntEvt);
                }

                // Epic 140: an interception auto-kills the QB's intended receiver
                // (not the intercepting defender), so the offense can't lean on
                // auto-tackles to bail out a bad read.
                if (Bus && !LastThrowTargetName.IsEmpty())
                {
                    for (APSPlayerPawn* Pawn : GM->CachedPawns)
                    {
                        if (Pawn && Pawn->GetAttributes().DisplayName == LastThrowTargetName)
                        {
                            if (UPSHealthComponent* TargetHealth = Pawn->GetHealthComponent())
                            {
                                TargetHealth->Kill();
                            }

                            FPSTelemetryDeathEvent DeathEvt;
                            DeathEvt.PlayerName = LastThrowTargetName;
                            DeathEvt.Cause = EPSDeathCause::InterceptionPunishment;
                            Bus->PublishDeath(DeathEvt);
                            UE_LOG(LogTemp, Display, TEXT("APSBall: Interception punishment -- intended target %s auto-killed."), *LastThrowTargetName);
                            break;
                        }
                    }
                    LastThrowTargetName.Empty();
                }

                // Keep direct phase set until C2 routes control through bus
                if (GM->PlaySimulation)
                {
                    GM->PlaySimulation->SetPlayPhase(EPlayPhase::BallCarrierMovement);
                }
                UE_LOG(LogTemp, Display, TEXT("APSBall: INTERCEPTED by %s (Roll: %.2f <= Chance: %.2f)"), *Attr.DisplayName, Roll, InterceptChance);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("APSBall: Pass deflected by DB %s (Roll: %.2f > Chance: %.2f)"), *Attr.DisplayName, Roll, InterceptChance);
            }
        }
    }
}

void APSBall::OnBallBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
    if (bIsFumbled)
    {
        return;
    }

    APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    if (GM && GM->PlaySimulation)
    {
        EPlayPhase Phase = GM->PlaySimulation->GetPlayState().Phase;
        if (Phase == EPlayPhase::Snap || Phase == EPlayPhase::PassRush)
        {
            UE_LOG(LogTemp, Display, TEXT("APSBall: Ball bounced on the ground before being caught. Pass is Incomplete!"));

            // Publish phase-change (incomplete pass) event on bus
            UPSTelemetryBus* Bus = GetWorld()->GetSubsystem<UPSTelemetryBus>();
            if (Bus)
            {
                FPSTelemetryPhaseChangeEvent PhaseEvt;
                PhaseEvt.OldPhase          = TEXT("PassRush");
                PhaseEvt.NewPhase          = TEXT("Scoring");
                PhaseEvt.GameClockSeconds  = GM->PlaySimulation->GetPlayState().GameClockSeconds;
                PhaseEvt.PlayClockSeconds  = GM->PlaySimulation->GetPlayState().PlayClockSeconds;
                Bus->PublishPhaseChange(PhaseEvt);
            }

            // Keep direct phase set until C2 routes control through bus
            GM->PlaySimulation->SetPlayPhase(EPlayPhase::Scoring);
        }
    }
}

void APSBall::OnBusThrowEvent(const FPSTelemetryThrowEvent& Event)
{
    LastThrowTargetName = Event.TargetReceiverName;
}
