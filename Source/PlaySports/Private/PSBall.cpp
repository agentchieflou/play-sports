#include "PSBall.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

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
}

void APSBall::BeginPlay()
{
    Super::BeginPlay();
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
