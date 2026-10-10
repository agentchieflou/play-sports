#include "PSBall.h"
#include "PSBallLook.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "PSPlayerPawn.h"
#include "PSAIFieldSnapshot.h"
#include "PSNetRandomStreams.h"
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

    // What it looks like (Epic 147.4): data, not an asset. Its collision stays the sphere.
    PSBallLook::Apply(MeshComponent, PSBallLook::LoadStyle(PSBallLook::GetDefaultStylePath()), this);

    if (CollisionComponent)
    {
        CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &APSBall::OnBallOverlap);
    }

    if (ProjectileMovement)
    {
        ProjectileMovement->OnProjectileBounce.AddDynamic(this, &APSBall::OnBallBounce);
    }

    BindToBus();

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

void APSBall::BindToBus()
{
    // The native delegate, not the dynamic one: an actor's dynamic calls go through
    // AActor::ProcessEvent, which drops them until the world's actors are initialized.
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    ThrowHandle = Bus->OnThrowMC.AddUObject(this, &APSBall::HandleBusThrow);
    BoundBus = Bus;
}

void APSBall::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnThrowMC.Remove(ThrowHandle);
    }
    ThrowHandle.Reset();
    BoundBus.Reset();
}

void APSBall::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void APSBall::Launch(const FVector& Velocity)
{
    DetachFromCarrier();
    bGroundedReported = false;

    if (ProjectileMovement)
    {
        // A ball that came to rest stopped simulating, which can leave the projectile without
        // the component it moves (UProjectileMovementComponent::StopSimulating clears it): point
        // it back at the ball, or a relaunched ball would never leave the spot.
        if (ProjectileMovement->UpdatedComponent != CollisionComponent)
        {
            ProjectileMovement->SetUpdatedComponent(CollisionComponent);
        }
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
    ResolveTouch(Cast<APSPlayerPawn>(OtherActor));
}

bool APSBall::ResolveTouch(APSPlayerPawn* PlayerPawn)
{
    if (!PlayerPawn || GetAttachParentActor() != nullptr)
    {
        return false;
    }
    if (!bIsFumbled && (!ProjectileMovement || !ProjectileMovement->IsActive() || ProjectileMovement->Velocity.IsNearlyZero()))
    {
        return false;
    }

    // Every outcome goes out on the bus (rule 5); the play simulation, the outcome authority,
    // moves the play on from there (a recovery or a catch makes a ball carrier).
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;

    if (bIsFumbled)
    {
        FPlayerAttributes Attr = PlayerPawn->GetAttributes();
        float RecoveryChance = PSBallResolutionHelpers::ComputeFumbleRecoveryChance(Attr, CatchTuningSettings);

        float Roll = UPSNetRandomStreams::RollFor(this, TEXT("FumbleRecovery"), Attr.PlayerId);
        if (Roll <= RecoveryChance)
        {
            AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
            PlayerPawn->GainPossession();
            bIsFumbled = false;

            if (Bus)
            {
                FPSTelemetryFumbleEvent FumbleEvt;
                FumbleEvt.FumblerName  = TEXT("Unknown");
                FumbleEvt.RecoveryName = Attr.DisplayName;
                FumbleEvt.YardLine     = 0;
                FumbleEvt.bIsTurnover  = false;
                Bus->PublishFumble(FumbleEvt);
            }
            UE_LOG(LogTemp, Display, TEXT("APSBall: Fumble RECOVERED by %s (Roll: %.2f <= Chance: %.2f)"), *Attr.DisplayName, Roll, RecoveryChance);
            return true;
        }
        UE_LOG(LogTemp, Display, TEXT("APSBall: %s failed to recover fumble (Roll: %.2f > Chance: %.2f)"), *Attr.DisplayName, Roll, RecoveryChance);
        return false;
    }

    if (PlayerPawn->TeamSide == EPSTeamSide::Offense)
    {
        FPlayerAttributes Attr = PlayerPawn->GetAttributes();
        float CatchChance = PSBallResolutionHelpers::ComputeCatchChance(Attr, CatchTuningSettings);

        float Roll = UPSNetRandomStreams::RollFor(this, TEXT("Catch"), Attr.PlayerId);
        if (PSBallResolutionHelpers::ResolveCatch(Attr, Roll, CatchTuningSettings))
        {
            AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
            PlayerPawn->GainPossession();

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
            UE_LOG(LogTemp, Display, TEXT("APSBall: Pass CAUGHT by %s (Roll: %.2f <= Chance: %.2f)"), *Attr.DisplayName, Roll, CatchChance);
            return true;
        }
        UE_LOG(LogTemp, Display, TEXT("APSBall: Pass DROPPED by %s (Roll: %.2f > Chance: %.2f)"), *Attr.DisplayName, Roll, CatchChance);
        return false;
    }

    FPlayerAttributes Attr = PlayerPawn->GetAttributes();
    float InterceptChance = PSBallResolutionHelpers::ComputeInterceptionChance(Attr, CatchTuningSettings);

    float Roll = UPSNetRandomStreams::RollFor(this, TEXT("Interception"), Attr.PlayerId);
    if (Roll > InterceptChance)
    {
        UE_LOG(LogTemp, Display, TEXT("APSBall: Pass deflected by DB %s (Roll: %.2f > Chance: %.2f)"), *Attr.DisplayName, Roll, InterceptChance);
        return false;
    }

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

    // Epic 140: an interception auto-kills the QB's intended receiver (not the intercepting
    // defender), so the offense can't lean on auto-tackles to bail out a bad read. He is found
    // on the field as the AI reads it (UPSAIFieldSnapshot).
    if (Bus && !LastThrowTargetName.IsEmpty())
    {
        for (APSPlayerPawn* Pawn : UPSAIFieldSnapshot::GetFieldPawns(GetWorld()))
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
    UE_LOG(LogTemp, Display, TEXT("APSBall: INTERCEPTED by %s (Roll: %.2f <= Chance: %.2f)"), *Attr.DisplayName, Roll, InterceptChance);
    return true;
}

void APSBall::OnBallBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
    ReportGrounded();
}

bool APSBall::ReportGrounded()
{
    // A fumble is live on the ground; a held ball isn't in flight; one landing is enough.
    if (bIsFumbled || bGroundedReported || GetAttachParentActor() != nullptr)
    {
        return false;
    }
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        return false;
    }
    bGroundedReported = true;
    FPSTelemetryBallGroundedEvent Grounded;
    Grounded.Location = GetActorLocation();
    Bus->PublishBallGrounded(Grounded);
    UE_LOG(LogTemp, Display, TEXT("APSBall: The ball came down at %s."), *Grounded.Location.ToString());
    return true;
}

void APSBall::HandleBusThrow(const FPSTelemetryThrowEvent& Event)
{
    LastThrowTargetName = Event.TargetReceiverName;
}
