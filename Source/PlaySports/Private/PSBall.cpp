#include "PSBall.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "PSPlayerPawn.h"
#include "PSGameMode.h"
#include "PSPlaySimulation.h"

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

        if (bIsFumbled)
        {
            FPlayerAttributes Attr = PlayerPawn->GetAttributes();
            float RecoveryChance = 0.60f + (Attr.Agility + Attr.Awareness) * 0.002f;
            RecoveryChance = FMath::Clamp(RecoveryChance, 0.20f, 0.98f);

            float Roll = FMath::FRand();
            if (Roll <= RecoveryChance)
            {
                AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
                PlayerPawn->GainPossession();
                bIsFumbled = false;

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
            float CatchChance = 0.50f + (Attr.Agility + Attr.Awareness) * 0.0025f;
            CatchChance = FMath::Clamp(CatchChance, 0.10f, 0.95f);

            float Roll = FMath::FRand();
            if (Roll <= CatchChance)
            {
                AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
                PlayerPawn->GainPossession();

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
            float InterceptChance = 0.05f + (Attr.Agility + Attr.Awareness) * 0.001f;
            InterceptChance = FMath::Clamp(InterceptChance, 0.01f, 0.40f);

            float Roll = FMath::FRand();
            if (Roll <= InterceptChance)
            {
                AttachToCarrier(PlayerPawn, TEXT("HandSocket"));
                PlayerPawn->GainPossession();

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
            GM->PlaySimulation->SetPlayPhase(EPlayPhase::Scoring);
        }
    }
}
